#pragma once

#include <cstdint>

namespace open_radio {

// A due Bluetooth request prefers healthy audio with a local reserve, but
// cannot wait for it forever (2026-09-26: 409 s; 2026-10-26: boot > 2 min).
// The owner calls decide even when the source is unhealthy or logos are busy,
// and resets explicitly when the request ends. A slow loop is not cancellation.
// At the cap, cancel logo work cooperatively and keep the expired window until
// its TLS buffers and task stack have been released. Never overlap them with
// stack startup. Media admission and the stack memory guard remain separate.
// capMs == 0 preserves the uncapped audio/logo wait.
class BluetoothRetryDeferral {
 public:
  enum class Decision : std::uint8_t { dial, wait, dialAtCap, abortLogoFetch };

  explicit constexpr BluetoothRetryDeferral(std::uint32_t capMs)
      : capMs_(capMs) {}

  void reset() { waiting_ = false; }

  Decision decide(bool localAudioReady, std::uint32_t nowMs,
                  bool logoFetchPending = false) {
    // Unsigned differences throughout: correct across the 49.7-day millis()
    // wrap.
    if (localAudioReady && !logoFetchPending) {
      waiting_ = false;
      return Decision::dial;
    }
    if (!waiting_) {
      waiting_ = true;
      waitingSinceMs_ = nowMs;
      return Decision::wait;
    }
    if (capMs_ != 0 && nowMs - waitingSinceMs_ >= capMs_) {
      if (logoFetchPending) return Decision::abortLogoFetch;
      waiting_ = false;  // the next retry gets a fresh window
      return Decision::dialAtCap;
    }
    return Decision::wait;
  }

 private:
  std::uint32_t capMs_;
  std::uint32_t waitingSinceMs_ = 0;
  bool waiting_ = false;
};

}  // namespace open_radio
