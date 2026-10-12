#pragma once

#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>

#include "open_radio/runtime_service_bridge.hpp"
#include "public_audio_pipeline.hpp"
#include "station_catalog.hpp"
#include "station_slots.hpp"

namespace open_radio::public_candidate {

class StationRuntime {
 public:
  // An injected predicate, not a second copy of the state. This class has no
  // business knowing anything about Bluetooth, and one source of truth cannot
  // drift from another — that drift, between the Bluetooth side and the TLS
  // side, was precisely the defect.
  using TlsGate = bool (*)();
  void setTlsGate(TlsGate gate) { tlsGate_ = gate; }

  StationRuntime(Preferences& preferences, RuntimeServiceBridge& services,
                 Mp3StreamPipeline& pipeline, const StationSlots& slots)
      : preferences_(preferences), services_(services), pipeline_(pipeline),
        slots_(slots) {}

  void select(std::uint8_t stationIndex, std::uint32_t nowMs) {
    if (stationIndex != stationIndex_) pipeline_.stop();
    stationIndex_ = stationIndex;
    // Start from the locally proven endpoint. Live operator discovery remains
    // the recovery path if it has expired, so first sound does not depend on a
    // remote catalog/API.
    activeEndpoint_ = readLastKnownGood();
    activeEndpointAac_ = parseStoredEndpoint(activeEndpoint_);
#if defined(OPEN_RADIO_HAS_HLS)
    // ...unless the proven endpoint is an aacp mount: MP3 is the default
    // again (heavy HE-AAC+SBR decode audibly starved the A2DP TX side —
    // owner A/B 2026-07-17 evening), so a stored aacp endpoint from the
    // AAC-first era yields to a fresh MP3-deck resolution. Cached pools
    // make that instant, and the stored endpoint still backs up a failed
    // resolution inside start().
    if (stationUsesRmfonPools(stationIndex_) && activeEndpointAac_) {
      activeEndpoint_ = "";
      activeEndpointAac_ = false;
    }
#endif
    running_ = false;
    healthy_ = false;
    hlsActive_ = false;
    retryIndex_ = 0;
    rmfCandidateIndex_ = 0;
    failuresSinceHealthy_ = 0;
    exhaustedCycles_ = 0;
    // A reassigned slot counts its own alternates, so "everything failed" means
    // the same thing whichever list the station came from.
    const std::size_t chosenCount = slots_.endpointCount(stationIndex_);
    candidatesAtSelect_ =
        chosenCount > 0
            ? chosenCount
            : (stationIndex_ < open_radio::generated::kStationCount
                   ? open_radio::generated::kStations[stationIndex_].playbackCount
                   : 0);
    retryAtMs_ = nowMs;
  }

  void networkConnected(std::uint32_t nowMs) {
    networkAvailable_ = true;
    retryAtMs_ = nowMs;
  }

  void networkLost(std::uint32_t nowMs) {
    networkAvailable_ = false;
    if (running_) pipeline_.stop();
    running_ = false;
    healthy_ = false;
    hlsActive_ = false;
    activeEndpoint_ = "";
    services_.stream(false, 1, nextSequence(), nowMs);
  }

  // Local modes deliberately close the live stream through the same owned
  // pipeline path as a terminal source failure. Keeping this in StationRuntime
  // prevents a second, subtly different teardown sequence in main.cpp.
  void suspend(std::uint32_t nowMs) {
    if (running_) pipeline_.stop();
    running_ = false;
    healthy_ = false;
    hlsActive_ = false;
    activeEndpoint_ = "";
    activeEndpointAac_ = false;
    sourceDeadSinceMs_ = 0;
    lastProgressMs_ = 0;
    services_.stream(false, 1, nextSequence(), nowMs);
  }

