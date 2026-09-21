#pragma once

#include <AudioFileSource.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace open_radio::public_candidate {

// A deliberately bounded HLS source for the ZPR live-radio profile:
// unencrypted live playlists, MPEG-TS segments and one ADTS AAC audio PID.
// Network work runs on core 0 and feeds a PSRAM ring, so a playlist refresh or
// segment download cannot stall PCM decoding on the Arduino loop core.
class HlsAacSource final : public ::AudioFileSource {
 public:
  HlsAacSource() = default;
  ~HlsAacSource() override {
    close();
    if (mutex_ != nullptr) vSemaphoreDelete(mutex_);
    heap_caps_free(ring_);
    heap_caps_free(segment_);
  }

  bool open(const char* url) override {
    close();
    if (url == nullptr || url[0] == '\0' || std::strlen(url) >= masterUrl_.size()) {
      return false;
    }
    if (!ensureStorage()) return false;
    std::snprintf(masterUrl_.data(), masterUrl_.size(), "%s", url);
    resetRing();
    lastSegmentUrl_ = "";
    initialPlaylist_ = true;
    open_.store(true, std::memory_order_release);
    workerRunning_.store(true, std::memory_order_release);
    if (xTaskCreatePinnedToCore(workerEntry, "hls-prefetch", 12288, this, 1,
                                nullptr, 0) != pdPASS) {
      workerRunning_.store(false, std::memory_order_release);
      open_.store(false, std::memory_order_release);
      return false;
    }
    const auto started = millis();
    while (open_.load(std::memory_order_acquire) &&
           workerRunning_.load(std::memory_order_acquire) &&
           bufferedBytes() < kStartupBytes && millis() - started < 10000U) {
      delay(10);
    }
    if (bufferedBytes() >= kStartupBytes) return true;
    close();
    return false;
  }

