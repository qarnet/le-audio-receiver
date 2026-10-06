#!/usr/bin/env bash
# Honest native coverage runner for le-audio-receiver.
#
# Builds every C suite discovered by scripts/test_inventory.py (twister
# suites under tests/unit/ with testcase.yaml, plus exec-only suites with
# CMakeLists.txt but no testcase.yaml) with
# CONFIG_COVERAGE=y, runs each native executable to normal exit so host
# libgcov writes .gcda, then produces gcovr 8.x reports.
#
# Native coverage semantics (NCS v3.4.1): CONFIG_COVERAGE on a NATIVE_BUILD
# selects COVERAGE_NATIVE_GCOV (host compiler --coverage).  COVERAGE_GCOV /
# COVERAGE_DUMP are hardware-platform options (depend on !NATIVE_BUILD) and
# are deliberately never set here.
#
# Modes (exactly one):
#   --report-only              no baseline write/check; may run on a dirty
#                              worktree (manifest records dirty: true) and
#                              can never create/update a committed baseline
#   --write-baseline PATH      clean worktree required; writes a candidate
#                              baseline JSON (never the committed path)
#   (default) / --baseline PATH
#                              enforce the baseline (default committed
#                              tests/coverage-baseline.json); clean worktree
#                              required; exact HEAD recorded
#
# Enforcement uses integer cross multiplication
# (current_covered/current_total >= baseline_covered/baseline_total) for
# overall lines/branches and every per-file lines/branches/functions
# record.  Population drift (new or missing population file) is a hard
# failure until manifest/baseline are intentionally updated.
#
# Tool-version enforcement (baseline mode only): the current gcovr/gcov
# version first lines are compared with the baseline's recorded
# gcovr_version/gcov_version when those fields are present.  Old baselines
# that omit either field entirely remain accepted; a present version must
# be a non-empty string (null/non-string/empty fails).  A mismatch or an
# invalid present value is a hard error that directs the operator to
# refresh intentionally with --write-baseline.  --write-baseline always
# records the current versions; --report-only never enforces them.
#
# Usage:
#   scripts/test-coverage.sh (--report-only|--write-baseline PATH|--baseline PATH)
#                            --output DIR [--clean-output] [--keep-builds]

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

MODE=""
OUTPUT_DIR=""
BASELINE_PATH=""
CLEAN_OUTPUT=0
KEEP_BUILDS=0
TMP_ROOT=""

die() { echo "FATAL: $*" >&2; exit 1; }

usage() {
    cat >&2 <<'EOF'
Usage: scripts/test-coverage.sh MODE --output DIR [options]

Modes (exactly one):
  --report-only            no baseline write/check; dirty worktree allowed
  --write-baseline PATH    clean worktree; write candidate baseline JSON
  --baseline PATH          enforce baseline (default: tests/coverage-baseline.json)

Options:
  --output DIR             report/artifact directory (created if missing)
  --clean-output           remove a nonempty --output DIR first (safety-checked)
  --keep-builds            keep per-suite build + trace directories (debugging)
EOF
    exit 1
}

# ---------- argument parsing ----------
while [ $# -gt 0 ]; do
    case "$1" in
        --report-only) MODE="report-only" ;;
        --write-baseline)
            [ $# -ge 2 ] || die "--write-baseline requires a path argument"
            MODE="write-baseline"
            BASELINE_PATH="$2"
            shift
            ;;
        --baseline)
            [ $# -ge 2 ] || die "--baseline requires a path argument"
            MODE="baseline"
            BASELINE_PATH="$2"
            shift
            ;;
        --output)
            [ $# -ge 2 ] || die "--output requires a directory argument"
            OUTPUT_DIR="$2"
            shift
            ;;
        --clean-output) CLEAN_OUTPUT=1 ;;
        --keep-builds) KEEP_BUILDS=1 ;;
        -h|--help) usage ;;
        *) die "unknown option: $1" ;;
    esac
    shift
done

[ -n "$OUTPUT_DIR" ] || die "--output DIR is required"
# Default mode: enforce the committed baseline.
[ -n "$MODE" ] || MODE="baseline"
[ "$MODE" = "report-only" ] || [ -n "$BASELINE_PATH" ] || BASELINE_PATH="$REPO_ROOT/tests/coverage-baseline.json"

