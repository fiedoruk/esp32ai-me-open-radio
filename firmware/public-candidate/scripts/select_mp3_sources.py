Import("env")

import json
import os
import re
import subprocess
from pathlib import Path


repository_root = Path(env.subst("$PROJECT_DIR")).resolve().parents[1]


def resolve_build_sha(root: Path) -> str:
    """The commit this binary will carry, and the reason it can be rebuilt.

    Git is the source when a repository is present. A corresponding-source
    tarball has no .git by policy, so it carries the same commit in a plain
    file written at the code cut. Without that fallback the published sources
    cannot rebuild the published binary at all: the pre-script dies before the
    compiler starts, which is exactly what a reader exercising GPL section 6
    would hit first.
    """
    recorded = root / "release" / "CODE-CUT-COMMIT"
    override = os.environ.get("OPEN_RADIO_BUILD_SHA")
    if override:
        candidate = override.strip()
    elif (root / ".git").exists():
        candidate = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
        ).strip()
    elif recorded.is_file():
        candidate = recorded.read_text(encoding="utf-8").strip()
    else:
        raise SystemExit(
            "No build commit: this tree has no .git and no release/CODE-CUT-COMMIT. "
            "A source package must carry the commit it was cut from."
        )
    if not re.fullmatch(r"[0-9a-f]{40}", candidate):
        raise SystemExit(f"Build commit is not a full sha1: {candidate!r}")
    return candidate


build_sha = resolve_build_sha(repository_root)
env.Append(CPPDEFINES=[("OPEN_RADIO_BUILD_SHA", f'\\"{build_sha}\\"')])

# Take the build machine's own path out of the binary.
#
# Every release up to and including 0.4 carried the absolute path of the
# directory it was compiled in, which is one person's home directory and their
# username, shipped to everyone who downloads the firmware. It also meant the
# published binary could only be reproduced by someone who happened to unpack
# the sources at the same path — a promise of reproducibility that quietly
# depended on a coincidence.
#
# Computed here rather than written into platformio.ini, because the whole
# point is that it maps THIS tree, wherever it is. A rebuilder unpacking the
# corresponding-source tarball anywhere on their disk gets the same rewrite and
# therefore the same bytes. A hardcoded path would fix the leak for one machine
# and leave the reproducibility half broken for every other.
env.Append(CCFLAGS=[f"-ffile-prefix-map={repository_root}=/open-radio"])

# The same rewrite for the PlatformIO core, resolved. When PLATFORMIO_CORE_DIR
# names the real toolchain directory, the compiler sees paths outside this tree
# and the map above never applies; 0.4.2 shipped the build account's home twice
# for exactly that reason. Mapped to the target the in-tree path already gets,
# so every rebuilder lands on the same bytes however the core was reached.
core_dir = Path(env.subst("$PROJECT_CORE_DIR")).resolve()
env.Append(CCFLAGS=[f"-ffile-prefix-map={core_dir}=/open-radio/.tools/platformio-core"])

expected_commit = "d6184e08977c0f15f1b09eeee119b238ce562383"
environment_name = env.subst("$PIOENV")
mp3_environments = {
    "core2-public-candidate",
    "core2-hardware-lab",
    "core2-owner-production",
    "core2-local-speaker-fallback",
}
hls_environments = {"core2-hardware-lab"}
a2dp_environments = {
    "core2-public-candidate",
    "core2-hardware-lab",
    "core2-owner-production",
}
audio_dependency_dir = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / environment_name
    / "ESP8266Audio"
)
manifest_path = audio_dependency_dir / "library.json"

