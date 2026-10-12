#pragma once

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "onboarding_assets.hpp"
#include "station_slots.hpp"
#include "open_radio/runtime_service_bridge.hpp"
#include "open_radio/service_adapters.hpp"

namespace open_radio::public_candidate {

class DeviceNetworkRuntime {
 public:
  using ConnectedCallback = void (*)(void* context, bool newlyProvisioned);

  DeviceNetworkRuntime(Preferences& preferences, RuntimeServiceBridge& services)
      : preferences_(preferences), services_(services) {}

  // main.cpp owns the rendered station table and has to rebuild it when the
  // installer reassigns a slot, or the screen keeps the old names.
  void bindSlots(StationSlots& slots, void (*changed)(void*), void* context) {
    slots_ = &slots;
    slotsChangedCallback_ = changed;
    slotsChangedContext_ = context;
  }

  void setConnectedCallback(ConnectedCallback callback, void* context) {
    connectedCallback_ = callback;
    connectedContext_ = context;
  }

  void begin(std::uint32_t nowMs) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    WiFi.mode(WIFI_STA);
    loadProfiles();
    Serial.printf("wifi_boot profiles=%u\n",
                  static_cast<unsigned>(profileCount_));
    if (!connectBestKnown(nowMs)) {
      startPortal();
      if (!portalActive_) scheduleRetry(nowMs);
    }
  }

