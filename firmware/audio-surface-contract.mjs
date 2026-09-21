import {createHash} from "node:crypto";

const sha256 = value => createHash("sha256").update(value).digest("hex");

// The audio path carries knowledge that cost weeks to acquire and one hard
// device hang to relearn: burst-equilibrium reserves, the decoder budget that
// couples touch latency to playback, and the coexistence preference that must
// stay BALANCE. Release 0.2 changes the catalog, never the sound. A promise
// not to touch it has no evidential value after 2026-07-18, so it is a gate.
export function checkAudioSurface(lock, contents) {
  const failures = [];
  // An exception is the audit trail of a reviewed change, NOT a bypass. Until
  // 2026-07-29 it was keyed by path alone and suppressed the hash check for
  // that file forever: one entry silently unfroze it, and release 0.3 plans to
  // touch five of the ten frozen files. The hash check below is unconditional
  // now — a deliberate change is recorded by rewriting the lock entry, which is
  // what the ritual already required — and each exception has to name the exact
  // transition it documents.
  for (const exception of lock.exceptions ?? []) {
    const entry = lock.files.find(item => item.path === exception.path);
    if (!entry) {
      failures.push(`${exception.path}: exception names a file that is not frozen`);
      continue;
    }
    for (const field of ["fromSha256", "toSha256", "reason", "verification"]) {
      if (!exception[field]) failures.push(`${exception.path}: exception needs ${field}`);
    }
    if (exception.fromSha256 === exception.toSha256) {
      failures.push(`${exception.path}: an exception must record a real transition`);
    }
  }
  // Only the LAST link of a path's chain may claim to describe the file as it
  // stands. Requiring that of every exception — as this did until a frozen file
  // moved twice — makes the second reviewed change unrepresentable: the earlier
  // link correctly names an intermediate hash, and the gate called that a
  // contradiction. The chain walk below is what proves the links join up.
  for (const path of new Set((lock.exceptions ?? []).map(exception => exception.path))) {
    const entry = lock.files.find(item => item.path === path);
    if (!entry) continue;
    const chain = (lock.exceptions ?? []).filter(exception => exception.path === path);
    const last = chain[chain.length - 1];
    if (last.toSha256 && last.toSha256 !== entry.sha256) {
      failures.push(
        `${path}: the last exception says the file moved to ${last.toSha256} ` +
        `but the lock holds ${entry.sha256} — the record and the file disagree`);
    }
  }
  for (const entry of lock.files) {
    const content = contents.get(entry.path);
    if (content === undefined) {
      failures.push(`${entry.path}: frozen audio file is missing`);
      continue;
    }
    if (sha256(content) !== entry.sha256) {
      failures.push(`${entry.path}: audio surface changed since ${lock.frozenAt}`);
    }
    // The write path rewrites sha256 but never frozenSha256, so a departure
    // from the original freeze is visible forever. Requiring the exception
    // chain here is what turns the ritual from discipline into a gate: a lock
    // rewrite with no reviewed exception fails loudly instead of passing.
    if (entry.frozenSha256 && entry.sha256 !== entry.frozenSha256) {
      const chain = (lock.exceptions ?? []).filter(item => item.path === entry.path);
      if (chain.length === 0) {
        failures.push(`${entry.path}: departed from its frozen hash with no exception documenting the change`);
      } else {
        let cursor = entry.frozenSha256;
        for (const link of chain) {
          if (link.fromSha256 !== cursor) {
            failures.push(`${entry.path}: exception chain breaks — expected a link from ${cursor}`);
            break;
          }
          cursor = link.toSha256;
        }
        if (cursor !== entry.sha256) {
          failures.push(`${entry.path}: exception chain ends at ${cursor}, not at the current hash`);
        }
      }
    }
  }
  for (const invariant of lock.invariants) {
    const content = contents.get(invariant.path);
    if (content === undefined) {
      failures.push(`${invariant.path}: invariant host file is missing`);
      continue;
    }
    if (!content.includes(invariant.requiredSubstring)) {
      failures.push(`${invariant.path}: lost audio invariant "${invariant.requiredSubstring}" (${invariant.reason})`);
    }
  }
  return {failures};
}
