# Firmware third-party notices

The release build uses the exact dependency graph recorded in
`../manifests/dependencies.lock.json`.

| Component | Version | License | Use |
|---|---:|---|---|
| Arduino-ESP32 | 2.0.17 | LGPL-2.1-or-later and component licenses | Framework |
| M5Unified | 0.2.18 | MIT | Core2 board HAL |
| M5GFX | 0.2.25 | MIT | Display dependency of M5Unified |
| ESP32-A2DP | 1.8.11 | Apache-2.0 | Bluetooth Classic A2DP Source |
| ESP8266Audio | 2.4.1 | GPL-3.0-or-later | HTTP/ICY and MP3 integration |
| libmad subset | bundled with ESP8266Audio 2.4.1 | GPL-2.0-or-later | MP3 decoder |
| Inter (glyph atlas, generated offline) | v4.1 | OFL-1.1 | Firmware typography |
| Tabler Icons (21-icon subset) | 3.44.0 | MIT | Onboarding portal icons |

Only the source files listed in `../manifests/dependencies.lock.json` are
eligible from ESP8266Audio. AAC, Opus, FLAC, MIDI, MOD and alternative hardware
outputs are mechanically excluded from the public link graph.

The released 0.4.1 binary ships with its corresponding-source bundle,
exact license texts, build instructions and the final generated SBOM
published next to the download (see release/release-0-4-1.json).