  void loop(std::uint32_t nowMs) {
    if (portalActive_) {
      if (dnsActive_) dns_.processNextRequest();
      server_.handleClient();
      // A portal nobody ever submits used to stay up forever. An open access
      // point is both a standing invitation and a competitor for the air the
      // audio path needs.
      if (portalIdleDeadlineMs_ == 0) portalIdleDeadlineMs_ = nowMs + kPortalIdleTimeoutMs;
      else if (static_cast<std::int32_t>(nowMs - portalIdleDeadlineMs_) >= 0) {
        if (profileCount_ == 0 || !connected_) {
          // An unprovisioned device has no radio to protect and no on-screen
          // way to reopen a closed portal — idling out here stranded the
          // owner on a blank QR square with only a power cycle left. The
          // setup portal outlives slow humans.
          //
          // `|| !connected_` closes the middle case, found on a box-fresh cube
          // on 2026-08-01. A profile that was SAVED but never connected — wrong
          // password, network out of range — left profileCount_ above zero, so
          // the portal closed on schedule while the screen went on showing the
          // QR code. From outside that is indistinguishable from a hang, and
          // the only way out was a power cycle: exactly the fault the branch
          // above exists to prevent, reached by a different door.
          //
          // Having profiles is not the same as having a network. The thing
          // worth protecting is a WORKING connection, and that is what the
          // condition now asks about.
          portalIdleDeadlineMs_ = nowMs + kPortalIdleTimeoutMs;
        } else {
          Serial.println("wifi_portal stage=session-closed reason=idle");
          portalIdleDeadlineMs_ = 0;
          stopPortal();
        }
      }
    }
    if (consoleActive_) {
      if (!portalActive_) server_.handleClient();
      if (!connected_) {
        Serial.println("console stage=session-lost reason=offline");
        stopConsoleSession();
      } else if (consoleCloseAtMs_ != 0 &&
                 static_cast<std::int32_t>(nowMs - consoleCloseAtMs_) >= 0) {
        Serial.println("console stage=session-done reason=slots-saved");
        stopConsoleSession();
      } else if (static_cast<std::int32_t>(nowMs - consoleHardDeadlineMs_) >=
                 0) {
        Serial.println("console stage=session-expired reason=window");
        stopConsoleSession();
      } else if (static_cast<std::int32_t>(nowMs - consoleDeadlineMs_) >= 0) {
        Serial.println("console stage=session-expired reason=page-gone");
        stopConsoleSession();
      }
    }
    if (pendingConnect_ &&
        static_cast<std::int32_t>(nowMs - pendingConnectAtMs_) >= 0) {
      pendingConnect_ = false;
      beginConnection(pendingSsid_, pendingPassword_, nowMs, true, kNoProfile);
    }
    if (connecting_) {
      if (WiFi.status() == WL_CONNECTED) {
        connecting_ = false;
        connected_ = true;
        Serial.printf("wifi_state connected=yes profile=%u\n",
                      static_cast<unsigned>(activeProfileIndex_));
        retryIndex_ = 0;
        failedProfileMask_ = 0;
        consecutiveTimeouts_ = 0;
        scanMissFallbackUsed_ = false;
        NetworkSelectionDto selected;
        selected.status = NetworkSelectionStatus::Selected;
        selected.selectedProfile = NetworkProfileRef::Home;
        services_.network(selected, 1, nextSequence(), nowMs);
        const bool provisioned = newlyProvisioned_;
        const bool persisted = !provisioned ||
                               persistProvisionedProfile(pendingSsid_,
                                                         pendingPassword_);
        clearPendingCredentials();
        provisioningDeadlineMs_ = 0;
        newlyProvisioned_ = false;
        // The portal used to end here, the instant the credentials worked.
        // That killed the access point the installer's phone was standing on,
        // so the four screens after this one — radio playing, location,
        // Bluetooth, done — were unreachable on real hardware even though they
        // exist in the page, the state machine and the parity tests. Nobody
        // caught it because the flow could not be walked without a cube.
        // The session now outlives provisioning and closes itself when nobody
        // is using it any more.
        // Down the moment the credentials work, and for two reasons. The page
        // cannot survive anyway: in AP+STA the soft access point is dragged
        // onto the router's channel as the station associates and every phone
        // attached to it is dropped. And an access point that keeps running
        // spends airtime the Bluetooth link needs — an earlier attempt to hold
        // it open for five minutes produced audible stuttering that stopped
        // when it finally closed.
        if (provisioned && persisted) stopPortal();
        if (connectedCallback_ != nullptr && (!provisioned || persisted)) {
          connectedCallback_(connectedContext_, provisioned && persisted);
        }
      } else if (nowMs - connectionStartedAtMs_ >= kConnectionTimeoutMs ||
                 (!newlyProvisioned_ &&
                  nowMs - connectionStartedAtMs_ >= kEarlyGiveUpMs &&
                  (WiFi.status() == WL_NO_SSID_AVAIL ||
                   WiFi.status() == WL_CONNECT_FAILED))) {
        connecting_ = false;
        Serial.printf("wifi_state connected=no reason=timeout status=%d\n",
                      static_cast<int>(WiFi.status()));
        services_.network({}, 1, nextSequence(), nowMs);
        if (newlyProvisioned_ && provisioningDeadlineMs_ != 0 &&
            static_cast<std::int32_t>(provisioningDeadlineMs_ - nowMs) > 0) {
          WiFi.disconnect(false, false);
          pendingConnectAtMs_ = nowMs + kProvisioningRetryMs;
          pendingConnect_ = true;
          Serial.printf("wifi_provisioning waiting_for_ap retry_in_ms=%lu\n",
                        static_cast<unsigned long>(kProvisioningRetryMs));
        } else {
          if (newlyProvisioned_) {
            clearPendingCredentials();
            provisioningDeadlineMs_ = 0;
            newlyProvisioned_ = false;
          } else if (activeProfileIndex_ < profileCount_) {
            // With several profiles a failed one steps aside for the others.
            // With a SINGLE profile, masking it guaranteed a wasted rescan,
            // a portal pop and an escalating backoff on every hiccup — the
            // owner measured two minutes of boot silence. One timeout gets a
            // second immediate chance; only repeat failures step aside (which
            // is what lets the portal open when the password really changed).
            ++consecutiveTimeouts_;
            if (profileCount_ > 1 || consecutiveTimeouts_ >= 2) {
              failedProfileMask_ |=
                  static_cast<std::uint8_t>(1U << activeProfileIndex_);
            }
          }
          if (!connectBestKnown(nowMs)) {
            startPortal();
            failedProfileMask_ = 0;
            scheduleRetry(nowMs);
          }
        }
      }
    } else if (connected_ && WiFi.status() != WL_CONNECTED) {
      connected_ = false;
      services_.network({}, 1, nextSequence(), nowMs);
      scheduleRetry(nowMs);
    } else if (!connected_ && retryAtMs_ != 0 &&
               static_cast<std::int32_t>(nowMs - retryAtMs_) >= 0) {
      retryAtMs_ = 0;
      if (!connectBestKnown(nowMs)) {
        startPortal();
        scheduleRetry(nowMs);
      }
    }
    updateSignalStrength(nowMs);
  }

  bool connected() const { return connected_; }
  std::uint8_t signalPercent() const { return signalPercent_; }
  bool hasProfiles() const { return profileCount_ != 0; }
  bool portalActive() const { return portalActive_; }
  const char* portalPassword() const { return portalPassword_.data(); }

  bool setPortalEnabled(bool enabled) {
    if (enabled) {
      startPortal();
      return portalActive_;
    }
    if (!connected_) return false;
    stopPortal();
    return !portalActive_;
  }

  bool restartPortal() {
    if (portalActive_) stopPortal();
    portalPassword_.fill(0);
    portalCsrfToken_.fill(0);
    startPortal();
    return portalActive_;
  }

