# Open Radio Core2 — user guide

[Polska wersja](USER-GUIDE.pl.md)

## Current hardware evidence

The current public release is **0.4.5** (2026-10-28), accepted with
exceptions. It fixes several causes of interrupted playback when station data
arrives unevenly, a memory problem that could leave the radio silent after
repeated reconnects, a crash during a Wi-Fi disconnect and an unbounded wait
before Bluetooth starts; it adds no features and no stations. Its evidence
comes from one unit — the owner's Core2 v1.3 — with the release application
installed as an update from 0.4.4 that kept the settings: two hours through a
Bluetooth speaker without a silent frame, a stream stop or a restart; over
21 hours overnight two restarts on a Bluetooth reconnect, back in 129 and
83 seconds, then 12.8 hours without a restart; ten speaker power cycles, the
eight undisturbed ones back in 3.2 to 16.5 seconds. The known limitations
below include the paths that were not tested on hardware at all.

**0.2.1 remains the release with the widest hardware evidence**: release-day
smoke on the owner's Core2 v1.0, an external user who independently installed
and confirmed it on a factory-fresh v1.1, and a customer who confirmed v1.3 — so
the whole hardware line has field evidence for that release, and not yet for
0.4.5. Battery duration is owner-measured (~2.5 h typical, up to ~3 h); power-bank
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

### Opening Wi-Fi setup manually in 0.4.5

After setup is complete and at least one Wi-Fi network is saved, losing Wi-Fi
no longer opens `OpenRadio-Setup` automatically. The radio keeps trying its
saved networks. If the password changed or you moved to a different network:

1. Open the settings menu using the icon in the top-right corner of the home
   screen. On the first settings page, tap **Wi-Fi**.
2. Tap **Add Wi-Fi network**. If a console session is open, tap **Close console**
   first, then **Add Wi-Fi network**.
3. Scan the QR code with your phone, or join `OpenRadio-Setup` using the displayed
   **Backup code**. Open `192.168.4.1` if the local setup page does not appear.
4. Select your secured network and enter its password. The radio closes the
   setup network once it connects. **Change code** creates a new setup password;
   use it only when you need to replace the displayed code.

With the Polish interface, the buttons are **Dodaj sieć Wi-Fi**,
**Zamknij konsolę** and **Zmień kod**; **Backup code** is **Kod awaryjny**.
Automatic setup remains available when there are no saved networks or initial
setup has not been completed.

## Daily screen

The bottom controls move to the previous station, open the station grid and move
to the next supported station. The settings button opens all device preferences.

Two home layouts are available:

- **Full** prioritizes station identity and the current ICY title when present.
- **Minimal** prioritizes the clock and keeps station information intentionally compact.

The selection is stored on the device. If a station supplies no title, the
corresponding field shows fallback text or disappears.

## Settings

Settings are split into three clearly numbered pages:

- **Quick:** Wi-Fi status, Bluetooth, volume, brightness, theme and language.
- **System:** Home screen, Wi-Fi & networks, About, Diagnostics, Erase data and Console.
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
network. In 0.4.5, tap **Add Wi-Fi network** to open setup, even when the radio is disconnected from your home network. If a console session is open, tap **Close console** first. Once setup is complete and a network is saved, losing Wi-Fi does not open setup automatically.

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

0.4.5 was accepted on 2026-10-28 with exceptions, on one Core2 v1.3 and one
Bluetooth speaker.

- **The radio can restart when it reconnects a Bluetooth speaker after a
  weak-signal link loss.** In a 21-hour run this happened twice; each time the
  radio came back on its own with Wi-Fi, the station and the speaker within
  about two minutes. Switching the speaker off and on does not trigger it.
  Release 0.4.4 has the same reconnect code. A fix is planned for 0.4.6.
- **A Wi-Fi protection event can still interrupt playback for about a minute.**
  On a mixed CCMP/TKIP network the router blocked Wi-Fi with reason 14 (MIC
  failure) eleven times in one night on the release image; each time the sound
  stopped for about a minute and came back without help.
- **The built-in speaker can pause every few seconds**, also when the station
  data arrives in time. We did not check whether 0.4.4 does the same.
- **A second Bluetooth speaker has not been qualified for this release.** With
  the one tested speaker, eight undisturbed power cycles came back in 3.2 to
  16.5 seconds. In one cycle, with a phone paired with the same speaker lying
  next to it, the return took 72 seconds.
- An unresponsive station address can leave the screen unresponsive for
  several seconds while the connection is opened.
- Bluetooth audio can wait for sufficient memory. A speaker can also require
  another connection attempt; the earlier stream reopen at speaker connection
  has not yet been declared resolved on 0.4.5.
- Delayed ICY metadata can still cause a stream reconnect. 0.4.5 does not add
  detailed screen explanations for these pauses; those are deferred to 0.4.6.
  There are no voice announcements.
- The 0.4.5 factory image and setup from an erased flash, the browser update
  from the installation page, station changes and pairing a new speaker were
  not tested on the release bytes. Tests of other Core2 revisions, the full
  directory, battery duration and optional sensors are also pending for 0.4.5.
- All nine selected stations use MP3 paths from the 236-entry directory.
  Endpoint availability can change independently of a firmware release.
  AAC, HLS and Bluetooth LE Audio-only speakers are not supported.
- Official station logos are not publicly bundled without permission.
