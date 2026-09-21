export async function synchronizeOptionalTime({syncImpl, timezone, nowEpochSeconds}) {
  if (typeof syncImpl !== "function") return {status: "NOT_IMPLEMENTED", synchronizedAtEpochSeconds: null, timezone, errorCode: null};
  try {
    const result = await syncImpl({server: "pool.ntp.org", timezone});
    if (result !== true) throw Object.assign(new Error("sync was not confirmed"), {code: "SYNC_UNCONFIRMED"});
    return {status: "SYNCHRONIZED", synchronizedAtEpochSeconds: nowEpochSeconds, timezone, errorCode: null};
  } catch (error) {
    return {status: "RTC_FALLBACK", synchronizedAtEpochSeconds: null, timezone, errorCode: typeof error?.code === "string" ? error.code : "SYNC_FAILED"};
  }
}
