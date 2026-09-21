#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "open_radio/runtime_orchestrator.hpp"
#include "runtime_ingress_limits.hpp"

namespace open_radio {

enum class RuntimeProducer : std::uint8_t {
  Storage,
  Wifi,
  Resolver,
  Stream,
  Bluetooth,
  LocalOutput,
  Power
};

enum class RuntimeMeasurementStatus : std::uint8_t { NotMeasured, Measured };

struct RuntimeFact {
  RuntimeProducer producer = RuntimeProducer::Storage;
  std::uint32_t epoch = 0;
  std::uint32_t producerSequence = 0;
  std::uint32_t rawTick = 0;
  RuntimeEventKind kind = RuntimeEventKind::ConfigMissing;
};

struct RuntimeIngressCounters {
  std::uint32_t acceptedFacts = 0;
  std::uint32_t deliveredFacts = 0;
  std::uint32_t mailboxOverflows = 0;
  std::uint32_t invalidFacts = 0;
  std::uint32_t duplicateFacts = 0;
  std::uint32_t staleFacts = 0;
  std::uint32_t staleEpochs = 0;
  std::uint32_t backwardTicks = 0;
  std::uint32_t rollovers = 0;
  std::uint32_t contentionDrops = 0;
  std::uint32_t ownerDeferrals = 0;
  std::uint32_t deliveryRejected = 0;
  std::uint32_t maximumMailboxDepth = 0;
};

struct RuntimeResourceProbes {
  RuntimeMeasurementStatus internalHeapFreeBytes =
      RuntimeMeasurementStatus::NotMeasured;
  RuntimeMeasurementStatus ownerTaskStackHeadroomBytes =
      RuntimeMeasurementStatus::NotMeasured;
  RuntimeMeasurementStatus maximumCallbackLatencyUs =
      RuntimeMeasurementStatus::NotMeasured;
  RuntimeMeasurementStatus audioUnderruns = RuntimeMeasurementStatus::NotMeasured;
};

struct RuntimeIngressSnapshot {
  std::uint32_t mailboxDepth = 0;
  std::uint64_t clockTick = 0;
  RuntimeIngressCounters counters{};
  RuntimeResourceProbes resourceProbes{};
};

struct TickNormalization {
  bool accepted = false;
  bool rolledOver = false;
  std::uint64_t value = 0;
};

class MonotonicTick32 {
 public:
  TickNormalization normalize(std::uint32_t rawTick) {
    if (!initialized_) {
      initialized_ = true;
      lastRawTick_ = rawTick;
      extendedTick_ = rawTick;
      return {true, false, extendedTick_};
    }
    if (rawTick >= lastRawTick_) {
      extendedTick_ += static_cast<std::uint64_t>(rawTick - lastRawTick_);
      lastRawTick_ = rawTick;
      return {true, false, extendedTick_};
    }
    constexpr std::uint32_t kRolloverThreshold = 0x80000000U;
    if (lastRawTick_ - rawTick <= kRolloverThreshold) {
      return {false, false, extendedTick_};
    }
    constexpr std::uint64_t kTickModulus = 0x100000000ULL;
    extendedTick_ += kTickModulus - lastRawTick_ + rawTick;
    lastRawTick_ = rawTick;
    return {true, true, extendedTick_};
  }

  std::uint64_t value() const { return extendedTick_; }

 private:
  bool initialized_ = false;
  std::uint32_t lastRawTick_ = 0;
  std::uint64_t extendedTick_ = 0;
};

struct GoldenIngressFact {
  RuntimeProducer producer = RuntimeProducer::Storage;
  std::uint32_t epoch = 0;
  std::uint32_t producerSequence = 0;
  std::uint32_t rawTick = 0;
  RuntimeEventKind kind = RuntimeEventKind::ConfigMissing;
};

struct IngressTraceExpected {
  RuntimeState finalState = RuntimeState::ConfigRequired;
  OutputKind finalOutput = OutputKind::LocalSpeaker;
  std::uint32_t acceptedFacts = 0;
  std::uint32_t deliveredFacts = 0;
  std::uint32_t mailboxOverflows = 0;
  std::uint32_t invalidFacts = 0;
  std::uint32_t duplicateFacts = 0;
  std::uint32_t staleFacts = 0;
  std::uint32_t staleEpochs = 0;
  std::uint32_t backwardTicks = 0;
  std::uint32_t rollovers = 0;
  std::uint32_t maximumMailboxDepth = 0;
  std::uint32_t deliveryRejected = 0;
  std::uint32_t runtimeProcessedEvents = 0;
  std::uint64_t stateHash = 0;
};

struct GoldenIngressTrace {
  const char* scenarioId = nullptr;
  std::uint32_t seed = 0;
  const char* convergenceGroup = nullptr;
  const GoldenIngressFact* facts = nullptr;
  std::size_t factCount = 0;
  IngressTraceExpected expected{};
};

class RuntimeIngress {
 public:
  explicit RuntimeIngress(RuntimeOrchestrator& orchestrator)
      : orchestrator_(orchestrator) {}

