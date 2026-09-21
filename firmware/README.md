# Firmware

The M5Stack Core2 firmware source. The `core2-public-candidate` environment is
the released lane: it produced the public 0.4.1 image, hardware-validated on a
single Core2 v1.0 unit. The Core2 line has three revisions (v1.0, v1.1, v1.3)
and only 0.2.1 carries field evidence from all of them. Everything here still
builds and tests without hardware,
and no script in this tree flashes a device on its own.

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

Run the complete no-flash gate:

```bash
# Run from the repository root.
bash scripts/build-firmware-public.sh
```

On a fresh machine with `uv` available, create the exact project-local Python,
PlatformIO, CMake and Ninja environment first:

```bash
bash scripts/bootstrap-firmware-toolchain.sh
```

The script performs two clean-cache builds and records evidence in
`output/firmware/s7-build-evidence.json`.

## ESP-ADF reference

`adf-reference/` records the pinned private reference build of Espressif's
official HTTP → MP3 → A2DP Source example. It is evidence, not the selected
Core2 application or a public artifact.

## Hardware boundary

Flashing overwrites the stock M5Stack firmware. Keep a copy of the factory image
before the first write, and never run upload, serial or flash commands against a
device you have not backed up.
