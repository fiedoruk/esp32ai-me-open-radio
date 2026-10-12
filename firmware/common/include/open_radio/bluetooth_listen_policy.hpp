#pragma once

#include <cstdint>

namespace open_radio {

// Who may open the Bluetooth link while the radio recovers a remembered
// speaker. Measured 2026-09-09 and 2026-09-21: links the speaker opened stalled
// 37 times out of 59, links the radio opened stalled 0 times out of 5. During
// recovery the radio dials and stops accepting the speaker's own connection,
// except in one idle gap out of every `listenEvery`, so a speaker that never
// answers a page still gets a door.
inline constexpr std::uint8_t kBluetoothRecoveryListenEvery = 4;

constexpr bool bluetoothRecoveryConnectable(
    bool peerEnabled, bool recovering, std::uint8_t recoveryAttempt,
    std::uint8_t listenEvery = kBluetoothRecoveryListenEvery) {
  if (!peerEnabled) return false;
  if (!recovering) return true;
  if (listenEvery == 0) return false;
  return recoveryAttempt % listenEvery == listenEvery - 1;
}

}  // namespace open_radio