  bool post(const RuntimeFact& fact) {
    TryLock lock(mailboxLock_);
    if (!lock.acquired()) {
      increment(contentionDrops_);
      return false;
    }
    if (!validMapping(fact) || fact.epoch == 0 || fact.producerSequence == 0) {
      increment(invalidFacts_);
      return false;
    }
    auto& state = producerStates_[producerIndex(fact.producer)];
    if (state.initialized && fact.epoch < state.epoch) {
      increment(staleEpochs_);
      return false;
    }
    if (state.initialized && fact.epoch == state.epoch &&
        fact.producerSequence <= state.sequence) {
      if (fact.producerSequence == state.sequence) {
        increment(duplicateFacts_);
      } else {
        increment(staleFacts_);
      }
      return false;
    }
    if (queueSize_ >= mailbox_.size()) {
      increment(mailboxOverflows_);
      return false;
    }
    mailbox_[writeIndex_] = fact;
    writeIndex_ = (writeIndex_ + 1) % mailbox_.size();
    ++queueSize_;
    queueDepth_.store(static_cast<std::uint32_t>(queueSize_),
                      std::memory_order_release);
    state = {true, fact.epoch, fact.producerSequence};
    increment(acceptedFacts_);
    updateMaximum(maximumMailboxDepth_, static_cast<std::uint32_t>(queueSize_));
    return true;
  }

  std::size_t drain(
      std::size_t maximum = generated::kRuntimeIngressMaximumDrainPerPass) {
    std::size_t drained = 0;
    RuntimeFact fact;
    while (drained < maximum) {
      const PopResult result = pop(fact);
      if (result != PopResult::Popped) break;
      const TickNormalization normalized = clock_.normalize(fact.rawTick);
      if (!normalized.accepted) {
        increment(backwardTicks_);
        ++drained;
        continue;
      }
      if (normalized.rolledOver) increment(rollovers_);
      const std::uint32_t sequence = nextRuntimeSequence_;
      if (nextRuntimeSequence_ < std::numeric_limits<std::uint32_t>::max()) {
        ++nextRuntimeSequence_;
      }
      if (!orchestrator_.post({fact.kind, normalized.value, sequence}) ||
          !orchestrator_.advance(normalized.value)) {
        increment(deliveryRejected_);
      } else {
        increment(deliveredFacts_);
      }
      ++drained;
    }
    return drained;
  }

  bool advanceOwnerClock(std::uint32_t rawTick) {
    const TickNormalization normalized = clock_.normalize(rawTick);
    if (!normalized.accepted) {
      increment(backwardTicks_);
      return false;
    }
    if (normalized.rolledOver) increment(rollovers_);
    return orchestrator_.advance(normalized.value);
  }

  RuntimeIngressSnapshot snapshot() const {
    RuntimeIngressSnapshot result;
    result.mailboxDepth = queueDepth_.load(std::memory_order_acquire);
    result.clockTick = clock_.value();
    result.counters = {
        acceptedFacts_.load(std::memory_order_relaxed),
        deliveredFacts_.load(std::memory_order_relaxed),
        mailboxOverflows_.load(std::memory_order_relaxed),
        invalidFacts_.load(std::memory_order_relaxed),
        duplicateFacts_.load(std::memory_order_relaxed),
        staleFacts_.load(std::memory_order_relaxed),
        staleEpochs_.load(std::memory_order_relaxed),
        backwardTicks_.load(std::memory_order_relaxed),
        rollovers_.load(std::memory_order_relaxed),
        contentionDrops_.load(std::memory_order_relaxed),
        ownerDeferrals_.load(std::memory_order_relaxed),
        deliveryRejected_.load(std::memory_order_relaxed),
        maximumMailboxDepth_.load(std::memory_order_relaxed)};
    return result;
  }

 private:
  class TryLock {
   public:
    explicit TryLock(std::atomic_flag& flag)
        : flag_(flag), acquired_(!flag_.test_and_set(std::memory_order_acquire)) {}
    ~TryLock() {
      if (acquired_) flag_.clear(std::memory_order_release);
    }
    bool acquired() const { return acquired_; }

   private:
    std::atomic_flag& flag_;
    bool acquired_;
  };

