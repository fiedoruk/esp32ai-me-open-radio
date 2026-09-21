# Local network onboarding prototype

This directory is the setup portal the radio serves from its own access point.
Opened from any static file server it runs against synthetic data.

Off the device the page falls back to synthetic networks and in-memory
state. On the device it calls only the local `/api/networks` and
`/api/config-form` routes. It never calls a remote API, writes browser storage or
retains the password after the connect action. Open networks remain blocked.
