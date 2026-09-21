import {RuntimeEvents} from "./orchestrator.mjs";

export const RuntimeProducers = Object.freeze({
  STORAGE: "STORAGE",
  WIFI: "WIFI",
  RESOLVER: "RESOLVER",
  STREAM: "STREAM",
  BLUETOOTH: "BLUETOOTH",
  LOCAL_OUTPUT: "LOCAL_OUTPUT",
  POWER: "POWER"
});

const producerEvents = new Map([
  [RuntimeProducers.STORAGE, new Set([RuntimeEvents.CONFIG_MISSING, RuntimeEvents.CONFIG_CORRUPT, RuntimeEvents.CONFIG_READY])],
  [RuntimeProducers.WIFI, new Set([RuntimeEvents.WIFI_CONNECTED, RuntimeEvents.WIFI_LOST])],
  [RuntimeProducers.RESOLVER, new Set([RuntimeEvents.RESOLVER_READY, RuntimeEvents.RESOLVER_UNSUPPORTED, RuntimeEvents.STREAM_STALLED])],
  [RuntimeProducers.STREAM, new Set([RuntimeEvents.STREAM_HEALTHY, RuntimeEvents.STREAM_STALLED])],
  [RuntimeProducers.BLUETOOTH, new Set([RuntimeEvents.BLUETOOTH_REMEMBERED, RuntimeEvents.BLUETOOTH_CONNECTED, RuntimeEvents.BLUETOOTH_LOST])],
  [RuntimeProducers.LOCAL_OUTPUT, new Set([RuntimeEvents.LOCAL_OUTPUT_AVAILABLE, RuntimeEvents.LOCAL_OUTPUT_LOST])],
  [RuntimeProducers.POWER, new Set([RuntimeEvents.POWER_INTERRUPTED])]
]);

const maximumUint32 = 0xffffffff;
const tickModulus = 0x100000000;
const rolloverThreshold = 0x80000000;

function saturatingIncrement(value) {
  return value >= maximumUint32 ? maximumUint32 : value + 1;
}

export class MonotonicTick32 {
  constructor() {
    this.initialized = false;
    this.lastRawTick = 0;
    this.extendedTick = 0;
  }

  normalize(rawTick) {
    if (!Number.isInteger(rawTick) || rawTick < 0 || rawTick > maximumUint32) {
      return Object.freeze({accepted: false, value: this.extendedTick, rolledOver: false});
    }
    if (!this.initialized) {
      this.initialized = true;
      this.lastRawTick = rawTick;
      this.extendedTick = rawTick;
      return Object.freeze({accepted: true, value: this.extendedTick, rolledOver: false});
    }
    if (rawTick >= this.lastRawTick) {
      this.extendedTick += rawTick - this.lastRawTick;
      this.lastRawTick = rawTick;
      return Object.freeze({accepted: true, value: this.extendedTick, rolledOver: false});
    }
    if (this.lastRawTick - rawTick <= rolloverThreshold) {
      return Object.freeze({accepted: false, value: this.extendedTick, rolledOver: false});
    }
    this.extendedTick += tickModulus - this.lastRawTick + rawTick;
    this.lastRawTick = rawTick;
    return Object.freeze({accepted: true, value: this.extendedTick, rolledOver: true});
  }
}

export class RuntimeIngress {
  constructor({limits, orchestrator}) {
    this.limits = Object.freeze({...limits});
    this.orchestrator = orchestrator;
    this.mailbox = [];
    this.producerState = new Map();
    this.clock = new MonotonicTick32();
    this.nextRuntimeSequence = 1;
    this.counters = {
      acceptedFacts: 0,
      deliveredFacts: 0,
      mailboxOverflows: 0,
      invalidFacts: 0,
      duplicateFacts: 0,
      staleFacts: 0,
      staleEpochs: 0,
      backwardTicks: 0,
      rollovers: 0,
      contentionDrops: 0,
      ownerDeferrals: 0,
      deliveryRejected: 0,
      maximumMailboxDepth: 0
    };
  }

  post(fact) {
    const allowed = producerEvents.get(fact?.producer);
    if (!allowed?.has(fact?.type) || !Number.isInteger(fact?.epoch) || fact.epoch <= 0 ||
        !Number.isInteger(fact?.producerSequence) || fact.producerSequence <= 0 ||
        !Number.isInteger(fact?.rawTick) || fact.rawTick < 0 || fact.rawTick > maximumUint32) {
      this.counters.invalidFacts = saturatingIncrement(this.counters.invalidFacts);
      return false;
    }
    const current = this.producerState.get(fact.producer);
    if (current && fact.epoch < current.epoch) {
      this.counters.staleEpochs = saturatingIncrement(this.counters.staleEpochs);
      return false;
    }
    if (current && fact.epoch === current.epoch && fact.producerSequence <= current.sequence) {
      const key = fact.producerSequence === current.sequence ? "duplicateFacts" : "staleFacts";
      this.counters[key] = saturatingIncrement(this.counters[key]);
      return false;
    }
    if (this.mailbox.length >= this.limits.mailboxCapacity) {
      this.counters.mailboxOverflows = saturatingIncrement(this.counters.mailboxOverflows);
      return false;
    }
    this.mailbox.push(Object.freeze({...fact}));
    this.producerState.set(fact.producer, {epoch: fact.epoch, sequence: fact.producerSequence});
    this.counters.acceptedFacts = saturatingIncrement(this.counters.acceptedFacts);
    this.counters.maximumMailboxDepth = Math.max(this.counters.maximumMailboxDepth, this.mailbox.length);
    return true;
  }

  drain(maximum = this.limits.maximumDrainPerPass) {
    let drained = 0;
    while (this.mailbox.length > 0 && drained < maximum) {
      const fact = this.mailbox.shift();
      const normalized = this.clock.normalize(fact.rawTick);
      if (!normalized.accepted) {
        this.counters.backwardTicks = saturatingIncrement(this.counters.backwardTicks);
        ++drained;
        continue;
      }
      if (normalized.rolledOver) this.counters.rollovers = saturatingIncrement(this.counters.rollovers);
      const sequence = this.nextRuntimeSequence;
      if (this.nextRuntimeSequence < maximumUint32) ++this.nextRuntimeSequence;
      if (!this.orchestrator.post({type: fact.type, atMs: normalized.value, sequence}) ||
          !this.orchestrator.advance(normalized.value)) {
        this.counters.deliveryRejected = saturatingIncrement(this.counters.deliveryRejected);
      } else {
        this.counters.deliveredFacts = saturatingIncrement(this.counters.deliveredFacts);
      }
      ++drained;
    }
    return drained;
  }

  advanceOwnerClock(rawTick) {
    const normalized = this.clock.normalize(rawTick);
    if (!normalized.accepted) {
      this.counters.backwardTicks = saturatingIncrement(this.counters.backwardTicks);
      return false;
    }
    if (normalized.rolledOver) this.counters.rollovers = saturatingIncrement(this.counters.rollovers);
    return this.orchestrator.advance(normalized.value);
  }

  snapshot() {
    return Object.freeze({
      mailboxDepth: this.mailbox.length,
      clockTick: this.clock.extendedTick,
      resourceProbes: Object.freeze({
        internalHeapFreeBytes: "NOT_MEASURED",
        ownerTaskStackHeadroomBytes: "NOT_MEASURED",
        maximumCallbackLatencyUs: "NOT_MEASURED",
        audioUnderruns: "NOT_MEASURED"
      }),
      counters: Object.freeze({...this.counters})
    });
  }
}
