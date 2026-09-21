export const RuntimeStates = Object.freeze({
  CONFIG_REQUIRED: "CONFIG_REQUIRED",
  RECOVERING: "RECOVERING",
  PLAYING: "PLAYING",
  DEGRADED_PLAYING: "DEGRADED_PLAYING",
  UNSUPPORTED_STATION: "UNSUPPORTED_STATION",
  SAFE_MODE: "SAFE_MODE"
});

export const RuntimeEvents = Object.freeze({
  CONFIG_MISSING: "CONFIG_MISSING",
  CONFIG_CORRUPT: "CONFIG_CORRUPT",
  CONFIG_READY: "CONFIG_READY",
  WIFI_CONNECTED: "WIFI_CONNECTED",
  WIFI_LOST: "WIFI_LOST",
  RESOLVER_READY: "RESOLVER_READY",
  RESOLVER_UNSUPPORTED: "RESOLVER_UNSUPPORTED",
  STREAM_HEALTHY: "STREAM_HEALTHY",
  STREAM_STALLED: "STREAM_STALLED",
  BLUETOOTH_REMEMBERED: "BLUETOOTH_REMEMBERED",
  BLUETOOTH_CONNECTED: "BLUETOOTH_CONNECTED",
  BLUETOOTH_LOST: "BLUETOOTH_LOST",
  LOCAL_OUTPUT_AVAILABLE: "LOCAL_OUTPUT_AVAILABLE",
  LOCAL_OUTPUT_LOST: "LOCAL_OUTPUT_LOST",
  POWER_INTERRUPTED: "POWER_INTERRUPTED"
});

const retryDelays = Object.freeze([5000, 30000, 120000, 600000]);
const supportedEvents = new Set(Object.values(RuntimeEvents));

function saturatingIncrement(value) {
  return value >= 0xffffffff ? 0xffffffff : value + 1;
}

function saturatingAdd(value, delta) {
  return value > Number.MAX_SAFE_INTEGER - delta ? Number.MAX_SAFE_INTEGER : value + delta;
}

export class RuntimeOrchestrator {
  constructor(limits) {
    this.limits = Object.freeze({...limits});
    this.queue = [];
    this.timers = new Map();
    this.diagnostics = [];
    this.nowMs = 0;
    this.lastAcceptedSequence = 0;
    this.state = RuntimeStates.CONFIG_REQUIRED;
    this.output = "LOCAL";
    this.recoveryReason = "NONE";
    this.flags = {
      configPresent: false,
      configValid: false,
      wifiConnected: false,
      resolverSupported: true,
      streamHealthy: false,
      bluetoothRemembered: false,
      bluetoothConnected: false,
      localOutputAvailable: true
    };
    this.retryAttempts = {NETWORK: 0, STREAM: 0, BLUETOOTH: 0};
    this.counters = {
      acceptedEvents: 0,
      processedEvents: 0,
      queueOverflows: 0,
      timerOverflows: 0,
      duplicateEvents: 0,
      staleEvents: 0,
      recoveries: 0,
      networkRetries: 0,
      streamRetries: 0,
      bluetoothRetries: 0,
      powerInterruptions: 0,
      maximumQueueDepth: 0,
      diagnosticOverwrites: 0
    };
    this.firstPlaybackReached = false;
  }

  post(event) {
    if (!event || !supportedEvents.has(event.type) || !Number.isSafeInteger(event.atMs) ||
        event.atMs < 0 || !Number.isInteger(event.sequence) || event.sequence <= 0) {
      this.counters.staleEvents = saturatingIncrement(this.counters.staleEvents);
      this.record("STALE_EVENT", this.nowMs, event?.sequence ?? 0);
      return false;
    }
    if (event.sequence <= this.lastAcceptedSequence) {
      const key = event.sequence === this.lastAcceptedSequence ? "duplicateEvents" : "staleEvents";
      this.counters[key] = saturatingIncrement(this.counters[key]);
      this.record(key === "duplicateEvents" ? "DUPLICATE_EVENT" : "STALE_EVENT", this.nowMs, event.sequence);
      return false;
    }
    if (event.atMs < this.nowMs) {
      this.counters.staleEvents = saturatingIncrement(this.counters.staleEvents);
      this.record("STALE_EVENT", this.nowMs, event.sequence);
      return false;
    }
    if (this.queue.length >= this.limits.eventQueueCapacity) {
      this.counters.queueOverflows = saturatingIncrement(this.counters.queueOverflows);
      this.record("QUEUE_OVERFLOW", this.nowMs, event.sequence);
      return false;
    }
    this.queue.push(Object.freeze({...event}));
    this.lastAcceptedSequence = event.sequence;
    this.counters.acceptedEvents = saturatingIncrement(this.counters.acceptedEvents);
    this.counters.maximumQueueDepth = Math.max(this.counters.maximumQueueDepth, this.queue.length);
    return true;
  }

