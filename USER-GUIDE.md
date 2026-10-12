# Open Radio Core2 — user guide

[Polska wersja](USER-GUIDE.pl.md)

## Current hardware evidence

The current public release is **0.4.4** (2026-10-12), the last release of the
0.4 line. It fixes two faults found in 0.4.2 and brings a Bluetooth speaker that
was switched off back in seconds instead of minutes; it adds no features and no
stations. Its evidence comes from one unit — the owner's Core2 v1.3 — on the
published factory image, installed from a full erase the way a buyer installs
it: onboarding from zero, audio from the built-in speaker, a speaker the cube
did not know paired, and ten speaker power cycles on RMF FM, back 3 to
16 seconds after the speaker's call and nine of ten within 10 seconds, with no
silence on the speaker, no drop on the built-in speaker, zero panics and zero
unexpected restarts. The known limitations below include the paths that were not tested on
hardware at all.

**0.2.1 remains the release with the widest hardware evidence**: release-day
smoke on the owner's Core2 v1.0, an external user who independently installed
and confirmed it on a factory-fresh v1.1, and a customer who confirmed v1.3 — so
the whole hardware line has field evidence for that release, and not yet for
0.4.4. Battery duration is owner-measured (~2.5 h typical, up to ~3 h); power-bank
behaviour and long endurance runs remain unmeasured.

## First start

1. Power the radio over USB-C.
2. If no approved Wi-Fi network is stored, connect a phone to the temporary
   `OpenRadio-Setup` network using the one-time WPA2 code shown on the radio.
   The code fills two lines on the screen: type it as one word, without spaces,
   in capitals (`OR-` and twelve characters).
   The captive portal opens locally; if it does not, open `192.168.4.1`.
3. Add a secured Wi-Fi network. Unknown and open networks are never autojoined.
4. The built-in speaker starts first so sound does not depend on Bluetooth.
5. The radio reconnects its remembered Bluetooth Classic A2DP/SBC speaker. With
   no remembered speaker it starts a standards-based audio-device scan; the
   local Bluetooth screen can repeat that scan.

The phone is not required after onboarding unless a new network must be added.

## Daily screen

The bottom controls move to the previous station, open the station grid and move
to the next supported station. The settings button opens all device preferences.

Two home layouts are available:

- **Full** prioritizes station identity and the current ICY title when present.
- **Minimal** prioritizes the clock and keeps station information intentionally compact.

The selection is stored on the device. Missing metadata never stops audio; the
relevant field falls back or disappears.

## Settings

Settings are split into three clearly numbered pages:

- **Quick:** Wi-Fi status, Bluetooth, volume, brightness, theme and language.
- **System:** location/region, display, project information, diagnostics, home
  layout and Bluetooth rescan.
- **Display:** screensaver on/off, delay, mode, screen-off on/off, delay and preview.

Cards use one-tap bounded choices. Values wrap through a short predefined list;
there is no hidden text configuration or source-code editing.

## Local noise

From Settings, tap the concentric-circles icon beside **X** to open the fully
local Noise mode. Tap the large control to play or stop and choose White, Pink
or Brown below it; hardware A/C change color, while B or **X** returns to the
last station. The cube stores the selected mode and color, resumes Noise after
a power cycle, and keeps the same Bluetooth output and built-in-speaker
fallback without making Wi-Fi a playback dependency.

## Air sensors (optional)

The cube works with no sensor at all, and nothing inside it waits for a module.
Plug a supported module into Port A and two more home layouts appear: the radio
with the air beneath it, and the air with the radio reduced to a mark. You
switch them from the same Settings tile as the basic layout. Without a module
those two are unavailable, because they would show an empty half-screen — and
your stored choice waits untouched and returns when the module does.

We show carbon dioxide, temperature, relative humidity, pressure and a figure
from the gas element. That last one is **relative**: it says how far the air has
drifted from what the sensor grew used to in this room, and it names nothing.
After a gas module is connected the cube warms up for a while and says so,
rather than showing a number it cannot yet place.

Alerts are quiet by default and carry hysteresis so they do not flicker around a
threshold. The thresholds come from published sources, not from our judgement.

