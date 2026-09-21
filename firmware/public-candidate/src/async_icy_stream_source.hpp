#pragma once

#include <AudioFileSource.h>
#include <AudioFileSourceICYStream.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "open_radio/spsc_byte_ring.hpp"

namespace open_radio::public_candidate {

// ICY network reads must not run on the Arduino audio-owner loop. At weak but
// usable Wi-Fi RSSI, a synchronous 500 ms HTTP read drains A2DP even though the
// Bluetooth link itself stays healthy. This source keeps exactly one bounded,
// low-priority producer on core 0 and feeds the decoder through a PSRAM SPSC
// ring. StationRuntime remains the only reconnect owner.
class AsyncIcyStreamSource final : public ::AudioFileSource {
 public:
  AsyncIcyStreamSource() = default;

  ~AsyncIcyStreamSource() override {
    close();
    // readNonBlock can enter the bounded 500 ms ICY metadata path. Destruction
    // must never release its storage while that producer still owns it.
    while (workerRunning_.load(std::memory_order_acquire)) delay(2);
    delete upstream_;
    heap_caps_free(scratch_);
    heap_caps_free(ringStorage_);
  }

  bool open(const char* url) override {
    // close() reports whether the worker actually stopped; deleting upstream_
    // while a worker still owns it is the use-after-free this file's own
    // destructor takes care to avoid, and open() skipped the check (audit
    // 2026-09-08, V1-06). Today unreachable — the reads are non-blocking and
    // the ICY metadata wait is bounded to 500 ms — which is exactly when a
    // guard costs nothing.
    if (!close()) return false;
    delete upstream_;
    upstream_ = nullptr;
    if (url == nullptr || url[0] == '\0' ||
        std::strlen(url) >= kMaximumUrlBytes || !ensureStorage()) {
      return false;
    }
    // Radio-browser data decorates duplicate mounts with "#NAME" fragments.
    // A fragment is client-side only, but HTTPClient forwards it into the
    // GET line and smcdn answers 400 (measured 2026-07-22: /5380-1.mp3 is
    // 200, /5380-1.mp3#ESKA_ROCK is 400). The catalog is cleaned at the
    // source too; this strip also heals fragment URLs already saved in
    // device slots by earlier builds.
    char sanitizedUrl[kMaximumUrlBytes];
    std::snprintf(sanitizedUrl, sizeof(sanitizedUrl), "%s", url);
    if (char* fragment = std::strchr(sanitizedUrl, '#')) *fragment = '\0';
    url = sanitizedUrl;

    ring_.begin(ringStorage_);
    position_ = 0;
    receivedBytes_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    sourceStatusEvents_.store(0, std::memory_order_relaxed);
    lastSourceStatus_.store(0, std::memory_order_relaxed);
    workerStackMinimumBytes_.store(UINT32_MAX, std::memory_order_relaxed);
    portENTER_CRITICAL(&callbackMux_);
    metadataPending_ = false;
    pendingTitle_[0] = '\0';
    portEXIT_CRITICAL(&callbackMux_);

    upstream_ = new (std::nothrow) AudioFileSourceICYStream();
    if (upstream_ == nullptr) return false;
    upstream_->RegisterMetadataCB(handleUpstreamMetadata, this);
    upstream_->RegisterStatusCB(handleUpstreamStatus, this);
    upstream_->SetReconnect(0, 0);
    if (!upstream_->open(url)) return false;

    upstreamEnded_.store(false, std::memory_order_relaxed);
    open_.store(true, std::memory_order_release);
    workerRunning_.store(true, std::memory_order_release);
    if (xTaskCreatePinnedToCore(workerEntry, "icy-prefetch",
                                kWorkerStackBytes, this, 1, nullptr, 0) !=
        pdPASS) {
      workerRunning_.store(false, std::memory_order_release);
      open_.store(false, std::memory_order_release);
      upstream_->close();
      return false;
    }

    const std::uint32_t startedAtMs = millis();
    while (workerRunning_.load(std::memory_order_acquire) &&
           ring_.size() < kStartupBufferedBytes &&
           millis() - startedAtMs < kStartupTimeoutMs) {
      delay(5);
    }
    if (ring_.size() >= kStartupBufferedBytes) return true;
    close();
    return false;
  }

