# Platform-neutral RGB565 renderer

This directory contains the platform-neutral production display renderer. The
core uses C++17 and depends only on the standard library plus generated project
headers.

## Generated headers

The headers under `renderer/generated/` are produced from `ui-contract/` by
`scripts/generate-renderer-contract.mjs`; run it with `--check` to confirm they
are current. The renderer is rendered deterministically: the same contract gives
the same pixels on every build.

## Render the fixture

```bash
mkdir -p output/renderer
build/renderer/open-radio-render output/renderer/now-playing.ppm \
  --screen now-playing-editorial --theme dark --locale pl \
  --output bluetooth
```

The host executable owns filesystem output. Code under `renderer/src/` and
`renderer/include/` has no filesystem, browser, network, audio, Bluetooth,
M5Unified or M5GFX dependency.

## Layout

- `include/`: stable renderer types and API,
- `src/`: framebuffer primitives and production screens,
- `generated/`: checked-in constants derived from JSON,
- `host/`: PPM command-line adapter,
- `tests/`: bounds, screen-matrix and deterministic-render tests,
- `evidence/`: expected fixture and production-matrix hashes.

The renderer consumes generated Light A and Dark B RGB565 tokens, defaults to
Dark B and draws a compile-time subset of Tabler icons through 24x24 masks. Its
bundled Inter 600 atlas covers the complete declared PL/EN character set. The
atlas is generated offline from the checked-in OFL-licensed source font and is
the only production typography path: there is no runtime download or font
parser. The PPM remains structural evidence and does not prove physical Core2
color, readability or orientation parity.
