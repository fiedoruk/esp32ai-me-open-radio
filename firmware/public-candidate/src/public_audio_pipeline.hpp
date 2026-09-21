#pragma once

#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "open_radio/firmware_contract.hpp"
#include "async_icy_stream_source.hpp"
#if defined(OPEN_RADIO_HAS_HLS)
#include <AudioGeneratorAAC.h>
#include "hls_aac_source.hpp"
#endif

namespace open_radio {
namespace public_candidate {

// M5Unified can queue two buffers per channel. Keep two complete ~2.97 s
// speaker blocks ready so local playback continues through the longest
// measured synchronous stream operation (5.313 s). The third backing buffer
// lets M5Unified finish the current block while the owner prepares the next.
// All large buffers live in PSRAM.
constexpr std::size_t kDecodedPcmFrames = 262144;
constexpr std::size_t kLocalSpeakerFramesPerBlock = 131072;
constexpr std::size_t kLocalDecodedReserveTargetFrames =
    kDecodedPcmFrames;
// A2DP has its own ~2.97 s output queue. Keep another ~3.34 s decoded while
// Bluetooth is active. On disconnect the two ordered queues are consumed as
// one local fallback stream, which fills both M5Unified slots immediately.
// Post-decoder-budget physics: a live stream delivers 1x realtime after its
// CDN burst, so decoded + BT-queue TOTAL can never exceed the burst
// equilibrium (~2-5 s, station dependent; 98,304 frames measured 2026-07-17).
// Every floor above that equilibrium silently starves the Bluetooth path —
// the old 147,456-frame reserve was only reachable via the monolithic decode
// slurp that froze touch. One second of local safety is what the fallback
// actually needs while frames stream onward to A2DP.
constexpr std::size_t kBluetoothFallbackReserveFrames = 44100;
// Keep about 5.94 s of A2DP PCM. The previous 2.97 s queue was 0.24 s shorter
// than a measured post-reconnect stream outage and produced an audible cut.
// Internet-radio latency is not interactive, and station changes explicitly
// clear this queue. The reserve lives in PSRAM, not scarce internal DRAM.
constexpr std::size_t kBluetoothPcmFrames = 262144;

// The full Wi-Fi + Bluetooth build has a tight internal-DRAM budget. PCM is
// not consumed by a DMA peripheral directly, so keep these large queues in
// PSRAM and reserve capability-aware internal memory for radio stacks, TLS,
// decoder state and DMA-owned buffers. Allocation is explicit after M5.begin;
// a missing PSRAM queue fails closed instead of silently falling back to DRAM.
template <std::size_t Capacity>
class PsramPcmRingBuffer {
 public:
  static_assert(Capacity > 1,
                "PCM ring buffer must contain at least two frames");

  ~PsramPcmRingBuffer() { heap_caps_free(frames_); }

