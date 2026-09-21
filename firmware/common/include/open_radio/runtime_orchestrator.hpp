#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "open_radio/firmware_contract.hpp"
#include "runtime_limits.hpp"

namespace open_radio {

enum class RuntimeEventKind : std::uint8_t {
  ConfigMissing,
  ConfigCorrupt,
  ConfigReady,
  WifiConnected,
  WifiLost,
  ResolverReady,
  ResolverUnsupported,
  StreamHealthy,
  StreamStalled,
  BluetoothRemembered,
  BluetoothConnected,
  BluetoothLost,
  LocalOutputAvailable,
  LocalOutputLost,
  PowerInterrupted
};

enum class RuntimeRecoveryReason : std::uint8_t {
  None,
  Wifi,
  Bluetooth,
  Stream,
  Memory
};

enum class RuntimeDiagnosticReason : std::uint8_t {
  ConfigReady,
  OnboardingRequired,
  SafeMode,
  WifiLost,
  WifiRecovered,
  ResolverReady,
  ResolverUnsupported,
  StreamStalled,
  StreamRecovered,
  BluetoothLost,
  BluetoothRecovered,
  LocalOutputLost,
  LocalOutputRecovered,
  PowerInterrupted,
  NetworkRetry,
  StreamRetry,
  BluetoothRetry,
  QueueOverflow,
  TimerOverflow,
  DuplicateEvent,
  StaleEvent
};

struct RuntimeEvent {
  RuntimeEventKind kind = RuntimeEventKind::ConfigMissing;
  std::uint64_t atMs = 0;
  std::uint32_t sequence = 0;
};

struct RuntimeCounters {
  std::uint32_t acceptedEvents = 0;
  std::uint32_t processedEvents = 0;
  std::uint32_t queueOverflows = 0;
  std::uint32_t timerOverflows = 0;
  std::uint32_t duplicateEvents = 0;
  std::uint32_t staleEvents = 0;
  std::uint32_t recoveries = 0;
  std::uint32_t networkRetries = 0;
  std::uint32_t streamRetries = 0;
  std::uint32_t bluetoothRetries = 0;
  std::uint32_t powerInterruptions = 0;
  std::uint32_t maximumQueueDepth = 0;
  std::uint32_t diagnosticOverwrites = 0;
};

struct RuntimeDiagnosticRecord {
  RuntimeDiagnosticReason reason = RuntimeDiagnosticReason::ConfigReady;
  RuntimeState state = RuntimeState::ConfigRequired;
  std::uint64_t atMs = 0;
  std::uint32_t sequence = 0;
};

struct RuntimeOrchestratorSnapshot {
  RuntimeState state = RuntimeState::ConfigRequired;
  OutputKind output = OutputKind::LocalSpeaker;
  RuntimeRecoveryReason recoveryReason = RuntimeRecoveryReason::None;
  bool wifiConnected = false;
  bool bluetoothConnected = false;
  bool localOutputAvailable = true;
  std::uint8_t queueDepth = 0;
  std::uint8_t timerCount = 0;
  std::uint8_t diagnosticsStored = 0;
  RuntimeCounters counters{};
};

struct GoldenRuntimeEvent {
  std::uint16_t minute = 0;
  RuntimeEventKind kind = RuntimeEventKind::ConfigMissing;
  std::uint32_t sequence = 0;
};

struct RuntimeSoakExpected {
  RuntimeState finalState = RuntimeState::ConfigRequired;
  OutputKind finalOutput = OutputKind::LocalSpeaker;
  std::uint32_t recoveries = 0;
  std::uint32_t networkRetries = 0;
  std::uint32_t streamRetries = 0;
  std::uint32_t bluetoothRetries = 0;
  std::uint32_t powerInterruptions = 0;
  std::uint32_t maximumQueueDepth = 0;
  std::uint32_t diagnosticsStored = 0;
  std::uint32_t diagnosticOverwrites = 0;
  std::uint32_t queueOverflows = 0;
  std::uint32_t timerOverflows = 0;
  std::uint64_t stateHash = 0;
};

struct GoldenRuntimeSoakVector {
  const char* scenarioId = nullptr;
  std::uint16_t durationMinutes = 0;
  const GoldenRuntimeEvent* events = nullptr;
  std::size_t eventCount = 0;
  RuntimeSoakExpected expected{};
};

class RuntimeOrchestrator {
 public:
  bool post(const RuntimeEvent& event) {
    if (!validKind(event.kind) || event.sequence == 0) {
      increment(counters_.staleEvents);
      record(RuntimeDiagnosticReason::StaleEvent, nowMs_, event.sequence);
      return false;
    }
    if (event.sequence <= lastAcceptedSequence_) {
      if (event.sequence == lastAcceptedSequence_) {
        increment(counters_.duplicateEvents);
        record(RuntimeDiagnosticReason::DuplicateEvent, nowMs_, event.sequence);
      } else {
        increment(counters_.staleEvents);
        record(RuntimeDiagnosticReason::StaleEvent, nowMs_, event.sequence);
      }
      return false;
    }
    if (event.atMs < nowMs_) {
      increment(counters_.staleEvents);
      record(RuntimeDiagnosticReason::StaleEvent, nowMs_, event.sequence);
      return false;
    }
    if (queueSize_ >= queue_.size()) {
      increment(counters_.queueOverflows);
      record(RuntimeDiagnosticReason::QueueOverflow, nowMs_, event.sequence);
      return false;
    }
    queue_[queueSize_++] = event;
    lastAcceptedSequence_ = event.sequence;
    increment(counters_.acceptedEvents);
    if (queueSize_ > counters_.maximumQueueDepth) {
      counters_.maximumQueueDepth = static_cast<std::uint32_t>(queueSize_);
    }
    return true;
  }