  // Console session: the same station endpoints, served over the HOME network
  // for a 15-minute idle window. Outside a session the server is not even
  // listening, so anything scanning the LAN gets a refused connection rather
  // than a page.
  bool startConsoleSession(std::uint32_t nowMs) {
    if (!connected_) return false;
    ensureCsrfToken();
    if (!consoleActive_ && !portalActive_) {
      registerRoutes();
      server_.begin();
    }
    consoleActive_ = true;
    consoleDeadlineMs_ = nowMs + kConsoleFirstClientMs;
    consoleHardDeadlineMs_ = nowMs + kConsoleHardCapMs;
    consoleCloseAtMs_ = 0;
    Serial.printf("console stage=session-open window_min=%u linger_s=%u\n",
                  static_cast<unsigned>(kConsoleHardCapMs / 60000U),
                  static_cast<unsigned>(kConsoleLingerMs / 1000U));
    return true;
  }

  void stopConsoleSession() {
    if (!consoleActive_) return;
    consoleActive_ = false;
    consoleDeadlineMs_ = 0;
    consoleHardDeadlineMs_ = 0;
    consoleCloseAtMs_ = 0;
    if (!portalActive_) server_.stop();
    Serial.println("console stage=session-closed");
  }

  bool consoleActive() const { return consoleActive_; }

 private:
  static std::uint8_t rssiToPercent(std::int16_t rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return static_cast<std::uint8_t>((rssi + 100) * 2);
  }

  void updateSignalStrength(std::uint32_t nowMs) {
    if (!connected_ || WiFi.status() != WL_CONNECTED) {
      signalInitialized_ = false;
      signalPercent_ = 0;
      nextSignalSampleMs_ = nowMs;
      return;
    }
    if (signalInitialized_ &&
        static_cast<std::int32_t>(nowMs - nextSignalSampleMs_) < 0) {
      return;
    }
    const auto rssi = static_cast<std::int16_t>(WiFi.RSSI());
    filteredRssi_ = signalInitialized_
                        ? static_cast<std::int16_t>((filteredRssi_ * 3 + rssi) / 4)
                        : rssi;
    signalInitialized_ = true;
    signalPercent_ = rssiToPercent(filteredRssi_);
    nextSignalSampleMs_ = nowMs + 2000U;
  }

  struct Profile {
    String ssid;
    String password;
  };

  struct StoredProfile {
    std::array<char, 33> ssid{};
    std::array<char, 64> password{};
  };

  struct StoredProfilesV1 {
    std::uint32_t magic = 0;
    std::uint8_t version = 0;
    std::uint8_t count = 0;
    std::uint16_t reserved = 0;
    std::array<StoredProfile, 4> profiles{};
    std::uint32_t checksum = 0;
  };

  struct StoredProfiles {
    std::uint32_t magic = 0;
    std::uint8_t version = 0;
    std::uint8_t count = 0;
    std::uint16_t reserved = 0;
    std::array<StoredProfile, open_radio::kMaxRememberedNetworks> profiles{};
    std::uint32_t checksum = 0;
  };

  static constexpr std::size_t kMaxProfiles =
      open_radio::kMaxRememberedNetworks;
  static constexpr std::size_t kNoProfile = kMaxProfiles;
  static constexpr std::uint32_t kProfileStoreMagic = 0x4f525750U;
  static constexpr std::uint8_t kProfileStoreVersionV1 = 1;
  static constexpr std::uint8_t kProfileStoreVersion = 2;
  static constexpr std::uint32_t kConnectionTimeoutMs = 15000;
  // A rejected association reports WL_NO_SSID_AVAIL / WL_CONNECT_FAILED well
  // before the blanket timeout; waiting the full window just prolonged the
  // boot silence. Provisioning keeps the blanket (the hotspot may still be
  // coming up).
  static constexpr std::uint32_t kEarlyGiveUpMs = 6000;
  // Five minutes is generous for four taps and short enough that the access
  // point is not still up when the household starts listening. The design note
  // says fifteen; that figure was written for the phase 2 relay session, which
  // has a human actively browsing. This one only has to outlast a walkthrough.
  static constexpr std::uint32_t kPortalIdleTimeoutMs = 300000;
  // Console lifetime: the page heartbeats every ~20 s, so a closed tab stops
  // the session (and silence) within a minute instead of a quarter hour. The
  // first client gets longer — the owner still has to type the address. The
  // hard cap bounds the whole window regardless of heartbeats.
  static constexpr std::uint32_t kConsoleLingerMs = 60U * 1000U;
  static constexpr std::uint32_t kConsoleFirstClientMs = 180U * 1000U;
  static constexpr std::uint32_t kConsoleHardCapMs = 15U * 60U * 1000U;
  static constexpr std::uint32_t kConsoleSavedCloseMs = 4000U;
  static constexpr std::uint32_t kProvisioningWindowMs = 120000;
  static constexpr std::uint32_t kProvisioningRetryMs = 5000;
  static constexpr std::array<std::uint32_t, 4> kRetryDelaysMs{{
      5000, 30000, 120000, 600000}};

