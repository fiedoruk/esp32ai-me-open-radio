#pragma once

namespace open_radio {

// Connected PCM prefill needs the existing stream watchdog and reopen path.
// Ready queues can legitimately stop production while media admission waits.
constexpr bool bluetoothStreamRecoveryAllowed(bool consoleActive,
                                             bool connectInFlight,
                                             bool mediaStarted,
                                             bool waitingForPcm) {
  return !consoleActive &&
         (!connectInFlight || mediaStarted || waitingForPcm);
}

// StationRuntime owns liveness (new PCM or drain within its existing 8 s).
// healthy() needs a drain and can still be false during first prefill.
// Historical starvation totals cannot establish current stream liveness.
constexpr bool bluetoothPrefillMayWait(bool radioRunning, bool waitingForPcm,
                                      bool recoveryAllowed) {
  return radioRunning && waitingForPcm && recoveryAllowed;
}

}  // namespace open_radio