  std::uint32_t read(void* data, std::uint32_t length) override {
    if (data == nullptr || length == 0) return 0;
    const auto started = millis();
    while (open_.load(std::memory_order_acquire) && bufferedBytes() == 0 &&
           millis() - started < kReadWaitMs) {
      delay(2);
    }
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
      return 0;
    }
    const std::size_t count = std::min<std::size_t>(length, ringSize_);
    auto* destination = static_cast<std::uint8_t*>(data);
    const std::size_t first = std::min(count, kRingBytes - ringRead_);
    std::memcpy(destination, ring_ + ringRead_, first);
    const std::size_t second = count - first;
    if (second != 0) std::memcpy(destination + first, ring_, second);
    ringRead_ = (ringRead_ + count) % kRingBytes;
    ringSize_ -= count;
    position_ += count;
    xSemaphoreGive(mutex_);
    if (count == 0) ++underruns_;
    return static_cast<std::uint32_t>(count);
  }

  std::uint32_t readNonBlock(void* data, std::uint32_t length) override {
    return read(data, length);
  }

  bool close() override {
    open_.store(false, std::memory_order_release);
    const auto started = millis();
    while (workerRunning_.load(std::memory_order_acquire) &&
           millis() - started < 7000U) {
      delay(5);
    }
    return !workerRunning_.load(std::memory_order_acquire);
  }

  bool isOpen() override { return open_.load(std::memory_order_acquire); }
  std::uint32_t getSize() override { return 0; }
  std::uint32_t getPos() override { return position_; }
  bool seek(std::int32_t, int) override { return false; }
  bool loop() override { return isOpen(); }

  std::uint32_t underruns() const { return underruns_; }

 private:
  static constexpr std::size_t kRingBytes = 256U * 1024U;
  static constexpr std::size_t kSegmentBytes = 192U * 1024U;
  static constexpr std::size_t kStartupBytes = 48U * 1024U;
  // The decoded/hardware PCM queues already cover short scheduling jitter.
  // Never freeze the UI/audio owner loop for most of a second when the HLS
  // producer is genuinely empty; the station supervisor can reconnect.
  static constexpr std::uint32_t kReadWaitMs = 100;
  static constexpr std::size_t kMaximumPlaylistBytes = 16U * 1024U;

  bool ensureStorage() {
    if (mutex_ == nullptr) mutex_ = xSemaphoreCreateMutex();
    if (ring_ == nullptr) {
      ring_ = static_cast<std::uint8_t*>(heap_caps_malloc(
          kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (segment_ == nullptr) {
      segment_ = static_cast<std::uint8_t*>(heap_caps_malloc(
          kSegmentBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return mutex_ != nullptr && ring_ != nullptr && segment_ != nullptr;
  }

  void resetRing() {
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
      return;
    }
    ringRead_ = 0;
    ringWrite_ = 0;
    ringSize_ = 0;
    position_ = 0;
    underruns_ = 0;
    xSemaphoreGive(mutex_);
  }

  std::size_t bufferedBytes() const {
    if (mutex_ == nullptr ||
        xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
      return 0;
    }
    const std::size_t size = ringSize_;
    xSemaphoreGive(mutex_);
    return size;
  }

  bool append(const std::uint8_t* data, std::size_t length) {
    std::size_t written = 0;
    while (written < length && open_.load(std::memory_order_acquire)) {
      if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) continue;
      const std::size_t free = kRingBytes - ringSize_;
      const std::size_t chunk = std::min<std::size_t>(length - written, free);
      const std::size_t first =
          std::min(chunk, kRingBytes - ringWrite_);
      std::memcpy(ring_ + ringWrite_, data + written, first);
      const std::size_t second = chunk - first;
      if (second != 0) std::memcpy(ring_, data + written + first, second);
      ringWrite_ = (ringWrite_ + chunk) % kRingBytes;
      ringSize_ += chunk;
      xSemaphoreGive(mutex_);
      written += chunk;
      if (chunk == 0) vTaskDelay(pdMS_TO_TICKS(20));
    }
    return written == length;
  }

  static void workerEntry(void* context) {
    auto* source = static_cast<HlsAacSource*>(context);
    source->workerLoop();
    Serial.printf("hls worker=stopped underruns=%lu stack_words=%u\n",
                  static_cast<unsigned long>(source->underruns_),
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    source->workerRunning_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
  }

  static String resolveUrl(const String& base, const String& reference) {
    if (reference.startsWith("http://") || reference.startsWith("https://")) {
      return reference;
    }
    const int slash = base.lastIndexOf('/');
    return slash >= 0 ? base.substring(0, slash + 1) + reference : reference;
  }

  static bool firstMediaLine(const String& playlist, String& line) {
    int cursor = 0;
    while (cursor < playlist.length()) {
      int end = playlist.indexOf('\n', cursor);
      if (end < 0) end = playlist.length();
      line = playlist.substring(cursor, end);
      line.trim();
      if (line.length() > 0 && line[0] != '#') return true;
      cursor = end + 1;
    }
    return false;
  }

  static std::size_t segmentLines(const String& playlist,
                                  std::array<String, 8>& segments) {
    std::size_t count = 0;
    int cursor = 0;
    while (cursor < playlist.length() && count < segments.size()) {
      int end = playlist.indexOf('\n', cursor);
      if (end < 0) end = playlist.length();
      String line = playlist.substring(cursor, end);
      line.trim();
      if (line.length() > 0 && line[0] != '#') segments[count++] = line;
      cursor = end + 1;
    }
    return count;
  }

  static bool fetchText(const String& url, String& body) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient request;
    request.setConnectTimeout(5000);
    request.setTimeout(5000);
    request.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    request.useHTTP10(true);
    request.setUserAgent("OpenRadioCore2/0.1");
    if (!request.begin(client, url)) return false;
    const int status = request.GET();
    const int size = request.getSize();
    if (status != HTTP_CODE_OK || size > static_cast<int>(kMaximumPlaylistBytes)) {
      request.end();
      return false;
    }
    body = request.getString();
    request.end();
    return body.length() > 0 && body.length() <= kMaximumPlaylistBytes;
  }

  bool fetchSegment(const String& url, std::size_t& size) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient request;
    request.setConnectTimeout(5000);
    request.setTimeout(7000);
    request.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    request.useHTTP10(true);
    request.setUserAgent("OpenRadioCore2/0.1");
    if (!request.begin(client, url)) return false;
    const int status = request.GET();
    const int contentLength = request.getSize();
    if (status != HTTP_CODE_OK || contentLength <= 0 ||
        contentLength > static_cast<int>(kSegmentBytes)) {
      request.end();
      return false;
    }
    auto* stream = request.getStreamPtr();
    const std::size_t received =
        stream == nullptr ? 0 : stream->readBytes(segment_, contentLength);
    request.end();
    size = received;
    return received == static_cast<std::size_t>(contentLength);
  }

  static std::size_t payloadOffset(const std::uint8_t* packet) {
    const std::uint8_t adaptationControl = (packet[3] >> 4U) & 0x03U;
    if ((adaptationControl & 0x01U) == 0) return 188;
    std::size_t offset = 4;
    if ((adaptationControl & 0x02U) != 0) offset += 1U + packet[4];
    return offset <= 188 ? offset : 188;
  }

  static std::size_t demuxAac(std::uint8_t* bytes, std::size_t length) {
    if (bytes == nullptr || length < 188 || length % 188 != 0) return 0;
    std::uint16_t audioPid = 0xffff;
    for (std::size_t packetOffset = 0; packetOffset < length;
         packetOffset += 188) {
      const auto* packet = bytes + packetOffset;
      if (packet[0] != 0x47 || (packet[1] & 0x80U) != 0 ||
          (packet[1] & 0x40U) == 0) {
        continue;
      }
      const std::size_t offset = payloadOffset(packet);
      if (offset + 6 > 188 || packet[offset] != 0 || packet[offset + 1] != 0 ||
          packet[offset + 2] != 1 || packet[offset + 3] < 0xc0 ||
          packet[offset + 3] > 0xdf) {
        continue;
      }
      audioPid = static_cast<std::uint16_t>(((packet[1] & 0x1fU) << 8U) |
                                            packet[2]);
      break;
    }
    if (audioPid == 0xffff) return 0;

    std::size_t output = 0;
    for (std::size_t packetOffset = 0; packetOffset < length;
         packetOffset += 188) {
      const auto* packet = bytes + packetOffset;
      if (packet[0] != 0x47 || (packet[1] & 0x80U) != 0) continue;
      const std::uint16_t pid = static_cast<std::uint16_t>(
          ((packet[1] & 0x1fU) << 8U) | packet[2]);
      if (pid != audioPid) continue;
      std::size_t offset = payloadOffset(packet);
      if (offset >= 188) continue;
      if ((packet[1] & 0x40U) != 0) {
        if (offset + 9 > 188 || packet[offset] != 0 ||
            packet[offset + 1] != 0 || packet[offset + 2] != 1) {
          continue;
        }
        offset += 9U + packet[offset + 8];
        if (offset > 188) continue;
      }
      const std::size_t payload = 188 - offset;
      std::memmove(bytes + output, packet + offset, payload);
      output += payload;
    }
    return output;
  }

  bool queueSegment(const String& url) {
    std::size_t transportBytes = 0;
    if (!fetchSegment(url, transportBytes)) return false;
    const std::size_t aacBytes = demuxAac(segment_, transportBytes);
    if (aacBytes == 0 || !append(segment_, aacBytes)) return false;
    Serial.printf("hls segment ts=%u aac=%u buffered=%u\n",
                  static_cast<unsigned>(transportBytes),
                  static_cast<unsigned>(aacBytes),
                  static_cast<unsigned>(bufferedBytes()));
    return true;
  }

  void workerLoop() {
    String master;
    if (!fetchText(masterUrl_.data(), master)) {
      Serial.println("hls master result=error");
      open_.store(false, std::memory_order_release);
      return;
    }
    mediaUrl_ = masterUrl_.data();
    if (master.indexOf("#EXTINF") < 0) {
      String reference;
      if (!firstMediaLine(master, reference)) {
        open_.store(false, std::memory_order_release);
        return;
      }
      mediaUrl_ = resolveUrl(mediaUrl_, reference);
    }

    while (open_.load(std::memory_order_acquire)) {
      String playlist;
      std::array<String, 8> segments{};
      const bool fetched = fetchText(mediaUrl_, playlist);
      const std::size_t count = fetched ? segmentLines(playlist, segments) : 0;
      if (count == 0) {
        Serial.println("hls playlist result=error");
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      std::size_t start = count;
      if (initialPlaylist_) {
        start = count > 1 ? count - 2 : 0;
        initialPlaylist_ = false;
      } else {
        bool foundLast = false;
        for (std::size_t index = 0; index < count; ++index) {
          const String absolute = resolveUrl(mediaUrl_, segments[index]);
          if (absolute == lastSegmentUrl_) {
            start = index + 1;
            foundLast = true;
          }
        }
        if (foundLast && start == count) {
          vTaskDelay(pdMS_TO_TICKS(1000));
          continue;
        }
        if (!foundLast) start = count - 1;
      }
      for (std::size_t index = start;
           index < count && open_.load(std::memory_order_acquire); ++index) {
        const String absolute = resolveUrl(mediaUrl_, segments[index]);
        if (queueSegment(absolute)) {
          lastSegmentUrl_ = absolute;
        } else {
          Serial.println("hls segment result=error");
          break;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
  }

  mutable SemaphoreHandle_t mutex_ = nullptr;
  std::uint8_t* ring_ = nullptr;
  std::uint8_t* segment_ = nullptr;
  std::size_t ringRead_ = 0;
  std::size_t ringWrite_ = 0;
  std::size_t ringSize_ = 0;
  std::uint32_t position_ = 0;
  std::uint32_t underruns_ = 0;
  std::array<char, 192> masterUrl_{};
  String mediaUrl_;
  String lastSegmentUrl_;
  std::atomic<bool> open_{false};
  std::atomic<bool> workerRunning_{false};
  bool initialPlaylist_ = true;
};

}  // namespace open_radio::public_candidate
