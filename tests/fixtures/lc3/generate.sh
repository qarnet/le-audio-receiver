#!/usr/bin/env bash
# Regenerate the checked-in LC3 fixtures (tests/fixtures/lc3/*.lc3, *.pcm).
#
# This is a REPRODUCIBILITY tool only.  The test suites never run it;
# they embed the checked-in binaries.  Run it from anywhere; it writes
# the fixture files next to this script and removes its temporary
# executable (mktemp + EXIT trap) afterwards.
#
# Prerequisite: NCS v3.3.0 installed at $HOME/ncs/v3.3.0 (override with
# NCS=/path/to/ncs).  Uses the host C compiler with the SAME relevant
# flags as the Zephyr liblc3 module build (-O3 -std=c11 -ffast-math) so
# the golden PCM is bit-exact against the native_sim production decoder.
#
# Usage:
#   bash tests/fixtures/lc3/generate.sh
#   bash tests/fixtures/lc3/generate.sh --rebase-portable
#
# Default mode validates the checked-in and temporary portable corpus against
# portable-oracle-manifest.json before copying anything. --rebase-portable is
# the only exception: it permits generated portable SHA-256 changes for an
# intentional reviewed rebase. Legacy fixtures are always protected.

set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: bash tests/fixtures/lc3/generate.sh [--rebase-portable]

Default mode requires checked-in and generated portable fixture hashes to match
portable-oracle-manifest.json. --rebase-portable permits only generated
portable SHA-256 mismatches; review and update the manifest and README hashes
afterward. Legacy fixture changes always fail.
EOF
}

REBASE_PORTABLE=0
case "$#" in
    0)
        ;;
    1)
        if [ "$1" = "--rebase-portable" ]; then
            REBASE_PORTABLE=1
        else
            usage
            exit 2
        fi
        ;;
    *)
        usage
        exit 2
        ;;
esac

HERE="$(cd -- "$(dirname "$0")" && pwd)"
cd "$HERE"

MANIFEST="$HERE/portable-oracle-manifest.json"

