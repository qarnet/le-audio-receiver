#!/usr/bin/env bash
# T7 Stage 1: honest native coverage runner for le-audio-receiver.
#
# Builds every Twister/native suite under tests/unit/ (testcase.yaml,
# exactly as test-all.sh discovers them) plus the four exec-only suites
# (audio_offload, flpr_audio_process, flpr_ring, offload_asrc) with
# CONFIG_COVERAGE=y, runs each native executable to normal exit so host
# libgcov writes .gcda, then produces gcovr 8.x reports.
#
# Native coverage semantics (NCS v3.3.0): CONFIG_COVERAGE on a NATIVE_BUILD
# selects COVERAGE_NATIVE_GCOV (host compiler --coverage).  COVERAGE_GCOV /
# COVERAGE_DUMP are hardware-platform options (depend on !NATIVE_BUILD) and
# are deliberately never set here.
#
# Stage 1: only --report-only is supported (no committed baseline yet).
#
# Usage:
#   scripts/test-coverage.sh --report-only --output DIR [--keep-builds]

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

REPORT_ONLY=0
OUTPUT_DIR=""
KEEP_BUILDS=0
TMP_ROOT=""

die() { echo "FATAL: $*" >&2; exit 1; }

usage() {
    cat >&2 <<'EOF'
Usage: scripts/test-coverage.sh --report-only --output DIR [--keep-builds]

  --report-only   allow report generation without a committed baseline
                  (required in Stage 1)
  --output DIR    report/artifact directory (created if missing)
  --keep-builds   keep per-suite build + trace directories (debugging only)
EOF
    exit 1
}

while [ $# -gt 0 ]; do
    case "$1" in
        --report-only) REPORT_ONLY=1 ;;
        --output)
            [ $# -ge 2 ] || die "--output requires a directory argument"
            OUTPUT_DIR="$2"
            shift
            ;;
        --keep-builds) KEEP_BUILDS=1 ;;
        -h|--help) usage ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

[ "$REPORT_ONLY" -eq 1 ] || die "Stage 1 requires --report-only (no committed baseline yet)"
[ -n "$OUTPUT_DIR" ] || die "--output DIR is required"

command -v gcovr >/dev/null 2>&1 || die "gcovr not found — add pkgs.gcovr to flake.nix and re-enter the dev shell"
command -v gcov >/dev/null 2>&1 || die "gcov not found in dev shell"
command -v west >/dev/null 2>&1 || die "west not found in dev shell"

# Resolve ZEPHYR_BASE if not set — same policy as test-all.sh.
if [ -z "${ZEPHYR_BASE:-}" ]; then
    if command -v nrfutil &>/dev/null; then
        eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" 2>/dev/null || true
    fi
fi
: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set or nrfutil must be in PATH}"
echo "ZEPHYR_BASE=${ZEPHYR_BASE}"

cd "$REPO_ROOT"
[ -d .git ] || die "not a git checkout: $REPO_ROOT"

SOURCE_COMMIT="$(git rev-parse HEAD)"

mkdir -p "$OUTPUT_DIR/html" "$OUTPUT_DIR/traces" "$OUTPUT_DIR/logs"
TMP_ROOT="$(mktemp -d)" || die "mktemp failed"
if [ "$KEEP_BUILDS" -eq 1 ]; then
    trap 'echo "builds kept at: $TMP_ROOT"' EXIT
else
    trap 'rm -rf "$TMP_ROOT"' EXIT
fi
TRACE_DIR="$TMP_ROOT/traces"
mkdir -p "$TRACE_DIR"