  void loop(std::uint32_t nowMs, bool startAllowed = true) {
    if (!networkAvailable_) return;
    if (!running_ && startAllowed &&
        static_cast<std::int32_t>(nowMs - retryAtMs_) >= 0) {
      start(nowMs);
      return;
    }
    if (running_ && !pipeline_.loop()) {
      running_ = false;
      healthy_ = false;
      ++unexpectedStops_;
      ++rmfCandidateIndex_;
      // Without this, the next start() replays the exact endpoint that just
      // died and the candidate bump above never takes effect — hit live
      // 2026-07-17: an aacp URL stored as last-known-good by a newer build
      // kept the MP3 decoder in an open-die loop (81 starts in 3 minutes).
      // Clearing forces a fresh resolution; the stored endpoint remains
      // start()'s fallback when resolution fails, and the next healthy
      // stream overwrites it.
      activeEndpoint_ = "";
      services_.stream(false, 1, nextSequence(), nowMs);
      retryAtMs_ = nowMs + kFastReconnectDelayMs;
      return;
    }
    // Fast lane for an announced death: streamtheworld edges drop the TCP
    // connection outright (STATUS_DISCONNECTED; ~65 drops in a 12-minute soak
    // 2026-07-17) and with library-internal reconnects disabled every drop
    // otherwise silences playback until the generic 8 s watchdog below. Two
    // seconds without a drained frame after an announced drop is unambiguous.
    const std::uint32_t sourceEvents = pipeline_.sourceStatusEvents();
    if (sourceEvents != lastSourceEventCount_) {
      lastSourceEventCount_ = sourceEvents;
      const int status = pipeline_.lastSourceStatus();
      // AudioFileSourceHTTPStream: 3 = STATUS_DISCONNECTED, 6 = STATUS_NODATA.
      if ((status == 3 || status == 6) && sourceDeadSinceMs_ == 0) {
        sourceDeadSinceMs_ = nowMs;
      }
    }
    if (running_ && healthy_ && startAllowed && sourceDeadSinceMs_ != 0 &&
        nowMs - sourceDeadSinceMs_ >= kSourceDeadReopenMs) {
      Serial.printf("stream_stage stage=source-dead duration_ms=%lu\n",
                    static_cast<unsigned long>(nowMs - sourceDeadSinceMs_));
      sourceDeadSinceMs_ = 0;
      pipeline_.stop();
      running_ = false;
      healthy_ = false;
      ++unexpectedStops_;
      ++rmfCandidateIndex_;
      activeEndpoint_ = "";
      services_.stream(false, 1, nextSequence(), nowMs);
      retryAtMs_ = nowMs + kFastReconnectDelayMs;
      return;
    }
    if (running_ && healthy_ && startAllowed && lastProgressMs_ != 0 &&
        nowMs - lastProgressMs_ >= kStreamProgressStallMs) {
      Serial.printf("stream_stage stage=stall-watchdog duration_ms=%lu\n",
                    static_cast<unsigned long>(nowMs - lastProgressMs_));
      pipeline_.stop();
      running_ = false;
      healthy_ = false;
      ++unexpectedStops_;
      ++rmfCandidateIndex_;
      activeEndpoint_ = "";
      services_.stream(false, 1, nextSequence(), nowMs);
      retryAtMs_ = nowMs + kFastReconnectDelayMs;
    }
  }

  void decodedFrames(std::size_t count, std::uint32_t nowMs) {
    if (count > 0) {
      lastProgressMs_ = nowMs;
      sourceDeadSinceMs_ = 0;
    }
    if (running_ && !healthy_ && count > 0) {
      healthy_ = true;
      retryIndex_ = 0;
      failuresSinceHealthy_ = 0;
      exhaustedCycles_ = 0;
      if (activeEndpoint_.length() > 0) {
        writeLastKnownGood(activeEndpointAac_ ? "aac|" + activeEndpoint_ : activeEndpoint_);
      }
      services_.stream(true, 1, nextSequence(), nowMs);
    }
  }

  bool running() const { return running_; }
  bool healthy() const { return healthy_; }
  std::uint32_t startAttempts() const { return startAttempts_; }
  std::uint32_t startFailures() const { return startFailures_; }
  std::uint32_t unexpectedStops() const { return unexpectedStops_; }
  std::uint32_t lastStartDurationMs() const { return lastStartDurationMs_; }
  std::size_t exhaustedCycles() const { return exhaustedCycles_; }
  bool networkCritical() const {
    return networkAvailable_ && hlsActive_;
  }

 private:
  TlsGate tlsGate_ = nullptr;
  static constexpr const char* kRmfonRootCa = R"CERT(-----BEGIN CERTIFICATE-----
MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT
Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg
VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm
aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo
I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng
o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G
A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD
VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB
zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW
RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=
-----END CERTIFICATE-----)CERT";
  static constexpr std::uint32_t kDiscoveryTimeoutMs = 5000;
  // The mbedTLS handshake has its own clock; setConnectTimeout and setTimeout
  // do not bound it and the framework default is 120 s. Five seconds was tried
  // first, copied from the icon fetch where the comment calls it generous
  // "because nothing stalls on it". It is not generous for api.rmfon.pl: on
  // 2026-09-09 four cold-start discoveries all failed at ~6.0-6.4 s with
  // 110 580 bytes of largest free internal block, and in the very next run the
  // same endpoint answered in 5645 ms and 6186 ms once the budget was twenty.
  // Memory was never the constraint; the cap was.
  //
  // Twenty is safe because the only caller left is prefetchRmfPools() on
  // logoFetchTask, which is deliberately not watchdog-subscribed and which
  // nothing waits on. resolveRmf() no longer reaches TLS at all — see there.
  static constexpr std::uint32_t kDiscoveryHandshakeSeconds = 20;
  static constexpr std::uint32_t kFastReconnectDelayMs = 250;
  // A stalled-but-open connection (server stops sending, socket stays up)
  // keeps AudioGeneratorMP3::loop() "running" forever, so unexpectedStops_
  // never fires and playback wedges silently (hit live 2026-07-17 on an
  // rmfstream edge minutes after the catalog swap). Eight seconds without a
  // single drained frame while healthy is unambiguous: local drain moves a
  // block at least every ~3 s and BT drain is continuous. The Bluetooth
  // standby window (drain paused) is excluded via startAllowed.
  static constexpr std::uint32_t kStreamProgressStallMs = 8000;
  static constexpr std::uint32_t kSourceDeadReopenMs = 2000;
  static constexpr std::size_t kRmfCandidateCount = 10;
  // Lanes with the AAC decoder turn the wheel through both decks of the pool.
#if defined(OPEN_RADIO_HAS_HLS)
  static constexpr std::size_t kRmfWheelSize = 2U * kRmfCandidateCount;
#else
  static constexpr std::size_t kRmfWheelSize = kRmfCandidateCount;
#endif
  static constexpr std::uint32_t kNextCandidateDelayMs = 1000;
  static constexpr std::uint32_t kRetryDelaysMs[] = {5000, 30000, 120000,
                                                      600000};