validate_portable_corpus() {
    local corpus_dir="$1"
    local hash_mode="$2"

    python3 - "$MANIFEST" "$corpus_dir" "$hash_mode" <<'PY'
import hashlib
import json
import pathlib
import re
import sys


EXPECTED_FLAGS = (
    "-O3",
    "-std=c11",
    "-ffast-math",
    "-Wall",
    "-Wextra",
    "-Wdouble-promotion",
    "-Wvla",
    "-pedantic",
    "-Werror",
)
EXPECTED_STREAMS = (
    ("bsim_48k_10ms_120b_l", 10000, 48000, "left", 120, 480),
    ("bsim_48k_10ms_120b_r", 10000, 48000, "right", 120, 480),
    ("bsim_48k_7p5ms_90b_l", 7500, 48000, "left", 90, 360),
    ("bsim_48k_7p5ms_90b_r", 7500, 48000, "right", 90, 360),
)
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


class ValidationError(RuntimeError):
    pass


def require_keys(value, expected, label):
    if type(value) is not dict:
        raise ValidationError("%s must be an object" % label)
    actual = set(value)
    expected = set(expected)
    unknown = sorted(actual - expected)
    missing = sorted(expected - actual)
    if unknown:
        raise ValidationError("%s has unknown fields: %s" % (label, ", ".join(unknown)))
    if missing:
        raise ValidationError("%s is missing fields: %s" % (label, ", ".join(missing)))


def require_string(value, label):
    if not isinstance(value, str) or not value:
        raise ValidationError("%s must be a non-empty string" % label)
    return value


def require_int(value, label, lower=None):
    if type(value) is not int:
        raise ValidationError("%s must be an integer" % label)
    if lower is not None and value < lower:
        raise ValidationError("%s is below range" % label)
    return value


def validate_pcm_limits(value):
    require_keys(
        value,
        ("max_abs_error", "max_rms_error", "min_correlation_q15"),
        "manifest.pcm_limits",
    )
    return {
        "max_abs_error": require_int(
            value["max_abs_error"], "manifest.pcm_limits.max_abs_error", 0
        ),
        "max_rms_error": require_int(
            value["max_rms_error"], "manifest.pcm_limits.max_rms_error", 0
        ),
        "min_correlation_q15": require_int(
            value["min_correlation_q15"], "manifest.pcm_limits.min_correlation_q15"
        ),
    }


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_binary(entry, label, stem, suffix, expected_size, corpus_dir, allow_hash_mismatch,
                    mismatches):
    require_keys(entry, ("path", "size", "sha256"), label)
    expected_name = stem + suffix
    declared_path = require_string(entry["path"], label + ".path")
    relative_path = pathlib.PurePath(declared_path)
    if (declared_path != expected_name or relative_path.is_absolute() or
            len(relative_path.parts) != 1 or relative_path.name != declared_path):
        raise ValidationError("%s.path must be safe fixture name %s" % (label, expected_name))
    declared_size = require_int(entry["size"], label + ".size", 1)
    if declared_size != expected_size:
        raise ValidationError("%s.size does not match corpus geometry" % label)
    declared_hash = require_string(entry["sha256"], label + ".sha256")
    if SHA256_RE.fullmatch(declared_hash) is None:
        raise ValidationError("%s.sha256 must be lowercase SHA-256" % label)

    candidate = (corpus_dir / relative_path).resolve(strict=False)
    if candidate.parent != corpus_dir or not candidate.is_file():
        raise ValidationError("%s fixture is missing or escapes fixture directory" % label)
    actual_size = candidate.stat().st_size
    if actual_size != declared_size:
        raise ValidationError("%s fixture size mismatch" % label)
    actual_hash = sha256(candidate)
    if actual_hash != declared_hash:
        if allow_hash_mismatch:
            mismatches.append(declared_path)
        else:
            raise ValidationError("%s fixture SHA-256 mismatch" % label)
    return declared_path


def main():
    if len(sys.argv) != 4:
        raise ValidationError("internal validator arguments are invalid")
    manifest_path = pathlib.Path(sys.argv[1]).resolve(strict=False)
    try:
        corpus_dir = pathlib.Path(sys.argv[2]).resolve(strict=True)
    except OSError as exc:
        raise ValidationError("cannot resolve portable corpus directory: %s" % exc) from exc
    hash_mode = sys.argv[3]
    if hash_mode not in ("strict", "rebase"):
        raise ValidationError("internal validator hash mode is invalid")

    try:
        raw = manifest_path.read_bytes()
        manifest = json.loads(raw.decode("utf-8"))
    except OSError as exc:
        raise ValidationError("cannot read portable manifest: %s" % exc) from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError("malformed portable manifest: %s" % exc) from exc

    require_keys(
        manifest,
        (
            "schema_version",
            "ncs_version",
            "liblc3",
            "generator_flags",
            "source_formula_identifier",
            "corpus_frame_count",
            "pcm_limits",
            "streams",
        ),
        "manifest",
    )
    if require_int(manifest["schema_version"], "manifest.schema_version") != 2:
        raise ValidationError("unsupported manifest schema_version")
    if require_string(manifest["ncs_version"], "manifest.ncs_version") != "v3.3.0":
        raise ValidationError("manifest NCS version is not v3.3.0")
    require_keys(manifest["liblc3"], ("semantic_label", "west_revision"), "manifest.liblc3")
    if require_string(manifest["liblc3"]["semantic_label"], "manifest.liblc3.semantic_label") != "1.1.2":
        raise ValidationError("manifest liblc3 semantic label is not 1.1.2")
    if require_string(manifest["liblc3"]["west_revision"], "manifest.liblc3.west_revision") != "48bbd3eacd36e99a57317a0a4867002e0b09e183":
        raise ValidationError("manifest liblc3 west revision is not pinned revision")
    if not isinstance(manifest["generator_flags"], list) or tuple(manifest["generator_flags"]) != EXPECTED_FLAGS:
        raise ValidationError("manifest generator_flags do not match exact generator flags")
    if require_string(manifest["source_formula_identifier"], "manifest.source_formula_identifier") != "bsim_tx_hash_mix_seq_i_ch_v1":
        raise ValidationError("manifest source formula identifier is unknown")
    if require_int(manifest["corpus_frame_count"], "manifest.corpus_frame_count", 1) != 128:
        raise ValidationError("manifest corpus frame count is not 128")
    pcm_limits = validate_pcm_limits(manifest["pcm_limits"])
    if pcm_limits["max_abs_error"] > 65535:
        raise ValidationError("manifest.pcm_limits.max_abs_error is above range")
    if pcm_limits["max_rms_error"] > 65535:
        raise ValidationError("manifest.pcm_limits.max_rms_error is above range")
    if not -32768 <= pcm_limits["min_correlation_q15"] <= 32767:
        raise ValidationError("manifest.pcm_limits.min_correlation_q15 is outside range")

    streams = manifest["streams"]
    if not isinstance(streams, list) or len(streams) != len(EXPECTED_STREAMS):
        raise ValidationError("manifest must contain exactly four corpus streams")

    allow_hash_mismatch = hash_mode == "rebase"
    expected_files = set()
    mismatches = []
    for index, (entry, expected) in enumerate(zip(streams, EXPECTED_STREAMS)):
        stem, duration_us, frequency_hz, channel, frame_bytes, samples_per_frame = expected
        label = "manifest.streams[%d]" % index
        require_keys(
            entry,
            (
                "stem",
                "duration_us",
                "frequency_hz",
                "channel",
                "frame_bytes",
                "samples_per_frame",
                "frame_count",
                "lc3",
                "pcm",
            ),
            label,
        )
        if require_string(entry["stem"], label + ".stem") != stem:
            raise ValidationError("%s stem has unexpected corpus order or value" % label)
        if require_int(entry["duration_us"], label + ".duration_us") != duration_us:
            raise ValidationError("%s duration does not match corpus geometry" % label)
        if require_int(entry["frequency_hz"], label + ".frequency_hz") != frequency_hz:
            raise ValidationError("%s frequency does not match corpus geometry" % label)
        if require_string(entry["channel"], label + ".channel") != channel:
            raise ValidationError("%s channel does not match corpus geometry" % label)
        if require_int(entry["frame_bytes"], label + ".frame_bytes") != frame_bytes:
            raise ValidationError("%s frame bytes do not match corpus geometry" % label)
        if require_int(entry["samples_per_frame"], label + ".samples_per_frame") != samples_per_frame:
            raise ValidationError("%s samples per frame do not match corpus geometry" % label)
        if require_int(entry["frame_count"], label + ".frame_count") != 128:
            raise ValidationError("%s frame count does not match corpus geometry" % label)
        expected_files.add(
            validate_binary(
                entry["lc3"],
                label + ".lc3",
                stem,
                ".lc3",
                frame_bytes * 128,
                corpus_dir,
                allow_hash_mismatch,
                mismatches,
            )
        )
        expected_files.add(
            validate_binary(
                entry["pcm"],
                label + ".pcm",
                stem,
                ".pcm",
                samples_per_frame * 128 * 2,
                corpus_dir,
                allow_hash_mismatch,
                mismatches,
            )
        )

    actual_files = set()
    for pattern in ("bsim_*.lc3", "bsim_*.pcm"):
        for candidate in corpus_dir.glob(pattern):
            if candidate.is_file() or candidate.is_symlink():
                actual_files.add(candidate.name)
    missing_files = sorted(expected_files - actual_files)
    extra_files = sorted(actual_files - expected_files)
    if missing_files or extra_files:
        parts = []
        if missing_files:
            parts.append("missing " + ", ".join(missing_files))
        if extra_files:
            parts.append("extra " + ", ".join(extra_files))
        raise ValidationError("portable fixture set mismatch: %s" % "; ".join(parts))

    if mismatches:
        print(
            "REBASE PORTABLE: generated portable SHA-256 differs from manifest: %s"
            % ", ".join(mismatches),
            file=sys.stderr,
        )

    return pcm_limits


try:
    main()
except ValidationError as exc:
    print("FATAL: %s" % exc, file=sys.stderr)
    raise SystemExit(1)
PY
}

