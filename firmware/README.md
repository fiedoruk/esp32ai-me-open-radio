# Firmware

The M5Stack Core2 firmware source. The `core2-public-candidate` environment is
the released lane: every public image is built from it. The Core2 line has three
revisions (v1.0, v1.1, v1.3) and only 0.2.1 carries field evidence from all of
them. Everything here builds and tests without hardware, and no script in this
tree flashes a device on its own.

## Leading public candidate

`public-candidate/` uses pinned PlatformIO, Arduino-ESP32, M5Unified,
ESP32-A2DP and the HTTP/ICY/MP3/libmad subset of ESP8266Audio. It compiles:

1. the embedded nine-station presentation catalog,
2. all four local onboarding assets,
3. the real HTTP/ICY MP3 decoder path,
4. one bounded project-owned PCM contract,
5. mutually exclusive local-speaker and A2DP output adapters,
6. recovery policy shared with host tests.
7. local secured-profile onboarding and bounded Wi-Fi reconnect,
8. nine executable MP3 station paths and user-requested A2DP scanning.

Build it from the repository root. On a fresh machine with `uv` available,
create the exact project-local Python, PlatformIO, CMake and Ninja environment
first:

```bash
bash scripts/bootstrap-firmware-toolchain.sh
```

Then build the released lane:

```bash
.tools/venv/bin/pio run -d firmware/public-candidate -e core2-public-candidate
```

The application is written to
`firmware/public-candidate/.pio/build/core2-public-candidate/firmware.bin`.
Nothing in this step touches a device. How to rebuild a published release byte
for byte is in `REPRODUCIBILITY.md`.

## Hardware boundary

Flashing overwrites the stock M5Stack firmware. Keep a copy of the factory image
before the first write, and never run upload, serial or flash commands against a
device you have not backed up.
