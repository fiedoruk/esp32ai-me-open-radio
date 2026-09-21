export const DisplayPhases = Object.freeze({ACTIVE: "ACTIVE", SCREENSAVER: "SCREENSAVER", OFF: "OFF"});
export const ScreensaverModes = Object.freeze(["pulse", "bars", "orbit", "cat"]);
export const ScreensaverDelays = Object.freeze([30, 60, 120, 300, 600]);
export const DisplayOffDelays = Object.freeze([900, 1800, 3600]);

export const defaultDisplayProfile = Object.freeze({
  screensaverEnabled: true,
  screensaverMode: "pulse",
  screensaverDelaySeconds: 120,
  displayOffEnabled: true,
  displayOffDelaySeconds: 1800
});

export function validateDisplayProfile(profile) {
  const errors = [];
  if (!profile || typeof profile !== "object" || Array.isArray(profile)) return {valid: false, errors: ["display profile must be an object"]};
  if (typeof profile.screensaverEnabled !== "boolean") errors.push("screensaverEnabled is invalid");
  if (!ScreensaverModes.includes(profile.screensaverMode)) errors.push("screensaverMode is invalid");
  if (!ScreensaverDelays.includes(profile.screensaverDelaySeconds)) errors.push("screensaverDelaySeconds is invalid");
  if (typeof profile.displayOffEnabled !== "boolean") errors.push("displayOffEnabled is invalid");
  if (!DisplayOffDelays.includes(profile.displayOffDelaySeconds)) errors.push("displayOffDelaySeconds is invalid");
  if (Object.keys(profile).some(key => !Object.hasOwn(defaultDisplayProfile, key))) errors.push("display profile contains unexpected keys");
  return {valid: errors.length === 0, errors};
}

export function evaluateDisplayPhase(profile, idleSeconds) {
  const validation = validateDisplayProfile(profile);
  if (!validation.valid) throw new Error(validation.errors.join("; "));
  const boundedIdle = Math.max(0, idleSeconds);
  if (profile.displayOffEnabled && boundedIdle >= profile.displayOffDelaySeconds) return DisplayPhases.OFF;
  if (profile.screensaverEnabled && boundedIdle >= profile.screensaverDelaySeconds) return DisplayPhases.SCREENSAVER;
  return DisplayPhases.ACTIVE;
}

export function nextBoundedValue(values, current) {
  const index = values.indexOf(current);
  return values[(index + 1 + values.length) % values.length];
}