validate_portable_corpus "$HERE" strict

NCS="${NCS:-$HOME/ncs/v3.3.0}"
LC3="$NCS/modules/lib/liblc3"

if [ ! -f "$LC3/include/lc3.h" ]; then
    echo "FATAL: liblc3 module not found at $LC3" >&2
    exit 1
fi

CC="${CC:-cc}"

LEGACY_FILES=(
    mono_48k_7p5ms_60b.lc3
    mono_48k_7p5ms_60b.pcm
    mono_48k_10ms_60b.lc3
    mono_48k_10ms_60b.pcm
    modeb_48k_7p5ms_60b.lc3
    modeb_48k_7p5ms_60b.pcm
    modeb_48k_10ms_60b.lc3
    modeb_48k_10ms_60b.pcm
)
PORTABLE_FILES=(
    bsim_48k_10ms_120b_l.lc3
    bsim_48k_10ms_120b_l.pcm
    bsim_48k_10ms_120b_r.lc3
    bsim_48k_10ms_120b_r.pcm
    bsim_48k_7p5ms_90b_l.lc3
    bsim_48k_7p5ms_90b_l.pcm
    bsim_48k_7p5ms_90b_r.lc3
    bsim_48k_7p5ms_90b_r.pcm
)
declare -A LEGACY_SHA256

