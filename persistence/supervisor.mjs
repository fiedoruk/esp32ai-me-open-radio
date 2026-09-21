export function evaluateBootSupervisor({consecutiveBootFailures, hasBootableConfig, threshold = 3}) {
  if (!Number.isInteger(consecutiveBootFailures) || consecutiveBootFailures < 0) throw new Error("consecutiveBootFailures must be a non-negative integer");
  if (!Number.isInteger(threshold) || threshold < 1 || threshold > 10) throw new Error("threshold must be from 1 to 10");
  if (typeof hasBootableConfig !== "boolean") throw new Error("hasBootableConfig must be boolean");
  if (!hasBootableConfig) return {mode: "SAFE_MODE", reason: "CONFIG_UNAVAILABLE", boundedFailureCount: Math.min(consecutiveBootFailures, threshold)};
  if (consecutiveBootFailures >= threshold) return {mode: "SAFE_MODE", reason: "BOOT_LOOP", boundedFailureCount: threshold};
  return {mode: "NORMAL", reason: null, boundedFailureCount: consecutiveBootFailures};
}
