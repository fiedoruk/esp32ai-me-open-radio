# Third-party notices

## Host software

Host-side project code uses platform APIs only. No npm package is installed or
bundled.

## GUI icon subset

The first-run onboarding portal uses a local 21-icon subset of Tabler Icons
`3.44.0`,
pinned to commit `6d128ed935d4546607b1e4d5d08c8b27bdbe7758`. Tabler Icons is
licensed under the MIT License. The exact notice is retained in
`ui-contract/icons/TABLER-LICENSE.txt`. The same subset is compiled into the
onboarding portal that the firmware serves, so it is listed in
`firmware/release/THIRD_PARTY_NOTICES.md` as well.

## Fonts

The firmware renders text from a bitmap glyph atlas generated
offline from Inter v4.1 (Copyright (c) 2016 The Inter Project Authors),
licensed under the SIL Open Font License 1.1. The upstream font and the licence
text ship in `third_party/fonts/inter/`. No font is downloaded or parsed at
runtime.

## Firmware

The exact pinned firmware graph, source filters, versions and licenses are in:

- `firmware/manifests/dependencies.lock.json`,
- `firmware/release/THIRD_PARTY_NOTICES.md`,
- `firmware/release/sbom.spdx.json`,
- `firmware/release/codec-bom.json`.

The public-candidate graph uses Arduino-ESP32, M5Unified/M5GFX, ESP32-A2DP and
the HTTP/ICY/MP3/libmad subset of ESP8266Audio. AAC and ESP-ADF binary codec
components are excluded from public artifacts.

No third-party station logo, jingle, recording or broadcaster artwork is part
of the public project license. The released 0.4.1 binary is hardware-validated
and ships with its corresponding-source bundle and these notices. The 0.4
archive is incomplete.
