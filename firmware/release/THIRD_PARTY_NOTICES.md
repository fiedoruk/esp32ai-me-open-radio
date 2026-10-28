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
`../public-candidate/scripts/select_mp3_sources.py` applies the following
changes, as of 2026-10-26:

- `src/AudioGeneratorMP3.h` and `.cpp`: retain partial MP3 input and yield for
  more data without ending an open stream (`NeedInput`); retry only an output
  sample that was refused, and bound corrupt-frame retries per loop call.
- `src/AudioFileSourceICYStream.cpp` and `src/AudioFileSourceHTTPStream.cpp`:
  check for a null stream pointer before reading a connection that has closed.
- The earlier ICY changes remain: a 500 ms bound on reading stream metadata,
  a cleared metadata buffer, HTTP/1.0 requests, and the correct length of a
  stream title split across two reads.
- `library.json`: restrict which files are compiled for the public MP3 image.

The same build script modifies M5GFX, as of 2026-10-26:

- `src/lgfx/v1/platforms/common.hpp` and
  `src/lgfx/v1/platforms/esp32/Bus_SPI.cpp`: expose the capacity of both reserved
  DMA conversion buffers and cap converted SPI transfers to that capacity.
  If the reservation is unavailable, use the CPU transfer path.
- The earlier `src/lgfx/utility/lgfx_pngle.c` change remains: leave the inner
  decompression loop when the zlib stream has ended without consuming input,
  so trailing data cannot keep that loop running indefinitely.

These patches keep the pinned upstream commits and are applied by the build
script; their source is included in the corresponding-source bundle.

Each released binary ships with its corresponding-source bundle, license
texts, build instructions and the generated SBOM, published next to the
download.
