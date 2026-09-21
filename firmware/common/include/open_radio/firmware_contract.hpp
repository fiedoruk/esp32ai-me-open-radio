#pragma once

#include <cstddef>
#include <cstdint>

namespace open_radio {

constexpr std::uint32_t kPcmSampleRate = 44100;
constexpr std::uint8_t kPcmChannels = 2;
constexpr std::uint8_t kPcmBitsPerSample = 16;

enum class OutputKind : std::uint8_t {
  LocalSpeaker,
  Bluetooth
};

enum class RuntimeState : std::uint8_t {
  ConfigRequired,
  Recovering,
  Playing,
  DegradedPlaying,
  UnsupportedStation,
  SafeMode
};

enum class CapabilityClass : std::uint8_t {
  Mp3Icy,
  HlsHeAac
};

struct PcmFrame {
  std::int16_t left;
  std::int16_t right;
};

// Select one ordered PCM transfer without consuming either queue. During
// Bluetooth standby, retainedSourceFrames protects the local speaker. Once
// A2DP is the active output it is zero: the Bluetooth queue itself is part of
// the ordered local-fallback stream and is preserved on disconnect, so
// stranding decoded PCM behind a separate floor only starves the live sink.
constexpr std::size_t selectPcmTransferFrames(
    std::size_t sourceBufferedFrames, std::size_t destinationBufferedFrames,
    std::size_t destinationCapacityFrames, std::size_t retainedSourceFrames,
    std::size_t maximumBatchFrames) {
  if (maximumBatchFrames == 0 ||
      sourceBufferedFrames <= retainedSourceFrames ||
      destinationBufferedFrames >= destinationCapacityFrames) {
    return 0;
  }
  const std::size_t sourceAvailable =
      sourceBufferedFrames - retainedSourceFrames;
  const std::size_t destinationAvailable =
      destinationCapacityFrames - destinationBufferedFrames;
  const std::size_t queueLimited =
      sourceAvailable < destinationAvailable ? sourceAvailable
                                             : destinationAvailable;
  return queueLimited < maximumBatchFrames ? queueLimited
                                            : maximumBatchFrames;
}

class AudioOutput {
 public:
  virtual ~AudioOutput() = default;
  virtual OutputKind kind() const = 0;
  virtual bool available() const = 0;
  virtual std::size_t write(const PcmFrame* frames, std::size_t count) = 0;
};

class BoardUi {
 public:
  virtual ~BoardUi() = default;
  virtual void present(RuntimeState state, std::uint8_t stationIndex,
                       OutputKind output) = 0;
};

template <std::size_t Capacity>
class PcmRingBuffer {
 public:
  static_assert(Capacity > 1, "PCM ring buffer must contain at least two frames");

  bool push(const PcmFrame& frame) {
    if (size_ == Capacity) {
      ++overruns_;
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
    if (readCount == 0) ++underruns_;
    return readCount;
  }

  std::size_t peek(PcmFrame* destination, std::size_t capacity) const {
    if (destination == nullptr || capacity == 0) return 0;
    const std::size_t readCount = capacity < size_ ? capacity : size_;
    std::size_t index = readIndex_;
    for (std::size_t offset = 0; offset < readCount; ++offset) {
      destination[offset] = frames_[index];
      index = (index + 1) % Capacity;
    }
    return readCount;
  }

  std::size_t discard(std::size_t count) {
    const std::size_t discarded = count < size_ ? count : size_;
    readIndex_ = (readIndex_ + discarded) % Capacity;
    size_ -= discarded;
    return discarded;
  }

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return Capacity; }
  std::uint32_t overruns() const { return overruns_; }
  std::uint32_t underruns() const { return underruns_; }

 private:
  PcmFrame frames_[Capacity]{};
  std::size_t readIndex_ = 0;
  std::size_t writeIndex_ = 0;
  std::size_t size_ = 0;
  std::uint32_t overruns_ = 0;
  std::uint32_t underruns_ = 0;
};

class OutputRouter {
 public:
  OutputRouter(AudioOutput& localOutput, AudioOutput& bluetoothOutput)
      : localOutput_(localOutput), bluetoothOutput_(bluetoothOutput) {}

  void preferBluetooth(bool preferred) { bluetoothPreferred_ = preferred; }

  OutputKind activeOutput() const {
    return bluetoothPreferred_ && bluetoothOutput_.available()
               ? OutputKind::Bluetooth
               : OutputKind::LocalSpeaker;
  }

  std::size_t write(const PcmFrame* frames, std::size_t count) {
    AudioOutput& selected = activeOutput() == OutputKind::Bluetooth
                                ? bluetoothOutput_
                                : localOutput_;
    // A zero-length Bluetooth write means transient queue backpressure, not a
    // lost A2DP link. Connection state owns failover; routing the same PCM to
    // the local speaker here would both leak audio from the cube and
    // permanently starve the Bluetooth callback after one full queue.
    return selected.write(frames, count);
  }

 private:
  AudioOutput& localOutput_;
  AudioOutput& bluetoothOutput_;
  bool bluetoothPreferred_ = true;
};

}