  bool advance(std::uint64_t nowMs) {
    if (nowMs < nowMs_) {
      increment(counters_.staleEvents);
      record(RuntimeDiagnosticReason::StaleEvent, nowMs_, 0);
      return false;
    }
    runDueTimers(nowMs);
    nowMs_ = nowMs;
    RuntimeEvent event;
    while (popDue(nowMs, event)) {
      dispatch(event);
      increment(counters_.processedEvents);
    }
    return true;
  }

  RuntimeOrchestratorSnapshot snapshot() const {
    RuntimeOrchestratorSnapshot result;
    result.state = state_;
    result.output = output_;
    result.recoveryReason = recoveryReason_;
    result.wifiConnected = wifiConnected_;
    result.bluetoothConnected = bluetoothConnected_;
    result.localOutputAvailable = localOutputAvailable_;
    result.queueDepth = static_cast<std::uint8_t>(queueSize_);
    result.timerCount = static_cast<std::uint8_t>(timerCount());
    result.diagnosticsStored = static_cast<std::uint8_t>(diagnosticCount_);
    result.counters = counters_;
    return result;
  }

  const RuntimeDiagnosticRecord& diagnostic(std::size_t index) const {
    const std::size_t oldest = diagnosticCount_ == diagnostics_.size()
                                   ? diagnosticWriteIndex_
                                   : 0;
    return diagnostics_[(oldest + index) % diagnostics_.size()];
  }

 private:
  enum class TimerKind : std::uint8_t { Network, Stream, Bluetooth };

  struct TimerSlot {
    bool active = false;
    TimerKind kind = TimerKind::Network;
    std::uint64_t dueAtMs = 0;
  };

  static bool validKind(RuntimeEventKind kind) {
    return static_cast<std::uint8_t>(kind) <=
           static_cast<std::uint8_t>(RuntimeEventKind::PowerInterrupted);
  }

  static void increment(std::uint32_t& value) {
    if (value < std::numeric_limits<std::uint32_t>::max()) ++value;
  }

  static std::uint64_t saturatingAdd(std::uint64_t value,
                                     std::uint64_t delta) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return delta > maximum - value ? maximum : value + delta;
  }

  bool popDue(std::uint64_t nowMs, RuntimeEvent& destination) {
    std::size_t selected = queueSize_;
    for (std::size_t index = 0; index < queueSize_; ++index) {
      if (queue_[index].atMs > nowMs) continue;
      if (selected == queueSize_ || queue_[index].atMs < queue_[selected].atMs ||
          (queue_[index].atMs == queue_[selected].atMs &&
           queue_[index].sequence < queue_[selected].sequence)) {
        selected = index;
      }
    }
    if (selected == queueSize_) return false;
    destination = queue_[selected];
    for (std::size_t index = selected + 1; index < queueSize_; ++index) {
      queue_[index - 1] = queue_[index];
    }
    --queueSize_;
    return true;
  }

