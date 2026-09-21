import {RuntimeIngress} from "./ingress.mjs";
import {RuntimeOrchestrator, RuntimeStates} from "./orchestrator.mjs";

const stateCodes = new Map([
  [RuntimeStates.CONFIG_REQUIRED, 0],
  [RuntimeStates.RECOVERING, 1],
  [RuntimeStates.PLAYING, 2],
  [RuntimeStates.DEGRADED_PLAYING, 3],
  [RuntimeStates.UNSUPPORTED_STATION, 4],
  [RuntimeStates.SAFE_MODE, 5]
]);

function fnvUpdate(hash, value) {
  let next = hash;
  for (let byte = 0; byte < 4; ++byte) {
    next ^= BigInt((value >>> (byte * 8)) & 0xff);
    next = BigInt.asUintN(64, next * 0x100000001b3n);
  }
  return next;
}

export function runIngressTrace({trace, ingressLimits, runtimeLimits}) {
  const orchestrator = new RuntimeOrchestrator(runtimeLimits);
  const ingress = new RuntimeIngress({limits: ingressLimits, orchestrator});
  for (const fact of trace.facts) ingress.post(fact);
  while (ingress.snapshot().mailboxDepth > 0) ingress.drain();
  const runtime = orchestrator.snapshot();
  const snapshot = ingress.snapshot();
  let stateHash = 0xcbf29ce484222325n;
  const values = [stateCodes.get(runtime.state), runtime.output === "BT" ? 1 : 0,
    snapshot.counters.acceptedFacts, snapshot.counters.deliveredFacts,
    snapshot.counters.mailboxOverflows, snapshot.counters.invalidFacts,
    snapshot.counters.duplicateFacts, snapshot.counters.staleFacts,
    snapshot.counters.staleEpochs, snapshot.counters.backwardTicks,
    snapshot.counters.rollovers, snapshot.counters.maximumMailboxDepth,
    snapshot.counters.deliveryRejected, runtime.counters.processedEvents];
  for (const value of values) stateHash = fnvUpdate(stateHash, value);
  return Object.freeze({
    finalState: runtime.state,
    finalOutput: runtime.output,
    acceptedFacts: snapshot.counters.acceptedFacts,
    deliveredFacts: snapshot.counters.deliveredFacts,
    mailboxOverflows: snapshot.counters.mailboxOverflows,
    invalidFacts: snapshot.counters.invalidFacts,
    duplicateFacts: snapshot.counters.duplicateFacts,
    staleFacts: snapshot.counters.staleFacts,
    staleEpochs: snapshot.counters.staleEpochs,
    backwardTicks: snapshot.counters.backwardTicks,
    rollovers: snapshot.counters.rollovers,
    maximumMailboxDepth: snapshot.counters.maximumMailboxDepth,
    deliveryRejected: snapshot.counters.deliveryRejected,
    runtimeProcessedEvents: runtime.counters.processedEvents,
    stateHash: stateHash.toString(16).padStart(16, "0")
  });
}
