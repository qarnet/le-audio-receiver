#!/usr/bin/env bash
# Regenerate checked-in LC3 decoder-history PCM reference traces.
#
# This reproducibility tool never changes source LC3 or lossless PCM corpus
# files. Default mode accepts only byte-identical stateful traces. An explicit
# rebase mode permits stateful trace differences after checked-in source and
# stateful manifest validation.

set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: bash tests/fixtures/lc3/generate_stateful_references.sh [--rebase-stateful]

Default mode requires checked-in and generated stateful PCM trace hashes to
match stateful-reference-manifest.json. --rebase-stateful permits generated
stateful trace SHA-256 differences only. It never permits source corpus
differences and never edits either manifest.
EOF
}

REBASE_STATEFUL=0
case "$#" in
    0)
        ;;
    1)
        if [ "$1" = "--rebase-stateful" ]; then
            REBASE_STATEFUL=1
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

PORTABLE_MANIFEST="$HERE/portable-oracle-manifest.json"
STATEFUL_MANIFEST="$HERE/stateful-reference-manifest.json"
SUPPORT_DIR="$HERE/../../support"

validate_inputs() {
    local trace_directory="$1"
    local trace_hash_mode="$2"

    python3 - "$PORTABLE_MANIFEST" "$STATEFUL_MANIFEST" "$HERE" "$trace_directory" \
        "$trace_hash_mode" <<'PY'
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
EXPECTED_RECIPES = (
    ("start8_10ms_l", "bsim_48k_10ms_120b_l", "portable-pcm", "bsim_48k_10ms_120b_l.pcm", 0, 10000, 120, 480, 108, 100,
     (("plc", 0, 8), ("corpus", 0, 100))),
    ("start8_10ms_r", "bsim_48k_10ms_120b_r", "portable-pcm", "bsim_48k_10ms_120b_r.pcm", 0, 10000, 120, 480, 108, 100,
     (("plc", 0, 8), ("corpus", 0, 100))),
    ("start11_7p5ms_l", "bsim_48k_7p5ms_90b_l", "portable-pcm", "bsim_48k_7p5ms_90b_l.pcm", 0, 7500, 90, 360, 111, 100,
     (("plc", 0, 11), ("corpus", 0, 100))),
    ("start11_7p5ms_r", "bsim_48k_7p5ms_90b_r", "portable-pcm", "bsim_48k_7p5ms_90b_r.pcm", 0, 7500, 90, 360, 111, 100,
     (("plc", 0, 11), ("corpus", 0, 100))),
    ("start13_7p5ms_l", "bsim_48k_7p5ms_90b_l", "portable-pcm", "bsim_48k_7p5ms_90b_l.pcm", 0, 7500, 90, 360, 113, 100,
     (("plc", 0, 13), ("corpus", 0, 100))),
    ("start13_7p5ms_r", "bsim_48k_7p5ms_90b_r", "portable-pcm", "bsim_48k_7p5ms_90b_r.pcm", 0, 7500, 90, 360, 113, 100,
     (("plc", 0, 13), ("corpus", 0, 100))),
    ("skip20_10ms_l", "bsim_48k_10ms_120b_l", "generated-pcm", "stateful_48k_10ms_skip20_l.pcm", 0, 10000, 120, 480, 108, 100,
     (("plc", 0, 8), ("corpus", 0, 20), ("corpus", 21, 80))),
    ("loss48x18_10ms_r", "bsim_48k_10ms_120b_r", "generated-pcm", "stateful_48k_10ms_loss48x18_r.pcm", 0, 10000, 120, 480, 108, 82,
     (("plc", 0, 8), ("corpus", 0, 48), ("plc", 0, 18), ("corpus", 48, 34))),
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


def require_int(value, label, lower=None, upper=None):
    if type(value) is not int:
        raise ValidationError("%s must be an integer" % label)
    if lower is not None and value < lower:
        raise ValidationError("%s is below range" % label)
    if upper is not None and value > upper:
        raise ValidationError("%s is above range" % label)
    return value


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_file(directory, name, label):
    relative = pathlib.PurePath(require_string(name, label))
    if relative.is_absolute() or len(relative.parts) != 1 or relative.name != name:
        raise ValidationError("%s must be a safe fixture filename" % label)
    candidate = (directory / relative).resolve(strict=False)
    if candidate.parent != directory or not candidate.is_file():
        raise ValidationError("%s fixture is missing or escapes fixture directory" % label)
    return candidate


def validate_binary(entry, label, directory, expected_name, expected_size, allow_hash_mismatch):
    require_keys(entry, ("path", "size", "sha256"), label)
    path_name = require_string(entry["path"], label + ".path")
    if path_name != expected_name:
        raise ValidationError("%s.path must be %s" % (label, expected_name))
    declared_size = require_int(entry["size"], label + ".size", 1)
    if declared_size != expected_size:
        raise ValidationError("%s.size does not match corpus geometry" % label)
    declared_hash = require_string(entry["sha256"], label + ".sha256")
    if SHA256_RE.fullmatch(declared_hash) is None:
        raise ValidationError("%s.sha256 must be lowercase SHA-256" % label)
    candidate = safe_file(directory, path_name, label + ".path")
    if candidate.stat().st_size != declared_size:
        raise ValidationError("%s fixture size mismatch" % label)
    actual_hash = sha256(candidate)
    if actual_hash != declared_hash and not allow_hash_mismatch:
        raise ValidationError("%s fixture SHA-256 mismatch" % label)
    return actual_hash != declared_hash


def load_json(path, label):
    try:
        raw = path.read_bytes()
        return raw, json.loads(raw.decode("utf-8"))
    except OSError as exc:
        raise ValidationError("cannot read %s: %s" % (label, exc)) from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValidationError("malformed %s: %s" % (label, exc)) from exc


def validate_portable(manifest, fixture_dir):
    require_keys(
        manifest,
        ("schema_version", "ncs_version", "liblc3", "generator_flags",
         "source_formula_identifier", "corpus_frame_count", "pcm_limits", "streams"),
        "portable manifest",
    )
    if require_int(manifest["schema_version"], "portable manifest.schema_version") != 2:
        raise ValidationError("unsupported portable manifest schema_version")
    if require_string(manifest["ncs_version"], "portable manifest.ncs_version") != "v3.3.0":
        raise ValidationError("portable manifest NCS version is not v3.3.0")
    require_keys(manifest["liblc3"], ("semantic_label", "west_revision"), "portable manifest.liblc3")
    if require_string(manifest["liblc3"]["semantic_label"], "portable manifest.liblc3.semantic_label") != "1.1.2":
        raise ValidationError("portable manifest liblc3 semantic label is not 1.1.2")
    if require_string(manifest["liblc3"]["west_revision"], "portable manifest.liblc3.west_revision") != "48bbd3eacd36e99a57317a0a4867002e0b09e183":
        raise ValidationError("portable manifest liblc3 west revision is not pinned revision")
    if type(manifest["generator_flags"]) is not list or tuple(manifest["generator_flags"]) != EXPECTED_FLAGS:
        raise ValidationError("portable manifest generator_flags do not match exact generator flags")
    if require_string(manifest["source_formula_identifier"], "portable manifest.source_formula_identifier") != "bsim_tx_hash_mix_seq_i_ch_v1":
        raise ValidationError("portable manifest source formula identifier is unknown")
    if require_int(manifest["corpus_frame_count"], "portable manifest.corpus_frame_count", 1) != 128:
        raise ValidationError("portable manifest corpus frame count is not 128")
    require_keys(manifest["pcm_limits"], ("max_abs_error", "max_rms_error", "min_correlation_q15"), "portable manifest.pcm_limits")
    require_int(manifest["pcm_limits"]["max_abs_error"], "portable manifest.pcm_limits.max_abs_error", 0, 65535)
    require_int(manifest["pcm_limits"]["max_rms_error"], "portable manifest.pcm_limits.max_rms_error", 0, 65535)
    require_int(manifest["pcm_limits"]["min_correlation_q15"], "portable manifest.pcm_limits.min_correlation_q15", -32768, 32767)
    if type(manifest["streams"]) is not list or len(manifest["streams"]) != len(EXPECTED_STREAMS):
        raise ValidationError("portable manifest must contain exactly four corpus streams")
    portable_pcm = {}
    for index, (entry, expected) in enumerate(zip(manifest["streams"], EXPECTED_STREAMS)):
        stem, duration_us, frequency_hz, channel, frame_bytes, samples_per_frame = expected
        label = "portable manifest.streams[%d]" % index
        require_keys(entry, ("stem", "duration_us", "frequency_hz", "channel", "frame_bytes", "samples_per_frame", "frame_count", "lc3", "pcm"), label)
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
        validate_binary(entry["lc3"], label + ".lc3", fixture_dir, stem + ".lc3", frame_bytes * 128, False)
        validate_binary(entry["pcm"], label + ".pcm", fixture_dir, stem + ".pcm", samples_per_frame * 128 * 2, False)
        portable_pcm[stem] = entry["pcm"]
    return portable_pcm


def validate_steps(steps, expected_steps, label):
    if type(steps) is not list or len(steps) != len(expected_steps):
        raise ValidationError("%s must have exact ordered steps" % label)
    action_count = 0
    valid_count = 0
    for index, (entry, expected) in enumerate(zip(steps, expected_steps)):
        expected_action, expected_first, expected_count = expected
        step_label = "%s[%d]" % (label, index)
        require_keys(entry, ("action", "first_sequence", "count"), step_label)
        action = require_string(entry["action"], step_label + ".action")
        first_sequence = require_int(entry["first_sequence"], step_label + ".first_sequence", 0, 127)
        count = require_int(entry["count"], step_label + ".count", 1, 128)
        if (action, first_sequence, count) != expected:
            raise ValidationError("%s does not match stateful recipe" % step_label)
        if action == "plc":
            if first_sequence != 0:
                raise ValidationError("%s PLC first_sequence must be zero" % step_label)
        elif action == "corpus":
            if count > 128 - first_sequence:
                raise ValidationError("%s corpus range escapes source stream" % step_label)
            valid_count += count
        else:
            raise ValidationError("%s action is unknown" % step_label)
        action_count += count
    return action_count, valid_count


def validate_reference(reference, label, fixture_dir, generated_dir, hash_mode, source_stem,
                       expected_kind, expected_path, expected_first_frame, expected_frame_count,
                       samples_per_frame, portable_pcm):
    require_keys(reference, ("kind", "path", "first_frame", "frame_count", "size", "sha256"), label)
    kind = require_string(reference["kind"], label + ".kind")
    path = require_string(reference["path"], label + ".path")
    first_frame = require_int(reference["first_frame"], label + ".first_frame", 0, 127)
    frame_count = require_int(reference["frame_count"], label + ".frame_count", 1, 128)
    if (kind, path, first_frame, frame_count) != (
            expected_kind, expected_path, expected_first_frame, expected_frame_count):
        raise ValidationError("%s does not match stateful reference metadata" % label)
    backing = {
        "path": reference["path"],
        "size": reference["size"],
        "sha256": reference["sha256"],
    }
    if kind == "portable-pcm":
        authoritative = portable_pcm[source_stem]
        require_keys(authoritative, ("path", "size", "sha256"), "portable manifest PCM authority")
        if reference["size"] != authoritative["size"] or reference["sha256"] != authoritative["sha256"]:
            raise ValidationError("%s does not match portable manifest authority" % label)
        validate_binary(backing, label, fixture_dir, expected_path, authoritative["size"], False)
        if frame_count > 128 - first_frame:
            raise ValidationError("%s portable frame range is invalid" % label)
        return False
    if kind != "generated-pcm":
        raise ValidationError("%s kind is unknown" % label)
    if first_frame != 0:
        raise ValidationError("%s generated first_frame must be zero" % label)
    return validate_binary(backing, label, generated_dir, expected_path,
                           frame_count * samples_per_frame * 2, hash_mode == "rebase")


def validate_stateful(manifest, portable_raw, fixture_dir, generated_dir, hash_mode, portable_pcm):
    require_keys(
        manifest,
        ("schema_version", "ncs_version", "liblc3", "generator_flags", "source_portable_manifest", "recipes"),
        "stateful manifest",
    )
    if require_int(manifest["schema_version"], "stateful manifest.schema_version") != 1:
        raise ValidationError("unsupported stateful manifest schema_version")
    if require_string(manifest["ncs_version"], "stateful manifest.ncs_version") != "v3.3.0":
        raise ValidationError("stateful manifest NCS version is not v3.3.0")
    require_keys(manifest["liblc3"], ("semantic_label", "west_revision"), "stateful manifest.liblc3")
    if require_string(manifest["liblc3"]["semantic_label"], "stateful manifest.liblc3.semantic_label") != "1.1.2":
        raise ValidationError("stateful manifest liblc3 semantic label is not 1.1.2")
    if require_string(manifest["liblc3"]["west_revision"], "stateful manifest.liblc3.west_revision") != "48bbd3eacd36e99a57317a0a4867002e0b09e183":
        raise ValidationError("stateful manifest liblc3 west revision is not pinned revision")
    if type(manifest["generator_flags"]) is not list or tuple(manifest["generator_flags"]) != EXPECTED_FLAGS:
        raise ValidationError("stateful manifest generator_flags do not match exact generator flags")
    source = manifest["source_portable_manifest"]
    require_keys(source, ("path", "size", "sha256"), "stateful manifest.source_portable_manifest")
    if require_string(source["path"], "stateful manifest.source_portable_manifest.path") != "portable-oracle-manifest.json":
        raise ValidationError("stateful manifest source portable manifest path is invalid")
    if require_int(source["size"], "stateful manifest.source_portable_manifest.size", 1) != len(portable_raw):
        raise ValidationError("stateful manifest source portable manifest size mismatch")
    source_hash = require_string(source["sha256"], "stateful manifest.source_portable_manifest.sha256")
    if SHA256_RE.fullmatch(source_hash) is None or source_hash != hashlib.sha256(portable_raw).hexdigest():
        raise ValidationError("stateful manifest source portable manifest SHA-256 mismatch")
    if type(manifest["recipes"]) is not list or len(manifest["recipes"]) != len(EXPECTED_RECIPES):
        raise ValidationError("stateful manifest must contain exactly eight recipes")
    mismatches = []
    for index, (entry, expected) in enumerate(zip(manifest["recipes"], EXPECTED_RECIPES)):
        (recipe_id, source_stem, reference_kind, reference_path, reference_first_frame,
         duration_us, frame_bytes, samples_per_frame, output_action_count,
         valid_frame_count, expected_steps) = expected
        label = "stateful manifest.recipes[%d]" % index
        require_keys(entry, ("id", "source_stem", "duration_us", "frame_bytes", "samples_per_frame", "output_action_count", "valid_frame_count", "steps", "reference"), label)
        for key, expected_value in (("id", recipe_id), ("source_stem", source_stem)):
            if require_string(entry[key], label + "." + key) != expected_value:
                raise ValidationError("%s does not match stateful recipe order or value" % (label + "." + key))
        for key, expected_value in (("duration_us", duration_us), ("frame_bytes", frame_bytes), ("samples_per_frame", samples_per_frame), ("output_action_count", output_action_count), ("valid_frame_count", valid_frame_count)):
            if require_int(entry[key], label + "." + key, 0) != expected_value:
                raise ValidationError("%s does not match stateful recipe geometry" % (label + "." + key))
        actions, valid = validate_steps(entry["steps"], expected_steps, label + ".steps")
        if actions != output_action_count or valid != valid_frame_count:
            raise ValidationError("%s replay accounting is invalid" % label)
        mismatch = validate_reference(
            entry["reference"], label + ".reference", fixture_dir, generated_dir, hash_mode,
            source_stem, reference_kind, reference_path, reference_first_frame,
            valid_frame_count, samples_per_frame, portable_pcm)
        if mismatch:
            mismatches.append(reference_path)
    if mismatches:
        print("REBASE STATEFUL: generated stateful SHA-256 differs from manifest: %s" % ", ".join(mismatches), file=sys.stderr)


def main():
    if len(sys.argv) != 6:
        raise ValidationError("internal validator arguments are invalid")
    portable_path = pathlib.Path(sys.argv[1]).resolve(strict=False)
    stateful_path = pathlib.Path(sys.argv[2]).resolve(strict=False)
    fixture_dir = pathlib.Path(sys.argv[3]).resolve(strict=True)
    trace_dir = pathlib.Path(sys.argv[4]).resolve(strict=True)
    hash_mode = sys.argv[5]
    if hash_mode not in ("strict", "rebase"):
        raise ValidationError("internal stateful hash mode is invalid")
    portable_raw, portable_manifest = load_json(portable_path, "portable manifest")
    _stateful_raw, stateful_manifest = load_json(stateful_path, "stateful manifest")
    portable_pcm = validate_portable(portable_manifest, fixture_dir)
    validate_stateful(stateful_manifest, portable_raw, fixture_dir, trace_dir, hash_mode,
                      portable_pcm)


try:
    main()
except ValidationError as exc:
    print("FATAL: %s" % exc, file=sys.stderr)
    raise SystemExit(1)
PY
}

validate_inputs "$HERE" strict

NCS="${NCS:-$HOME/ncs/v3.3.0}"
LC3="$NCS/modules/lib/liblc3"
EXPECTED_NRF_REVISION="ba167d9f3db4abbdc9b67887ca3ea66c64f2d956"
EXPECTED_ZEPHYR_REVISION="fd9204a02d52630660ce8d729945a4dd743feabf"
EXPECTED_LIBLC3_REVISION="48bbd3eacd36e99a57317a0a4867002e0b09e183"

verify_git_repository() {
    local directory="$1"
    local expected_revision="$2"
    local label="$3"
    local status_mode="$4"
    local revision
    local status

    if ! revision="$(git -C "$directory" rev-parse HEAD 2>/dev/null)"; then
        echo "FATAL: cannot verify $label Git revision at $directory" >&2
        exit 1
    fi
    if [ "$revision" != "$expected_revision" ]; then
        echo "FATAL: $label Git revision is not $expected_revision" >&2
        exit 1
    fi
    if [ "$status_mode" = "all" ]; then
        if ! status="$(git -C "$directory" status --porcelain --untracked-files=all --ignored 2>/dev/null)"; then
            echo "FATAL: cannot verify $label Git working tree at $directory" >&2
            exit 1
        fi
    else
        if ! status="$(git -C "$directory" status --porcelain --untracked-files=no 2>/dev/null)"; then
            echo "FATAL: cannot verify $label Git working tree at $directory" >&2
            exit 1
        fi
    fi
    if [ -n "$status" ]; then
        echo "FATAL: $label Git working tree is dirty at $directory" >&2
        exit 1
    fi
}

verify_ncs_workspace() {
    local manifest_path
    local manifest_file
    local zephyr_base
    local nrf_version

    if [ ! -f "$NCS/.west/config" ]; then
        echo "FATAL: NCS workspace config is missing at $NCS/.west/config" >&2
        exit 1
    fi
    if ! manifest_path="$(git config --file "$NCS/.west/config" --get manifest.path 2>/dev/null)" || \
       ! manifest_file="$(git config --file "$NCS/.west/config" --get manifest.file 2>/dev/null)" || \
       ! zephyr_base="$(git config --file "$NCS/.west/config" --get zephyr.base 2>/dev/null)"; then
        echo "FATAL: cannot verify NCS workspace config at $NCS/.west/config" >&2
        exit 1
    fi
    if [ "$manifest_path" != "nrf" ] || [ "$manifest_file" != "west.yml" ] || \
       [ "$zephyr_base" != "zephyr" ]; then
        echo "FATAL: NCS workspace config is not v3.3.0 layout" >&2
        exit 1
    fi
    if [ ! -f "$NCS/nrf/VERSION" ]; then
        echo "FATAL: NCS VERSION is missing at $NCS/nrf/VERSION" >&2
        exit 1
    fi
    nrf_version="$(tr -d '\r\n' < "$NCS/nrf/VERSION")"
    if [ "$nrf_version" != "3.3.0" ]; then
        echo "FATAL: NCS VERSION is not 3.3.0" >&2
        exit 1
    fi
    verify_git_repository "$NCS/nrf" "$EXPECTED_NRF_REVISION" "NCS nrf" tracked
    verify_git_repository "$NCS/zephyr" "$EXPECTED_ZEPHYR_REVISION" "NCS zephyr" tracked
}

if [ ! -f "$LC3/include/lc3.h" ]; then
    echo "FATAL: liblc3 module not found at $LC3" >&2
    exit 1
fi
verify_git_repository "$LC3" "$EXPECTED_LIBLC3_REVISION" "liblc3" all
verify_ncs_workspace

LC3_SOURCES=(
    attdet.c
    bits.c
    bwdet.c
    energy.c
    lc3.c
    ltpf.c
    mdct.c
    plc.c
    sns.c
    spec.c
    tables.c
    tns.c
)
LC3_SOURCE_PATHS=()
for source in "${LC3_SOURCES[@]}"; do
    if [ ! -f "$LC3/src/$source" ]; then
        echo "FATAL: liblc3 source is missing at $LC3/src/$source" >&2
        exit 1
    fi
    LC3_SOURCE_PATHS+=("$LC3/src/$source")
done
if [ ! -f "$SUPPORT_DIR/lc3_stateful_recipes.c" ] || \
   [ ! -f "$SUPPORT_DIR/lc3_stateful_recipes.h" ]; then
    echo "FATAL: stateful recipe support is missing at $SUPPORT_DIR" >&2
    exit 1
fi

CC="${CC:-cc}"
TMPBIN=""
TMP_OUTPUT_DIR=""

cleanup() {
    if [ -n "$TMPBIN" ]; then
        rm -f -- "$TMPBIN"
    fi
    if [ -n "$TMP_OUTPUT_DIR" ]; then
        rm -rf -- "$TMP_OUTPUT_DIR"
    fi
}

trap cleanup EXIT

TMPBIN="$(mktemp /tmp/le-audio-lc3-stateful-gen.XXXXXX)" || {
    echo "FATAL: mktemp failed" >&2
    exit 1
}
TMP_OUTPUT_DIR="$(mktemp -d /tmp/le-audio-lc3-stateful-fixtures.XXXXXX)" || {
    echo "FATAL: temporary output directory creation failed" >&2
    exit 1
}

"$CC" -O3 -std=c11 -ffast-math \
       -Wall -Wextra -Wdouble-promotion -Wvla -pedantic -Werror \
       -I "$LC3/include" \
        -I "$SUPPORT_DIR" \
        "$HERE/gen_stateful_references.c" \
        "$SUPPORT_DIR/lc3_stateful_recipes.c" \
        "${LC3_SOURCE_PATHS[@]}" \
       -lm -o "$TMPBIN"

"$TMPBIN" "$HERE" "$TMP_OUTPUT_DIR"

GENERATED_HASH_MODE="strict"
if [ "$REBASE_STATEFUL" -eq 1 ]; then
    GENERATED_HASH_MODE="rebase"
fi
validate_inputs "$TMP_OUTPUT_DIR" "$GENERATED_HASH_MODE"

STATEFUL_FILES=(
    stateful_48k_10ms_skip20_l.pcm
    stateful_48k_10ms_loss48x18_r.pcm
)

copy_rebased_stateful_files() {
    python3 - "$TMP_OUTPUT_DIR" "$HERE" "${STATEFUL_FILES[@]}" <<'PY'
import os
import pathlib
import stat
import sys
import tempfile


def write_staged(directory, name, payload, mode):
    descriptor, temporary = tempfile.mkstemp(prefix=".%s." % name, dir=directory)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, mode)
    except BaseException:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise
    return pathlib.Path(temporary)


def sync_directory(directory):
    descriptor = os.open(directory, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def main():
    if len(sys.argv) != 5:
        raise RuntimeError("internal stateful copy arguments are invalid")
    source_directory = pathlib.Path(sys.argv[1])
    destination_directory = pathlib.Path(sys.argv[2])
    names = tuple(sys.argv[3:])
    staged = {}
    backups = {}
    committed = []

    try:
        for name in names:
            source = source_directory / name
            destination = destination_directory / name
            if not source.is_file() or not destination.is_file():
                raise RuntimeError("stateful transaction source or destination is missing: %s" % name)
            mode = stat.S_IMODE(destination.stat().st_mode)
            staged[name] = write_staged(destination_directory, name, source.read_bytes(), mode)
            backups[name] = write_staged(destination_directory, name, destination.read_bytes(), mode)

        for name in names:
            os.replace(staged[name], destination_directory / name)
            committed.append(name)
        sync_directory(destination_directory)
    except BaseException:
        rollback_error = None
        for name in reversed(committed):
            try:
                os.replace(backups[name], destination_directory / name)
            except OSError as exc:
                rollback_error = exc
        if committed:
            try:
                sync_directory(destination_directory)
            except OSError as exc:
                rollback_error = rollback_error or exc
        if rollback_error is not None:
            raise RuntimeError("stateful transaction rollback failed: %s" % rollback_error)
        raise
    finally:
        for temporary in tuple(staged.values()) + tuple(backups.values()):
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass


try:
    main()
except (OSError, RuntimeError) as exc:
    print("FATAL: stateful reference transaction failed: %s" % exc, file=sys.stderr)
    raise SystemExit(1)
PY
}

if [ "$REBASE_STATEFUL" -eq 1 ]; then
    copy_rebased_stateful_files
    echo "REBASE STATEFUL: review and update stateful-reference-manifest.json and README.md SHA-256 records before committing." >&2
else
    echo "Stateful reference manifest hashes unchanged."
fi

echo "---"
sha256sum -- "${STATEFUL_FILES[@]}"
