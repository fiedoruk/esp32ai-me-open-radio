# ESP-ADF reference lane

This lane validates Espressif's maintained HTTP → MP3 → A2DP Source topology.
It is not the public firmware candidate and is never packaged into the public
release rehearsal.

The build uses ESP-ADF `v2.8` at commit
`d0493218b6986e5663a5465b6e2545a336ef901d` and container image digest
`sha256:9352fff95ecee99e953b8fdd4949cda6baeeae76fe23c330dca1133ef92679f9`.
The exact submodule pins are in `../manifests/toolchains.lock.json`.

Run `scripts/build-adf-reference.sh`. The script clones only the pinned source
when the ignored project cache is missing, builds the official example and
does not flash, upload or publish anything.

ESP-ADF brings a much larger graph and prebuilt codec/service components. It
remains private reference evidence until every binary component and license is
independently cleared for the exact artifact.