Environmental readings come from optional, user-installed sensors and are
informational. This is NOT a carbon-monoxide, smoke or gas-leak detector: its
gas element answers many different substances with a single number and cannot
identify any of them. Fit certified detectors for those hazards.

## Screen sleep

Pulse, Bars, Orbit and Kiara are visual screensavers. They start after 30 seconds to
10 minutes, depending on the selected setting. The display can turn off after
15, 30 or 60 minutes. Audio, networking and recovery continue. Touch wakes the
screen and returns to the selected daily layout.

## Changing Wi-Fi and reaching the console (changed in 0.4.2)

Wi-Fi is changed from the radio's own setup access point, not over your home
network. On the Wi-Fi screen the bottom button opens that access point while the
radio is connected, and when the saved network is genuinely gone the portal
opens by itself. If a console session is open, close it first: while it is live
that same button means "close console".

The console answers at the two addresses the radio itself shows — `http://open-radio.local`
and the IP address printed on the Wi-Fi screen and encoded in its QR code. A
bookmark that uses a name your router invented for the device is refused, with a
short JSON reply rather than the page. Use the address on the screen.

Both are deliberate. Until 0.4.2 any machine on the same network could move the
radio to a different access point during the console window.

## Air pressure trend, and why it never moved before

The tendency arrow beside the pressure reading, and the pressure alerts that go
with it, need three hours of history to say anything. Until 0.4.2 the ring that
holds that history was one sample short of three hours, so the arrow could never
change however long the radio had been running — an owner reported exactly that
after ten hours. It can change now. The first arrow still appears three hours
after a cold start, because that is how long the question takes to answer.

## Recovery behavior

- Lost Wi-Fi: the radio retries known networks with bounded backoff.
- Broken stream: the radio reopens the stream and rotates available candidates.
  Stable-dwell scoring and per-endpoint quarantine are not implemented yet.
- Lost Bluetooth: audio falls back to the built-in speaker while bounded reconnect
  attempts continue.
- Invalid configuration: safe mode avoids unknown networks and offers local repair.

## Privacy

There is no account, analytics or project-operated backend. Wi-Fi credentials,
known-network identifiers and diagnostics stay on the device. The current
firmware uses optional direct NTP time synchronization. Planned location and
weather enhancements are not present in the device firmware and must never
become playback dependencies.

## Known limitations

- **The speaker return was measured on one speaker.** On the published bytes,
  ten power cycles on RMF FM brought it back 3 to 16 seconds after the speaker's
  call. A second speaker has not been tested.
- The speaker often does not answer the radio's first call, because it is still
  calling the radio at that moment. The second call gets through, and the
  return takes about five seconds longer.
- Once in twenty cycles the radio held the sound back for five seconds, because
  its memory was too fragmented to start Bluetooth audio safely, and then called
  again: 23 seconds.
- **The built-in speaker can stutter on RMF FM** while Bluetooth is not playing.
  Through a Bluetooth speaker no drop was measured.
- When the speaker's link comes up, the station stream sometimes restarts on the
  next server within about a second. No silence or drop was measured.
- Opening a stream on an address that does not answer freezes the screen for up
  to five seconds, once per dead address.
- Two hours of Bluetooth play, the update path that writes the application
  alone over 0.4.2 with settings kept, and stations other than RMF FM were not
  exercised on the published 0.4.4 bytes.
- No uninterrupted 60-minute run exists for 0.4. The qualifying session ran 69
  minutes but contained deliberate reboots, which produced recovery evidence
  instead. The historical notes below about a failed 60-minute run describe an
  older pre-release lab image.
- Forced Wi-Fi/stream/speaker recovery cycles, PMU/battery/SD behaviour and
  longer endurance remain formally unqualified.
- All nine stations you pick from the 236-entry directory use MP3 paths, but endpoint availability
  can change independently of any firmware release.
- Official station logos are not publicly bundled without permission.
- Bluetooth LE Audio-only speakers are outside the Core2 contract.
- The current public release is 0.4.4; endurance and battery duration are not yet
  formally qualified, and 0.4.4's hardware evidence comes from a single unit.