command -v gcovr >/dev/null 2>&1 || die "gcovr not found — re-enter the dev shell (flake.nix provides gcovr)"
command -v gcov >/dev/null 2>&1 || die "gcov not found in dev shell"
command -v west >/dev/null 2>&1 || die "west not found in dev shell"
command -v python3 >/dev/null 2>&1 || die "python3 not found in dev shell"

# Resolve ZEPHYR_BASE if not set — same policy as test-all.sh.
if [ -z "${ZEPHYR_BASE:-}" ]; then
    if command -v nrfutil &>/dev/null; then
        eval "$(nrfutil sdk-manager toolchain env --toolchain-bundle-id 8285d8ad56 --as-script sh)" 2>/dev/null || true
    fi
fi
: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set or nrfutil must be in PATH}"
echo "ZEPHYR_BASE=${ZEPHYR_BASE}"

cd "$REPO_ROOT"
# Linked worktrees have a .git file, not a directory. Ask Git instead of
# rejecting valid clean exact-commit worktrees used to preserve owner edits.
[ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" = true ] || die "not a git checkout: $REPO_ROOT"

# ---------- clean worktree / provenance ----------
WORKTREE_DIRTY=0
if [ -n "$(git status --porcelain)" ]; then
    WORKTREE_DIRTY=1
    if [ "$MODE" != "report-only" ]; then
        die "worktree is dirty — --write-baseline and baseline enforcement require a clean exact commit"
    fi
fi
SOURCE_COMMIT="$(git rev-parse HEAD)"

# ---------- output-dir safety ----------
# Path-shape validation runs unconditionally: never accept empty/root/
# repo-root/home, relative paths, paths outside the caller-selected output
# tree (/tmp or $HOME), paths inside the repo, the repo's ancestors, or the
# working directory.  Every check runs against the canonical path (symlinks
# resolved, traversal like /tmp/../ resolved) so aliases of the repo are
# caught before any rm -rf.  No output path is accepted unless its canonical
# target can be proven to live outside the repo root.
[ -n "$OUTPUT_DIR" ] || die "refusing to clean unsafe output path: $OUTPUT_DIR"
case "$OUTPUT_DIR" in
    /*) : ;;
    *) die "refusing to clean path outside the allowed output tree (/tmp or \$HOME): $OUTPUT_DIR" ;;
esac
case "$OUTPUT_DIR" in
    ""|/|"$HOME") die "refusing to clean unsafe output path: $OUTPUT_DIR" ;;
esac
CANON_REPO="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$REPO_ROOT")" \
    || die "cannot canonicalize repo root: $REPO_ROOT"
CANON_OUT="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$OUTPUT_DIR")" \
    || die "cannot canonicalize output path: $OUTPUT_DIR"
CANON_HOME="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$HOME")" \
    || die "cannot canonicalize home directory: $HOME"
CANON_PWD="$(python3 -c 'import os; print(os.path.realpath(os.getcwd()))')"
[ -n "$CANON_OUT" ] || die "cannot resolve output path: $OUTPUT_DIR"
[ "$CANON_OUT" = "/" ] && die "refusing to clean unsafe output path: $OUTPUT_DIR"
[ "$CANON_OUT" = "$CANON_REPO" ] && die "refusing to clean the repo root: $OUTPUT_DIR"
case "$CANON_OUT" in
    "$CANON_REPO"/*) die "refusing to clean a path inside the repo root: $OUTPUT_DIR" ;;
esac
case "$CANON_REPO" in
    "$CANON_OUT"/*) die "refusing to clean a path containing the repo root: $OUTPUT_DIR" ;;
esac
case "$CANON_OUT" in
    /tmp|/tmp/*|"$CANON_HOME"|"$CANON_HOME"/*) : ;;
    *) die "refusing to clean path outside the allowed output tree (/tmp or \$HOME): $OUTPUT_DIR" ;;
esac
[ "$CANON_OUT" = "$CANON_PWD" ] && die "refusing to clean the current working directory"
# Operate on the canonical path from here on so every later mkdir/rm targets
# the verified location.
OUTPUT_DIR="$CANON_OUT"

if [ -d "$OUTPUT_DIR" ] && [ -n "$(ls -A "$OUTPUT_DIR" 2>/dev/null)" ]; then
    [ "$CLEAN_OUTPUT" -eq 1 ] || die "output directory not empty: $OUTPUT_DIR (pass --clean-output to remove it)"
    rm -rf "$OUTPUT_DIR"
fi
mkdir -p "$OUTPUT_DIR/html" "$OUTPUT_DIR/traces" "$OUTPUT_DIR/logs"

TMP_ROOT="$(mktemp -d)" || die "mktemp failed"
if [ "$KEEP_BUILDS" -eq 1 ]; then
    trap 'echo "builds kept at: $TMP_ROOT"' EXIT
else
    trap 'rm -rf "$TMP_ROOT"' EXIT
fi
TRACE_DIR="$TMP_ROOT/traces"
mkdir -p "$TRACE_DIR"

# ---------- suite discovery (shared inventory module) ----------
# Both categories come from scripts/test_inventory.py — the single
# filesystem classification source also used by test-all.sh and
# check-test-matrix.py.  An empty category is valid; the runner still
# requires at least one C suite overall.
twister_suites=()
while IFS= read -r line; do
    [ -n "$line" ] || continue
    twister_suites+=("$line")
done <<<"$(python3 "$SCRIPT_DIR/test_inventory.py" --twister)" \
    || die "test_inventory.py --twister failed"
exec_suites=()
while IFS= read -r line; do
    [ -n "$line" ] || continue
    exec_suites+=("$line")
done <<<"$(python3 "$SCRIPT_DIR/test_inventory.py" --exec-only)" \
    || die "test_inventory.py --exec-only failed"
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
        || { cp "$TMP_ROOT/$suite.run.log" "$OUTPUT_DIR/logs/" 2>/dev/null || true; \
             die "native run failed for $suite — see $OUTPUT_DIR/logs/$suite.run.log"; }

    gcda_count="$(find "$build_dir" -name '*.gcda' | wc -l)"
    [ "$gcda_count" -gt 0 ] \
        || die "no .gcda after run for $suite — host libgcov did not write data"

    # Per-suite trace flags. Only the ltv_bounds suite traces its guard
    # wrapper with gcovr's internal-function filter relaxed: the guard's
    # sole function is __wrap_bt_audio_data_parse, which the default
    # "__" prefix filter drops (gcovr 8.4 exclusions._function_can_be_excluded).
    # Old per-suite traces keep the default filter, so the historical
    # frozen population is unchanged; the final merge below re-adds the
    # flag only for reading the already-relaxed trace files.
    suite_trace_args=()
    if [ "$suite" = "ltv_bounds" ]; then
        suite_trace_args+=(--include-internal-functions)
    fi
    gcovr_json "$TRACE_DIR/trace_$suite.json" \
        "${suite_trace_args[@]}" \
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
    --include-internal-functions \
    --filter "$REPO_ROOT/src/" \
    "${merge_args[@]}" \
    --json="$OUTPUT_DIR/coverage.json" \
    --json-summary="$OUTPUT_DIR/coverage-summary.json" \
    --txt="$OUTPUT_DIR/coverage.txt" \
    --html-details="$OUTPUT_DIR/html/index.html" \
    >"$OUTPUT_DIR/gcovr-merge.log" 2>&1 \
    || die "gcovr merge failed — see $OUTPUT_DIR/gcovr-merge.log"

# ---------- numeric summary + baseline handling ----------
# numeric-summary.json: per-file + overall counts for the manifest numeric
# population only (no headers, no excluded files).
python3 - "$OUTPUT_DIR" "$REPO_ROOT" <<'PYEOF'
import json, os, sys

out_dir, repo = sys.argv[1:3]
manifest_path = os.path.join(repo, "tests", "test-matrix.json")
with open(manifest_path, "r", encoding="utf-8") as fh:
    manifest = json.load(fh)
entries = manifest["entries"] if isinstance(manifest, dict) else manifest

population = sorted(
    e["source"]
    for e in entries
    if e.get("source", "").startswith("src/")
    and e["source"].endswith(".c")
    and not e.get("excluded_from_numeric", False)
)
excluded = sorted(
    e["source"]
    for e in entries
    if e.get("source", "").startswith("src/")
    and e["source"].endswith(".c")
    and e.get("excluded_from_numeric", False)
)
exclusion_reasons = {
    e["source"]: e.get("reason", "excluded_from_numeric (see manifest)")
    for e in entries
    if e.get("source", "").startswith("src/")
    and e["source"].endswith(".c")
    and e.get("excluded_from_numeric", False)
}

with open(os.path.join(out_dir, "coverage.json"), "r", encoding="utf-8") as fh:
    cov = json.load(fh)

by_path = {f["file"]: f for f in cov.get("files", [])}
files = {}
for src in population:
    rec = by_path.get(src)
    if rec is None or not isinstance(rec, dict):
        continue
    if not isinstance(rec.get("lines", []), list) or not isinstance(
        rec.get("functions", []), list
    ):
        # Non-list metric types cannot be counted; record zero totals so
        # later enforcement rejects them with the exact valid_pair error
        # instead of a TypeError mid-summary.
        files[src] = {
            "lines": [0, 0],
            "branches": [0, 0],
            "functions": [0, 0],
        }
        continue
    # Map function -> excluded flags of its lines (GCOVR_EXCL blocks).
    lines_by_fn = {}
    for line in rec.get("lines", []):
        fn = line.get("function_name")
        if fn:
            lines_by_fn.setdefault(fn, []).append(line.get("gcovr/excluded", False))
    lt = lc = bt = bc = 0
    for line in rec.get("lines", []):
        if line.get("gcovr/excluded", False):
            continue  # test-only helpers never enter production metrics
        lt += 1
        if line.get("count", 0) > 0:
            lc += 1
        for b in line.get("branches", []):
            bt += 1
            if b.get("count", 0) > 0:
                bc += 1
    ft = fc = 0
    for fn in rec.get("functions", []):
        name = fn.get("name") or fn.get("demangled_name")
        excl_flags = lines_by_fn.get(name, [])
        if excl_flags and all(excl_flags):
            continue  # fully excluded test-only helper
        ft += 1
        if fn.get("execution_count", 0) > 0:
            fc += 1
    files[src] = {
        "lines": [lc, lt],
        "branches": [bc, bt],
        "functions": [fc, ft],
    }

totals = {"lines": [0, 0], "branches": [0, 0], "functions": [0, 0]}
for src, rec in files.items():
    for metric in totals:
        totals[metric][0] += rec[metric][0]
        totals[metric][1] += rec[metric][1]

summary = {
    "schema_version": 1,
    "commit": None,  # filled by the shell wrapper
    "population": population,
    "files": files,
    "totals": totals,
    "exclusions": {"numeric_population": excluded, "reasons": exclusion_reasons},
}
summary["addition_sidecar"] = None
with open(os.path.join(out_dir, "numeric-summary.json"), "w", encoding="utf-8") as fh:
    json.dump(summary, fh, indent=2, sort_keys=True)
    fh.write("\n")

for metric in ("lines", "branches", "functions"):
    c, t = totals[metric]
    pct = 100.0 * c / t if t else 0.0
    print("numeric %s: %d/%d (%.1f%%)" % (metric, c, t, pct))
print("numeric population files: %d" % len(population))
PYEOF

# ---------- run manifest ----------
if ! python3 - "$OUTPUT_DIR" "$SOURCE_COMMIT" "$GCOVR_VERSION" "$GCOV_VERSION" "$REPO_ROOT" "$WORKTREE_DIRTY" "$MODE" <<'PYEOF'
import json, os, sys

out_dir, commit, gcovr_ver, gcov_ver, repo, dirty, mode = sys.argv[1:8]

# Never write __pycache__ into the (possibly fixture) repo when importing
# the shared inventory module — a generated .pyc would dirty the worktree
# and break baseline enforcement on the next run.
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(repo, "scripts"))
import test_inventory  # noqa: E402 — shared suite classification

inv = test_inventory.discover(repo)
twister = inv.twister
exec_only = set(inv.exec_only)
suites = inv.all_c_suites()

manifest = {
    "tool": "test-coverage.sh",
    "mode": mode,
    "source_commit": commit,
    "dirty": dirty == "1",
    "gcovr_version": gcovr_ver,
    "gcov_version": gcov_ver,
    "gcovr_merge_mode_functions": "separate",
    "gcovr_internal_function_filter": "relaxed-only-for-ltv_bounds-trace",
    "filter": [os.path.join(repo, "src/")],
    "coverage_additions_sha256": None,
    "coverage_additions": None,
    "suites": [{"name": s, "kind": "exec-only" if s in exec_only else "twister",
                "command": "west build --no-sysbuild -b native_sim/native/64 -d BUILD tests/unit/%s -p -- -DCONFIG_COVERAGE=y; BUILD/zephyr/zephyr.exe" % s,
                "status": "ok", "config": "CONFIG_COVERAGE=y"} for s in suites],
    "outputs": ["coverage.json", "coverage-summary.json", "numeric-summary.json",
                "coverage.txt", "html/index.html", "traces/", "logs/",
                "gcovr.log", "gcovr-merge.log"],
}
with open(os.path.join(out_dir, "run-manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2, sort_keys=True)
    f.write("\n")
print("run-manifest.json written")
PYEOF
then
    die "run-manifest generation failed"
fi

# ---------- baseline write or enforcement ----------
if [ "$MODE" = "write-baseline" ]; then
    if ! python3 - "$OUTPUT_DIR/numeric-summary.json" "$BASELINE_PATH" "$SOURCE_COMMIT" "$GCOVR_VERSION" "$GCOV_VERSION" <<'PYEOF'
import json, os, sys

summary_path, baseline_path, commit, gcovr_ver, gcov_ver = sys.argv[1:6]
with open(summary_path, "r", encoding="utf-8") as fh:
    summary = json.load(fh)

baseline = {
    "schema_version": 1,
    "generated_commit": commit,
    "gcovr_version": gcovr_ver,
    "gcov_version": gcov_ver,
    "population": summary["population"],
    "totals": summary["totals"],
    "files": summary["files"],
    "exclusions": summary["exclusions"],
}
with open(baseline_path, "w", encoding="utf-8") as fh:
    json.dump(baseline, fh, indent=2, sort_keys=True)
    fh.write("\n")
print("baseline written: %s (commit %s)" % (baseline_path, commit))
PYEOF
    then
        die "baseline write failed"
    fi
elif [ "$MODE" = "baseline" ]; then
    [ -f "$BASELINE_PATH" ] || die "baseline not found: $BASELINE_PATH"
    # Optional additive non-regression sidecar (the sole path is
    # tests/coverage-additions.json; no environment override is offered).
    # It pins independently selected new sources with exact reference
    # metrics while the frozen baseline stays byte-for-byte unchanged.
    # Presence uses -e or -L: a dangling symlink, directory or empty path
    # is invalid, not absent.
    ADDITIONS_JSON="$REPO_ROOT/tests/coverage-additions.json"
    if ! [ -e "$ADDITIONS_JSON" ] && ! [ -L "$ADDITIONS_JSON" ]; then
        ADDITIONS_JSON=""
    fi
    python3 - "$OUTPUT_DIR/numeric-summary.json" "$BASELINE_PATH" \
        "$GCOVR_VERSION" "$GCOV_VERSION" \
        "$OUTPUT_DIR/run-manifest.json" \
        "${ADDITIONS_JSON}" <<'PYEOF' || die "baseline enforcement failed"
import hashlib
import json
import os
import stat
import sys


def ge(a, b):
    """a/b >= c/d via integer cross multiplication (b, d > 0)."""
    return a[0] * b[1] >= b[0] * a[1]


def fail_additions(exc, sidecar_path):
    """One explicit error and exit; never silently ignore a bad sidecar."""
    print("error: invalid additions sidecar %s: %s" % (sidecar_path, exc))
    print("baseline enforcement: 1 error(s)")
    sys.exit(1)


def duplicate_pairs(pairs):
    seen = set()
    result = {}
    for key, value in pairs:
        if key in seen:
            raise ValueError("duplicate JSON key %r" % key)
        seen.add(key)
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError("nonfinite JSON constant %r" % value)


def read_sidecar_bounded(path, limit=65536):
    """One bounded raw read; returns (data, sha256) from the same snapshot."""
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except OSError as exc:
        raise ValueError("open failed: %s" % exc) from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ValueError("not a regular file")
        if info.st_size > limit:
            raise ValueError(
                "size %d exceeds %d bytes" % (info.st_size, limit)
            )
        with os.fdopen(fd, "rb") as fh:
            fd = None
            data = fh.read(limit + 1)
        if len(data) == 0:
            raise ValueError("file is empty")
        if len(data) > limit:
            raise ValueError(
                "read %d exceeds %d bytes" % (len(data), limit)
            )
        return data, (hashlib.sha256(data).hexdigest() if data else None)
    finally:
        if fd is not None:
            os.close(fd)


def valid_pair(value, label, minimum_total=1):
    if type(value) is not list or len(value) != 2:
        raise ValueError("%s must be a [covered, total] pair" % label)
    covered, total = value
    bad = [item for item in (covered, total) if type(item) is not int]
    if bad:
        raise ValueError("%s has non-int members %r" % (label, bad))
    if covered < 0 or covered > total:
        raise ValueError("%s covered outside 0..total" % label)
    if total < minimum_total:
        raise ValueError("%s total below minimum %d" % (label, minimum_total))
    return covered, total


def safe_source(src, label):
    if (
        type(src) is not str
        or not src.startswith("src/")
        or not src.endswith(".c")
        or "\\" in src
    ):
        raise ValueError("%s %r must be a relative src/*.c path" % (label, src))
    parts = src.split("/")
    if any(p in ("", "..", ".") for p in parts):
        raise ValueError("%s %r has an unsafe path component" % (label, src))
    return src


summary_path, baseline_path, cur_gcovr, cur_gcov, manifest_path, additions_raw = (
    sys.argv[1:7]
)

with open(summary_path, "r", encoding="utf-8") as fh:
    cur = json.load(fh)
with open(baseline_path, "rb") as fh:
    baseline_bytes = fh.read()
base = json.loads(baseline_bytes)

if not additions_raw:
    # Legacy path: sidecar genuinely absent; enforcement stays exactly as
    # before with no additions record in the run reports.
    additions = None
    additions_sha = None
    additions_record = None
else:
    try:
        raw, additions_sha = read_sidecar_bounded(additions_raw)
        data = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=duplicate_pairs,
            parse_constant=reject_constant,
        )
        if type(data) is not dict:
            raise ValueError("top-level additions must be an object")
        keys = set(data)
        if keys != {"schema_version", "frozen_baseline_sha256", "files"}:
            raise ValueError(
                "keys must be exactly schema_version, frozen_baseline_sha256, "
                "files: got %r" % sorted(keys)
            )
        if type(data["schema_version"]) is not int or data["schema_version"] != 1:
            raise ValueError("schema_version must be int 1 (bool is not int)")
        anchor = data["frozen_baseline_sha256"]
        if (
            type(anchor) is not str
            or len(anchor) != 64
            or any(c not in "0123456789abcdef" for c in anchor)
        ):
            raise ValueError("anchor must be exactly 64 lowercase hex")
        actual_anchor = hashlib.sha256(baseline_bytes).hexdigest()
        if anchor != actual_anchor:
            raise ValueError(
                "anchor %r does not match actual baseline SHA %s"
                % (anchor, actual_anchor)
            )
        if type(data["files"]) is not dict or not data["files"]:
            raise ValueError("files must be a nonempty object")
        additions = {}
        for src, metrics in data["files"].items():
            safe_source(src, "additions source")
            if type(metrics) is not dict:
                raise ValueError("additions %s metrics must be an object" % src)
            if set(metrics) != {"lines", "branches", "functions"}:
                raise ValueError(
                    "additions %s metrics keys must be exactly lines, branches, "
                    "functions" % src
                )
            additions[src] = {
                metric: valid_pair(
                    metrics[metric], "additions %s %s" % (src, metric)
                )
                for metric in ("lines", "branches", "functions")
            }
        overlap = set(additions) & set(base["population"])
        if overlap:
            fail_additions(
                "additions sources overlap the frozen baseline population: %s"
                % sorted(overlap),
                additions_raw,
            )
    except (ValueError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail_additions(exc, additions_raw)
    additions_record = {
        "sha256": additions_sha,
        "path": "tests/coverage-additions.json",
    }

errors = []

# Tool-version enforcement: current first-line versions must equal the
# baseline's recorded versions when present (legacy baselines that omit
# either field entirely remain accepted).  A present version must be a
# non-empty string; null/non-string/empty fails with the refresh
# instruction.  Comparison of valid strings is case-sensitive equality.
for tool, cur_ver, base_key in (
    ("gcovr", cur_gcovr, "gcovr_version"),
    ("gcov", cur_gcov, "gcov_version"),
):
    if base_key not in base:
        continue  # legacy baseline without this field stays accepted
    base_ver = base[base_key]
    if not isinstance(base_ver, str) or not base_ver:
        errors.append(
            "%s baseline version invalid: %r (expected non-empty string; "
            "refresh intentionally with --write-baseline %s)"
            % (tool, base_ver, baseline_path)
        )
        continue
    if cur_ver != base_ver:
        errors.append(
            "%s version mismatch: current '%s' vs baseline '%s' "
            "(refresh intentionally with --write-baseline %s)"
            % (tool, cur_ver, base_ver, baseline_path)
        )

cur_pop = set(cur["population"])
base_pop = set(base["population"])
addition_keys = set(additions) if additions is not None else set()

if additions is not None:
    required_population = base_pop | addition_keys
    missing_label = "required population source missing from current run: %s"
    extra_label = (
        "new file in current population not in baseline or additions: %s"
    )
else:
    required_population = base_pop
    missing_label = "baseline file missing from current population: %s"
    extra_label = "new file in current population not in baseline: %s"
for src in sorted(required_population - cur_pop):
    errors.append(missing_label % src)
for src in sorted(cur_pop - required_population):
    errors.append(extra_label % src)

# Frozen-population ratios come ONLY from current records of base_pop
# sources, so a perfectly covered additive source can never mask a
# frozen-population regression; every metric is validated first
# (minimum_total=0 keeps legitimate 0/0 branches legal) and a missing or
# invalid metric is an explicit error, never silently skipped.
frozen_records = {}
for src in sorted(base_pop):
    rec = cur["files"].get(src)
    if rec is None:
        errors.append("current file missing for frozen population source: %s" % src)
        continue
    validated = {}
    for metric in ("lines", "branches", "functions"):
        try:
            validated[metric] = valid_pair(
                rec.get(metric), "frozen %s %s" % (src, metric), minimum_total=0
            )
        except ValueError as exc:
            errors.append(str(exc))
            validated[metric] = None
    frozen_records[src] = validated
frozen_totals = {"lines": [0, 0], "branches": [0, 0], "functions": [0, 0]}
for src in sorted(base_pop):
    validated = frozen_records.get(src)
    if validated is None or not all(v is not None for v in validated.values()):
        continue
    for metric in frozen_totals:
        covered, total = validated[metric]
        frozen_totals[metric][0] += covered
        frozen_totals[metric][1] += total

for metric in ("lines", "branches"):
    if not ge(frozen_totals[metric], base["totals"][metric]):
        errors.append(
            "frozen-population overall %s below baseline: current %d/%d vs baseline %d/%d"
            % (
                metric,
                frozen_totals[metric][0],
                frozen_totals[metric][1],
                base["totals"][metric][0],
                base["totals"][metric][1],
            )
        )

for src in sorted(frozen_records):
    validated = frozen_records[src]
    if validated is None or not all(v is not None for v in validated.values()):
        continue
    base_rec = base["files"].get(src, {})
    for metric in ("lines", "branches", "functions"):
        ref = base_rec.get(metric)
        if ref is None:
            errors.append("%s %s baseline metric missing" % (src, metric))
            continue
        if not ge(validated[metric], ref):
            errors.append(
                "%s %s below baseline: current %d/%d vs baseline %d/%d"
                % (
                    src,
                    metric,
                    validated[metric][0],
                    validated[metric][1],
                    ref[0],
                    ref[1],
                )
            )

# Additive sources: present, nonzero-validated metrics with the exact
# reference minimum totals and the ratio via the same cross
# multiplication (bool/string/missing/nonpositive all reject).
additive_totals = {"lines": [0, 0], "branches": [0, 0], "functions": [0, 0]}
additive_records = {}
for src in sorted(addition_keys):
    ref = additions[src]
    rec = cur["files"].get(src)
    if rec is None:
        errors.append("additive source missing from current run: %s" % src)
        continue
    validated = {}
    for metric in ("lines", "branches", "functions"):
        ref_covered, ref_total = ref[metric]
        try:
            covered, total = valid_pair(
                rec.get(metric),
                "additive %s %s" % (src, metric),
                minimum_total=ref_total,
            )
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if not ge((covered, total), (ref_covered, ref_total)):
            errors.append(
                "%s %s below additive reference: current %d/%d vs reference %d/%d"
                % (src, metric, covered, total, ref_covered, ref_total)
            )
        validated[metric] = (covered, total)
        additive_totals[metric][0] += covered
        additive_totals[metric][1] += total
    additive_records[src] = validated

# Separate truthful per-group metric display alongside the combined
# totals already shown by the numeric summary step.
if additions is not None:
    for label, records, totals in (
        ("frozen-population", frozen_records, frozen_totals),
        ("additive-sidecar", additive_records, additive_totals),
    ):
        for metric in ("lines", "branches", "functions"):
            covered, total = totals[metric]
            print(
                "numeric %s %s: %d/%d" % (label, metric, covered, total)
            )

for e in sorted(errors):
    print("error: %s" % e)
print("baseline enforcement: %d error(s)" % len(errors))

# Update the current-run report files only after validation completed.
# Reports are NEW current-run artifacts, so recording the validated
# sidecar identity and the per-group totals there is safe; a legacy
# report-only run never carries an enforced-sidecar claim.
try:
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["coverage_additions_sha256"] = (
        additions_record["sha256"] if additions_record is not None else None
    )
    manifest["coverage_additions"] = (
        additions_record["path"] if additions_record is not None else None
    )
    with open(manifest_path, "r+", encoding="utf-8") as fh:
        fh.seek(0)
        json.dump(manifest, fh, indent=2, sort_keys=True)
        fh.truncate()
        fh.write("\n")
except (OSError, ValueError, KeyError) as exc:
    print("note: run manifest additions record skipped: %s" % exc)

try:
    numeric_summary = json.load(open(summary_path, encoding="utf-8"))
    if additions is not None:
        numeric_summary["addition_sidecar"] = additions_record["path"]
        numeric_summary["addition_sidecar_sha256"] = additions_sha
        numeric_summary["frozen_population_totals"] = frozen_totals
        numeric_summary["additive_sidecar_totals"] = additive_totals
    with open(summary_path, "r+", encoding="utf-8") as fh:
        fh.seek(0)
        json.dump(numeric_summary, fh, indent=2, sort_keys=True)
        fh.truncate()
        fh.write("\n")
except (OSError, ValueError, KeyError) as exc:
    print("note: numeric summary additions fields skipped: %s" % exc)

print("additions sidecar sha256: %s" % (additions_sha or "null"))
sys.exit(1 if errors else 0)
PYEOF
    if [ -n "${ADDITIONS_JSON}" ]; then
        echo "baseline enforcement PASS (against $BASELINE_PATH and additions sidecar)"
    else
        echo "baseline enforcement PASS (against $BASELINE_PATH)"
    fi
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
echo "  $OUTPUT_DIR/numeric-summary.json"
echo "  $OUTPUT_DIR/coverage.txt"
echo "  $OUTPUT_DIR/html/index.html"
echo "  $OUTPUT_DIR/run-manifest.json"
