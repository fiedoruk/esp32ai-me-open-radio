#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace open_radio {

// Single-producer/single-consumer byte ring backed by caller-owned storage.
// The producer and consumer never share a lock while copying PSRAM blocks.
// Sequence counters are monotonic; unsigned subtraction remains valid across
// wrap as long as the bounded capacity stays below half the counter range.
template <std::size_t Capacity>
class SpscByteRing {
 public:
  static_assert(Capacity > 1, "SPSC byte ring must hold at least two bytes");
  static_assert((Capacity & (Capacity - 1U)) == 0,
                "SPSC byte ring capacity must be a power of two");
  static_assert(Capacity < (std::uint64_t{1} << 31U),
                "SPSC byte ring capacity must fit sequence arithmetic");

  void begin(std::uint8_t* storage) {
    storage_ = storage;
    clear();
  }

  bool ready() const { return storage_ != nullptr; }

  std::size_t write(const std::uint8_t* source, std::size_t length) {
    if (storage_ == nullptr || source == nullptr || length == 0) return 0;
    const std::uint32_t writeSequence =
        writeSequence_.load(std::memory_order_relaxed);
    const std::uint32_t readSequence =
        readSequence_.load(std::memory_order_acquire);
    const std::size_t used =
        static_cast<std::uint32_t>(writeSequence - readSequence);
    const std::size_t count = std::min(length, Capacity - used);
    const std::size_t writeIndex = writeSequence & (Capacity - 1U);
    const std::size_t first = std::min(count, Capacity - writeIndex);
    std::memcpy(storage_ + writeIndex, source, first);
    if (count != first) std::memcpy(storage_, source + first, count - first);
    writeSequence_.store(writeSequence + static_cast<std::uint32_t>(count),
                         std::memory_order_release);
    return count;
  }

  std::size_t read(std::uint8_t* destination, std::size_t length) {
    if (storage_ == nullptr || destination == nullptr || length == 0) return 0;
    const std::uint32_t readSequence =
        readSequence_.load(std::memory_order_relaxed);
    const std::uint32_t writeSequence =
        writeSequence_.load(std::memory_order_acquire);
    const std::size_t available =
        static_cast<std::uint32_t>(writeSequence - readSequence);
    const std::size_t count = std::min(length, available);
    const std::size_t readIndex = readSequence & (Capacity - 1U);
    const std::size_t first = std::min(count, Capacity - readIndex);
    std::memcpy(destination, storage_ + readIndex, first);
    if (count != first) {
      std::memcpy(destination + first, storage_, count - first);
    }
    readSequence_.store(readSequence + static_cast<std::uint32_t>(count),
                        std::memory_order_release);
    return count;
  }

  std::size_t size() const {
    const std::uint32_t readSequence =
        readSequence_.load(std::memory_order_acquire);
    const std::uint32_t writeSequence =
        writeSequence_.load(std::memory_order_acquire);
    return static_cast<std::uint32_t>(writeSequence - readSequence);
  }

  constexpr std::size_t capacity() const { return Capacity; }

  // Call only while producer and consumer are stopped.
  void clear() {
    readSequence_.store(0, std::memory_order_relaxed);
    writeSequence_.store(0, std::memory_order_relaxed);
  }

  SpscByteRing() = default;
  SpscByteRing(const SpscByteRing&) = delete;
  SpscByteRing& operator=(const SpscByteRing&) = delete;

 private:
  std::uint8_t* storage_ = nullptr;
  std::atomic<std::uint32_t> readSequence_{0};
  std::atomic<std::uint32_t> writeSequence_{0};
};

}  // namespace open_radio