# ---------- suite discovery (mirrors test-all.sh) ----------
twister_suites=()
for d in tests/unit/*/; do
    if [ -f "$d/testcase.yaml" ]; then
        twister_suites+=("$(basename "$d")")
    fi
done
exec_suites=(audio_offload flpr_audio_process flpr_ring offload_asrc)
all_suites=("${twister_suites[@]}" "${exec_suites[@]}")
[ ${#all_suites[@]} -gt 0 ] || die "no suites discovered"

GCOVR="$(command -v gcovr)"
GCOV="$(command -v gcov)"
GCOVR_VERSION="$("$GCOVR" --version | head -n1)"
GCOV_VERSION="$("$GCOV" --version | head -n1)"
PARSE_ERROR_FLAGS=(
    --gcov-ignore-parse-errors=negative_hits.warn_once_per_file
    --gcov-ignore-parse-errors=suspicious_hits.warn_once_per_file
)

# gcovr 8.x json report/tracefile output — full function records included.
gcovr_json() { # outfile [extra args...]
    local out="$1"
    shift
    echo "=== gcovr trace: $* ===" >>"$OUTPUT_DIR/gcovr.log"
    "$GCOVR" --root "$REPO_ROOT" --gcov-executable "$GCOV" \
        --merge-mode-functions=separate \
        "${PARSE_ERROR_FLAGS[@]}" \
        --filter "$REPO_ROOT/src/" \
        "$@" --json="$out" >>"$OUTPUT_DIR/gcovr.log" 2>&1
}

# ---------- build + run every suite, collect per-build traces ----------
for suite in "${all_suites[@]}"; do
    build_dir="$TMP_ROOT/build_$suite"
    echo "=== [$suite] build + run ==="
    env NIX_HARDENING_ENABLE="" \
        west build --no-sysbuild -b native_sim/native/64 \
        -d "$build_dir" "$REPO_ROOT/tests/unit/$suite" \
        -p -- -DCONFIG_COVERAGE=y >"$TMP_ROOT/$suite.build.log" 2>&1 \
        || die "build failed for $suite — see $TMP_ROOT/$suite.build.log"

    gcno_count="$(find "$build_dir" -name '*.gcno' | wc -l)"
    [ "$gcno_count" -gt 0 ] \
        || die "no .gcno produced for $suite — coverage instrumentation missing"
    gcda_count="$(find "$build_dir" -name '*.gcda' | wc -l)"
    if [ "$gcda_count" -gt 0 ]; then
        die "pre-existing .gcda in pristine build for $suite"
    fi

    "$build_dir/zephyr/zephyr.exe" >"$TMP_ROOT/$suite.run.log" 2>&1 \
        || die "native run failed for $suite — see $TMP_ROOT/$suite.run.log"

    gcda_count="$(find "$build_dir" -name '*.gcda' | wc -l)"
    [ "$gcda_count" -gt 0 ] \
        || die "no .gcda after run for $suite — host libgcov did not write data"

    gcovr_json "$TRACE_DIR/trace_$suite.json" \
        --object-directory "$build_dir" \
        || die "gcovr trace failed for $suite"

    cp "$TRACE_DIR/trace_$suite.json" "$OUTPUT_DIR/traces/"
    cp "$TMP_ROOT/$suite.build.log" "$OUTPUT_DIR/logs/$suite.build.log"
    cp "$TMP_ROOT/$suite.run.log" "$OUTPUT_DIR/logs/$suite.run.log"

    echo "  ok (trace: $TRACE_DIR/trace_$suite.json)"
done

# ---------- merge all traces into final reports ----------
echo "=== merging $(find "$TRACE_DIR" -name 'trace_*.json' | wc -l) traces ==="
merge_args=()
for t in "$TRACE_DIR"/trace_*.json; do
    merge_args+=(--add-tracefile "$t")
done

"$GCOVR" --root "$REPO_ROOT" --gcov-executable "$GCOV" \
    --merge-mode-functions=separate "${PARSE_ERROR_FLAGS[@]}" \
    --filter "$REPO_ROOT/src/" \
    "${merge_args[@]}" \
    --json="$OUTPUT_DIR/coverage.json" \
    --json-summary="$OUTPUT_DIR/coverage-summary.json" \
    --txt="$OUTPUT_DIR/coverage.txt" \
    --html-details="$OUTPUT_DIR/html/index.html" \
    >"$OUTPUT_DIR/gcovr-merge.log" 2>&1 \
    || die "gcovr merge failed — see $OUTPUT_DIR/gcovr-merge.log"

# ---------- run manifest ----------
if ! python3 - "$OUTPUT_DIR" "$SOURCE_COMMIT" "$GCOVR_VERSION" "$GCOV_VERSION" "$REPO_ROOT" <<'PYEOF'
import json, os, sys, glob

out_dir, commit, gcovr_ver, gcov_ver, repo = sys.argv[1:6]

# Re-derive suite list deterministically from the same discovery rules.
twister = sorted(os.path.basename(d) for d in glob.glob(os.path.join(repo, "tests/unit/*/"))
                 if os.path.isfile(os.path.join(d, "testcase.yaml")))
