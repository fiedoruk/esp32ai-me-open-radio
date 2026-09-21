#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
venv="$root/.tools/venv"
requirements="$root/firmware/manifests/requirements-firmware.lock.txt"
shared_bootstrap="${CODEX_ESP32_BOOTSTRAP:-}"

if [[ -n "$shared_bootstrap" && -x "$shared_bootstrap" ]]; then
  exec "$shared_bootstrap" "$root"
fi

command -v uv >/dev/null 2>&1 || {
  printf 'uv is required to create the isolated Python 3.13.12 environment.\n' >&2
  exit 1
}

mkdir -p "$root/.tools"
uv venv --python 3.13.12 "$venv"
uv pip install --python "$venv/bin/python" --requirement "$requirements"

"$venv/bin/python" --version
"$venv/bin/pio" --version
"$venv/bin/cmake" --version | head -1
"$venv/bin/ninja" --version
"$venv/bin/pio" pkg install -d "$root/firmware/public-candidate"

printf 'PASS project-local-firmware-toolchain path=%s\n' "$venv"