  static String decodeJsonString(const String& input, int start) {
    String output;
    output.reserve(127);
    bool escaped = false;
    for (int index = start; index < input.length() && output.length() < 127;
         ++index) {
      const char value = input[index];
      if (escaped) {
        if (value == '/' || value == '\\' || value == '"') output += value;
        escaped = false;
      } else if (value == '\\') {
        escaped = true;
      } else if (value == '"') {
        break;
      } else {
        output += value;
      }
    }
    return output;
  }

  static String extractArrayString(const String& input, const char* marker,
                                   std::size_t candidateIndex) {
    int cursor = input.indexOf(marker);
    if (cursor < 0) return {};
    cursor += std::strlen(marker);
    for (std::size_t index = 0; index <= candidateIndex; ++index) {
      const String candidate = decodeJsonString(input, cursor);
      if (candidate.length() == 0) return {};
      if (index == candidateIndex) return candidate;
      const int next = input.indexOf("\",\"", cursor);
      if (next < 0) return {};
      cursor = next + 3;
    }
    return {};
  }

  // Host (and port) of an endpoint, never its path, query or fragment.
  static String hostOf(const String& endpoint) {
    const int scheme = endpoint.indexOf("://");
    const int from = scheme < 0 ? 0 : scheme + 3;
    int to = endpoint.length();
    for (const char stop : {'/', '?', '#'}) {
      const int at = endpoint.indexOf(stop, from);
      if (at >= 0 && at < to) to = at;
    }
    return endpoint.substring(from, to);
  }

  // Alternates in one lap: the cached operator pool of a built-in RMF station,
  // otherwise the slot's or the catalog row's own list (candidatesAtSelect_).
  std::size_t candidateLap() const {
    if (slots_.endpointCount(stationIndex_) == 0 &&
        stationUsesRmfonPools(stationIndex_)) {
      const int slot = rmfPoolSlot(rmfonStationIdFor(stationIndex_));
      if (slot >= 0) {
        PoolLock lock;
        if (rmfPoolBody_[slot].length() > 0) return kRmfWheelSize;
      }
    }
    return candidatesAtSelect_;
  }

  static String audioHttpUrl(String endpoint) {
    if (endpoint.startsWith("https://")) endpoint.remove(4, 1);
    return endpoint;
  }

  std::uint32_t nextSequence() { return sequence_++; }