exec_only = ["audio_offload", "flpr_audio_process", "flpr_ring", "offload_asrc"]
suites = twister + exec_only

manifest = {
    "tool": "test-coverage.sh (T7 Stage 1)",
    "source_commit": commit,
    "gcovr_version": gcovr_ver,
    "gcov_version": gcov_ver,
    "gcovr_merge_mode_functions": "separate",
    "filter": [os.path.join(repo, "src/")],
    "exclusions": {
        "numeric_population": [
            "src/main.c",
            "src/bt_bap.c",
            "src/flpr/main.c",
            "src/audio_clock_actuator_sample_adjust.c",
        ],
        "outside_numeric_population": ["dongle/hci_ipc/src/main.c"],
        "note": "exclusions follow pre-refactor-testing-t7-stage1-handoff.md section 3",
    },
    "suites": [{"name": s, "kind": "exec-only" if s in exec_only else "twister",
                "command": "west build --no-sysbuild -b native_sim/native/64 -d BUILD tests/unit/%s -p -- -DCONFIG_COVERAGE=y; BUILD/zephyr/zephyr.exe" % s,
                "status": "ok", "config": "CONFIG_COVERAGE=y"} for s in suites],
    "outputs": ["coverage.json", "coverage-summary.json", "coverage.txt",
                "html/index.html", "traces/", "logs/", "gcovr.log", "gcovr-merge.log"],
}
with open(os.path.join(out_dir, "run-manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2, sort_keys=True)
    f.write("\n")
print("run-manifest.json written")
PYEOF
then
    die "run-manifest generation failed"
fi

# ---------- summary ----------
echo ""
echo "=== coverage summary ($OUTPUT_DIR) ==="
python3 - "$OUTPUT_DIR/coverage-summary.json" <<'EOF' || die "summary print failed"
import json, sys
d = json.load(open(sys.argv[1]))
if "totals" in d:
    totals = d["totals"]
    for metric in ("lines", "branches", "functions"):
        m = totals.get(metric, {})
        print(f"{metric}: {m.get('covered', 0)}/{m.get('count', 0)} ({m.get('percent', 0.0):.1f}%)")
else:
    # gcovr 8.x json-summary: per-file records, totals must be summed.
    agg = {"lines": [0, 0], "branches": [0, 0], "functions": [0, 0]}
    for f in d.get("files", []):
        agg["lines"][0] += f.get("line_covered", 0)
        agg["lines"][1] += f.get("line_total", 0)
        agg["branches"][0] += f.get("branch_covered", 0)
        agg["branches"][1] += f.get("branch_total", 0)
        agg["functions"][0] += f.get("function_covered", 0)
        agg["functions"][1] += f.get("function_total", 0)
    for metric, (covered, total) in agg.items():
        pct = 100.0 * covered / total if total else 0.0
        print(f"{metric}: {covered}/{total} ({pct:.1f}%)")
EOF

echo ""
echo "Reports:"
echo "  $OUTPUT_DIR/coverage.json"
echo "  $OUTPUT_DIR/coverage-summary.json"
echo "  $OUTPUT_DIR/coverage.txt"
echo "  $OUTPUT_DIR/html/index.html"
echo "  $OUTPUT_DIR/run-manifest.json"
