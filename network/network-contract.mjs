const securityModes = new Set(["WPA2_PSK", "WPA3_SAE", "OPEN", "UNKNOWN"]);
const profileReferencePattern = /^wifi:[a-z0-9][a-z0-9-]{0,47}$/;
const scanIdPattern = /^scan:[a-z0-9][a-z0-9-]{0,47}$/;

function isPlainObject(value) {
  return Boolean(value) && typeof value === "object" && !Array.isArray(value);
}

export function validateRememberedProfile(profile) {
  const errors = [];
  if (!isPlainObject(profile)) return {valid: false, errors: ["profile must be an object"]};
  if (profile.schemaVersion !== 1) errors.push("profile schemaVersion must be 1");
  if (typeof profile.ref !== "string" || !profileReferencePattern.test(profile.ref)) errors.push("profile reference is invalid");
  if (typeof profile.approved !== "boolean") errors.push("approved must be boolean");
  if (!Number.isInteger(profile.priority) || profile.priority < 0 || profile.priority > 100) errors.push("priority must be from 0 to 100");
  if (!Number.isInteger(profile.healthScore) || profile.healthScore < 0 || profile.healthScore > 100) errors.push("healthScore must be from 0 to 100");
  if (!Number.isInteger(profile.consecutiveFailures) || profile.consecutiveFailures < 0 || profile.consecutiveFailures > 1000) errors.push("consecutiveFailures is invalid");
  if (!Number.isInteger(profile.quarantineUntilMs) || profile.quarantineUntilMs < 0) errors.push("quarantineUntilMs is invalid");
  if (profile.lastSuccessMs !== null && (!Number.isInteger(profile.lastSuccessMs) || profile.lastSuccessMs < 0)) errors.push("lastSuccessMs is invalid");
  if (typeof profile.preferred !== "boolean") errors.push("preferred must be boolean");
  return {valid: errors.length === 0, errors};
}

export function validateScanResult(scan) {
  const errors = [];
  if (!isPlainObject(scan)) return {valid: false, errors: ["scan must be an object"]};
  if (scan.schemaVersion !== 1) errors.push("scan schemaVersion must be 1");
  if (typeof scan.scanId !== "string" || !scanIdPattern.test(scan.scanId)) errors.push("scanId is invalid");
  if (scan.profileRef !== null && (typeof scan.profileRef !== "string" || !profileReferencePattern.test(scan.profileRef))) errors.push("scan profile reference is invalid");
  if (!securityModes.has(scan.security)) errors.push("scan security is invalid");
  if (!Number.isInteger(scan.rssi) || scan.rssi < -100 || scan.rssi > 0) errors.push("scan RSSI must be from -100 to 0");
  if (typeof scan.reachable !== "boolean") errors.push("scan reachable must be boolean");
  if (typeof scan.captivePortal !== "boolean") errors.push("scan captivePortal must be boolean");
  return {valid: errors.length === 0, errors};
}

export function validateNetworkInputs(profiles, scans) {
  const errors = [];
  const profileList = Array.isArray(profiles) ? profiles : [];
  const scanList = Array.isArray(scans) ? scans : [];
  if (!Array.isArray(profiles) || profileList.length > 8) errors.push("profiles must contain at most eight entries");
  if (!Array.isArray(scans) || scanList.length > 64) errors.push("scans must contain at most 64 entries");
  const profileRefs = new Set();
  for (const profile of profileList) {
    const validation = validateRememberedProfile(profile);
    for (const error of validation.errors) errors.push(`${profile?.ref || "profile"}: ${error}`);
    if (profileRefs.has(profile.ref)) errors.push(`duplicate profile reference: ${profile.ref}`);
    profileRefs.add(profile.ref);
  }
  const scanIds = new Set();
  for (const scan of scanList) {
    const validation = validateScanResult(scan);
    for (const error of validation.errors) errors.push(`${scan?.scanId || "scan"}: ${error}`);
    if (scanIds.has(scan.scanId)) errors.push(`duplicate scan id: ${scan.scanId}`);
    scanIds.add(scan.scanId);
  }
  return {valid: errors.length === 0, errors};
}