  void dispatch(const RuntimeEvent& event) {
    switch (event.kind) {
      case RuntimeEventKind::ConfigMissing:
        configPresent_ = false;
        configValid_ = false;
        cancelAllTimers();
        record(RuntimeDiagnosticReason::OnboardingRequired, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::ConfigCorrupt:
        configPresent_ = true;
        configValid_ = false;
        cancelAllTimers();
        record(RuntimeDiagnosticReason::SafeMode, event.atMs, event.sequence);
        break;
      case RuntimeEventKind::ConfigReady:
        configPresent_ = true;
        configValid_ = true;
        record(RuntimeDiagnosticReason::ConfigReady, event.atMs, event.sequence);
        break;
      case RuntimeEventKind::WifiConnected:
        wifiConnected_ = true;
        cancelTimer(TimerKind::Network);
        record(RuntimeDiagnosticReason::WifiRecovered, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::WifiLost:
        wifiConnected_ = false;
        streamHealthy_ = false;
        scheduleTimer(TimerKind::Network, event.atMs);
        record(RuntimeDiagnosticReason::WifiLost, event.atMs, event.sequence);
        break;
      case RuntimeEventKind::ResolverReady:
        resolverSupported_ = true;
        record(RuntimeDiagnosticReason::ResolverReady, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::ResolverUnsupported:
        resolverSupported_ = false;
        cancelTimer(TimerKind::Stream);
        record(RuntimeDiagnosticReason::ResolverUnsupported, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::StreamHealthy:
        if (wifiConnected_) streamHealthy_ = true;
        cancelTimer(TimerKind::Stream);
        record(RuntimeDiagnosticReason::StreamRecovered, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::StreamStalled:
        streamHealthy_ = false;
        scheduleTimer(TimerKind::Stream, event.atMs);
        record(RuntimeDiagnosticReason::StreamStalled, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::BluetoothRemembered:
        bluetoothRemembered_ = true;
        break;
      case RuntimeEventKind::BluetoothConnected:
        bluetoothRemembered_ = true;
        bluetoothConnected_ = true;
        cancelTimer(TimerKind::Bluetooth);
        record(RuntimeDiagnosticReason::BluetoothRecovered, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::BluetoothLost:
        bluetoothConnected_ = false;
        if (bluetoothRemembered_) {
          scheduleTimer(TimerKind::Bluetooth, event.atMs);
        }
        record(RuntimeDiagnosticReason::BluetoothLost, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::LocalOutputAvailable:
        localOutputAvailable_ = true;
        record(RuntimeDiagnosticReason::LocalOutputRecovered, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::LocalOutputLost:
        localOutputAvailable_ = false;
        record(RuntimeDiagnosticReason::LocalOutputLost, event.atMs,
               event.sequence);
        break;
      case RuntimeEventKind::PowerInterrupted:
        wifiConnected_ = false;
        streamHealthy_ = false;
        bluetoothConnected_ = false;
        increment(counters_.powerInterruptions);
        scheduleTimer(TimerKind::Network, event.atMs);
        record(RuntimeDiagnosticReason::PowerInterrupted, event.atMs,
               event.sequence);
        break;
    }
    recomputeState();
  }

  void recomputeState() {
    const RuntimeState previous = state_;
    if (!configPresent_) {
      state_ = RuntimeState::ConfigRequired;
      recoveryReason_ = RuntimeRecoveryReason::None;
    } else if (!configValid_) {
      state_ = RuntimeState::SafeMode;
      recoveryReason_ = RuntimeRecoveryReason::Memory;
    } else if (!wifiConnected_) {
      state_ = RuntimeState::Recovering;
      recoveryReason_ = RuntimeRecoveryReason::Wifi;
    } else if (!resolverSupported_) {
      state_ = RuntimeState::UnsupportedStation;
      recoveryReason_ = RuntimeRecoveryReason::Stream;
    } else if (!streamHealthy_) {
      state_ = RuntimeState::Recovering;
      recoveryReason_ = RuntimeRecoveryReason::Stream;
    } else if (bluetoothConnected_) {
      state_ = RuntimeState::Playing;
      recoveryReason_ = RuntimeRecoveryReason::None;
    } else if (localOutputAvailable_) {
      state_ = bluetoothRemembered_ ? RuntimeState::DegradedPlaying
                                    : RuntimeState::Playing;
      recoveryReason_ = bluetoothRemembered_
                            ? RuntimeRecoveryReason::Bluetooth
                            : RuntimeRecoveryReason::None;
    } else {
      state_ = RuntimeState::Recovering;
      recoveryReason_ = RuntimeRecoveryReason::Bluetooth;
    }
    output_ = state_ != RuntimeState::SafeMode &&
                      state_ != RuntimeState::ConfigRequired &&
                      bluetoothConnected_
                  ? OutputKind::Bluetooth
                  : OutputKind::LocalSpeaker;
    const bool playable = state_ == RuntimeState::Playing ||
                          state_ == RuntimeState::DegradedPlaying;
    if (playable && !firstPlaybackReached_) {
      firstPlaybackReached_ = true;
    } else if (playable && previous != RuntimeState::Playing &&
               previous != RuntimeState::DegradedPlaying) {
      increment(counters_.recoveries);
    }
  }

  void scheduleTimer(TimerKind kind, std::uint64_t atMs) {
    for (const auto& timer : timers_) {
      if (timer.active && timer.kind == kind) return;
    }
    for (auto& timer : timers_) {
      if (timer.active) continue;
      const std::uint8_t attempt = retryAttempts_[timerIndex(kind)];
      constexpr std::array<std::uint64_t, 4> delays{{5000, 30000, 120000,
                                                     600000}};
      std::uint64_t delay = delays[attempt < delays.size()
                                       ? attempt
                                       : delays.size() - 1];
      if (delay > generated::kRuntimeMaximumRetryDelayMs) {
        delay = generated::kRuntimeMaximumRetryDelayMs;
      }
      timer = {true, kind, saturatingAdd(atMs, delay)};
      return;
    }
    increment(counters_.timerOverflows);
    record(RuntimeDiagnosticReason::TimerOverflow, atMs, 0);
  }

  void cancelTimer(TimerKind kind) {
    for (auto& timer : timers_) {
      if (timer.active && timer.kind == kind) timer.active = false;
    }
    retryAttempts_[timerIndex(kind)] = 0;
  }

  void cancelAllTimers() {
    for (auto& timer : timers_) timer.active = false;
    retryAttempts_.fill(0);
  }

  void runDueTimers(std::uint64_t nowMs) {
    for (auto& timer : timers_) {
      if (!timer.active || timer.dueAtMs > nowMs) continue;
      const TimerKind kind = timer.kind;
      timer.active = false;
      auto& attempt = retryAttempts_[timerIndex(kind)];
      if (attempt < 3) ++attempt;
      bool stillRequired = false;
      if (kind == TimerKind::Network) {
        increment(counters_.networkRetries);
        stillRequired = !wifiConnected_;
        record(RuntimeDiagnosticReason::NetworkRetry, nowMs, 0);
      } else if (kind == TimerKind::Stream) {
        increment(counters_.streamRetries);
        stillRequired = !streamHealthy_ && wifiConnected_;
        record(RuntimeDiagnosticReason::StreamRetry, nowMs, 0);
      } else {
        increment(counters_.bluetoothRetries);
        stillRequired = !bluetoothConnected_ && bluetoothRemembered_;
        record(RuntimeDiagnosticReason::BluetoothRetry, nowMs, 0);
      }
      if (stillRequired) scheduleTimer(kind, nowMs);
    }
  }

  static std::size_t timerIndex(TimerKind kind) {
    return static_cast<std::size_t>(kind);
  }

  std::size_t timerCount() const {
    std::size_t count = 0;
    for (const auto& timer : timers_) {
      if (timer.active) ++count;
    }
    return count;
  }

  void record(RuntimeDiagnosticReason reason, std::uint64_t atMs,
              std::uint32_t sequence) {
    diagnostics_[diagnosticWriteIndex_] = {reason, state_, atMs, sequence};
    diagnosticWriteIndex_ = (diagnosticWriteIndex_ + 1) % diagnostics_.size();
    if (diagnosticCount_ < diagnostics_.size()) {
      ++diagnosticCount_;
    } else {
      increment(counters_.diagnosticOverwrites);
    }
  }

  std::array<RuntimeEvent, generated::kRuntimeEventQueueCapacity> queue_{};
  std::size_t queueSize_ = 0;
  std::array<TimerSlot, generated::kRuntimeTimerCapacity> timers_{};
  std::array<std::uint8_t, 3> retryAttempts_{};
  std::array<RuntimeDiagnosticRecord,
             generated::kRuntimeDiagnosticCapacity> diagnostics_{};
  std::size_t diagnosticWriteIndex_ = 0;
  std::size_t diagnosticCount_ = 0;
  std::uint64_t nowMs_ = 0;
  std::uint32_t lastAcceptedSequence_ = 0;
  RuntimeState state_ = RuntimeState::ConfigRequired;
  OutputKind output_ = OutputKind::LocalSpeaker;
  RuntimeRecoveryReason recoveryReason_ = RuntimeRecoveryReason::None;
  RuntimeCounters counters_{};
  bool configPresent_ = false;
  bool configValid_ = false;
  bool wifiConnected_ = false;
  bool resolverSupported_ = true;
  bool streamHealthy_ = false;
  bool bluetoothRemembered_ = false;
  bool bluetoothConnected_ = false;
  bool localOutputAvailable_ = true;
  bool firstPlaybackReached_ = false;
};

}