for fixture in "${LEGACY_FILES[@]}"; do
    if [ ! -f "$fixture" ]; then
        echo "FATAL: legacy fixture missing: $fixture" >&2
        exit 1
    fi
    LEGACY_SHA256["$fixture"]="$(sha256sum -- "$fixture" | cut -d ' ' -f1)"
done

GENERATED_HASH_MODE="strict"
if [ "$REBASE_PORTABLE" -eq 1 ]; then
    GENERATED_HASH_MODE="rebase"
fi

TMPBIN="$(mktemp /tmp/le-audio-lc3-gen.XXXXXX)" || {
    echo "FATAL: mktemp failed" >&2
    exit 1
}
TMP_OUTPUT_DIR="$(mktemp -d /tmp/le-audio-lc3-fixtures.XXXXXX)" || {
    echo "FATAL: temporary output directory creation failed" >&2
    exit 1
}
trap 'rm -f "$TMPBIN"; rm -rf "$TMP_OUTPUT_DIR"' EXIT

# Same relevant flags as zephyr/modules/liblc3/CMakeLists.txt. -Wno-array-bounds
# is intentionally NOT passed. -Werror requires direct host compilation to be
# warning-free instead of allowing a warning to pass unnoticed.
"$CC" -O3 -std=c11 -ffast-math \
       -Wall -Wextra -Wdouble-promotion -Wvla -pedantic -Werror \
       -I "$LC3/include" \
       gen_fixtures.c "$LC3"/src/*.c \
       -lm -o "$TMPBIN"

(
    cd "$TMP_OUTPUT_DIR"
    "$TMPBIN"
)

for fixture in "${LEGACY_FILES[@]}"; do
    if [ ! -f "$TMP_OUTPUT_DIR/$fixture" ]; then
        echo "FATAL: generated legacy fixture missing: $fixture" >&2
        exit 1
    fi
    generated_hash="$(sha256sum -- "$TMP_OUTPUT_DIR/$fixture" | cut -d ' ' -f1)"
    if [ "$generated_hash" != "${LEGACY_SHA256[$fixture]}" ]; then
        echo "FATAL: legacy fixture changed: $fixture" >&2
        exit 1
    fi
done

validate_portable_corpus "$TMP_OUTPUT_DIR" "$GENERATED_HASH_MODE"

for fixture in "${PORTABLE_FILES[@]}"; do
    cp -- "$TMP_OUTPUT_DIR/$fixture" "$HERE/$fixture"
done

echo "Legacy fixture hashes unchanged."
if [ "$REBASE_PORTABLE" -eq 1 ]; then
    echo "REBASE PORTABLE: review and update portable-oracle-manifest.json and README.md SHA-256 records before committing." >&2
else
    echo "Portable corpus manifest hashes unchanged."
fi

echo "---"
sha256sum ./*.lc3 ./*.pcm
