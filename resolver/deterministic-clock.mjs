export function createDeterministicClock(startMs = 1_000_000) {
  if (!Number.isInteger(startMs) || startMs < 0) throw new Error("startMs must be a non-negative integer");
  let currentMs = startMs;
  return Object.freeze({
    now: () => currentMs,
    advance(milliseconds) {
      if (!Number.isInteger(milliseconds) || milliseconds < 0) throw new Error("advance requires a non-negative integer");
      currentMs += milliseconds;
      return currentMs;
    }
  });
}
