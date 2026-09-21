# Network endpoints

The firmware has no account, outbound analytics, telemetry upload, OTA or
project-catalog endpoint. Normal operation does not contact project
infrastructure. The current device firmware contacts broadcaster/resolver
surfaces for playback and may use NTP for the visible clock. Location and
weather adapters exist only in the host-side autonomy model; they are not linked
into the current device firmware. Any future device integration must remain
optional and cannot block radio boot or playback.

## Expected endpoint classes

| Purpose | Owner | Current device firmware |
|---|---|---|
| Wi-Fi DHCP/DNS | local network | required |
| Radio player/resolver APIs | station operators | required for live resolution; local LKG/fallback remains available |
| Radio audio streams | station operators/CDNs | required for playback |
| IP location approximation | ipwho.is | not integrated; host-side model only |
| Weather forecast | api.open-meteo.com | not integrated; host-side model only |
| Time synchronization | pool.ntp.org | optional; never a playback dependency |

Current firmware-bound hosts are `api.rmfon.pl` for CA-verified RMF discovery,
`playerservices.streamtheworld.com` for operator MP3 redirectors,
`ic1.smcdn.pl`/`ic2.smcdn.pl` for the two direct ESKA MP3 mounts and
`rs102-krk.rmfstream.pl`/`rs202-krk.rmfstream.pl` as last-resort RMF mounts.
Hosts reached through a StreamTheWorld or RMF discovery response remain
transient and are not embedded. The four direct edge hosts are explicit,
unauthenticated HTTP fallbacks; they may drift and must be requalified or
replaced with an operator resolver when their behavior changes.

## Audited operator surfaces

- `api.rmfon.pl`, `rmfon.pl`
- `player.radiozet.pl`, `player.chillizet.pl`
- `audycje.tokfm.pl`, `radiostream.pl`, `tuba.pl`
- `player.voxfm.pl`
- `player.eska.pl`

The catalog remains the source of official operator surfaces. Firmware-specific
direct fallbacks are reviewed in `firmware/manifests/network-services.lock.json`.
New temporary CDN edges must not be hard-coded merely because one live probe
succeeds; a direct fallback needs an explicit owner, rotation/recovery behavior
and a recorded removal condition.

Since 0.2 the firmware optionally fetches station favicons at runtime: after
Wi-Fi connects (off the audio path, before Bluetooth starts), it downloads the
catalog-listed icon URLs — bounded to 96 KiB per icon, PNG only, cached on
SPIFFS, with project-original monograms as the permanent fallback. Redirects
are followed; TLS transport integrity is deliberately waived for this single
decorative fetch (the decoder accepts only sane PNG and the icon is drawn,
never executed). A failed or absent icon never affects playback. The firmware
additionally prefetches the RMF discovery pools right after Wi-Fi connects
(`api.rmfon.pl`), so an in-session station switch can reuse a cached pool
instead of opening TLS next to live audio.

The complete outbound classes are therefore: DHCP/DNS, station audio
streams/CDNs, operator resolver APIs (including the rmfon discovery above),
optional SNTP, and the optional favicon fetches. There are no accounts, no
telemetry and no project-operated backend.

Wall-clock time is not required for boot, playback, retry or recovery.
Recovery timers continue to use monotonic device time. The hardware
stabilization candidate uses optional SNTP to set the user-visible clock and
Core2 RTC after approved Wi-Fi connects. Failure leaves the last RTC value and
hides time-sensitive enhancements; it never stops audio.

## S19 host adapters

The bounded software-only implementation lives in
`autonomy/autonomy-layer.mjs`. It uses `location/ip-location-adapter.mjs`,
`weather/open-meteo-adapter.mjs` and `time/time-sync-supervisor.mjs`. Fresh and
stale cache windows, retry counts and failure behavior are frozen in
`ui-contract/autonomy/autonomy-layer.v1.json`. Provider failures return a
degraded result instead of throwing into playback state.

The source candidate now binds the local onboarding portal, approved Wi-Fi
profiles, RMF discovery and the MP3 playback path. RMF discovery authenticates
`api.rmfon.pl` with the USERTrust ECC root CA. Public MP3 audio currently uses
operator redirectors over HTTP because the pinned ICY source does not support a
TLS client; audio is neither private nor executable, but transport integrity and
Wi-Fi/Bluetooth coexistence still require the physical Core2 gate.

## Host-model automatic configuration data flow

The following flow is executable in the host-side S19 model, not in the current
Core2 firmware. It defines the privacy boundary for a possible future bounded
integration; it is not a claim that the device currently makes these requests.

After an approved secured Wi-Fi profile reaches the internet, the radio may
call `https://ipwho.is/` without an account or API key. The service necessarily
sees the request's public IP and returns an approximate city, coordinates,
country and timezone. The result is cached locally per approved network profile
and refreshed only for a new profile, after 30 days or on explicit retry.

Weather calls `https://api.open-meteo.com/v1/forecast` directly. The provider
necessarily sees the request IP and coordinates in the query. No result passes
through a project backend and no listening history, device identifier, SSID,
password or Bluetooth identity is sent.

Optional Wi-Fi positioning can improve accuracy if an approved provider exists.
It is not enabled in the public default because available services may require
an API key, billing and transmission of nearby BSSIDs. The location contract
supports that source without embedding a shared secret in firmware.

The canonical catalog is `ui-contract/catalog/station-catalog.v1.json`. It stores
official discovery surfaces only. Host-model resolver traces may contain
station ID, candidate ID, outcome, score and retry time. Current device
telemetry does not yet implement per-candidate scoring. Neither form may contain
resolved URLs, query strings, temporary CDN hosts or user network identifiers.

The UI contract reserves diagnostic fields for source, locality, accuracy and
result age, but the current device firmware does not populate them from a live
location/weather lookup. Any local diagnostic display and all future public or
community exports must preserve the redaction boundary: never exact coordinates,
public IP, BSSID, credentials or full resolved URLs.

## Prohibited endpoint classes

- project analytics,
- listening-history upload,
- device fingerprinting,
- credential collection,
- audio proxy or relay,
- remote shell or remote code execution.
- firmware/catalog update polling.
