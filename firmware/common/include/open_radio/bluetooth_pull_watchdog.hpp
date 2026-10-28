#pragma once

#include <cstdint>

namespace open_radio {

// The pull callback stamps its time on the Bluetooth core while the owner loop
// reads the clock on the other one. A stamp newer than the loop's clock (the
// callback ran between the two reads) is a fresh pull, not one 49 days old:
// the unsigned age wrapped, tripped the watchdog and tore down a healthy link.
constexpr bool bluetoothPullStalled(std::uint32_t nowMs,
                                    std::uint32_t lastPullMs,
                                    std::uint32_t limitMs) {
  const auto age = static_cast<std::int32_t>(nowMs - lastPullMs);
  return age >= 0 && static_cast<std::uint32_t>(age) >= limitMs;
}

}  // namespace open_radio