  static String escapedJson(const String& input) {
    String output;
    output.reserve(input.length() + 8);
    for (std::size_t index = 0; index < input.length(); ++index) {
      const char value = input[index];
      if (value == '"' || value == '\\') output += '\\';
      if (static_cast<unsigned char>(value) >= 0x20) output += value;
    }
    return output;
  }

  static const char* mimeType(ContentType type) {
    switch (type) {
      case ContentType::Html:
        return "text/html; charset=utf-8";
      case ContentType::Css:
        return "text/css; charset=utf-8";
      case ContentType::JavaScript:
        return "text/javascript; charset=utf-8";
      default:
        return "application/octet-stream";
    }
  }

  std::uint32_t nextSequence() { return sequence_++; }

  template <std::size_t Size>
  static std::size_t boundedLength(const std::array<char, Size>& value) {
    std::size_t length = 0;
    while (length < value.size() && value[length] != '\0') ++length;
    return length;
  }

  template <std::size_t Size>
  bool acceptStoredProfiles(const std::array<StoredProfile, Size>& records,
                            std::size_t count) {
    if (count > records.size() || count > profiles_.size()) return false;
    std::size_t accepted = 0;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& record = records[index];
      const std::size_t ssidLength = boundedLength(record.ssid);
      const std::size_t passwordLength = boundedLength(record.password);
      if (ssidLength == 0 || ssidLength > 32 || passwordLength < 8 ||
          passwordLength > 63) {
        profileCount_ = 0;
        return false;
      }
      profiles_[accepted++] =
          {String(record.ssid.data()), String(record.password.data())};
    }
    profileCount_ = accepted;
    return true;
  }

  void loadProfiles() {
    StoredProfiles stored;
    if (preferences_.getBytesLength("wifi_profiles") == sizeof(stored) &&
        preferences_.getBytes("wifi_profiles", &stored, sizeof(stored)) ==
            sizeof(stored) &&
        stored.magic == kProfileStoreMagic &&
        stored.version == kProfileStoreVersion &&
        stored.count <= stored.profiles.size() &&
        stored.checksum ==
            open_radio::crc32(reinterpret_cast<const std::uint8_t*>(&stored),
                              offsetof(StoredProfiles, checksum))) {
      acceptStoredProfiles(stored.profiles, stored.count);
      return;
    }
    StoredProfilesV1 storedV1;
    if (preferences_.getBytesLength("wifi_profiles") == sizeof(storedV1) &&
        preferences_.getBytes("wifi_profiles", &storedV1,
                              sizeof(storedV1)) == sizeof(storedV1) &&
        storedV1.magic == kProfileStoreMagic &&
        storedV1.version == kProfileStoreVersionV1 &&
        storedV1.count <= storedV1.profiles.size() &&
        storedV1.checksum ==
            open_radio::crc32(
                reinterpret_cast<const std::uint8_t*>(&storedV1),
                offsetof(StoredProfilesV1, checksum))) {
      acceptStoredProfiles(storedV1.profiles, storedV1.count);
      return;
    }
    profileCount_ = std::min<std::size_t>(preferences_.getUChar("wifi_count", 0),
                                          profiles_.size());
    std::size_t accepted = 0;
    for (std::size_t index = 0; index < profileCount_; ++index) {
      const String ssid = preferences_.getString(("wifi_s" + String(index)).c_str());
      const String password =
          preferences_.getString(("wifi_p" + String(index)).c_str());
      if (ssid.length() == 0 || ssid.length() > 32 || password.length() < 8 ||
          password.length() > 63) {
        continue;
      }
      profiles_[accepted++] = {ssid, password};
    }
    profileCount_ = accepted;
  }

  bool connectBestKnown(std::uint32_t nowMs) {
    if (profileCount_ == 0) return false;
    // One usable profile — the shipped product's normal case — connects
    // DIRECTLY. The pre-connect scan existed only to pick the strongest of
    // several profiles; for a single known network it cost 4-5 blocking
    // seconds per attempt and the scan→associate sequence itself is what
    // kept failing with status=1 on real hardware. The customer's radio has
    // to come up by itself, fast, with nobody watching a serial port.
    std::size_t onlyProfile = profileCount_;
    std::size_t unmaskedCount = 0;
    for (std::size_t profileIndex = 0; profileIndex < profileCount_;
         ++profileIndex) {
      if ((failedProfileMask_ &
           static_cast<std::uint8_t>(1U << profileIndex)) == 0U) {
        onlyProfile = profileIndex;
        ++unmaskedCount;
      }
    }
    if (unmaskedCount == 1) {
      Serial.printf("wifi_scan visible=skipped candidate=direct profiles=%u\n",
                    static_cast<unsigned>(profileCount_));
      beginConnection(profiles_[onlyProfile].ssid,
                      profiles_[onlyProfile].password, nowMs, false,
                      onlyProfile);
      return true;
    }
    const int scanCount = WiFi.scanNetworks(false, true, false, 300, 0);
    int bestRssi = -128;
    std::size_t bestProfile = profileCount_;
    for (int scanIndex = 0; scanIndex < scanCount; ++scanIndex) {
      if (WiFi.encryptionType(scanIndex) == WIFI_AUTH_OPEN) continue;
      for (std::size_t profileIndex = 0; profileIndex < profileCount_;
           ++profileIndex) {
        if ((failedProfileMask_ & static_cast<std::uint8_t>(1U << profileIndex)) !=
            0U) {
          continue;
        }
        if (WiFi.SSID(scanIndex) == profiles_[profileIndex].ssid &&
            WiFi.RSSI(scanIndex) > bestRssi) {
          bestRssi = WiFi.RSSI(scanIndex);
          bestProfile = profileIndex;
        }
      }
    }
    WiFi.scanDelete();
    const char* candidate = bestProfile == profileCount_ ? "no" : "yes";
    if (bestProfile == profileCount_ && !scanMissFallbackUsed_) {
      // A scan can miss a live network (measured: 3-5 networks visible with
      // the home SSID absent for one pass). One blind attempt at the first
      // unmasked profile costs a connection timeout at worst; skipping it
      // cost the whole retry ladder. Used once per disconnection, so a
      // genuinely absent network still falls through to the portal.
      for (std::size_t profileIndex = 0; profileIndex < profileCount_;
           ++profileIndex) {
        if ((failedProfileMask_ &
             static_cast<std::uint8_t>(1U << profileIndex)) == 0U) {
          bestProfile = profileIndex;
          scanMissFallbackUsed_ = true;
          candidate = "blind";
          break;
        }
      }
    }
    Serial.printf("wifi_scan visible=%d candidate=%s profiles=%u\n", scanCount,
                  candidate,
                  static_cast<unsigned>(profileCount_));
    if (bestProfile == profileCount_) return false;
    beginConnection(profiles_[bestProfile].ssid,
                    profiles_[bestProfile].password, nowMs, false,
                    bestProfile);
    return true;
  }

  void beginConnection(const String& ssid, const String& password,
                       std::uint32_t nowMs, bool newlyProvisioned,
                       std::size_t profileIndex) {
    WiFi.mode(portalActive_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    WiFi.begin(ssid.c_str(), password.c_str());
    connecting_ = true;
    connected_ = false;
    newlyProvisioned_ = newlyProvisioned;
    activeProfileIndex_ = profileIndex;
    connectionStartedAtMs_ = nowMs;
  }

  bool persistProvisionedProfile(const String& ssid, const String& password) {
    auto nextProfiles = profiles_;
    std::size_t nextCount = profileCount_;
    std::size_t slot = nextCount;
    for (std::size_t index = 0; index < nextCount; ++index) {
      if (nextProfiles[index].ssid == ssid) slot = index;
    }
    if (slot == nextCount) {
      slot = nextCount < nextProfiles.size() ? nextCount++
                                             : nextProfiles.size() - 1;
    }
    nextProfiles[slot] = {ssid, password};

    StoredProfiles stored;
    stored.magic = kProfileStoreMagic;
    stored.version = kProfileStoreVersion;
    stored.count = static_cast<std::uint8_t>(nextCount);
    for (std::size_t index = 0; index < nextCount; ++index) {
      std::snprintf(stored.profiles[index].ssid.data(),
                    stored.profiles[index].ssid.size(), "%s",
                    nextProfiles[index].ssid.c_str());
      std::snprintf(stored.profiles[index].password.data(),
                    stored.profiles[index].password.size(), "%s",
                    nextProfiles[index].password.c_str());
    }
    stored.checksum = open_radio::crc32(
        reinterpret_cast<const std::uint8_t*>(&stored),
        offsetof(StoredProfiles, checksum));
    if (preferences_.putBytes("wifi_profiles", &stored, sizeof(stored)) !=
        sizeof(stored)) {
      return false;
    }
    profiles_ = nextProfiles;
    profileCount_ = nextCount;
    return true;
  }

  void clearPendingCredentials() {
    pendingSsid_ = "";
    pendingPassword_ = "";
  }

  void scheduleRetry(std::uint32_t nowMs) {
    const auto delay = kRetryDelaysMs[std::min<std::size_t>(
        retryIndex_, kRetryDelaysMs.size() - 1)];
    if (retryIndex_ + 1 < kRetryDelaysMs.size()) ++retryIndex_;
    retryAtMs_ = nowMs + delay;
  }

  void ensureCsrfToken() {
    if (portalCsrfToken_[0] != '\0') return;
    std::snprintf(portalCsrfToken_.data(), portalCsrfToken_.size(),
                  "%08lX%08lX", static_cast<unsigned long>(esp_random()),
                  static_cast<unsigned long>(esp_random()));
  }

  void registerRoutes() {
    if (routesRegistered_) return;
    server_.on("/", HTTP_GET, [this]() { serveIndex(); });
    for (std::size_t index = 0; index < generated::kOnboardingAssetCount;
         ++index) {
      const auto* asset = &generated::kOnboardingAssets[index];
      server_.on(asset->path, HTTP_GET, [this, asset]() {
        if (!requirePortalClient()) return;
        server_.send_P(200, mimeType(asset->contentType), asset->content,
                       asset->size);
      });
    }
    server_.on("/api/networks", HTTP_GET, [this]() { sendNetworks(); });
    server_.on("/api/session", HTTP_GET, [this]() { sendSession(); });
    server_.on("/api/directory", HTTP_GET, [this]() { sendDirectory(); });
    server_.on("/api/slots", HTTP_POST, [this]() { acceptSlots(); });
    server_.on("/api/config-form", HTTP_POST,
               [this]() { acceptConfiguration(); });
    server_.onNotFound([this]() {
      if (!requirePortalClient()) return;
      server_.sendHeader("Location", "/network-onboarding/index.html", true);
      server_.send(302, "text/plain", "");
    });
    const char* collectedHeaders[] = {"X-Open-Radio-CSRF", "Host"};
    server_.collectHeaders(collectedHeaders, 2);
    routesRegistered_ = true;
  }

  void startPortal() {
    if (portalActive_) return;
    if (portalPassword_[0] == '\0') {
      std::snprintf(portalPassword_.data(), portalPassword_.size(),
                    "OR-%08lX%04lX", static_cast<unsigned long>(esp_random()),
                    static_cast<unsigned long>(esp_random() & 0xffffU));
    }
    ensureCsrfToken();
    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP("OpenRadio-Setup", portalPassword_.data())) {
      return;
    }
    dnsActive_ = dns_.start(53, "*", WiFi.softAPIP());
    registerRoutes();
    if (!consoleActive_) server_.begin();
    portalActive_ = true;
  }

  void stopPortal() {
    if (!portalActive_) return;
    // The console session may still be serving over the home network on the
    // same server instance; only stop listening when nobody needs it.
    if (!consoleActive_) server_.stop();
    if (dnsActive_) dns_.stop();
    dnsActive_ = false;
    WiFi.softAPdisconnect(true);
    portalActive_ = false;
    portalIdleDeadlineMs_ = 0;
  }

  void serveIndex() {
    if (!requirePortalClient()) return;
    server_.send_P(200, "text/html; charset=utf-8", generated::k_index_html,
                   sizeof(generated::k_index_html) - 1);
  }

  // Tells the page which flow it is in — the provisioning wizard (AP portal)
  // or the station console (home network) — and doubles as its heartbeat.
  void sendSession() {
    if (!requirePortalClient()) return;
    const bool viaConsole =
        consoleActive_ && server_.client().localIP() == WiFi.localIP();
    server_.sendHeader("X-Open-Radio-CSRF", portalCsrfToken_.data());
    server_.send(200, "application/json",
                 viaConsole ? "{\"mode\":\"console\"}"
                            : "{\"mode\":\"portal\"}");
  }

  // Names and endpoint counts only. The device already holds every URL, so
  // sending them would double the payload to say nothing the phone can use —
  // the portal posts back indices, never addresses.
  void sendDirectory() {
    if (!requirePortalClient()) return;
    String body;
    body.reserve(12288);
    body += "{\"probedAt\":\"";
    body += open_radio::generated::kDirectoryProbedAt;
    body += "\",\"stations\":[";
    for (std::size_t index = 0; index < open_radio::generated::kDirectoryCount; ++index) {
      if (index > 0) body += ',';
      body += "{\"i\":";
      body += String(static_cast<unsigned>(index));
      body += ",\"n\":\"";
      body += escapedJson(String(open_radio::generated::kDirectory[index].name));
      body += "\",\"e\":";
      body += String(static_cast<unsigned>(open_radio::generated::kDirectory[index].endpointCount));
      // "l": whether picking this station will put a real logo on the tile.
      // The installer deserves to know before they choose, not after.
      body += ",\"l\":";
      body += open_radio::generated::kDirectory[index].favicon[0] != '\0' ? "1" : "0";
      body += '}';
    }
    body += "]}";
    server_.sendHeader("X-Open-Radio-CSRF", portalCsrfToken_.data());
    server_.send(200, "application/json; charset=utf-8", body);
  }

  // Nine indices, or -1 to keep the factory station in that slot. Rejected
  // whole rather than in part: a half-applied layout is a cube whose screen
  // disagrees with what it plays.
  void acceptSlots() {
    if (!requirePortalClient()) return;
    if (server_.header("X-Open-Radio-CSRF") != portalCsrfToken_.data()) {
      server_.send(403, "application/json", "{\"accepted\":false}");
      return;
    }
    const String body = server_.arg("plain");
    if (body.length() == 0 || body.length() > 256) {
      server_.send(422, "application/json", "{\"accepted\":false}");
      return;
    }
    std::array<std::int16_t, StationSlots::kSlotCount> assignments{};
    std::size_t parsed = 0;
    int value = 0;
    bool negative = false;
    bool inNumber = false;
    for (unsigned int cursor = 0; cursor < body.length(); ++cursor) {
      const char character = body[cursor];
      if (character == '-' && !inNumber) { negative = true; inNumber = true; value = 0; continue; }
      if (character >= '0' && character <= '9') {
        inNumber = true;
        value = value * 10 + (character - '0');
        if (value > 32767) { server_.send(422, "application/json", "{\"accepted\":false}"); return; }
        continue;
      }
      if (inNumber) {
        if (parsed >= assignments.size()) { server_.send(422, "application/json", "{\"accepted\":false}"); return; }
        assignments[parsed++] = static_cast<std::int16_t>(negative ? -value : value);
        value = 0; negative = false; inNumber = false;
      }
    }
    if (inNumber && parsed < assignments.size()) {
      assignments[parsed++] = static_cast<std::int16_t>(negative ? -value : value);
    }
    if (parsed != assignments.size()) {
      server_.send(422, "application/json", "{\"accepted\":false}");
      return;
    }
    if (slots_ == nullptr || !slots_->store(preferences_, assignments.data())) {
      server_.send(422, "application/json", "{\"accepted\":false}");
      return;
    }
    if (slotsChangedCallback_ != nullptr) slotsChangedCallback_(slotsChangedContext_);
    Serial.println("slots stage=stored count=9");
    // Saving slots is the console errand's natural end: close the session a
    // few seconds later so the restart (fresh boot fetches the new logos)
    // happens promptly instead of after the idle window.
    if (consoleActive_ && server_.client().localIP() == WiFi.localIP()) {
      consoleCloseAtMs_ = millis() + kConsoleSavedCloseMs;
    }
    server_.send(202, "application/json", "{\"accepted\":true}");
  }

  void sendNetworks() {
    if (!requirePortalClient()) return;
    const int scanCount = WiFi.scanNetworks(false, true, false, 300, 0);
    String body = "{\"networks\":[";
    bool first = true;
    for (int index = 0; index < scanCount && index < 16; ++index) {
      const String ssid = WiFi.SSID(index);
      if (ssid.length() == 0) continue;
      if (!first) body += ',';
      first = false;
      const bool open = WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
      bool known = false;
      for (std::size_t profile = 0; profile < profileCount_; ++profile) {
        if (profiles_[profile].ssid == ssid) known = true;
      }
      body += "{\"id\":\"" + escapedJson(ssid) + "\",\"label\":\"" +
              escapedJson(ssid) + "\",\"security\":\"" +
              String(open ? "OPEN" : "WPA2_PSK") +
              "\",\"captivePortal\":false,\"known\":" +
              String(known ? "true" : "false") + ",\"rssi\":" +
              String(WiFi.RSSI(index)) + "}";
    }
    WiFi.scanDelete();
    body += "]}";
    server_.sendHeader("X-Open-Radio-CSRF", portalCsrfToken_.data());
    server_.send(200, "application/json; charset=utf-8", body);
  }

  enum class TargetNetworkStatus : std::uint8_t { Missing, Secured, Open };

  TargetNetworkStatus targetNetworkStatus(const String& ssid) {
    const int scanCount = WiFi.scanNetworks(false, true, false, 300, 0);
    auto status = TargetNetworkStatus::Missing;
    for (int index = 0; index < scanCount; ++index) {
      if (WiFi.SSID(index) == ssid) {
        status = WiFi.encryptionType(index) == WIFI_AUTH_OPEN
                     ? TargetNetworkStatus::Open
                     : TargetNetworkStatus::Secured;
        break;
      }
    }
    WiFi.scanDelete();
    return status;
  }

  void acceptConfiguration() {
    if (!requirePortalClient()) return;
    // Credentials are accepted from the setup portal ONLY. A console session
    // is reachable by every host on the home network, and the CSRF token is
    // handed to any of them by /api/session — so until 0.4.2 a compromised
    // camera or a guest could re-provision the radio onto its own access
    // point inside the fifteen-minute window (review of 2026-07-30, confirmed
    // for the public lane by the 2026-09-08 audit).
    // The console keeps stations and directory; Wi-Fi changes need the
    // physical step of joining the cube's own network.
    if (!(portalActive_ && server_.client().localIP() == WiFi.softAPIP())) {
      server_.send(403, "application/json",
                   "{\"accepted\":false,\"reason\":\"portal-only\"}");
      return;
    }
    if (server_.header("X-Open-Radio-CSRF") != portalCsrfToken_.data()) {
      server_.send(403, "application/json", "{\"accepted\":false}");
      return;
    }
    if (pendingConnect_ || connecting_) {
      server_.send(409, "application/json", "{\"accepted\":false}");
      return;
    }
    const String ssid = server_.arg("ssid");
    const String password = server_.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() < 8 ||
        password.length() > 63) {
      server_.send(422, "application/json", "{\"accepted\":false}");
      return;
    }
    const auto target = targetNetworkStatus(ssid);
    if (target == TargetNetworkStatus::Open) {
      server_.send(422, "application/json", "{\"accepted\":false}");
      return;
    }
    pendingSsid_ = ssid;
    pendingPassword_ = password;
    pendingConnectAtMs_ = millis() + 250U;
    provisioningDeadlineMs_ = millis() + kProvisioningWindowMs;
    pendingConnect_ = true;
    server_.send(202, "application/json",
                 target == TargetNetworkStatus::Missing
                     ? "{\"accepted\":true,\"waitingForNetwork\":true}"
                     : "{\"accepted\":true,\"waitingForNetwork\":false}");
  }

  bool requirePortalClient() {
    const auto clientLocal = server_.client().localIP();
    if (portalActive_ && clientLocal == WiFi.softAPIP()) {
      // Idle means idle, not elapsed: someone working through the steps keeps
      // pushing the deadline out.
      if (portalIdleDeadlineMs_ != 0) {
        portalIdleDeadlineMs_ = millis() + kPortalIdleTimeoutMs;
      }
      return true;
    }
    // A console session admits clients arriving over the home network; each
    // request (including the page heartbeat) pushes the linger deadline out.
    if (consoleActive_ && clientLocal == WiFi.localIP()) {
      if (!hostHeaderIsThisDevice()) {
        server_.send(403, "application/json",
                     "{\"accepted\":false,\"reason\":\"host\"}");
        return false;
      }
      consoleDeadlineMs_ = millis() + kConsoleLingerMs;
      return true;
    }
    server_.send(403, "application/json", "{\"accepted\":false}");
    return false;
  }

  // DNS-rebinding guard for the console (audit 2026-09-08). A page on
  // the owner's laptop that resolves an attacker's hostname to this address
  // would otherwise be same-origin to the device, read the token from
  // /api/session and post to any route without being in the LAN itself. The
  // console is only ever reached by the raw address the screen shows or by
  // its mDNS name, so those two are the whole allow-list. Not applied to the
  // setup portal: captive-portal detection deliberately arrives with foreign
  // Host headers and relies on the redirect.
  bool hostHeaderIsThisDevice() {
    String host = server_.header("Host");
    host.toLowerCase();
    const int colon = host.indexOf(':');
    if (colon >= 0) host.remove(colon);
    if (host.length() == 0) return false;
    if (host == "open-radio.local") return true;
    return host == WiFi.localIP().toString();
  }

  Preferences& preferences_;
  RuntimeServiceBridge& services_;
  WebServer server_{80};
  DNSServer dns_;
  std::array<Profile, kMaxProfiles> profiles_{};
  std::array<char, 16> portalPassword_{};
  std::array<char, 17> portalCsrfToken_{};
  std::size_t profileCount_ = 0;
  ConnectedCallback connectedCallback_ = nullptr;
  void* connectedContext_ = nullptr;
  String pendingSsid_;
  String pendingPassword_;
  std::uint32_t sequence_ = 10;
  std::uint32_t connectionStartedAtMs_ = 0;
  std::uint32_t pendingConnectAtMs_ = 0;
  std::uint32_t provisioningDeadlineMs_ = 0;
  std::uint32_t retryAtMs_ = 0;
  std::uint32_t nextSignalSampleMs_ = 0;
  std::size_t retryIndex_ = 0;
  std::size_t activeProfileIndex_ = kNoProfile;
  std::uint8_t failedProfileMask_ = 0;
  std::uint8_t consecutiveTimeouts_ = 0;
  bool scanMissFallbackUsed_ = false;
  StationSlots* slots_ = nullptr;
  void (*slotsChangedCallback_)(void*) = nullptr;
  void* slotsChangedContext_ = nullptr;
  bool portalActive_ = false;
  bool consoleActive_ = false;
  std::uint32_t portalIdleDeadlineMs_ = 0;
  std::uint32_t consoleDeadlineMs_ = 0;
  std::uint32_t consoleHardDeadlineMs_ = 0;
  std::uint32_t consoleCloseAtMs_ = 0;
  bool dnsActive_ = false;
  bool routesRegistered_ = false;
  bool pendingConnect_ = false;
  bool connecting_ = false;
  bool connected_ = false;
  bool newlyProvisioned_ = false;
  bool signalInitialized_ = false;
  std::int16_t filteredRssi_ = -100;
  std::uint8_t signalPercent_ = 0;
};

}  // namespace open_radio::public_candidate