  struct ProducerState {
    bool initialized = false;
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0;
  };

  enum class PopResult : std::uint8_t { Empty, Deferred, Popped };

  static void increment(std::atomic<std::uint32_t>& value) {
    auto current = value.load(std::memory_order_relaxed);
    while (current < std::numeric_limits<std::uint32_t>::max() &&
           !value.compare_exchange_weak(current, current + 1,
                                        std::memory_order_relaxed)) {
    }
  }

  static void updateMaximum(std::atomic<std::uint32_t>& value,
                            std::uint32_t candidate) {
    auto current = value.load(std::memory_order_relaxed);
    while (current < candidate &&
           !value.compare_exchange_weak(current, candidate,
                                        std::memory_order_relaxed)) {
    }
  }

  static std::size_t producerIndex(RuntimeProducer producer) {
    return static_cast<std::size_t>(producer);
  }

  static bool validMapping(const RuntimeFact& fact) {
    if (producerIndex(fact.producer) >= generated::kRuntimeIngressProducerCount) {
      return false;
    }
    switch (fact.producer) {
      case RuntimeProducer::Storage:
        return fact.kind == RuntimeEventKind::ConfigMissing ||
               fact.kind == RuntimeEventKind::ConfigCorrupt ||
               fact.kind == RuntimeEventKind::ConfigReady;
      case RuntimeProducer::Wifi:
        return fact.kind == RuntimeEventKind::WifiConnected ||
               fact.kind == RuntimeEventKind::WifiLost;
      case RuntimeProducer::Resolver:
        return fact.kind == RuntimeEventKind::ResolverReady ||
               fact.kind == RuntimeEventKind::ResolverUnsupported ||
               fact.kind == RuntimeEventKind::StreamStalled;
      case RuntimeProducer::Stream:
        return fact.kind == RuntimeEventKind::StreamHealthy ||
               fact.kind == RuntimeEventKind::StreamStalled;
      case RuntimeProducer::Bluetooth:
        return fact.kind == RuntimeEventKind::BluetoothRemembered ||
               fact.kind == RuntimeEventKind::BluetoothConnected ||
               fact.kind == RuntimeEventKind::BluetoothLost;
      case RuntimeProducer::LocalOutput:
        return fact.kind == RuntimeEventKind::LocalOutputAvailable ||
               fact.kind == RuntimeEventKind::LocalOutputLost;
      case RuntimeProducer::Power:
        return fact.kind == RuntimeEventKind::PowerInterrupted;
    }
    return false;
  }

  PopResult pop(RuntimeFact& destination) {
    TryLock lock(mailboxLock_);
    if (!lock.acquired()) {
      increment(ownerDeferrals_);
      return PopResult::Deferred;
    }
    if (queueSize_ == 0) return PopResult::Empty;
    destination = mailbox_[readIndex_];
    readIndex_ = (readIndex_ + 1) % mailbox_.size();
    --queueSize_;
    queueDepth_.store(static_cast<std::uint32_t>(queueSize_),
                      std::memory_order_release);
    return PopResult::Popped;
  }

  RuntimeOrchestrator& orchestrator_;
  std::array<RuntimeFact, generated::kRuntimeIngressMailboxCapacity> mailbox_{};
  std::array<ProducerState, generated::kRuntimeIngressProducerCount>
      producerStates_{};
  mutable std::atomic_flag mailboxLock_ = ATOMIC_FLAG_INIT;
  std::size_t readIndex_ = 0;
  std::size_t writeIndex_ = 0;
  std::size_t queueSize_ = 0;
  std::atomic<std::uint32_t> queueDepth_{0};
  MonotonicTick32 clock_{};
  std::uint32_t nextRuntimeSequence_ = 1;
  std::atomic<std::uint32_t> acceptedFacts_{0};
  std::atomic<std::uint32_t> deliveredFacts_{0};
  std::atomic<std::uint32_t> mailboxOverflows_{0};
  std::atomic<std::uint32_t> invalidFacts_{0};
  std::atomic<std::uint32_t> duplicateFacts_{0};
  std::atomic<std::uint32_t> staleFacts_{0};
  std::atomic<std::uint32_t> staleEpochs_{0};
  std::atomic<std::uint32_t> backwardTicks_{0};
  std::atomic<std::uint32_t> rollovers_{0};
  std::atomic<std::uint32_t> contentionDrops_{0};
  std::atomic<std::uint32_t> ownerDeferrals_{0};
  std::atomic<std::uint32_t> deliveryRejected_{0};
  std::atomic<std::uint32_t> maximumMailboxDepth_{0};
};

}