if environment_name in mp3_environments:
    if not manifest_path.exists():
        raise RuntimeError(f"missing pinned ESP8266Audio dependency: {manifest_path}")

    actual_commit = subprocess.check_output(
        ["git", "-C", str(audio_dependency_dir), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual_commit != expected_commit:
        raise RuntimeError(
            f"ESP8266Audio commit mismatch: expected {expected_commit}, got {actual_commit}"
        )

    manifest = json.loads(manifest_path.read_text())
    source_filter = [
        "-<*>",
        "+<AudioFileSourceHTTPStream.cpp>",
        "+<AudioFileSourceICYStream.cpp>",
        "+<AudioGeneratorMP3.cpp>",
        "+<AudioLogger.cpp>",
        "+<libmad/*.c>",
    ]
    if environment_name in hls_environments:
        source_filter.extend([
            "+<AudioGeneratorAAC.cpp>",
            "+<libhelix-aac/*.c>",
        ])
    manifest["build"]["srcFilter"] = source_filter
    manifest_path.write_text(json.dumps(manifest, indent=4) + "\n")

    # The pinned ICY parser waits forever when a metadata block is truncated.
    # Apply a small, reproducible dependency-workspace patch: abort the socket
    # after 500 ms so the existing bounded reconnect path can recover and the
    # owner loop cannot freeze indefinitely.
    icy_source_path = audio_dependency_dir / "src" / "AudioFileSourceICYStream.cpp"
    icy_source = icy_source_path.read_text()
    deadline_declaration = (
        "        int mdSize;\n"
        "        const uint32_t metadataStartedAtMs = millis();"
    )
    if "metadataStartedAtMs" not in icy_source:
        icy_source = icy_source.replace(
            "        int mdSize;",
            deadline_declaration,
            1,
        )
        wait_pattern = re.compile(
            r"(?P<indent> +)if \(ret == 0\) \{\n"
            r"(?P=indent)    delay\(1\);\n"
            r"(?P=indent)    continue;\n"
            r"(?P=indent)\}"
        )

        def bound_metadata_wait(match):
            indent = match.group("indent")
            return (
                f"{indent}if (ret == 0) {{\n"
                f"{indent}    if (millis() - metadataStartedAtMs >= 500U) {{\n"
                f"{indent}        http.end();\n"
                f"{indent}        return read;\n"
                f"{indent}    }}\n"
                f"{indent}    delay(1);\n"
                f"{indent}    continue;\n"
                f"{indent}}}"
            )

        icy_source, replacements = wait_pattern.subn(
            bound_metadata_wait, icy_source
        )
        if replacements != 3:
            raise RuntimeError(
                f"unexpected ICY metadata wait-loop count: {replacements}"
            )
        icy_source_path.write_text(icy_source)
    # Second workspace patch (audit 2026-09-08). The
    # pinned parser zeroes only the first 16 bytes of its 273-byte metadata
    # buffer and then runs strstr/strchr over it — and WRITES a NUL wherever it
    # finds the terminator. A title with no ';' and no closing quote made those
    # searches leave the array on the task stack. Zero the whole buffer once and
    # terminate the valid range before parsing; upstream unchanged otherwise.
    icy_source = icy_source_path.read_text()
    if "memset(icyBuff, 0, 16);" in icy_source:
        icy_source = icy_source.replace(
            "memset(icyBuff, 0, 16); // Ensure no residual matches occur",
            "memset(icyBuff, 0, sizeof(icyBuff)); // Ensure no residual matches occur, and a NUL for the searches below",
            1,
        )
    terminator = "                icyBuff[lastValidByte < 272 ? lastValidByte : 272] = '\\0';\n"
    parse_marker = "                // Buffer now contains StreamTitle=....., parse it\n"
    if terminator not in icy_source:
        if parse_marker not in icy_source:
            raise RuntimeError("ICY parse marker not found; cannot terminate metadata buffer")
        icy_source = icy_source.replace(parse_marker, parse_marker + terminator, 1)
    icy_source_path.write_text(icy_source)
    if ("memset(icyBuff, 0, sizeof(icyBuff))" not in icy_source_path.read_text() or
            terminator not in icy_source_path.read_text()):
        raise RuntimeError("failed to terminate pinned ICY metadata buffer")

    patched_icy_source = icy_source_path.read_text()
    if ("metadataStartedAtMs" not in patched_icy_source or
            patched_icy_source.count(
                "millis() - metadataStartedAtMs >= 500U") != 3):
        raise RuntimeError("failed to bound pinned ICY metadata reads")

    # Third workspace patch (audit of 2026-09-24). The pinned source asks
    # HTTP/1.1 and reads the raw socket; a server that answers chunked puts its
    # framing ("20a7\r\n" every ~8 KB) into the bytes the ICY counter and the MP3
    # decoder consume. 11 of 337 catalogue endpoints do; the catalogue itself was
    # admitted by measure-station-directory.mjs over HTTP/1.0, which has no
    # chunked coding. The radio now asks the way its catalogue was measured.
    icy_source = icy_source_path.read_text()
    http10_line = "    http.useHTTP10(true); // open_radio_http10\n"
    if http10_line not in icy_source:
        reuse_anchor = "    http.setReuse(true);\n"
        if icy_source.count(reuse_anchor) != 1:
            raise RuntimeError("ICY open() anchor not found; cannot force HTTP/1.0")
        icy_source = icy_source.replace(reuse_anchor, reuse_anchor + http10_line, 1)
        icy_source_path.write_text(icy_source)
    if icy_source_path.read_text().count(http10_line) != 1:
        raise RuntimeError("failed to force HTTP/1.0 on the pinned ICY source")

    # Fourth workspace patch (audit of 2026-09-24). `end` is already the count
    # of valid bytes, so "+ 1" carried one byte past them — a NUL since the whole
    # buffer is zeroed — and a title split across two reads stopped at the seam.
    title_line = "                int lastValidByte = end - (header - icyBuff); // open_radio_title_split\n"
    icy_source = icy_source_path.read_text()
    if title_line not in icy_source:
        title_anchor = "                int lastValidByte = end - (header - icyBuff) + 1;\n"
        if icy_source.count(title_anchor) != 1:
            raise RuntimeError("ICY title anchor not found; cannot fix the split-title count")
        icy_source = icy_source.replace(title_anchor, title_line, 1)
        icy_source_path.write_text(icy_source)
    if icy_source_path.read_text().count(title_line) != 1:
        raise RuntimeError("failed to fix the pinned ICY split-title count")

    # The pinned decoder's default PCM scratch fits AAC-LC but not the maximum
    # stereo SBR frame documented by Helix. Keep the upstream pin and apply the
    # minimal reproducible capacity correction in the build workspace.
    if environment_name in hls_environments:
        aac_source_path = audio_dependency_dir / "src" / "AudioGeneratorAAC.cpp"
        aac_source = aac_source_path.read_text()
        if "1024 * 2" in aac_source:
            aac_source = aac_source.replace("1024 * 2", "2048 * 2")
            aac_source_path.write_text(aac_source)
        if "1024 * 2" in aac_source_path.read_text():
            raise RuntimeError("failed to expand pinned AAC SBR output buffer")

a2dp_dependency_dir = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / environment_name
    / "ESP32-A2DP"
)
a2dp_manifest_path = a2dp_dependency_dir / "library.json"
a2dp_expected_commit = "b6da4744286ca15b6f1dee607c077a4747294dd4"
if environment_name in a2dp_environments:
    a2dp_actual_commit = subprocess.check_output(
        ["git", "-C", str(a2dp_dependency_dir), "rev-parse", "HEAD"], text=True
    ).strip()
    if a2dp_actual_commit != a2dp_expected_commit:
        raise RuntimeError(
            f"ESP32-A2DP commit mismatch: expected {a2dp_expected_commit}, got {a2dp_actual_commit}"
        )

    a2dp_manifest = (
        json.loads(a2dp_manifest_path.read_text())
        if a2dp_manifest_path.exists()
        else {
            "name": "ESP32-A2DP",
            "version": "1.8.11",
            "frameworks": "arduino",
            "platforms": "espressif32",
        }
    )
    a2dp_manifest.setdefault("build", {})["srcFilter"] = [
        "-<*>",
        "+<BluetoothA2DPCommon.cpp>",
        "+<BluetoothA2DPOutput.cpp>",
        "+<BluetoothA2DPSource.cpp>",
    ]
    a2dp_manifest_path.write_text(json.dumps(a2dp_manifest, indent=4) + "\n")


# The pinned PNG decoder can be made to spin forever on a crafted icon.
#
# lgfx_pngle.c drives tinfl in `do { ... } while (len);` and subtracts only what
# the decompressor consumed. Once the zlib stream inside the IDAT data has
# ended, tinfl answers TINFL_STATUS_DONE and consumes ZERO bytes, which is not
# an error, so `len` never reaches zero and the loop turns without yielding.
# Any byte placed after the end of the zlib stream therefore hangs the task:
# the idle task on that core starves and its watchdog resets the device.
#
# It is reachable from outside. Broadcaster icons are fetched with
# setInsecure() by deliberate policy (pinning certificates for eighty CDNs is
# unmaintainable), so anyone on the path, not only the site owner, can answer
# with such a file. Logos are re-fetched on every boot, so the result is a
# reboot loop rather than one reset. Our own pngChunksWellFormed() cannot see
# it: every chunk length and CRC in the crafted file is correct.
#
# Found by an adversarial pre-release review, 2026-09-09, and confirmed by
# reading this loop rather than by taking the report on trust. Fixed by leaving
# the inner loop when no progress is possible; the outer loop still drains the
# chunk, so trailing rubbish is skipped instead of decoded and a healthy image
# is unaffected.
graphics_dependency_dir = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR")) / environment_name / "M5GFX"
)
expected_graphics_commit = "ad9b814264d4e2000e9f30070002310bbccaffc9"
pngle_path = graphics_dependency_dir / "src" / "lgfx" / "utility" / "lgfx_pngle.c"
if pngle_path.exists():
    actual_graphics_commit = subprocess.check_output(
        ["git", "-C", str(graphics_dependency_dir), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual_graphics_commit != expected_graphics_commit:
        raise RuntimeError(
            f"M5GFX commit mismatch: expected {expected_graphics_commit}, "
            f"got {actual_graphics_commit}"
        )
    pngle_source = pngle_path.read_text()
    guard_marker = "open_radio_no_progress"
    if guard_marker not in pngle_source:
        anchor = (
            "          len -= in_bytes;\n"
            "          in_pos += in_bytes;\n"
        )
        if pngle_source.count(anchor) != 1:
            raise RuntimeError(
                "lgfx_pngle.c does not carry the expected IDAT accounting; "
                "the no-progress guard cannot be placed safely"
            )
        patched = (
            "          len -= in_bytes;\n"
            "          in_pos += in_bytes;\n"
            "          /* open_radio_no_progress: DONE with nothing consumed means the\n"
            "             zlib stream ended and these are trailing bytes. Without this\n"
            "             the `while (len)` below never terminates and never yields. */\n"
            "          if (in_bytes == 0 && status == TINFL_STATUS_DONE) { break; }\n"
        )
        pngle_path.write_text(pngle_source.replace(anchor, patched))
