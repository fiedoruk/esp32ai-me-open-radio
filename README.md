# Open Radio Core2

<p align="center"><img src="brand/logo-a-signal-cube.svg" width="92" alt="Open Radio — signal cube logo"></p>

<p align="center">
  <img src="https://img.shields.io/badge/released-0.4.4-3db8f5" alt="Released 0.4.4">
  <img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="GPL-3.0-or-later">
  <img src="https://img.shields.io/badge/stations-236_shipped-success" alt="236 stations shipped">
</p>

<p align="center"><a href="https://esp32ai.me/radio"><img src="https://esp32ai.me/assets/img/or/core2-1.jpg" width="740" alt="Open Radio playing on a real M5Stack Core2 cube"></a></p>

<p align="center">
  <img src="https://esp32ai.me/assets/img/or/screen-1.png" height="150" alt="Now-playing screen">
  <img src="https://esp32ai.me/assets/img/or/screen-2.png" height="150" alt="Station picker">
  <img src="https://esp32ai.me/assets/img/or/screen-3.png" height="150" alt="Settings screen">
  <img src="https://esp32ai.me/assets/img/or/screen-4.png" height="150" alt="Status card">
</p>

<p align="center"><b><a href="https://esp32ai.me/radio/install">⚡ Install from the browser</a></b> · <a href="https://fiedoruk.pl/os/open-radio-0-4-4.bin">Download 0.4.4</a> · <a href="https://esp32ai.me/radio/stations">The 236 stations</a></p>

