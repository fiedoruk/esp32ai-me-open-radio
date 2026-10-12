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

ESP8266Audio is modified when the firmware is built:
`../public-candidate/scripts/select_mp3_sources.py` changes its
`src/AudioFileSourceICYStream.cpp` (a 500 ms bound on reading stream metadata,
a cleared metadata buffer, HTTP/1.0 requests, and the length of a stream title
split across two reads) and its `library.json` (which files are compiled).
Modified as of 2026-09-25.

Each released binary ships with its corresponding-source bundle, license
texts, build instructions and the generated SBOM, published next to the
download.
