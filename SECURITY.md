# Security policy

## Current state

Public firmware releases 0.1, 0.2, 0.2.1, 0.4, 0.4.1 and the current 0.4.2 are
live; official binaries come only from `fiedoruk.pl/os/`. There is no OTA channel — a
fixed release can only reach devices through a manual reflash, so severity
assessments must assume affected devices stay on their installed version.

## Support window

Security fixes target the current public release (0.4.2). An end-of-support
date for the 0.4 line has not been declared yet and will be published here when
it is. There is no OTA channel: a fix reaches a device only through a manual
reflash from `fiedoruk.pl/os/`, so severity assessment must assume affected
devices stay on their installed version.

## Please report

- credential exposure,
- unsafe Wi-Fi onboarding,
- remote code execution,
- malicious or hijacked station-resolver responses,
- manual USB rescue image/rollback bypass if that path is accepted,
- memory corruption reachable from network input,
- privacy or unexpected telemetry issues.

Do not include Wi-Fi passwords, private MAC addresses or personal listening
data in a public issue. Report privately through GitHub private vulnerability
reporting (Security Advisories) at
[github.com/fiedoruk/open-radio/security/advisories](https://github.com/fiedoruk/open-radio/security/advisories),
or by e-mail to the address published in `/.well-known/security.txt` on
`fiedoruk.pl` and `esp32ai.me`. We acknowledge a report within 72 hours and
follow a coordinated disclosure policy: a fixed vulnerability is published once
a release carrying the fix is downloadable.

## Persistence boundary

Public configuration fixtures contain only opaque `wifi:*` and `bt:*`
references. They must never contain SSIDs, passwords, Bluetooth addresses,
tokens, precise location or resolved stream URLs. CRC32 is used only for
accidental corruption detection; it does not protect secrets. Firmware keeps
actual credentials in device-local NVS and redacts them from diagnostics. The
DIY first-bring-up profile does not enable secure boot or flash encryption:
credentials are not confidential against physical flash access. Keep the
device and private factory backup trusted and wipe the device before transfer
or service. Any irreversible eFuse hardening requires a separate post-H0 owner
gate and recovery design.

## Local onboarding boundary

The host portal in `network-onboarding/` is dependency-free and uses synthetic
networks outside the device. On Core2 it calls only same-origin device routes.
The embedded portal uses a per-boot 48-bit WPA2 access code, a separate per-boot
64-bit request token and AP-interface client binding. It has no remote request,
query-string credential transport or browser-storage write. Physical captive-
portal behavior and RF recovery remain hardware tests.

Station streams are a deliberate trust boundary. The station resolver is
reached over TLS with a pinned root certificate, but the stream address it
returns is downgraded from `https://` to plain `http://` before playback,
because the pinned ICY/MP3 path has no TLS client. The integrity gained in the
resolver handshake therefore does not extend to the audio bytes: an on-path
attacker can substitute the stream. Audio is neither private nor executable
(`NETWORK-ENDPOINTS.md`), and this downgrade is accepted knowingly rather than
overlooked.

## Station console boundary

After onboarding, the device opens a station console on the home network for a
window of at most 15 minutes.

**In 0.4.1 and everything before it, treat that window as trusted-LAN only.** The
console routes, including `POST /api/config-form`, check only which interface a
request arrived on, not which host sent it, and the CSRF token is issued by
`GET /api/session` without further verification. Any host on the same LAN can
therefore re-provision the radio onto another access point during the window. No
route returns Wi-Fi passwords or other credentials.

**In 0.4.2, the published release, both halves are closed.** Wi-Fi credentials are accepted only from a
client of the device's own setup access point, so `POST /api/config-form` over
the home network is refused; and every console route requires a `Host` header
naming this device, either `open-radio.local` or its current IPv4 address. The
owner-facing cost is stated plainly in the user guide: changing Wi-Fi now means
joining the radio's own access point, and a console bookmark that uses a
router-assigned name rather than the two the device itself advertises will be
refused.
