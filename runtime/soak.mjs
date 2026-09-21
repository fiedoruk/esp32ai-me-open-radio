import {RuntimeEvents, RuntimeOrchestrator, RuntimeStates} from "./orchestrator.mjs";

const eventOrder = new Map(Object.values(RuntimeEvents).map((value, index) => [value, index]));
const stateCodes = new Map([
  [RuntimeStates.CONFIG_REQUIRED, 0],
  [RuntimeStates.RECOVERING, 1],
  [RuntimeStates.PLAYING, 2],
  [RuntimeStates.DEGRADED_PLAYING, 3],
  [RuntimeStates.UNSUPPORTED_STATION, 4],
  [RuntimeStates.SAFE_MODE, 5]
]);

function xorshift(value) {
  let next = value >>> 0;
  next ^= next << 13;
  next ^= next >>> 17;
  next ^= next << 5;
  return next >>> 0;
}

export function buildSoakEvents(scenario) {
  let random = scenario.seed >>> 0;
  const periods = [];
  for (const base of [17, 29, 43, 127]) {
    random = xorshift(random);
    periods.push(base + (random % Math.max(3, Math.floor(base / 4))));
  }
  const events = [
    {minute: 0, type: RuntimeEvents.CONFIG_READY},
    {minute: 0, type: RuntimeEvents.LOCAL_OUTPUT_AVAILABLE},
    {minute: 0, type: RuntimeEvents.WIFI_CONNECTED},
    {minute: 0, type: RuntimeEvents.RESOLVER_READY},
    {minute: 0, type: RuntimeEvents.STREAM_HEALTHY},
    {minute: 0, type: RuntimeEvents.BLUETOOTH_REMEMBERED},
    {minute: 0, type: RuntimeEvents.BLUETOOTH_CONNECTED}
  ];
  const addRecovery = (minute, types) => {
    if (minute <= scenario.durationMinutes &&
        (scenario.corruptAtMinute === null || minute < scenario.corruptAtMinute)) {
      for (const type of types) events.push({minute, type});
    }
  };
  for (let minute = 1; minute < scenario.durationMinutes; ++minute) {
    if (scenario.corruptAtMinute !== null && minute === scenario.corruptAtMinute) {
      events.push({minute, type: RuntimeEvents.CONFIG_CORRUPT});
      break;
    }
    if (minute % periods[0] === 0) {
      events.push({minute, type: RuntimeEvents.WIFI_LOST});
      addRecovery(minute + 1, [RuntimeEvents.WIFI_CONNECTED, RuntimeEvents.RESOLVER_READY, RuntimeEvents.STREAM_HEALTHY]);
    }
    if (minute % periods[1] === 0) {
      events.push({minute, type: RuntimeEvents.STREAM_STALLED});
      addRecovery(minute + 1, [RuntimeEvents.STREAM_HEALTHY]);
    }
    if (minute % periods[2] === 0) {
      events.push({minute, type: RuntimeEvents.BLUETOOTH_LOST});
      addRecovery(minute + 1, [RuntimeEvents.BLUETOOTH_CONNECTED]);
    }
    if (minute % periods[3] === 0) {
      events.push({minute, type: RuntimeEvents.POWER_INTERRUPTED});
      addRecovery(minute + 1, [RuntimeEvents.CONFIG_READY, RuntimeEvents.WIFI_CONNECTED,
        RuntimeEvents.RESOLVER_READY, RuntimeEvents.STREAM_HEALTHY, RuntimeEvents.BLUETOOTH_CONNECTED]);
    }
  }
  events.sort((left, right) => left.minute - right.minute || eventOrder.get(left.type) - eventOrder.get(right.type));
  return events.map((event, index) => ({...event, sequence: index + 1}));
}

function fnvUpdate(hash, value) {
  let next = hash;
  for (let byte = 0; byte < 4; ++byte) {
    next ^= BigInt((value >>> (byte * 8)) & 0xff);
    next = BigInt.asUintN(64, next * 0x100000001b3n);
  }
  return next;
}

export function runVirtualSoak({scenario, limits, events = buildSoakEvents(scenario)}) {
  const orchestrator = new RuntimeOrchestrator(limits);
  let eventIndex = 0;
  let stateHash = 0xcbf29ce484222325n;
  for (let minute = 0; minute <= scenario.durationMinutes; ++minute) {
    while (events[eventIndex]?.minute === minute) {
      const event = events[eventIndex++];
      if (!orchestrator.post({type: event.type, atMs: minute * 60000, sequence: event.sequence})) {
        throw new Error(`${scenario.id}: generated event rejected at minute ${minute}`);
      }
    }
    orchestrator.advance(minute * 60000);
    const snapshot = orchestrator.snapshot();
    const retries = snapshot.counters.networkRetries + snapshot.counters.streamRetries + snapshot.counters.bluetoothRetries;
    for (const value of [stateCodes.get(snapshot.state), snapshot.output === "BT" ? 1 : 0,
      snapshot.counters.recoveries, retries, snapshot.queueDepth, snapshot.timerCount]) {
      stateHash = fnvUpdate(stateHash, value);
    }
  }
  const snapshot = orchestrator.snapshot();
  return {
    eventCount: events.length,
    finalState: snapshot.state,
    finalOutput: snapshot.output,
    recoveries: snapshot.counters.recoveries,
    networkRetries: snapshot.counters.networkRetries,
    streamRetries: snapshot.counters.streamRetries,
    bluetoothRetries: snapshot.counters.bluetoothRetries,
    powerInterruptions: snapshot.counters.powerInterruptions,
    maximumQueueDepth: snapshot.counters.maximumQueueDepth,
    diagnosticsStored: snapshot.diagnosticsStored,
    diagnosticOverwrites: snapshot.counters.diagnosticOverwrites,
    queueOverflows: snapshot.counters.queueOverflows,
    timerOverflows: snapshot.counters.timerOverflows,
    stateHash: stateHash.toString(16).padStart(16, "0")
  };
}
