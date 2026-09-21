# Runtime orchestration

This directory owns the software-only S9 application runtime contract.

- `runtime-contract.json` is the source of queue, timer, diagnostic and soak limits.
- `orchestrator.mjs` is the host reference used by the simulator and generators.
- `soak.mjs` creates and evaluates deterministic fault timelines.
- `diagnostics.mjs` emits fixed-field PL/EN operator summaries without network,
  endpoint, credential, MAC/BSSID or artwork data.

The generated C++ contract and soak vectors are compiled into host tests and the
M5Stack Core2 candidate. They prove policy parity only; they do not start a real
network, stream, Bluetooth session, NVS write or hardware timer.