  std::uint32_t read(void* data, std::uint32_t length) override {
    return readFromRing(data, length, true);
  }

  std::uint32_t readNonBlock(void* data, std::uint32_t length) override {
    return readFromRing(data, length, false);
  }

  bool close() override {
    open_.store(false, std::memory_order_release);
    const std::uint32_t startedAtMs = millis();
    while (workerRunning_.load(std::memory_order_acquire) &&
           millis() - startedAtMs < kWorkerCloseTimeoutMs) {
      delay(2);
    }
    if (workerRunning_.load(std::memory_order_acquire)) return false;
    if (upstream_ != nullptr) upstream_->close();
    flushPendingCallbacks();
    return true;
  }

  bool isOpen() override {
    return open_.load(std::memory_order_acquire) &&
           (!upstreamEnded_.load(std::memory_order_acquire) ||
            ring_.size() != 0);
  }

  std::uint32_t getSize() override { return 0; }
  std::uint32_t getPos() override { return position_; }
  bool seek(std::int32_t, int) override { return false; }

  bool loop() override {
    flushPendingCallbacks();
    return isOpen();
  }

  std::size_t bufferedBytes() const { return ring_.size(); }
  std::uint32_t receivedBytes() const {
    return receivedBytes_.load(std::memory_order_relaxed);
  }
  std::uint32_t underruns() const {
    return underruns_.load(std::memory_order_relaxed);
  }
  std::uint32_t workerStackMinimumBytes() const {
    const auto value =
        workerStackMinimumBytes_.load(std::memory_order_relaxed);
    return value == UINT32_MAX ? 0 : value;
  }
  int lastSourceStatus() const {
    return lastSourceStatus_.load(std::memory_order_relaxed);
  }
  std::uint32_t sourceStatusEvents() const {
    return sourceStatusEvents_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr std::size_t kRingBytes = 128U * 1024U;
  static constexpr std::size_t kScratchBytes = 4096U;
  static constexpr std::size_t kStartupBufferedBytes = 16U * 1024U;
  static constexpr std::uint32_t kStartupTimeoutMs = 3000U;
  static constexpr std::uint32_t kReadWaitMs = 100U;
  static constexpr std::uint32_t kWorkerCloseTimeoutMs = 1500U;
  static constexpr std::uint32_t kWorkerStackBytes = 4096U;
  static constexpr std::size_t kMaximumUrlBytes = 128U;
  static constexpr std::size_t kTitleBytes = 192U;

  bool ensureStorage() {
    if (ringStorage_ == nullptr) {
      ringStorage_ = static_cast<std::uint8_t*>(heap_caps_malloc(
          kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (scratch_ == nullptr) {
      scratch_ = static_cast<std::uint8_t*>(heap_caps_malloc(
          kScratchBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    return ringStorage_ != nullptr && scratch_ != nullptr;
  }

  std::uint32_t readFromRing(void* data, std::uint32_t length, bool wait) {
    flushPendingCallbacks();
    if (data == nullptr || length == 0) return 0;
    if (wait) {
      const std::uint32_t startedAtMs = millis();
      while (open_.load(std::memory_order_acquire) && ring_.size() == 0 &&
             workerRunning_.load(std::memory_order_acquire) &&
             millis() - startedAtMs < kReadWaitMs) {
        delay(2);
      }
    }
    const std::size_t count = ring_.read(
        static_cast<std::uint8_t*>(data), static_cast<std::size_t>(length));
    position_ += static_cast<std::uint32_t>(count);
    if (count == 0) underruns_.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::uint32_t>(count);
  }

  void flushPendingCallbacks() {
    std::array<char, kTitleBytes> title{};
    bool hasMetadata = false;
    portENTER_CRITICAL(&callbackMux_);
    if (metadataPending_) {
      std::memcpy(title.data(), pendingTitle_.data(), title.size());
      metadataPending_ = false;
      hasMetadata = true;
    }
    portEXIT_CRITICAL(&callbackMux_);
    if (hasMetadata) cb.md("StreamTitle", false, title.data());
  }

  void updateWorkerStackMinimum() {
    const auto availableBytes =
        static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    auto minimum =
        workerStackMinimumBytes_.load(std::memory_order_relaxed);
    while (availableBytes < minimum &&
           !workerStackMinimumBytes_.compare_exchange_weak(
               minimum, availableBytes, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
  }

  void workerLoop() {
    std::uint32_t nextStackCheckMs = 0;
    while (open_.load(std::memory_order_acquire)) {
      if (static_cast<std::int32_t>(millis() - nextStackCheckMs) >= 0) {
        updateWorkerStackMinimum();
        nextStackCheckMs = millis() + 1000U;
      }
      const std::uint32_t received =
          upstream_ == nullptr
              ? 0
              : upstream_->readNonBlock(scratch_, kScratchBytes);
      if (received == 0) {
        if (upstream_ == nullptr || !upstream_->isOpen()) break;
        vTaskDelay(pdMS_TO_TICKS(2));
        continue;
      }

      std::size_t written = 0;
      while (written < received && open_.load(std::memory_order_acquire)) {
        const std::size_t chunk =
            ring_.write(scratch_ + written, received - written);
        written += chunk;
        if (chunk == 0) vTaskDelay(pdMS_TO_TICKS(2));
      }
      receivedBytes_.fetch_add(static_cast<std::uint32_t>(written),
                               std::memory_order_relaxed);
    }
    if (upstream_ != nullptr) upstream_->close();
    upstreamEnded_.store(true, std::memory_order_release);
    updateWorkerStackMinimum();
  }

  static void workerEntry(void* context) {
    auto* source = static_cast<AsyncIcyStreamSource*>(context);
    source->workerLoop();
    source->workerRunning_.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
  }

  static void handleUpstreamStatus(void* context, int code, const char*) {
    auto* source = static_cast<AsyncIcyStreamSource*>(context);
    if (source == nullptr) return;
    source->lastSourceStatus_.store(code, std::memory_order_relaxed);
    source->sourceStatusEvents_.fetch_add(1, std::memory_order_relaxed);
  }

  static void handleUpstreamMetadata(void* context, const char* type, bool,
                                     const char* value) {
    auto* source = static_cast<AsyncIcyStreamSource*>(context);
    if (source == nullptr || type == nullptr || value == nullptr ||
        std::strcmp(type, "StreamTitle") != 0) {
      return;
    }
    portENTER_CRITICAL(&source->callbackMux_);
    std::strncpy(source->pendingTitle_.data(), value,
                 source->pendingTitle_.size() - 1U);
    source->pendingTitle_.back() = '\0';
    source->metadataPending_ = true;
    portEXIT_CRITICAL(&source->callbackMux_);
  }

  AudioFileSourceICYStream* upstream_ = nullptr;
  std::uint8_t* ringStorage_ = nullptr;
  std::uint8_t* scratch_ = nullptr;
  open_radio::SpscByteRing<kRingBytes> ring_;
  std::atomic<bool> open_{false};
  std::atomic<bool> workerRunning_{false};
  std::atomic<bool> upstreamEnded_{true};
  std::atomic<std::uint32_t> receivedBytes_{0};
  std::atomic<std::uint32_t> underruns_{0};
  std::atomic<std::uint32_t> workerStackMinimumBytes_{UINT32_MAX};
  std::atomic<int> lastSourceStatus_{0};
  std::atomic<std::uint32_t> sourceStatusEvents_{0};
  std::uint32_t position_ = 0;
  std::array<char, kTitleBytes> pendingTitle_{};
  bool metadataPending_ = false;
  portMUX_TYPE callbackMux_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace open_radio::public_candidate
