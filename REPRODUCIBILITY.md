# Reproducibility

[Polska wersja](REPRODUCIBILITY.pl.md)

Source archives are bit-for-bit reproducible, and the released firmware is
reproducible byte-for-byte outside its provenance stamp when three build
parameters are honored. Measured on 0.2.1 (2026-07-23) and proved again by
execution on **0.4** (2026-07-31): the published image was rebuilt from a
commit later than its own code cut, by stamping the recorded cut, and the hash
matched. On **0.4.4** (2026-10-12) the published corresponding-source package,
unpacked outside the repository and built in a fresh environment, gave the
release application byte for byte. The same test on **0.4.5** (2026-10-27) gave
the release application and the factory image byte for byte.

1. **Dependency pins, by name.** Every library in
   `firmware/manifests/dependencies.lock.json` must resolve exactly once;
   M5GFX is pinned as a *named* entry
   (`M5GFX=https://github.com/m5stack/M5GFX.git#ad9b814…`, content-identical
   to registry 0.2.25).
2. **The build root path — neutralized as of 0.4.1, and again as of 0.4.4.**
   Two framework assert strings used to embed the absolute build directory, so
   an exact byte match required the same build root, and the published binary
   carried the compiling machine's home directory and username. The pre-build
   script passes `-ffile-prefix-map`, computed from whichever tree is being
   built, so those strings read `/open-radio/...`. 0.4.2 carries the toolchain
   path in those two strings again: its build ran with `PLATFORMIO_CORE_DIR`
   naming the toolchain's real directory, outside the tree the map covers. As
   of 0.4.4 the toolchain directory, with links resolved, is mapped to the same
   target, and the release build rejects an image that still carries a
   home-directory path. Four paths naming the machine that built the
   *upstream toolchain* remain inside precompiled libraries this project does
   not rebuild; they are identical for everyone using the pinned toolchain and
   therefore do not affect reproduction.
3. **The git commit stamp.** The build embeds `git rev-parse HEAD`; building
   the 0.2.1 sources from any other commit differs in exactly 104 bytes of
   provenance fields (app-descriptor ELF hash, the 40-character commit id,
   and the trailing image hash). With those fields excluded the images are
   byte-identical; building at the release tag itself yields full equality.

## Install the toolchain

```bash
scripts/bootstrap-firmware-toolchain.sh
```

The script creates an isolated Python environment from
`firmware/manifests/requirements-firmware.lock.txt`, installs the pinned
PlatformIO, CMake and Ninja into it and resolves the firmware libraries for
`firmware/public-candidate`. The `pio` used below is the one it installed,
`.tools/venv/bin/pio`.

## Reproduce a published release

The binary embeds the commit it was cut from, so the record naming that commit
cannot live inside it: writing the record moves `HEAD`. Rather than asking a
verifier to work out which commit to check out, the build stamps the recorded
cut explicitly.

```bash
git checkout open-radio-0-4      # the tag carries the record
OPEN_RADIO_BUILD_SHA=<code cut commit> \
  pio run -d firmware/public-candidate -e core2-public-candidate
```

`OPEN_RADIO_BUILD_SHA` is the code cut the release was taken from — the commit
the published image reports as its own. Build twice, require the two passes to
be byte-identical, then require the hash to equal the one published for that
release. Whether your sources are the right sources is proved by that hash —
which covers every file — rather than by `HEAD`, which covers none. Build from
a clean tree: a stamped binary built from uncommitted changes carries a
provenance claim nobody can check.

**The tag points at the commit carrying the record, not at the code cut.** The
two trees differ only in files that are never compiled, so they build the same
bytes; tagging the record is what makes a single `git checkout` enough.
