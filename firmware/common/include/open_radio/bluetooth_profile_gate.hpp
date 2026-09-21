#pragma once

#include <atomic>
#include <cstdint>

namespace open_radio {

enum class BluetoothProfileOpenReason : std::uint8_t {
  None,
  BootRemembered,
  Discovery,
  Recovery,
  InboundAcl
};

struct BluetoothProfileOpenDecision {
  bool allowed = false;
  std::uint32_t generation = 0;
  BluetoothProfileOpenReason reason = BluetoothProfileOpenReason::None;
};

struct BluetoothProfileEvent {
  std::uint8_t state = 0;
  std::uint32_t generation = 0;
};

constexpr std::uint32_t kBluetoothProfileGenerationMask = 0x1fffffffU;

constexpr std::uint32_t packBluetoothProfileEvent(std::uint8_t state,
                                                  std::uint32_t generation) {
  return ((static_cast<std::uint32_t>(state) + 1U) << 29U) |
         (generation & kBluetoothProfileGenerationMask);
}

constexpr BluetoothProfileEvent unpackBluetoothProfileEvent(
    std::uint32_t event) {
  return {static_cast<std::uint8_t>((event >> 29U) - 1U),
          event & kBluetoothProfileGenerationMask};
}

// The dependency ultimately funnels every local A2DP Source profile open
// through its virtual esp_a2d_connect() boundary, including the discovery path
// that bypasses connect_to(). Keep that boundary single-flight even when
// discovery, the owner retry loop and an inbound ACL wake-up converge on the
// same peer.
// Authorization is one-shot; terminal callbacks may clear only the generation
// they observed, so a stale completion cannot release a newer attempt.
class BluetoothProfileOpenGate {
 public:
  void authorize(BluetoothProfileOpenReason reason) {
    pendingReason_.store(static_cast<std::uint8_t>(reason),
                         std::memory_order_relaxed);
    authorized_.store(true, std::memory_order_release);
  }

  void cancelAuthorization() {
    authorized_.store(false, std::memory_order_release);
    pendingReason_.store(
        static_cast<std::uint8_t>(BluetoothProfileOpenReason::None),
        std::memory_order_relaxed);
  }

  BluetoothProfileOpenDecision begin() {
    if (!authorized_.exchange(false, std::memory_order_acq_rel)) return {};
    const auto reason = static_cast<BluetoothProfileOpenReason>(
        pendingReason_.exchange(
            static_cast<std::uint8_t>(BluetoothProfileOpenReason::None),
            std::memory_order_acq_rel));
    if (reason == BluetoothProfileOpenReason::None) return {};

    std::uint32_t expected = 0;
    std::uint32_t generation =
        nextGeneration_.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (generation == 0 || generation > kBluetoothProfileGenerationMask) {
      nextGeneration_.store(1, std::memory_order_relaxed);
      generation = 1;
    }
    if (!activeGeneration_.compare_exchange_strong(
            expected, generation, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return {};
    }
    return {true, generation, reason};
  }

  // A connection callback does not carry the generation of the local open
  // that produced it. Bind a generation only after this attempt has emitted
  // CONNECTING/CONNECTED. A delayed terminal callback from an older attempt
  // therefore cannot claim a newly-authorized generation that has not made
  // observable progress yet.
  std::uint32_t observeProgress() {
    const auto generation = activeGeneration();
    observedGeneration_.store(generation, std::memory_order_release);
    return generation;
  }

  std::uint32_t observedGeneration() const {
    return observedGeneration_.load(std::memory_order_acquire);
  }

  bool finish(std::uint32_t generation) {
    if (generation == 0) return false;
    std::uint32_t expected = generation;
    if (!activeGeneration_.compare_exchange_strong(
            expected, 0, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return false;
    }
    observedGeneration_.compare_exchange_strong(
        generation, 0, std::memory_order_acq_rel, std::memory_order_acquire);
    return true;
  }

  std::uint32_t activeGeneration() const {
    return activeGeneration_.load(std::memory_order_acquire);
  }

  bool openInFlight() const { return activeGeneration() != 0; }

 private:
  std::atomic<bool> authorized_{false};
  std::atomic<std::uint8_t> pendingReason_{
      static_cast<std::uint8_t>(BluetoothProfileOpenReason::None)};
  std::atomic<std::uint32_t> nextGeneration_{0};
  std::atomic<std::uint32_t> activeGeneration_{0};
  std::atomic<std::uint32_t> observedGeneration_{0};
};

}  // namespace open_radio
