export async function requestJson({url, fetchImpl = globalThis.fetch, timeoutMilliseconds = 3500}) {
  if (typeof fetchImpl !== "function") throw Object.assign(new Error("fetch is unavailable"), {code: "FETCH_UNAVAILABLE"});
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMilliseconds);
  try {
    const response = await fetchImpl(url, {headers: {Accept: "application/json"}, signal: controller.signal});
    if (!response?.ok) throw Object.assign(new Error(`HTTP ${response?.status ?? 0}`), {code: `HTTP_${response?.status ?? 0}`});
    return await response.json();
  } catch (error) {
    if (error?.name === "AbortError") throw Object.assign(new Error("request timed out"), {code: "TIMEOUT"});
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}