  String fetchJson(const char* endpoint) {
    // The ONLY door to TLS in this class, which is why the gate stands here
    // rather than at the call sites. Guarding `prefetchRmfPools()` alone left
    // `resolveRmf()` open: with an empty cache, switching to an rmfon station
    // opened a handshake during Bluedroid's asynchronous bring-up — exactly
    // the window the device panicked in on 2026-08-01. An adversarial review
    // found it after I had called the matter closed.
    //
    // An empty result is a normal, handled path here: `resolveEndpoint()` then
    // falls back to the pinned plain-HTTP mount, and the comment on
    // `rmfPoolSlot()` states outright that TLS discovery during playback
    // "fails BY DESIGN". Nothing that would have worked is lost.
    if (tlsGate_ != nullptr && !tlsGate_()) {
      Serial.println("rmf discovery_skipped reason=internal-heap-reserved");
      return {};
    }
    WiFiClientSecure client;
    client.setCACert(kRmfonRootCa);
    // The mbedTLS handshake has its own clock: HTTPClient's connect and read
    // timeouts below do not bound it, and the framework default is 120 s. A
    // peer that accepts TCP and then goes quiet in the handshake held this
    // call — and the owner task it runs on, under a 30-second watchdog —
    // for two minutes (audit 2026-09-08). Same figure as
    // logo_store.hpp uses for the icon fetch.
    client.setHandshakeTimeout(kDiscoveryHandshakeSeconds);
    HTTPClient request;
    // HTTP/1.0, for the same reason logo_store.hpp uses it. Reading the body
    // ourselves means reading the RAW socket, and on HTTP/1.1 that carries the
    // chunk framing rather than the body — and worse, a keep-alive peer never
    // closes, so a loop waiting for "no more data" waits out its whole deadline
    // with the complete body already in hand. That is exactly what happened on
    // the first device run of this change: reason=body-deadline with len=1271,
    // a full answer thrown away, and the two discoveries after it failing at
    // the connect timeout on a client left in a bad state. Under 1.0 the server
    // sends a length or closes, so the read ends when the answer does.
    request.useHTTP10(true);
    request.setConnectTimeout(kDiscoveryTimeoutMs);
    request.setTimeout(kDiscoveryTimeoutMs);
    // Every failure below used to `return {}` in silence, so three failed
    // discoveries produced one terse "pools-missing" and no reason at all. On
    // 2026-09-09 that cost a whole measurement: the pool came back empty for
    // the first time in the project's history, the owner heard the stream cut
    // out, and the log could not say whether the handshake budget, the heap or
    // the peer was to blame. A discovery that fails must say why; the reason is
    // a short token, never the endpoint, so the soak sanitizer still passes it.
    // The audit of 2026-09-09 could name every hypothesis for an empty pool but
    // one: internal-heap exhaustion during a depth-3 chain verification. The
    // gate above tests Bluetooth flags, never actual free memory, and nothing
    // sampled the heap at the moment of failure. One number settles it.
    const std::uint32_t startedAtMs = millis();
    const auto largestInternal = static_cast<unsigned long>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!request.begin(client, endpoint)) {
      Serial.printf("rmf discovery_failed reason=begin elapsed_ms=%lu heap_largest=%lu\n",
                    static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
      return {};
    }
    const int status = request.GET();
    const int length = request.getSize();
    // getSize() returns -1 when the peer answers without a Content-Length, which
    // is what a chunked response looks like. Rejecting that alongside a genuine
    // zero cost one of the three pools permanently: on 2026-09-09 the middle
    // discovery answered HTTP 200 with len=-1 in 6312 ms and was thrown away, so
    // a third of the rotation could never be cached no matter how generous the
    // handshake budget was. An unknown length is not an error; it only means the
    // bound has to come from the body, and it already does — getString() is
    // checked against kDiscoveryResponseBytes immediately below, and the read is
    // bounded by setTimeout(kDiscoveryTimeoutMs) above.
    const bool lengthUnknown = length < 0;
    if (status != HTTP_CODE_OK || length == 0 ||
        (!lengthUnknown && length > static_cast<int>(kDiscoveryResponseBytes))) {
      // A negative status is an HTTPClient transport error (-1 connection
      // refused, -5 connection lost, -11 read timeout); the handshake budget
      // and the connect budget both surface here, which is exactly the pair we
      // need to tell apart.
      Serial.printf(
          "rmf discovery_failed reason=%s status=%d len=%d elapsed_ms=%lu "
          "heap_largest=%lu\n",
          status != HTTP_CODE_OK ? "http" : "length", status, length,
          static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
      request.end();
      return {};
    }
    // NOT getString(). The pinned core's body loop is
    // `while (connected() && (len > 0 || len == -1)) { … else delay(1); }`
    // (HTTPClient.cpp:1316-1395) and consults no clock at all: setTimeout()
    // bounds the header read and the socket, never this. A peer that completes
    // TLS, answers 200 and then holds the connection open without sending the
    // body spins here forever — on the owner loop that is a 30-second watchdog
    // reboot, and on logoFetchTask it never releases the Bluetooth auto-start
    // gate, so the radio silently never gets Bluetooth. Found in the
    // pre-release review of 2026-09-09; I had claimed in writing that setTimeout
    // covered this, and it does not. The deadline below is ours.
    String body;
    if (Stream* upstream = request.getStreamPtr(); upstream != nullptr) {
      const std::uint32_t readDeadlineMs = millis() + kDiscoveryTimeoutMs;
      char chunk[129];  // one spare byte: String::concat(ptr, n) copies n + 1
      while (static_cast<std::int32_t>(millis() - readDeadlineMs) < 0) {
        const int ready = upstream->available();
        if (ready <= 0) {
          // Under HTTP/1.0 the peer closes when the body ends, so a closed
          // connection with an empty buffer is the normal, successful exit.
          if (!request.connected()) break;
          delay(5);
          continue;
        }
        const int wanted = ready < static_cast<int>(sizeof(chunk) - 1)
                               ? ready
                               : static_cast<int>(sizeof(chunk) - 1);
        const int got = upstream->readBytes(chunk, wanted);
        if (got <= 0) continue;
        chunk[got] = '\0';
        if (body.length() + static_cast<std::size_t>(got) > kDiscoveryResponseBytes) {
          Serial.printf("rmf discovery_failed reason=oversize len=%u elapsed_ms=%lu "
                        "heap_largest=%lu\n",
                        static_cast<unsigned>(body.length() + got),
                        static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
          request.end();
          return {};
        }
        if (!body.concat(chunk, static_cast<unsigned int>(got))) {
          Serial.printf("rmf discovery_failed reason=alloc len=%u elapsed_ms=%lu heap_largest=%lu\n",
                        static_cast<unsigned>(body.length()),
                        static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
          request.end();
          return {};
        }
        if (!lengthUnknown && body.length() >= static_cast<std::size_t>(length)) break;
      }
      if (static_cast<std::int32_t>(millis() - readDeadlineMs) >= 0) {
        Serial.printf("rmf discovery_failed reason=body-deadline len=%u elapsed_ms=%lu "
                      "heap_largest=%lu\n",
                      static_cast<unsigned>(body.length()),
                      static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
        request.end();
        return {};
      }
    }
    request.end();
    if (!lengthUnknown && body.length() != static_cast<std::size_t>(length)) {
      Serial.printf("rmf discovery_failed reason=short len=%u want=%d elapsed_ms=%lu heap_largest=%lu\n",
                    static_cast<unsigned>(body.length()), length,
                    static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
      return {};
    }
    if (body.length() == 0 || body.length() > kDiscoveryResponseBytes) {
      Serial.printf("rmf discovery_failed reason=body len=%u elapsed_ms=%lu heap_largest=%lu\n",
                    static_cast<unsigned>(body.length()),
                    static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
      return {};
    }
    Serial.printf("rmf discovery_ok len=%u elapsed_ms=%lu heap_largest=%lu\n",
                  static_cast<unsigned>(body.length()),
                  static_cast<unsigned long>(millis() - startedAtMs), largestInternal);
    return body;
  }

  // TLS discovery needs ~40 KB of internal heap for the handshake; with the
  // Bluetooth stack active only ~25-30 KB is free, so a station switch during
  // playback fails discovery BY DESIGN. With last-known-good re-namespaced to
  // lkg2_ that stranded every rmfon station until reboot. Pinned plain-HTTP
  // edge mounts (live-probed 2026-07-17; mount names are long-lived, the
  // rotating part is only the rs-host pool) keep those stations startable in
  // any heap state; a later healthy TLS discovery refreshes LKG normally.
  static int rmfPoolSlot(int rmfonStationId) {
    if (rmfonStationId == 5) return 0;
    if (rmfonStationId == 6) return 1;
    if (rmfonStationId == 29) return 2;
    return -1;
  }

  // The operator's discovery API is keyed by its own station ids (5, 6, 29),
  // which have no relation to our catalog order. This is the only place that
  // knows the mapping, and it derives it from the catalog row rather than from
  // a second hand-maintained list that could drift out of lockstep.
  static int rmfonStationIdFor(std::uint8_t stationIndex) {
    if (stationIndex >= open_radio::generated::kStationCount) return -1;
    const auto& station = open_radio::generated::kStations[stationIndex];
    if (station.playbackCount == 0) return -1;
    const auto& first =
        open_radio::generated::kPlaybackEndpoints[station.playbackOffset];
    if (first.resolver != open_radio::generated::PlaybackResolver::RmfonPool) {
      return -1;
    }
    const String url(first.url);
    if (url.endsWith("/rmf_fm")) return 5;
    if (url.endsWith("/rmf_maxxx")) return 6;
    if (url.endsWith("/rmf_club")) return 29;
    return -1;
  }

  static bool stationUsesRmfonPools(std::uint8_t stationIndex) {
    return rmfonStationIdFor(stationIndex) > 0;
  }

 public:
  // Discovery over TLS needs ~40 KB of internal heap and is therefore only
  // reliable before the Bluetooth stack starts. Prefetching all three rmfon
  // pools right after Wi-Fi connects (heap-rich window) gives every later
  // in-session station switch a cached pool with real edge rotation and no
  // TLS at all; a stalled edge rotates to the next cached candidate instead
  // of stuttering forever (owner-reported on RMF FM 2026-07-17).
  //
  // Since 0.4.2 this runs on the logo-fetch task, not the owner loop: three
  // sequential discoveries (DNS ladder, connect, handshake) at EVERY Wi-Fi
  // connect could exceed the owner's 30-second watchdog on a sick network
  // (audit 2026-09-08). The pool bodies are therefore shared between two tasks and every
  // access goes through PoolLock; the lock is held only around the String
  // assignment or the candidate extraction, never across the network.
  void prefetchRmfPools(const std::atomic<bool>* abort = nullptr) {
    // Without the mutex there is no safe way to be the second writer of
    // rmfPoolBody_, so this pass stands down and leaves the pool to
    // resolveRmf() on the owner loop, which is single-threaded. The station
    // still plays: resolveEndpoint() falls back to the pinned mount.
    if (!PoolLock::available()) {
      Serial.println("rmf prefetch_skipped reason=no-lock");
      return;
    }
    static constexpr int kRmfIds[3] = {5, 6, 29};
    bool cached = false;
    for (int index = 0; index < 3; ++index) {
      // A pairing tap or a Bluetooth recovery asks the logo task to stop, and the
      // tap waiting behind it expires after sixty seconds. Three discoveries can
      // take longer than that on a sick network, so the flag is asked before each.
      if (abort != nullptr && abort->load(std::memory_order_relaxed)) {
        Serial.printf("rmf prefetch_aborted before=%d\n", index);
        break;
      }
      const String discovery = "https://api.rmfon.pl/stations/" +
                               String(kRmfIds[index]) + "/streams";
      const String body = fetchJson(discovery.c_str());
      if (body.length() > 0) {
        PoolLock lock;
        rmfPoolBody_[index] = body;
        if (index == 0) cached = true;
      }
    }
    Serial.printf("stream_stage stage=resolver duration_ms=0 result=%s\n",
                  // Renamed on 2026-09-09. Until f0e50be this line read the STATE
                  // of slot 0 from any source; now it reports whether THIS pass
                  // filled it. The old tokens survived the change and invited a
                  // comparison across releases that is not valid — one was made,
                  // and it produced a wrong conclusion about a regression. New
                  // tokens make the incompatibility impossible to miss.
                  cached ? "pools-pass-filled" : "pools-pass-empty");
  }

 private:
  // One candidate from a pool body. The wheel spans two decks: indices
  // 0..N-1 pick the 128 kb/s MP3 mounts — the DEFAULT, because the Helix
  // HE-AAC+SBR decode is heavy enough to starve the A2DP TX side on this
  // device (owner A/B 2026-07-17 evening, perfect Wi-Fi: aacp over BT cut
  // audibly with full queues and zero injected silence, MP3 played clean).
  // Indices N..2N-1 pick the 48-64 kb/s aacp mounts (~6-8 KB/s) as the
  // CONGESTION FALLBACK: sustained MP3 deaths under a starved 2.4 GHz band
  // advance the wheel into the aacp deck automatically. Lanes without the
  // AAC decoder never leave the MP3 deck. The deck is the codec authority —
  // mount NAMES are not (Club's aacp pool mounts plain "CLUB", no "48").
  String rmfCandidateFromBody(const String& body) {
    if (body.length() == 0) return {};
    resolvedAacStream_ = false;
#if defined(OPEN_RADIO_HAS_HLS)
    const std::size_t wheel = rmfCandidateIndex_ % (2U * kRmfCandidateCount);
    if (wheel < kRmfCandidateCount) {
      const String mp3Candidate = audioHttpUrl(
          extractArrayString(body, "\"item_mp3\":[\"", wheel));
      if (mp3Candidate.length() > 0) return mp3Candidate;
    }
    const String aacCandidate = audioHttpUrl(extractArrayString(
        body, "\"item\":[\"", wheel % kRmfCandidateCount));
    if (aacCandidate.length() > 0) {
      resolvedAacStream_ = true;
      return aacCandidate;
    }
    return {};
#else
    return audioHttpUrl(extractArrayString(
        body, "\"item_mp3\":[\"", rmfCandidateIndex_ % kRmfCandidateCount));
#endif
  }

  String resolveRmf(int rmfonStationId) {
    const int slot = rmfPoolSlot(rmfonStationId);
    if (slot >= 0) {
      String cached;
      {
        PoolLock lock;
        if (rmfPoolBody_[slot].length() > 0) {
          cached = rmfCandidateFromBody(rmfPoolBody_[slot]);
        }
      }
      if (cached.length() > 0) return cached;
    }
    // The owner loop does NO TLS. It used to open a discovery here, and that
    // put a name lookup, a TCP connect, a handshake and a body read inside one
    // iteration of a task subscribed to a 30-second watchdog. Three of those
    // four we bound ourselves; the name lookup we do not — the core resolves
    // through getaddrinfo and asks for A and AAAA, and nothing in our code
    // caps how long that takes. Rather than argue the total stays under thirty
    // seconds, the owner stops reaching for the network at all (pre-release
    // review of 2026-09-09).
    //
    // Nothing is lost that was reliably there: when the pool is empty
    // resolveEndpoint() falls back to the pinned mount, which is exactly what
    // happened on the 2026-09-09 device run, where this call failed at 5210 ms
    // and the station started from its pinned edge one second later. The pool
    // is filled by prefetchRmfPools() on logoFetchTask, which is not
    // watchdog-subscribed, and it refills at every Wi-Fi connect.
    // The operator returns a pool of transient edges. Rotate through the live
    // pool after failed opens/disconnects instead of pinning its first result.
    // An empty result is fine: resolveEndpoint() then falls back to the pinned
    // mount recorded in the catalog row, which is where that URL now lives.
    return {};
  }

  // Stored/session endpoints carry their codec explicitly: "aac|<url>" for
  // aacp streams, bare URL for MP3. Prefix-less values written before this
  // scheme fall back to the RMFFM48/RMFMAXXX48 suffix (the only aacp entries
  // that can exist from that era); a later healthy stream rewrites them with
  // the prefix.
  static bool parseStoredEndpoint(String& value) {
    if (value.startsWith("aac|")) {
      value = value.substring(4);
      return true;
    }
    return value.endsWith("48");
  }

  // Endpoints are catalog data, not code. The wheel walks this station's own
  // playback rows, so every station self-heals across its alternates. Before
  // 0.2 only the rmfon stations (through their pools) and ESKA (through a
  // hand-written ic1/ic2 flip) could do that, and the other six replayed one
  // dead URL forever no matter how often the rotation counter advanced.
  String resolveEndpoint() {
    resolvedAacStream_ = false;
    if (stationIndex_ >= open_radio::generated::kStationCount) return {};
    // A reassigned slot rotates through its own verified edges on the same
    // wheel the curated stations use, so a dead edge recovers identically.
    if (const char* chosen = slots_.url(stationIndex_, rmfCandidateIndex_);
        chosen != nullptr) {
      return String(chosen);
    }
    const auto& station = open_radio::generated::kStations[stationIndex_];
    if (station.playbackCount == 0) return {};
    const std::size_t slot = rmfCandidateIndex_ % station.playbackCount;
    const auto& endpoint =
        open_radio::generated::kPlaybackEndpoints[station.playbackOffset + slot];
    if (endpoint.resolver ==
        open_radio::generated::PlaybackResolver::RmfonPool) {
      const int rmfonStationId = rmfonStationIdFor(stationIndex_);
      if (rmfonStationId > 0) {
        const String resolved = resolveRmf(rmfonStationId);
        if (resolved.length() > 0) return resolved;
      }
    }
    return String(endpoint.url);
  }

  // lkg4: the key still names the TILE, because the upgrade path erases derived
  // state by enumerating `lkg4_0`..`lkg4_8` and a key that varied with content
  // could not be enumerated at all. What changed is the VALUE: it now carries
  // the directory index of the station the endpoint was proven for, and a
  // reader that does not recognise it treats the cache as absent.
  //
  // Why that was needed. The cache was keyed by tile alone and erased only when
  // build_sha changed. Swap the station in tile 3 without reflashing and the
  // tile still opened tile 3's remembered endpoint — the station the owner had
  // just replaced. Nothing errored; it simply played the wrong radio. The same
  // shape bit twice before (Pogoda 07-17, Jedynka's 192 kb/s mount) and both
  // times the answer was to bump the prefix, which fixes the release that ships
  // and nothing the owner does afterwards.
  //
  // Cross-build drift of the index does not matter: a new image changes
  // build_sha, which erases every lkg4_* before any of them is read.
  String lastKnownGoodKey() const { return "lkg4_" + String(stationIndex_); }

  // The station this tile currently points at. kBuiltIn means the factory one.
  std::int16_t boundDirectoryIndex() const { return slots_.directoryIndex(stationIndex_); }

  // Reads the cache ONLY if it was stored for the station now in this tile.
  String readLastKnownGood() const {
    const String raw = preferences_.getString(lastKnownGoodKey().c_str());
    const int split = raw.indexOf('|');
    if (split <= 0) return {};                       // absent, or written by an older format
    // Compared as TEXT, never with toInt(). Arduino's String::toInt() is atol():
    // it skips blanks, stops at the first non-digit and answers 0 when it finds
    // no digits at all. So "aac" parses as 0 — and a value in the old format,
    // written as "aac" then a bar then the address, would have been accepted on
    // any tile whose directory index
    // happens to be 0 and handed back another station's endpoint. Two independent
    // reviews found this on the same afternoon, from different angles.
    // Text comparison closes the whole class: "007", " 3", "+3", "3abc" and
    // "junk" stop matching anything, because the writer only ever emits the
    // canonical form.
    if (raw.substring(0, split) != String(boundDirectoryIndex())) return {};
    return raw.substring(split + 1);
  }

  void writeLastKnownGood(const String& payload) {
    preferences_.putString(lastKnownGoodKey().c_str(),
                           String(boundDirectoryIndex()) + "|" + payload);
  }

  void start(std::uint32_t nowMs) {
    const std::uint32_t startedAtMs = millis();
    ++startAttempts_;
    String endpoint = activeEndpoint_;
    bool aacEndpoint = activeEndpointAac_;
    ResolverSelection selection = ResolverSelection::LastKnownGood;
    if (endpoint.length() == 0) {
      const std::uint32_t resolverStartedAtMs = millis();
      endpoint = resolveEndpoint();
      aacEndpoint = resolvedAacStream_;
      Serial.printf("stream_stage stage=resolver duration_ms=%lu result=%s\n",
                    static_cast<unsigned long>(millis() - resolverStartedAtMs),
                    endpoint.length() > 0 ? "ok" : "fail");
      selection = ResolverSelection::Primary;
    }
    if (endpoint.length() == 0 || endpoint.length() >= 128) {
      endpoint = readLastKnownGood();
      aacEndpoint = parseStoredEndpoint(endpoint);
      selection = ResolverSelection::LastKnownGood;
    }
    // Which edge this open goes to. The key is address= so the evidence
    // sanitizer drops the line; the raw lab log keeps it. Without it a run of
    // failed opens cannot tell a dead edge from a radio that lost the network.
    Serial.printf("stream_stage stage=endpoint source=%s candidate=%u address=%s\n",
                  selection == ResolverSelection::Primary ? "resolved" : "last-known-good",
                  static_cast<unsigned>(rmfCandidateIndex_), hostOf(endpoint).c_str());
    hlsActive_ = endpoint.endsWith(".m3u8");
    const bool aacActive = !hlsActive_ && aacEndpoint;
    bool started = false;
#if defined(OPEN_RADIO_HAS_HLS)
    if (hlsActive_) started = pipeline_.startHls(endpoint.c_str());
#endif
    if (!hlsActive_) started = pipeline_.start(endpoint.c_str(), aacActive);
    lastStartDurationMs_ = millis() - startedAtMs;
    if (endpoint.length() == 0 || endpoint.length() >= 128 || !started) {
      ++startFailures_;
      ++rmfCandidateIndex_;
      ++failuresSinceHealthy_;
      // A full lap through this station's alternates without a single healthy
      // stream is the difference between "the edge blinked" and "this station
      // is not broadcasting"; the failure screen reads this counter. The lap of
      // an RMF station is its operator pool, not its one pinned catalog row.
      // On 2026-10-12 three or four of the ten RMF FM MP3 edges were dead (the
      // -cyfronet hosts answered on neither 80 nor 443); every failed open was
      // counted as the whole station failing, and four in a row put the radio
      // on its 600 s retry with "not broadcasting" on screen while six live
      // edges stayed untried.
      const std::size_t lap = candidateLap();
      const bool lapFailed = lap == 0 || failuresSinceHealthy_ % lap == 0;
      if (lap > 0 && lapFailed) {
        ++exhaustedCycles_;
        Serial.printf(
            "station_health station=%u exhausted_cycles=%u candidates=%u\n",
            static_cast<unsigned>(stationIndex_),
            static_cast<unsigned>(exhaustedCycles_),
            static_cast<unsigned>(lap));
      }
      activeEndpoint_ = "";
      ResolverResultDto failed;
      failed.status = ResolverStatus::RetryScheduled;
      // Waits count from the end of the failed open, not from nowMs: the open
      // above blocks for its whole connect timeout, and a deadline taken
      // before it had already passed when the loop came back.
      const std::uint32_t failedAtMs = millis();
      if (lapFailed) {
        failed.retryAtMs = failedAtMs + kRetryDelaysMs[std::min<std::size_t>(
                                            retryIndex_, std::size(kRetryDelaysMs) - 1)];
        scheduleRetry(failedAtMs);
      } else {
        // Inside a lap the next alternate goes next. Backing off is for a
        // station that failed on all of them, not for one dead edge; the
        // second between opens is for touch and Bluetooth, because each open
        // that fails holds this loop for its whole connect timeout.
        retryAtMs_ = failedAtMs + kNextCandidateDelayMs;
        failed.retryAtMs = retryAtMs_;
      }
      services_.resolver(failed, 1, nextSequence(), failedAtMs);
      return;
    }
    running_ = true;
    healthy_ = false;
    lastProgressMs_ = nowMs;
    sourceDeadSinceMs_ = 0;
    lastSourceEventCount_ = pipeline_.sourceStatusEvents();
    activeEndpoint_ = endpoint;
    activeEndpointAac_ = aacActive;
    ResolverResultDto ready;
    ready.status = ResolverStatus::Playing;
    ready.selected = selection;
    services_.resolver(ready, 1, nextSequence(), nowMs);
  }

  void scheduleRetry(std::uint32_t nowMs) {
    retryAtMs_ = nowMs + kRetryDelaysMs[std::min<std::size_t>(
                                  retryIndex_, std::size(kRetryDelaysMs) - 1)];
    if (retryIndex_ + 1 < std::size(kRetryDelaysMs)) ++retryIndex_;
  }

  Preferences& preferences_;
  RuntimeServiceBridge& services_;
  Mp3StreamPipeline& pipeline_;
  const StationSlots& slots_;
  String activeEndpoint_;
  std::uint32_t sequence_ = 100;
  std::uint32_t retryAtMs_ = 0;
  std::uint32_t startAttempts_ = 0;
  std::uint32_t startFailures_ = 0;
  std::uint32_t unexpectedStops_ = 0;
  std::uint32_t lastStartDurationMs_ = 0;
  std::uint32_t lastProgressMs_ = 0;
  std::uint32_t sourceDeadSinceMs_ = 0;
  std::uint32_t lastSourceEventCount_ = 0;
  bool activeEndpointAac_ = false;
  bool resolvedAacStream_ = false;
  std::size_t retryIndex_ = 0;
  std::size_t rmfCandidateIndex_ = 0;
  std::size_t exhaustedCycles_ = 0;
  // Failed opens since the last healthy stream; a lap is counted on these,
  // not on rmfCandidateIndex_, which also turns when a healthy stream dies.
  std::size_t failuresSinceHealthy_ = 0;
  std::size_t candidatesAtSelect_ = 0;
  // Guards rmfPoolBody_ between the logo-fetch task (prefetch) and the owner
  // loop (resolve). A function-local static: created once, thread-safe by the
  // language, no constructor-order question for a global StationRuntime.
  struct PoolLock {
    // xSemaphoreCreateMutex() can return nullptr, and xSemaphoreTake(nullptr,
    // …) trips a FreeRTOS assertion rather than failing softly. The audit of
    // 2026-09-09 flagged this against a measured dram_min of 13 992 bytes, so
    // the allocation failing is not a theoretical concern on this device. A
    // null handle therefore means "do not lock", and the only caller that can
    // create a second writer refuses to run at all — see prefetchRmfPools().
    // Degrading to an unguarded String shared by two tasks would trade an
    // assertion for a torn read, which is not an improvement.
    PoolLock() {
      if (mutex() != nullptr) xSemaphoreTake(mutex(), portMAX_DELAY);
    }
    ~PoolLock() {
      if (mutex() != nullptr) xSemaphoreGive(mutex());
    }
    PoolLock(const PoolLock&) = delete;
    PoolLock& operator=(const PoolLock&) = delete;
    static bool available() { return mutex() != nullptr; }
    static SemaphoreHandle_t mutex() {
      static SemaphoreHandle_t handle = xSemaphoreCreateMutex();
      return handle;
    }
  };
  String rmfPoolBody_[3];
  std::uint8_t stationIndex_ = 0;
  bool networkAvailable_ = false;
  bool hlsActive_ = false;
  bool running_ = false;
  bool healthy_ = false;
};

}  // namespace open_radio::public_candidate
