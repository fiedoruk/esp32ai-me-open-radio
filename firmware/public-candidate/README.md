# Public candidate firmware lane

This target compiles the pinned Core2 HAL, local Wi-Fi onboarding portal,
CA-verified RMF resolver, ICY/MP3 decoder, A2DP Source and project-owned bounded
PCM routing in one Arduino/PlatformIO graph.

On empty storage the device opens the short-lived, WPA2-protected
`OpenRadio-Setup` access point with a random per-boot code rendered on the
device, then serves the bundled PL/EN portal. It stores only approved secured
2.4 GHz profiles in NVS, selects the strongest reachable remembered profile and
retries with bounded backoff. Unknown and open networks are never joined
automatically.

All nine default stations are executable over MP3/ICY: RMF FM, Radio ZET,
TOK FM, Antyradio, Radio ESKA, RMF MAXX, Radio Plus, Złote Przeboje and
Meloradio; the console can swap any of them for another catalog entry. The built-in speaker is the first
output. After healthy buffered local playback, Bluetooth starts automatically:
the remembered address has priority, otherwise bounded Class-of-Device
audio-rendering discovery runs without model-name profiles. The first compatible
candidate is remembered by address only after A2DP media starts; the local UI
scan remains a recovery/replacement action. A missing advertised name uses a
neutral local label and does not block standards-based discovery.

The entry point links the shared 320x240 RGB565 renderer and allocates its
framebuffer in PSRAM. It does not seed successful runtime facts. TLS, audio,
touch, RF coexistence and reconnect behavior require continued physical gates.
Optional SNTP updates only the visible clock and RTC; recovery policy keeps
using monotonic device time and has no wall-clock dependency. This lane makes no IP-location or weather requests: those adapters are a
host-side model only, and `NETWORK-ENDPOINTS.md` is the register of what the
firmware can actually put on the wire. The timezone is compiled in
(`CET-1CEST`), not resolved.

```bash
pio run -d firmware/public-candidate
```

The project uses a pinned ESP32 runtime through compatibility links under
`.tools/`, so reproducibility scripts work outside an interactive shell. Pinned
inputs live in `platformio.ini`. Do not run an upload target from this lane
without the separate hardware gate.

ESP32-A2DP `1.8.11` names the suspended state differently than the pinned
Arduino-ESP32 headers. The build contains one explicit compatibility define
mapping it to `ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND`; no dependency source is
silently modified.
