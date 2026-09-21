export function createMemoryServiceCache(initialEntries = []) {
  const entries = new Map(initialEntries.map(([key, value]) => [key, structuredClone(value)]));
  return Object.freeze({
    get(key) {
      const value = entries.get(key);
      return value === undefined ? null : structuredClone(value);
    },
    set(key, value) {
      entries.set(key, structuredClone(value));
    }
  });
}

export async function runBoundedService({
  serviceId,
  cacheKey,
  cache,
  execute,
  nowEpochSeconds,
  freshForSeconds,
  maximumStaleSeconds,
  maximumAttempts = 2,
  backoffSeconds = [1],
  sleep = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds)),
  force = false
}) {
  const cached = cache.get(cacheKey);
  const ageSeconds = cached === null ? null : Math.max(0, nowEpochSeconds - cached.fetchedAtEpochSeconds);
  if (!force && cached !== null && ageSeconds <= freshForSeconds) {
    return {serviceId, status: "FRESH_CACHE", data: cached.data, ageSeconds, attempts: 0, errorCode: null};
  }

  let errorCode = "UNAVAILABLE";
  for (let attempt = 1; attempt <= maximumAttempts; attempt += 1) {
    try {
      const data = await execute();
      cache.set(cacheKey, {fetchedAtEpochSeconds: nowEpochSeconds, data});
      return {serviceId, status: "LIVE", data, ageSeconds: 0, attempts: attempt, errorCode: null};
    } catch (error) {
      errorCode = typeof error?.code === "string" ? error.code : "REQUEST_FAILED";
      if (attempt < maximumAttempts) await sleep((backoffSeconds[attempt - 1] ?? backoffSeconds.at(-1) ?? 1) * 1000);
    }
  }

  if (cached !== null && ageSeconds <= maximumStaleSeconds) {
    return {serviceId, status: "STALE_CACHE", data: cached.data, ageSeconds, attempts: maximumAttempts, errorCode};
  }
  return {serviceId, status: "UNAVAILABLE", data: null, ageSeconds, attempts: maximumAttempts, errorCode};
}