  bool begin() {
    if (frames_ != nullptr) return true;
    frames_ = static_cast<PcmFrame*>(heap_caps_calloc(
        Capacity, sizeof(PcmFrame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return frames_ != nullptr;
  }

  bool ready() const { return frames_ != nullptr; }

  bool push(const PcmFrame& frame) {
    if (frames_ == nullptr || size_ == Capacity) {
      ++backpressureEvents_;
      return false;
    }
    frames_[writeIndex_] = frame;
    writeIndex_ = (writeIndex_ + 1) % Capacity;
    ++size_;
    return true;
  }

  std::size_t pop(PcmFrame* destination, std::size_t capacity) {
    const std::size_t readCount = peek(destination, capacity);
    discard(readCount);
    return readCount;
  }

  std::size_t peek(PcmFrame* destination, std::size_t capacity) const {
    if (frames_ == nullptr || destination == nullptr || capacity == 0) return 0;
    const std::size_t readCount = std::min(capacity, size_);
    std::size_t index = readIndex_;
    for (std::size_t offset = 0; offset < readCount; ++offset) {
      destination[offset] = frames_[index];
      index = (index + 1) % Capacity;
    }
    return readCount;
  }

  std::size_t discard(std::size_t count) {
    const std::size_t discarded = std::min(count, size_);
    readIndex_ = (readIndex_ + discarded) % Capacity;
    size_ -= discarded;
    return discarded;
  }

  void clear() {
    readIndex_ = 0;
    writeIndex_ = 0;
    size_ = 0;
  }

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return Capacity; }
  std::uint32_t backpressureEvents() const { return backpressureEvents_; }

  PsramPcmRingBuffer() = default;
  PsramPcmRingBuffer(const PsramPcmRingBuffer&) = delete;
  PsramPcmRingBuffer& operator=(const PsramPcmRingBuffer&) = delete;

 private:
  PcmFrame* frames_ = nullptr;
  std::size_t readIndex_ = 0;
  std::size_t writeIndex_ = 0;
  std::size_t size_ = 0;
  std::uint32_t backpressureEvents_ = 0;
};

class DecoderQueueOutput final : public ::AudioOutput {
 public:
  explicit DecoderQueueOutput(PsramPcmRingBuffer<kDecodedPcmFrames>& queue)
      : queue_(queue) {}

  void beginOwnerLoop() { ownerLoopFrames_ = 0; }

  bool SetRate(int rate) override {
    // 48000: HE-AAC 24 kHz core expanded by SBR (HLS lane). 22050: HE-AAC
    // 44.1 kHz family core decoded WITHOUT SBR by Helix — the 48 kb/s aacp
    // rmfstream mounts land here and get upsampled 2x by the resampler below.
    rateSupported_ = rate == static_cast<int>(kPcmSampleRate) ||
                     rate == 48000 || rate == 22050;
    if (rateSupported_ && sourceRate_ != rate) {
      sourceRate_ = rate;
      resetResampler();
    }
    Serial.printf("audio_format rate=%d supported=%s\n", rate,
                  rateSupported_ ? "yes" : "no");
    return rateSupported_;
  }

  bool SetChannels(int channelCount) override {
    // Mono is fine: every ESP8266Audio generator duplicates a mono sample
    // into both slots of the frame before ConsumeSample (verified in
    // AudioGeneratorAAC.cpp), so the queue always receives stereo frames.
    channelsSupported_ = channelCount == static_cast<int>(kPcmChannels) ||
                         channelCount == 1;
    Serial.printf("audio_format channels=%d supported=%s\n", channelCount,
                  channelsSupported_ ? "yes" : "no");
    return channelsSupported_;
  }

  bool begin() override {
    active_ = true;
    rateSupported_ = true;
    channelsSupported_ = true;
    sourceRate_ = kPcmSampleRate;
    resetResampler();
    return true;
  }

  bool ConsumeSample(std::int16_t sample[2]) override {
    if (!active_ || !formatSupported()) return false;
    if (ownerLoopFrames_ >= kFramesPerOwnerLoop) return false;
    // AudioGeneratorMP3 keeps producing samples inside one loop() call until
    // the output refuses one. Bound that burst independently from the large
    // PSRAM reserve so live A2DP playback cannot monopolize the UI loop while
    // filling several seconds of decoded PCM in one pass.
    if (queue_.size() >= highWaterFrames_) return false;
    if (sourceRate_ == static_cast<int>(kPcmSampleRate)) {
      if (!queue_.push({sample[0], sample[1]})) return false;
      ++ownerLoopFrames_;
      return true;
    }
    // Generic-ratio linear resampler on the shared 44.1 kHz output clock.
    // Downsampling (48 kHz HE-AAC core from HLS) emits at most one frame per
    // input; upsampling (22.05 kHz HE-AAC core from the 48 kb/s aacp mounts)
    // emits up to two. outputIndex_ persists across a mid-burst queue
    // refusal, so when the generator retries the same input sample the loop
    // resumes at exactly the next missing output frame — nothing doubles.
    if (!hasPrevious_) {
      if (!queue_.push({sample[0], sample[1]})) return false;
      previous_ = {sample[0], sample[1]};
      hasPrevious_ = true;
      inputIndex_ = 0;
      outputIndex_ = 1;
      ++ownerLoopFrames_;
      return true;
    }
    const std::uint64_t currentPosition =
        (inputIndex_ + 1U) * static_cast<std::uint64_t>(kPcmSampleRate);
    while (true) {
      const std::uint64_t nextOutputPosition =
          outputIndex_ * static_cast<std::uint64_t>(sourceRate_);
      if (nextOutputPosition > currentPosition) break;
      const std::uint64_t previousPosition =
          inputIndex_ * static_cast<std::uint64_t>(kPcmSampleRate);
      const std::uint32_t fraction = static_cast<std::uint32_t>(
          nextOutputPosition - previousPosition);
      const auto interpolate = [fraction](std::int16_t from,
                                          std::int16_t to) {
        const std::int64_t weighted =
            static_cast<std::int64_t>(from) *
                (static_cast<std::int64_t>(kPcmSampleRate) - fraction) +
            static_cast<std::int64_t>(to) * fraction;
        return static_cast<std::int16_t>(weighted / kPcmSampleRate);
      };
      const PcmFrame output{interpolate(previous_.left, sample[0]),
                            interpolate(previous_.right, sample[1])};
      if (!queue_.push(output)) return false;
      ++outputIndex_;
      ++ownerLoopFrames_;
    }
    previous_ = {sample[0], sample[1]};
    ++inputIndex_;
    return true;
  }

  bool formatSupported() const {
    return rateSupported_ && channelsSupported_;
  }

  std::size_t bufferedFrames() const { return queue_.size(); }
  void setHighWaterFrames(std::size_t frames) {
    highWaterFrames_ =
        std::max<std::size_t>(1, std::min(frames, queue_.capacity()));
  }
  std::uint32_t backpressureEvents() const {
    return queue_.backpressureEvents();
  }

  bool stop() override {
    active_ = false;
    return true;
  }

  void clear() {
    queue_.clear();
    resetResampler();
  }

 private:
  // Under a sustained ingest deficit every decode pass fills the whole budget
  // with blocking socket reads inside it, so the budget sets the LOOP PASS
  // LENGTH, not throughput: 24,576 measured p50 784 ms / max 1,634 ms per pass
  // (touch dead), 8,192 measured p50 ~350 ms, while injected silence stayed
  // ~identical (soaks 2026-07-17: 4.0M frames at 8,192 vs 3.9M at 24,576).
  // Throughput is capped by Wi-Fi socket drain under A2DP coexistence
  // (~12-14 KB/s ~= LWIP 5.7 KB window / coex-inflated RTT); raising this
  // budget cannot buy frames, it only starves the UI. Keep it touch-sized.
  static constexpr std::size_t kFramesPerOwnerLoop = 8192;

  void resetResampler() {
    previous_ = {};
    hasPrevious_ = false;
    inputIndex_ = 0;
    outputIndex_ = 0;
  }

  PsramPcmRingBuffer<kDecodedPcmFrames>& queue_;
  PcmFrame previous_{};
  std::uint64_t inputIndex_ = 0;
  std::uint64_t outputIndex_ = 0;
  std::size_t ownerLoopFrames_ = 0;
  int sourceRate_ = kPcmSampleRate;
  bool active_ = false;
  bool rateSupported_ = true;
  bool channelsSupported_ = true;
  bool hasPrevious_ = false;
  std::size_t highWaterFrames_ = kDecodedPcmFrames;
};

class LocalSpeakerOutput final : public AudioOutput {
 public:
  ~LocalSpeakerOutput() override {
    for (auto* buffer : buffers_) heap_caps_free(buffer);
  }

  OutputKind kind() const override { return OutputKind::LocalSpeaker; }
  bool available() const override { return M5.Speaker.isEnabled(); }

  std::size_t write(const PcmFrame* frames, std::size_t count) override {
    if (count != kLocalSpeakerFramesPerBlock) return 0;
    return writeAcceptedFrames(frames, count);
  }

  // Only a completed local-source fade may use a short final block. Network
  // playback remains locked to the multi-second block contract in write().
  std::size_t writeFadeTail(const PcmFrame* frames, std::size_t count) {
    if (count == 0 || count > kLocalSpeakerFramesPerBlock) return 0;
    return writeAcceptedFrames(frames, count);
  }

  std::uint32_t starvations() const { return starvations_; }
  std::size_t bufferedBlocks() const { return M5.Speaker.isPlaying(kChannel); }

  void stopPlayback() {
    M5.Speaker.stop(kChannel);
    started_ = false;
    starvationLatched_ = false;
  }

 private:
  std::size_t writeAcceptedFrames(const PcmFrame* frames, std::size_t count) {
    if (!available() || !ensureBuffers() || frames == nullptr) return 0;

    const auto queuedBlocks = M5.Speaker.isPlaying(kChannel);
    if (started_ && queuedBlocks == 0 && !starvationLatched_) {
      ++starvations_;
      starvationLatched_ = true;
    } else if (queuedBlocks > 0) {
      starvationLatched_ = false;
    }
    if (queuedBlocks >= 2) return 0;

    auto* samples = buffers_[nextBuffer_];
    for (std::size_t index = 0; index < count; ++index) {
      // Core2 has one physical speaker. M5Unified otherwise sums stereo L+R
      // without headroom in its mono mixer, which can make correlated peaks
      // sound compressed or muffled. Average explicitly; Bluetooth keeps the
      // untouched stereo PCM path.
      const std::int32_t mono = static_cast<std::int32_t>(frames[index].left) +
                                frames[index].right;
      samples[index] = static_cast<std::int16_t>(mono / 2);
    }
    const bool accepted =
        M5.Speaker.playRaw(samples, count, kPcmSampleRate, false, 1,
                           kChannel, false);
    if (!accepted) return 0;
    started_ = true;
    nextBuffer_ = (nextBuffer_ + 1) % buffers_.size();
    return count;
  }
  // M5Unified exposes two queued playback slots per channel. Two complete
  // blocks cover about 5.94 s at 44.1 kHz.
  static constexpr std::uint8_t kChannel = 0;
  bool ensureBuffers() {
    if (buffers_[0] != nullptr) return true;
    for (auto& buffer : buffers_) {
      buffer = static_cast<std::int16_t*>(heap_caps_malloc(
          kLocalSpeakerFramesPerBlock * sizeof(std::int16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (buffer == nullptr) {
        for (auto*& allocated : buffers_) {
          heap_caps_free(allocated);
          allocated = nullptr;
        }
        return false;
      }
    }
    return true;
  }

  std::array<std::int16_t*, 3> buffers_{};
  std::size_t nextBuffer_ = 0;
  std::uint32_t starvations_ = 0;
  bool started_ = false;
  bool starvationLatched_ = false;
};

class BluetoothQueueOutput final : public AudioOutput {
 public:
  bool begin() { return queue_.begin(); }

  OutputKind kind() const override { return OutputKind::Bluetooth; }
  bool available() const override {
    return connected_.load() && queue_.ready();
  }

  std::size_t write(const PcmFrame* frames, std::size_t count) override {
    if (!connected_.load() || frames == nullptr) return 0;
    std::size_t accepted = 0;
    while (accepted < count) {
      const std::size_t chunk =
          std::min<std::size_t>(kWriteChunkFrames, count - accepted);
      std::size_t chunkAccepted = 0;
      portENTER_CRITICAL(&queueMux_);
      while (chunkAccepted < chunk &&
             queue_.push(frames[accepted + chunkAccepted])) {
        ++chunkAccepted;
      }
      portEXIT_CRITICAL(&queueMux_);
      accepted += chunkAccepted;
      if (chunkAccepted != chunk) {
        backpressureEvents_.fetch_add(1, std::memory_order_relaxed);
        break;
      }
    }
    return accepted;
  }

  std::size_t read(PcmFrame* frames, std::size_t capacity) {
    portENTER_CRITICAL(&queueMux_);
    const std::size_t received = connected_.load(std::memory_order_relaxed)
                                     ? queue_.pop(frames, capacity)
                                     : 0;
    portEXIT_CRITICAL(&queueMux_);
    return received;
  }

  void setConnected(bool connected, bool clearBuffered = true) {
    connected_.store(connected);
    if (!connected && clearBuffered) {
      portENTER_CRITICAL(&queueMux_);
      queue_.clear();
      portEXIT_CRITICAL(&queueMux_);
    }
  }

  void setPlaybackActive(bool active) {
    playbackActive_.store(active, std::memory_order_relaxed);
  }

  void recordSilenceFrames(std::size_t count) {
    if (count == 0) return;
    auto& counter = playbackActive_.load(std::memory_order_relaxed)
                        ? activeUnderrunFrames_
                        : idleSilenceFrames_;
    counter.fetch_add(static_cast<std::uint32_t>(count),
                      std::memory_order_relaxed);
  }

  void clear() {
    portENTER_CRITICAL(&queueMux_);
    queue_.clear();
    portEXIT_CRITICAL(&queueMux_);
  }

  std::uint32_t activeUnderrunFrames() const {
    return activeUnderrunFrames_.load(std::memory_order_relaxed);
  }

  std::uint32_t idleSilenceFrames() const {
    return idleSilenceFrames_.load(std::memory_order_relaxed);
  }

  std::uint32_t backpressureEvents() const {
    return backpressureEvents_.load(std::memory_order_relaxed);
  }

  std::size_t bufferedFrames() {
    portENTER_CRITICAL(&queueMux_);
    const auto value = queue_.size();
    portEXIT_CRITICAL(&queueMux_);
    return value;
  }

  std::size_t peekBuffered(PcmFrame* frames, std::size_t capacity) {
    portENTER_CRITICAL(&queueMux_);
    const auto value = queue_.peek(frames, capacity);
    portEXIT_CRITICAL(&queueMux_);
    return value;
  }

  std::size_t discardBuffered(std::size_t count) {
    portENTER_CRITICAL(&queueMux_);
    const auto value = queue_.discard(count);
    portEXIT_CRITICAL(&queueMux_);
    return value;
  }

 private:
  static constexpr std::size_t kWriteChunkFrames = 256;
  PsramPcmRingBuffer<kBluetoothPcmFrames> queue_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> playbackActive_{false};
  std::atomic<std::uint32_t> activeUnderrunFrames_{0};
  std::atomic<std::uint32_t> idleSilenceFrames_{0};
  std::atomic<std::uint32_t> backpressureEvents_{0};
  portMUX_TYPE queueMux_ = portMUX_INITIALIZER_UNLOCKED;
};

class Mp3StreamPipeline {
 public:
  using MetadataCallback = void (*)(void* context, const char* title);

  explicit Mp3StreamPipeline(PsramPcmRingBuffer<kDecodedPcmFrames>& queue)
      : output_(queue) {}
  ~Mp3StreamPipeline() { stop(); }

  void setMetadataCallback(MetadataCallback callback, void* context) {
    metadataCallback_ = callback;
    metadataContext_ = context;
  }

  void setBluetoothRealtimeMode(bool active) {
    bluetoothRealtimeMode_ = active;
    pumpLowWaterFrames_ =
        active ? kBluetoothDecoderLowWaterFrames : kDecoderPumpTargetFrames;
    output_.setHighWaterFrames(
        active ? kBluetoothDecoderHighWaterFrames : kDecodedPcmFrames);
  }

  void beginOwnerLoop() { output_.beginOwnerLoop(); }

  bool start(const char* endpoint, bool aacStream = false) {
    close(false);
    if (endpoint == nullptr || endpoint[0] == '\0' ||
        std::strlen(endpoint) >= kEndpointBytesMax) {
      return false;
    }
#if !defined(OPEN_RADIO_HAS_HLS)
    // The Helix AAC decoder ships only in lanes that also carry HLS (see
    // select_mp3_sources.py srcFilter); without it an aacp mount cannot play.
    if (aacStream) return false;
#endif
    auto* asyncSource = new (std::nothrow) AsyncIcyStreamSource();
    source_ = asyncSource;
    asyncSource_ = asyncSource;
    if (asyncSource == nullptr) {
      close(false);
      return false;
    }
    asyncSource->RegisterMetadataCB(handleMetadata, this);
    const std::uint32_t connectStartedAtMs = millis();
    const bool connected = asyncSource->open(endpoint);
    Serial.printf(
        "stream_stage stage=connect duration_ms=%lu result=%s codec=%s\n",
        static_cast<unsigned long>(millis() - connectStartedAtMs),
        connected ? "ok" : "fail", aacStream ? "aac" : "mp3");
    if (!connected) {
      close(false);
      return false;
    }
#if defined(OPEN_RADIO_HAS_HLS)
    if (aacStream) {
      decoder_ = new (std::nothrow) AudioGeneratorAAC();
    } else {
      decoder_ = new (std::nothrow) AudioGeneratorMP3();
    }
#else
    decoder_ = new (std::nothrow) AudioGeneratorMP3();
#endif
    if (decoder_ == nullptr) {
      close(false);
      return false;
    }
    const std::uint32_t firstReadStartedAtMs = millis();
    const bool decoderReady = decoder_->begin(source_, &output_);
    Serial.printf(
        "stream_stage stage=first-read duration_ms=%lu result=%s codec=%s\n",
        static_cast<unsigned long>(millis() - firstReadStartedAtMs),
        decoderReady ? "ok" : "fail", aacStream ? "aac" : "mp3");
    if (!decoderReady) {
      close(false);
      return false;
    }
    decoderStarted_ = true;
    return true;
  }

  bool startHls(const char* endpoint) {
#if defined(OPEN_RADIO_HAS_HLS)
    close(false);
    if (endpoint == nullptr || endpoint[0] == '\0' ||
        std::strlen(endpoint) >= kHlsEndpointBytesMax) {
      return false;
    }
    auto* hls = new (std::nothrow) HlsAacSource();
    source_ = hls;
    if (hls == nullptr) {
      close(false);
      return false;
    }
    const std::uint32_t connectStartedAtMs = millis();
    const bool connected = hls->open(endpoint);
    Serial.printf(
        "stream_stage stage=connect duration_ms=%lu result=%s codec=hls\n",
        static_cast<unsigned long>(millis() - connectStartedAtMs),
        connected ? "ok" : "fail");
    if (!connected) {
      close(false);
      return false;
    }
    decoder_ = new (std::nothrow) AudioGeneratorAAC();
    if (decoder_ == nullptr) {
      close(false);
      return false;
    }
    const std::uint32_t firstReadStartedAtMs = millis();
    const bool decoderReady = decoder_->begin(source_, &output_);
    Serial.printf(
        "stream_stage stage=first-read duration_ms=%lu result=%s codec=hls\n",
        static_cast<unsigned long>(millis() - firstReadStartedAtMs),
        decoderReady ? "ok" : "fail");
    if (!decoderReady) {
      close(false);
      return false;
    }
    decoderStarted_ = true;
    return true;
#else
    (void)endpoint;
    return false;
#endif
  }

  bool loop() {
    if (decoder_ == nullptr || !decoder_->isRunning()) return false;
    const std::uint32_t refillStartedAtMs = millis();
    const std::size_t refillFramesBefore = output_.bufferedFrames();
    const auto reportRefill = [this, refillStartedAtMs, refillFramesBefore](
                                  const char* result) {
      const std::uint32_t durationMs = millis() - refillStartedAtMs;
      // 50 ms flooded the 512-event capture budget once the per-loop decoder
      // budget landed (steady passes sit at 65-85 ms). Keep only real stalls.
      if (durationMs < 150U) return;
      Serial.printf(
          "stream_stage stage=decode-refill duration_ms=%lu result=%s "
          "frames_before=%u frames_after=%u\n",
          static_cast<unsigned long>(durationMs), result,
          static_cast<unsigned>(refillFramesBefore),
          static_cast<unsigned>(output_.bufferedFrames()));
    };
    // One Helix HE-AAC frame expands to roughly 1882 output frames after the
    // 48 -> 44.1 kHz conversion. Pump a bounded burst so the speaker receives
    // its second hardware block before the first one drains. The queue stops
    // ConsumeSample automatically, so this cannot grow without bound.
    std::size_t pass = 0;
    std::size_t noProgressPasses = 0;
    std::size_t previousBufferedFrames = output_.bufferedFrames();
    while (output_.bufferedFrames() < pumpLowWaterFrames_ &&
           (bluetoothRealtimeMode_ || pass < kDecoderPumpPasses)) {
      const bool running = decoder_->loop();
      if (!output_.formatSupported()) {
        reportRefill("format-error");
        close(false);
        return false;
      }
      if (!running) {
        reportRefill("stopped");
        close(false);
        return false;
      }
      ++pass;
      const std::size_t bufferedFrames = output_.bufferedFrames();
      if (bufferedFrames > previousBufferedFrames) {
        noProgressPasses = 0;
      } else if (++noProgressPasses >= kDecoderNoProgressPasses) {
        // A running decoder can consume headers or wait for the next input
        // block without emitting PCM. Keep that state bounded instead of
        // spinning the owner task forever.
        break;
      }
      previousBufferedFrames = bufferedFrames;
    }
    reportRefill("running");
    return true;
  }

  void stop() { close(true); }

  std::uint32_t decodedBackpressureEvents() const {
    return output_.backpressureEvents();
  }

  std::size_t inputBufferedBytes() const {
    return asyncSource_ == nullptr ? 0 : asyncSource_->bufferedBytes();
  }

  std::uint32_t inputReceivedBytes() const {
    return completedInputReceivedBytes_ +
           (asyncSource_ == nullptr ? 0 : asyncSource_->receivedBytes());
  }

  std::uint32_t inputUnderruns() const {
    return completedInputUnderruns_ +
           (asyncSource_ == nullptr ? 0 : asyncSource_->underruns());
  }

  std::uint32_t inputWorkerStackMinimumBytes() const {
    const std::uint32_t live =
        asyncSource_ == nullptr ? 0 : asyncSource_->workerStackMinimumBytes();
    if (completedInputWorkerStackMinimumBytes_ == UINT32_MAX) return live;
    if (live == 0) return completedInputWorkerStackMinimumBytes_;
    return std::min(completedInputWorkerStackMinimumBytes_, live);
  }

  int lastSourceStatus() const {
    return asyncSource_ == nullptr ? lastSourceStatus_
                                   : asyncSource_->lastSourceStatus();
  }

  std::uint32_t sourceStatusEvents() const {
    return completedSourceStatusEvents_ +
           (asyncSource_ == nullptr ? 0 : asyncSource_->sourceStatusEvents());
  }

 private:
  void close(bool clearOutput) {
    if (decoder_ != nullptr && decoderStarted_) decoder_->stop();
    decoderStarted_ = false;
    delete decoder_;
    decoder_ = nullptr;
    if (asyncSource_ != nullptr) {
      asyncSource_->loop();
      completedInputReceivedBytes_ += asyncSource_->receivedBytes();
      completedInputUnderruns_ += asyncSource_->underruns();
      lastSourceStatus_ = asyncSource_->lastSourceStatus();
      completedSourceStatusEvents_ += asyncSource_->sourceStatusEvents();
      const std::uint32_t workerStackBytes =
          asyncSource_->workerStackMinimumBytes();
      if (workerStackBytes != 0) {
        completedInputWorkerStackMinimumBytes_ = std::min(
            completedInputWorkerStackMinimumBytes_, workerStackBytes);
      }
    }
    delete source_;
    source_ = nullptr;
    asyncSource_ = nullptr;
    if (clearOutput) output_.clear();
  }

  static void handleMetadata(void* context, const char* type, bool,
                             const char* value) {
    auto* pipeline = static_cast<Mp3StreamPipeline*>(context);
    if (pipeline == nullptr || type == nullptr || value == nullptr ||
        std::strcmp(type, "StreamTitle") != 0 ||
        pipeline->metadataCallback_ == nullptr) {
      return;
    }
    pipeline->metadataCallback_(pipeline->metadataContext_, value);
  }

  // Keep each owner-loop decode burst bounded. The transition supervisor fills
  // the BT queue before media starts instead of trying to rebuild every reserve
  // in one monolithic call; eight MP3 frames provide catch-up without starving
  // local playback or the UI during a blocking network read.
  static constexpr std::size_t kDecoderPumpPasses = 8;
  static constexpr std::size_t kDecoderNoProgressPasses = 8;
  // Local output accepts only complete long-lived hardware blocks. Refill as
  // soon as the decoded reserve cannot provide the next block; a smaller low
  // watermark can deadlock at a non-zero remainder that is too short to drain.
  static constexpr std::size_t kDecoderPumpTargetFrames =
      kLocalDecodedReserveTargetFrames;
  static constexpr std::size_t kBluetoothDecoderLowWaterFrames =
      kDecodedPcmFrames;
  static constexpr std::size_t kBluetoothDecoderHighWaterFrames =
      kDecodedPcmFrames;
  static constexpr std::size_t kEndpointBytesMax = 128;
  static constexpr std::size_t kHlsEndpointBytesMax = 192;
  DecoderQueueOutput output_;
  AudioFileSource* source_ = nullptr;
  AsyncIcyStreamSource* asyncSource_ = nullptr;
  AudioGenerator* decoder_ = nullptr;
  MetadataCallback metadataCallback_ = nullptr;
  void* metadataContext_ = nullptr;
  std::uint32_t completedInputReceivedBytes_ = 0;
  std::uint32_t completedInputUnderruns_ = 0;
  std::uint32_t completedInputWorkerStackMinimumBytes_ = UINT32_MAX;
  int lastSourceStatus_ = 0;
  std::uint32_t completedSourceStatusEvents_ = 0;
  bool decoderStarted_ = false;
  bool bluetoothRealtimeMode_ = false;
  std::size_t pumpLowWaterFrames_ = kDecoderPumpTargetFrames;
};

}
}
