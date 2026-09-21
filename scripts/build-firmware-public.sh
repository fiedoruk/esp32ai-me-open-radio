#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pio="$root/.tools/venv/bin/pio"
project="$root/firmware/public-candidate"
build="$project/.pio/build/core2-public-candidate"
log="$(mktemp -t open-radio-build.XXXXXX)"
trap 'rm -f "$log"' EXIT

[[ -x "$pio" ]] || {
  printf 'Missing PlatformIO compatibility path at %s; run scripts/bootstrap-firmware-toolchain.sh\n' "$pio" >&2
  exit 1
}

node "$root/scripts/check-firmware-lock.mjs"
node "$root/scripts/generate-firmware-assets.mjs" --check
node "$root/scripts/generate-runtime-contract.mjs" --check
node "$root/scripts/generate-ingress-contract.mjs" --check
node "$root/scripts/check-firmware-surface.mjs"
node "$root/scripts/check-logo-pack-boundary.mjs"

# A corresponding-source archive ships without .git by policy, and its INSTALL
# tells the reader to run this script. So this script has to work there: no
# repository means no HEAD and no dirty tree, and the commit comes from
# release/CODE-CUT-COMMIT the way the pre-build script reads it. Without this
# branch the documented rebuild instruction stops on its first line, which is
# exactly where somebody exercising GPL section 3 would meet it.
# The same test the pre-build script uses (`(root / ".git").exists()`), and
# deliberately not `git rev-parse --git-dir`: that walks upwards and would
# find the enclosing repository of whoever unpacked the sources inside one,
# then stamp their HEAD. The shell and the Python must agree on what counts
# as a repository or they will disagree about what the binary says it is.
if [[ -e "$root/.git" ]]; then
  head_commit="$(git -C "$root" rev-parse HEAD)"
  # A binary that stamps itself with a commit must have been built from that
  # commit and nothing else. An uncommitted edit produces an artifact whose stamp
  # is a lie and whose hash nobody can reproduce — the failure is silent, which is
  # the only kind worth a gate. Checked before the compiler runs, not after: two
  # clean builds take long enough that finding out afterwards is its own defect.
  git -C "$root" diff --quiet && git -C "$root" diff --cached --quiet || {
    printf 'Working tree is dirty at %s: a stamped binary cannot be built from uncommitted changes.\n' "$head_commit" >&2
    exit 1
  }
else
  [[ -f "$root/release/CODE-CUT-COMMIT" ]] || {
    printf 'No .git and no release/CODE-CUT-COMMIT: this tree cannot say which commit it is.\n' >&2
    exit 1
  }
  head_commit="$(tr -d '[:space:]' < "$root/release/CODE-CUT-COMMIT")"
  printf 'NOTE no repository here; the code cut comes from release/CODE-CUT-COMMIT (%s).\n' "$head_commit"
fi

# An inherited OPEN_RADIO_BUILD_SHA wins over everything the pre-build script
# would otherwise work out, so a stale one left in a shell would stamp a new cut
# with an unrelated commit while this script reported HEAD as the cut. Cutting
# is the one mode that must stamp what it says it stamps, so it refuses rather
# than quietly overriding: an environment nobody meant to set is a fact worth
# seeing.
if [[ "${OPEN_RADIO_CODE_CUT:-0}" == "1" && -n "${OPEN_RADIO_BUILD_SHA:-}" ]]; then
  printf 'OPEN_RADIO_BUILD_SHA=%s is set while cutting a candidate: unset it, or the stamp will not be the commit this reports.\n' \
    "$OPEN_RADIO_BUILD_SHA" >&2
  exit 1
fi

# The binary carries the commit it was built from, so a record of its hash can
# never be written into the commit that produced it: writing the record moves
# HEAD, and a build at the new HEAD stamps the new HEAD and hashes differently.
#
#   OPEN_RADIO_CODE_CUT=1  cut a new candidate. Build from HEAD, stamp HEAD,
#                          report. Compare nothing — there is nothing to compare
#                          to yet. The record and the tag come afterwards.
#   (default)              reproduce a recorded candidate. The recorded code cut
#                          is stamped into the binary explicitly, so the build
#                          does NOT depend on where HEAD happens to be — which is
#                          the only way the record commit, the tag and a source
#                          tarball can all rebuild the same bytes. Whether the
#                          sources are right is then proved by the hash, which
#                          covers every file, rather than by HEAD, which covers
#                          nothing.
if [[ "${OPEN_RADIO_CODE_CUT:-0}" != "1" ]]; then
  read -r candidate_commit candidate_hash <<<"$(node -e 'const fs = require("node:fs"); const c = JSON.parse(fs.readFileSync(process.argv[1], "utf8")); process.stdout.write(`${c.codeCutCommit ?? ""} ${c.evidenceFirmwareSha256}`);' "$root/release/rc1-candidate.json")"
  [[ -n "$candidate_commit" ]] || {
    printf 'release/rc1-candidate.json has no codeCutCommit; a hash without the commit it came from cannot be verified.\n' >&2
    exit 1
  }
  export OPEN_RADIO_BUILD_SHA="$candidate_commit"
  if [[ "$head_commit" != "$candidate_commit" ]]; then
    printf 'NOTE reproducing the candidate cut at %s from a tree at %s; the stamp is the recorded cut.\n' \
      "$candidate_commit" "$head_commit"
  fi
fi

hashes=()
for pass in 1 2; do
  rm -rf "$root/.tools/platformio-cache"
  "$pio" run -d "$project" -e core2-public-candidate -t clean
  "$pio" run -d "$project" -e core2-public-candidate | tee "$log"
  hashes+=("$(shasum -a 256 "$build/firmware.bin" | awk '{print $1}')")
done

[[ "${hashes[0]}" == "${hashes[1]}" ]] || {
  printf 'Non-deterministic firmware.bin: %s != %s\n' "${hashes[0]}" "${hashes[1]}" >&2
  exit 1
}

node "$root/scripts/check-resolved-libdeps.mjs"
# The configuration was checked before the compiler ran; this asks the artifact.
# Broadcaster artwork in a published binary is a redistribution the project has
# no right to make, and the binary would look entirely ordinary.
node "$root/scripts/check-logo-pack-boundary.mjs" "$build/firmware.map"

if [[ "${OPEN_RADIO_CODE_CUT:-0}" == "1" ]]; then
  printf 'CUT firmware-candidate commit=%s sha256=%s bytes=%s\n' \
    "$head_commit" "${hashes[0]}" "$(wc -c < "$build/firmware.bin" | tr -d ' ')"
else
  # candidate_commit / candidate_hash were read before the build, because the
  # cut has to be stamped into the binary rather than compared to afterwards.
  [[ "${hashes[0]}" == "$candidate_hash" ]] || {
    printf 'Candidate not reproduced: stamping cut %s built %s, the record says %s. The sources differ from the ones that produced the published binary.\n' \
      "$candidate_commit" "${hashes[0]}" "$candidate_hash" >&2
    exit 1
  }
  printf 'REPRODUCED firmware-candidate cut=%s sha256=%s from-tree=%s\n' \
    "$candidate_commit" "${hashes[0]}" "$head_commit"
fi

OPEN_RADIO_BUILD_LOG="$log" OPEN_RADIO_EVIDENCE_NAME="${OPEN_RADIO_EVIDENCE_NAME:-s10-build-evidence.json}" node "$root/scripts/collect-firmware-evidence.mjs"
printf 'PASS firmware-reproducibility sha256=%s\n' "${hashes[0]}"