  advance(nowMs) {
    if (!Number.isSafeInteger(nowMs) || nowMs < this.nowMs) {
      this.counters.staleEvents = saturatingIncrement(this.counters.staleEvents);
      this.record("STALE_EVENT", this.nowMs, 0);
      return false;
    }
    this.runDueTimers(nowMs);
    this.nowMs = nowMs;
    this.queue.sort((left, right) => left.atMs - right.atMs || left.sequence - right.sequence);
    while (this.queue[0]?.atMs <= nowMs) {
      const event = this.queue.shift();
      this.dispatch(event);
      this.counters.processedEvents = saturatingIncrement(this.counters.processedEvents);
    }
    return true;
  }

  dispatch(event) {
    switch (event.type) {
      case RuntimeEvents.CONFIG_MISSING:
        this.flags.configPresent = false;
        this.flags.configValid = false;
        this.cancelAllTimers();
        this.record("ONBOARDING_REQUIRED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.CONFIG_CORRUPT:
        this.flags.configPresent = true;
        this.flags.configValid = false;
        this.cancelAllTimers();
        this.record("SAFE_MODE", event.atMs, event.sequence);
        break;
      case RuntimeEvents.CONFIG_READY:
        this.flags.configPresent = true;
        this.flags.configValid = true;
        this.record("CONFIG_READY", event.atMs, event.sequence);
        break;
      case RuntimeEvents.WIFI_CONNECTED:
        this.flags.wifiConnected = true;
        this.cancelTimer("NETWORK");
        this.record("WIFI_RECOVERED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.WIFI_LOST:
        this.flags.wifiConnected = false;
        this.flags.streamHealthy = false;
        this.scheduleTimer("NETWORK", event.atMs);
        this.record("WIFI_LOST", event.atMs, event.sequence);
        break;
      case RuntimeEvents.RESOLVER_READY:
        this.flags.resolverSupported = true;
        this.record("RESOLVER_READY", event.atMs, event.sequence);
        break;
      case RuntimeEvents.RESOLVER_UNSUPPORTED:
        this.flags.resolverSupported = false;
        this.cancelTimer("STREAM");
        this.record("RESOLVER_UNSUPPORTED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.STREAM_HEALTHY:
        if (this.flags.wifiConnected) this.flags.streamHealthy = true;
        this.cancelTimer("STREAM");
        this.record("STREAM_RECOVERED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.STREAM_STALLED:
        this.flags.streamHealthy = false;
        this.scheduleTimer("STREAM", event.atMs);
        this.record("STREAM_STALLED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.BLUETOOTH_REMEMBERED:
        this.flags.bluetoothRemembered = true;
        break;
      case RuntimeEvents.BLUETOOTH_CONNECTED:
        this.flags.bluetoothRemembered = true;
        this.flags.bluetoothConnected = true;
        this.cancelTimer("BLUETOOTH");
        this.record("BLUETOOTH_RECOVERED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.BLUETOOTH_LOST:
        this.flags.bluetoothConnected = false;
        if (this.flags.bluetoothRemembered) this.scheduleTimer("BLUETOOTH", event.atMs);
        this.record("BLUETOOTH_LOST", event.atMs, event.sequence);
        break;
      case RuntimeEvents.LOCAL_OUTPUT_AVAILABLE:
        this.flags.localOutputAvailable = true;
        this.record("LOCAL_OUTPUT_RECOVERED", event.atMs, event.sequence);
        break;
      case RuntimeEvents.LOCAL_OUTPUT_LOST:
        this.flags.localOutputAvailable = false;
        this.record("LOCAL_OUTPUT_LOST", event.atMs, event.sequence);
        break;
      case RuntimeEvents.POWER_INTERRUPTED:
        this.flags.wifiConnected = false;
        this.flags.streamHealthy = false;
        this.flags.bluetoothConnected = false;
        this.counters.powerInterruptions = saturatingIncrement(this.counters.powerInterruptions);
        this.scheduleTimer("NETWORK", event.atMs);
        this.record("POWER_INTERRUPTED", event.atMs, event.sequence);
        break;
    }
    this.recomputeState();
  }

  recomputeState() {
    const previous = this.state;
    if (!this.flags.configPresent) {
      this.state = RuntimeStates.CONFIG_REQUIRED;
      this.recoveryReason = "NONE";
    } else if (!this.flags.configValid) {
      this.state = RuntimeStates.SAFE_MODE;
      this.recoveryReason = "MEMORY";
    } else if (!this.flags.wifiConnected) {
      this.state = RuntimeStates.RECOVERING;
      this.recoveryReason = "WIFI";
    } else if (!this.flags.resolverSupported) {
      this.state = RuntimeStates.UNSUPPORTED_STATION;
      this.recoveryReason = "STREAM";
    } else if (!this.flags.streamHealthy) {
      this.state = RuntimeStates.RECOVERING;
      this.recoveryReason = "STREAM";
    } else if (this.flags.bluetoothConnected) {
      this.state = RuntimeStates.PLAYING;
      this.recoveryReason = "NONE";
    } else if (this.flags.localOutputAvailable) {
      this.state = this.flags.bluetoothRemembered
        ? RuntimeStates.DEGRADED_PLAYING
        : RuntimeStates.PLAYING;
      this.recoveryReason = this.flags.bluetoothRemembered ? "BLUETOOTH" : "NONE";
    } else {
      this.state = RuntimeStates.RECOVERING;
      this.recoveryReason = "BLUETOOTH";
    }
    this.output = this.state !== RuntimeStates.SAFE_MODE &&
        this.state !== RuntimeStates.CONFIG_REQUIRED && this.flags.bluetoothConnected
      ? "BT"
      : "LOCAL";
    const playable = this.state === RuntimeStates.PLAYING || this.state === RuntimeStates.DEGRADED_PLAYING;
    if (playable && !this.firstPlaybackReached) this.firstPlaybackReached = true;
    else if (playable && previous !== RuntimeStates.PLAYING && previous !== RuntimeStates.DEGRADED_PLAYING) {
      this.counters.recoveries = saturatingIncrement(this.counters.recoveries);
    }
  }

  scheduleTimer(kind, atMs) {
    if (this.timers.has(kind)) return;
    if (this.timers.size >= this.limits.timerCapacity) {
      this.counters.timerOverflows = saturatingIncrement(this.counters.timerOverflows);
      this.record("TIMER_OVERFLOW", atMs, 0);
      return;
    }
    const attempt = this.retryAttempts[kind];
    const delay = Math.min(retryDelays[Math.min(attempt, retryDelays.length - 1)], this.limits.maximumRetryDelayMs);
    this.timers.set(kind, saturatingAdd(atMs, delay));
  }

  cancelTimer(kind) {
    this.timers.delete(kind);
    this.retryAttempts[kind] = 0;
  }

  cancelAllTimers() {
    this.timers.clear();
    for (const kind of Object.keys(this.retryAttempts)) this.retryAttempts[kind] = 0;
  }

  runDueTimers(nowMs) {
    for (const [kind, dueAtMs] of [...this.timers]) {
      if (dueAtMs > nowMs) continue;
      this.timers.delete(kind);
      const counter = `${kind.toLowerCase()}Retries`;
      this.counters[counter] = saturatingIncrement(this.counters[counter]);
      this.retryAttempts[kind] = Math.min(this.retryAttempts[kind] + 1, retryDelays.length - 1);
      const stillRequired = kind === "NETWORK"
        ? !this.flags.wifiConnected
        : kind === "STREAM"
          ? !this.flags.streamHealthy && this.flags.wifiConnected
          : !this.flags.bluetoothConnected && this.flags.bluetoothRemembered;
      if (stillRequired) this.scheduleTimer(kind, nowMs);
      this.record(`${kind}_RETRY`, nowMs, 0);
    }
  }

  record(reason, atMs, sequence) {
    const entry = Object.freeze({reason, atMs, sequence, state: this.state});
    if (this.diagnostics.length >= this.limits.diagnosticCapacity) {
      this.diagnostics.shift();
      this.counters.diagnosticOverwrites = saturatingIncrement(this.counters.diagnosticOverwrites);
    }
    this.diagnostics.push(entry);
  }

  snapshot() {
    return Object.freeze({
      state: this.state,
      output: this.output,
      recoveryReason: this.recoveryReason,
      wifiConnected: this.flags.wifiConnected,
      bluetoothConnected: this.flags.bluetoothConnected,
      localOutputAvailable: this.flags.localOutputAvailable,
      queueDepth: this.queue.length,
      timerCount: this.timers.size,
      diagnosticsStored: this.diagnostics.length,
      counters: Object.freeze({...this.counters})
    });
  }
}
