# Firmware service contracts

The common C++17 layer is platform-neutral and contains no Wi-Fi credentials,
network calls, Arduino storage writes or device assumptions.

- `firmware_contract.hpp` owns PCM, output routing and the runtime supervisor.
- `service_contracts.hpp` owns compact device DTOs and bounded enums.
- `service_adapters.hpp` owns A/B storage, known-network selection, MP3 resolver
  policy, discovery response validation, onboarding routes and UI projection.
- `service_golden_vectors.hpp` is generated from the canonical JS traces.

```bash
node scripts/generate-firmware-service-vectors.mjs --check
```

The host fake implements the same slot backend consumed by the NVS-shaped A/B
adapter. A future device backend may map `readSlot` and `writeSlot` to ESP NVS;
it must not change selection, checksum, commit-marker or safe-mode policy.