> Released publicly as **Open Radio**. The published release is **0.4.4**
> (2026-10-12), the last release of the 0.4 line: it fixes two faults found in
> 0.4.2 and brings a Bluetooth speaker that was switched off back in seconds
> instead of minutes. No new features, no new stations, the same 236-station
> directory. 0.4.3 was never published on its own; its fixes ship in 0.4.4.
> Earlier releases 0.1, 0.2, 0.2.1, 0.4, 0.4.1 and 0.4.2 remain downloadable as
> historical builds.
> Official binaries come only from `fiedoruk.pl/os/`, linked from
> [esp32ai.me/radio](https://esp32ai.me/radio), and the corresponding source
> is published beside each binary under the GPL.

Open Radio is AI-assisted, human-directed and owner-accountable: every public
release names the hardware it was flashed to, the exact artifact hashes it
ships, and the limitations that are still open.

Open Radio Core2 is a reliability-first, open-source internet-radio cube for
M5Stack Core2. It is designed to play Polish stations over Wi-Fi through a
remembered Bluetooth speaker, with the built-in speaker as an automatic fallback.

[Polska wersja](README.pl.md)

## Product promise

Turn it on and it plays. Daily listening requires no account, phone, project
cloud or routine update. A phone is used only when a new Wi-Fi network must be
configured through the local device portal.

The radio is designed to:

- remember approved Wi-Fi networks and select one that is reachable,
- reconnect to the last Bluetooth speaker,
- resume the last working station,
- fall back to its built-in speaker,
- recover from Wi-Fi and stream failures without a reboot,
- explain actionable failures in plain language,
- keep its last-known-good catalog and configuration on the device.

It never autojoins unknown or open networks. There is no telemetry, account,
automatic OTA, recording, project-operated backend or routine remote catalog.
The current device firmware uses optional direct NTP time synchronization only.
Any future approximate-location or weather lookup must be bounded and must never
become a boot or playback dependency.

## What “Core2” means

Core2 is the target hardware model: **M5Stack Core2**. It is a technical
qualifier, not necessarily the final consumer-facing brand. Keeping it in the
working title makes the hardware boundary honest while the final name remains
open.

## Honest Bluetooth promise

The testable contract is standards-based interoperability with Bluetooth
Classic A2DP Sink speakers using SBC. Playback does not require AVRCP or
vendor-specific features. Bluetooth LE Audio-only speakers are outside the
original ESP32 hardware contract. No claim of universal speaker compatibility
will be made before physical qualification.

**Known limitations in 0.4.4.** A Bluetooth speaker that was switched off now
comes back in seconds: over ten power cycles on RMF FM with the published bytes,
3 to 16 seconds after the speaker called the radio, nine of ten within
10 seconds (0.4.2 took from 13 seconds to about five minutes). That was measured
on one speaker; a second has not been tested. The speaker often does not answer
the radio's first call, and the return then takes about five seconds longer.
The radio's own speaker can stutter on RMF FM while Bluetooth is not playing:
decoding a 128 kb/s stream runs only about a quarter faster than playback on
this hardware. Through a Bluetooth speaker no drop was measured.

## Honest air-sensor promise

Sensors are optional, and nothing waits or blinks without them. Plug a supported
module into Port A and the cube shows carbon dioxide, temperature, humidity,
pressure and a **relative** figure from the gas element — relative because it
only says how far the air has drifted from what the sensor grew used to in this
room. The thresholds carry published sources, and alerts are quiet by default.

Environmental readings come from optional, user-installed sensors and are
informational. This is NOT a carbon-monoxide, smoke or gas-leak detector: its
gas element answers many different substances with a single number and cannot
identify any of them. Fit certified detectors for those hazards.

## Current station scope

The device holds nine station slots, and **you pick those nine yourself from
a built-in directory of 236 Polish stations** — each verified by a real
connection and measured under load — at first power-on and any time later
through the phone console (QR). Browse the full directory at
[esp32ai.me/radio/stations](https://esp32ai.me/radio/stations).

The factory default nine (used until you choose your own): RMF FM, Radio ZET,
TOK FM, Antyradio, Radio ESKA, RMF MAXX, Radio Plus, Złote Przeboje and
Meloradio.

Every directory entry ships with an MP3 playback path in the public source lane.
AAC and HLS remain outside a public binary until separate legal, transport and
resource gates pass. The project does not proxy, rebroadcast or record station
audio and does not imply broadcaster endorsement.

## Hardware target

- M5Stack Core2 with ESP32-D0WDQ6-V3 — field-validated on hardware
  revisions v1.0 (AXP192), v1.1 (AXP2101) and v1.3 with the same image;
  every new Core2 revision joins the release test matrix as soon as it is
  available
- 16 MB flash and 8 MB PSRAM
- 320×240 touch display
- built-in I2S speaker and internal battery
- continuous USB-C power from a power bank
- Bluetooth Classic A2DP Source output

## What 0.4.4 fixes

Two faults found in the published 0.4.2, the slow return of a Bluetooth speaker,
and the fixes of 0.4.3, which was never published on its own. Nothing else
changed: no new features, no new stations.

- **A missing station icon could reboot the radio at every start.** An icon
  server that answered `200` with an empty body made the radio call a method on
  a null pointer. An empty answer is now treated as no icon.
- **The speaker comes back in seconds.** Four causes are fixed: the radio no
  longer waits for a full buffer in its own speaker before it reaches for
  Bluetooth; it closes a link the speaker opens and calls the speaker itself,
  again at once if that call goes unanswered; a five-second timeout no longer
  tears working links down; and a link the Bluetooth library lost track of
  during the call now starts the sound instead of being ignored.
- **Fewer silences on RMF.** A dead server address no longer counts as the whole
  station failing. The radio tries the next RMF FM address after one second and
  waits longer only after all of them failed.
- **Some stations played with glitches.** Eleven catalogue addresses answer an
  HTTP/1.1 request with chunked transfer, and the chunk markers ended up in the
  audio data. Audio is now requested over HTTP/1.0.
- **Smaller fixes.** A stream title split across two network reads is no longer
  cut at the seam. The setup code and the home-screen title are drawn whole. The
  setup screen's code button is a neutral "Change code". Small text and neutral
  controls are easier to see in the light theme. A pairing tap stops address
  discovery. A corrupt pressure calibration gives no reading instead of an
  overflow. The published binary no longer carries the build machine's path.

## Current status

**The public release is 0.4.4, published 2026-10-12.** It was installed from an
erased flash onto a Core2 v1.3 on the same day: onboarding from zero, a speaker
the cube did not know paired, ten speaker power cycles on RMF FM back 3 to
16 seconds after the speaker's call, zero panics and zero unexpected restarts.
Not tested on these bytes: two hours of Bluetooth play, the update path from
0.4.2 with settings kept, a second speaker, and stations other than RMF FM. The
limitations that stay open are stated above rather than buried.

Available now:

- exact 320×240 light and dark GUI previews,
- full and minimal now-playing layouts,
- local settings, screensavers and screen-off policy,
- local Wi-Fi onboarding with multiple secured profiles and bounded recovery,
- executable MP3 resolver paths for all nine current catalog entries,
- built-in-speaker playback routing and automatic model-agnostic discovery of
  Classic Bluetooth audio-rendering devices, with one approved peer remembered
  by address through the bounded pairing/link flow,
- up to eight automatically selected approved Wi-Fi profiles plus a two-touch
  full local-data reset,
- reproducible firmware source.

Measured on the 0.2.1 release line, which is still the only release with
evidence from more than one unit:

- release-day smoke on the owner's Core2 v1.0, an independent field install
  on a factory-fresh v1.1, and a customer confirmation on v1.3 — the whole
  hardware line covered by the same image,
- all nine station logos fetched in a single pass, first audio ≈1.2 s after
  the network, the remembered speaker back in ≈3.1 s, zero underruns at
  start,
- battery, owner-measured: about 2.5 hours of typical playback, close to
  three in favourable conditions (draw ~140 mA, ~190 mA at peaks; screen
  dimming changes little),
- Xiaomi Sound Pocket remains the Classic A2DP/SBC reference speaker.

Still open on the release line:

- the Bluetooth items described above: a second speaker, the unanswered first
  call, and the built-in speaker's stutter on RMF FM,
- forced-recovery cycles (Wi-Fi, stream, speaker) and the long endurance
  soak, with SD and power-bank behavior,
- additional Classic Bluetooth A2DP/SBC speaker interoperability,
- DRAM headroom during Bluetooth dialing (a ~5 KB dip stays on watch).

The public MP3-only lane is the released lane: 0.4.1, 0.4 and 0.2.1 all
reproduce byte-for-byte outside their provenance stamp when the documented
build parameters are honored, and a build of 0.4.4 from its published source
package, outside this repository, gives the release application byte for byte
(see [REPRODUCIBILITY.md](REPRODUCIBILITY.md)).
The endurance items above stay open and are listed honestly rather than
quietly.

## Building

The firmware builds with PlatformIO from `firmware/public-candidate`
(environment `core2-public-candidate`). [REPRODUCIBILITY.md](REPRODUCIBILITY.md)
describes the pinned toolchain and dependencies, and how to check a build
against a published release.

## User guide

Read [USER-GUIDE.md](USER-GUIDE.md) for first start, daily controls, display
settings, recovery behavior, privacy and known limitations.

## Documentation

- [USER-GUIDE.md](USER-GUIDE.md) — first start, daily controls, known limitations
- [REPRODUCIBILITY.md](REPRODUCIBILITY.md) — pinned toolchain and how to rebuild a released image
- [NETWORK-ENDPOINTS.md](NETWORK-ENDPOINTS.md) — every address the device may contact, and why
- [SECURITY.md](SECURITY.md) — how to report a security issue
- [SUPPORT.md](SUPPORT.md) — where to ask a question
- [CONTRIBUTING.md](CONTRIBUTING.md) and [CONTRIBUTOR-LICENCE.md](CONTRIBUTOR-LICENCE.md) — contribution rules
- [GOVERNANCE.md](GOVERNANCE.md) — who decides what, and how

## Attribution and license

Founder and original author: **Tomasz Fiedoruk — [fiedoruk.pl](https://fiedoruk.pl)**.

Software is licensed under `GPL-3.0-or-later`, original documentation and
artwork under `CC BY-SA 4.0`, and catalog data under `CC0-1.0`. Official station
logos are not bundled without documented permission. Public firmware releases
(0.1, 0.2, 0.2.1, 0.4, 0.4.1, 0.4.2 and the current 0.4.4) ship from `fiedoruk.pl/os/`;
the corresponding-source archive is published alongside each binary, and every
release records its provenance in a release manifest. Details:
[LICENSES.md](LICENSES.md), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
and [TRADEMARKS.md](TRADEMARKS.md).
