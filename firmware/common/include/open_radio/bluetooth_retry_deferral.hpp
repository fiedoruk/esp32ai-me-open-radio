#pragma once

#include <cstdint>

namespace open_radio {

// How long a due Bluetooth retry may wait for local audio (speaker plan B5).
// The radio holds a dial until the local speaker has two blocks and a second of
// decoded audio, so the hand-over does not start on an empty buffer. When the
// stream cannot keep that much, the hold never ends: on 2026-09-26 (cycle 9)
// the radio deferred 344 times over 409 s and did not dial at all until the
// station was changed. With a cap, the retry dials anyway once it has waited
// capMs in one stretch; the media start keeps its own buffer gate. A cap of 0
// keeps the old unbounded wait. A stretch is a run of decisions no more than
// maxGapMs apart (the owner polls every 250 ms while a retry is due): a longer
// silence means the retry stopped being due, so the next one starts afresh
// instead of firing at once on a window left open hours ago.
class BluetoothRetryDeferral {
 public:
  enum class Decision : std::uint8_t { dial, wait, dialAtCap };

  explicit constexpr BluetoothRetryDeferral(std::uint32_t capMs,
                                            std::uint32_t maxGapMs = 5000U)
      : capMs_(capMs), maxGapMs_(maxGapMs) {}

  Decision decide(bool localAudioReady, std::uint32_t nowMs) {
    // Unsigned differences throughout: correct across the 49.7-day millis() wrap.
    if (waiting_ && nowMs - lastDecisionMs_ > maxGapMs_) waiting_ = false;
    lastDecisionMs_ = nowMs;
    if (localAudioReady) {
      waiting_ = false;
      return Decision::dial;
    }
    if (!waiting_) {
      waiting_ = true;
      waitingSinceMs_ = nowMs;
      return Decision::wait;
    }
    if (capMs_ != 0 && nowMs - waitingSinceMs_ >= capMs_) {
      waiting_ = false;  // the next retry gets a fresh window
      return Decision::dialAtCap;
    }
    return Decision::wait;
  }

 private:
  std::uint32_t capMs_;
  std::uint32_t maxGapMs_;
  std::uint32_t waitingSinceMs_ = 0;
  std::uint32_t lastDecisionMs_ = 0;
  bool waiting_ = false;
};

}  // namespace open_radio
