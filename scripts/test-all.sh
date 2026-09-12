#!/usr/bin/env bash
# Canonical full local gate script for le-audio-receiver.
#
# Runs every test suite:
#   1. Twister C unit suites (testcase.yaml under tests/unit/)
#   2. Exec-only C unit suites (CMakeLists.txt without testcase.yaml under
#      tests/unit/ — audio_offload, flpr_audio_process, flpr_ring,
#      offload_asrc, offload_asrc_verify)
#   3. Python unit suites (tests/unit/*/test_*.py in CMake-less dirs plus
#      scripts/test_*.py)
#   4. Coverage: rebuilds all native C suites with CONFIG_COVERAGE=y
#      and enforces the committed tests/coverage-baseline.json
#   5. Test-matrix checker: consumes the coverage run's coverage.json
#      — zero-hit function enforcement, public API inventory, outcome ledger
#   6. BabbleSim Stage 1 (canonical 17-scenario T4+R7 BAP matrix, scenarios
#      1–9 run twice, remaining eight once; deterministic across runs)
#
# All suite discovery comes from scripts/test_inventory.py (the single
# filesystem classification source shared with test-coverage.sh and
# check-test-matrix.py) — adding a suite cannot silently omit it from the
# gate.  Current inventory (scripts/test_inventory.py): 40 twister + 5
# exec-only + 24 Python = 69 unit children; the canonical gate is 72
# children (69 + coverage + matrix + BSim).
#
# Optional external output root: TEST_OUTPUT_DIR.  When set to an absolute
# directory outside the repository, coverage reports are retained at
# $TEST_OUTPUT_DIR/coverage instead of the private mktemp root (which still
# holds all build trees and is still removed on exit).  When unset, the
# historical mktemp behavior is unchanged.  CI runs the gate on the plain
# host runner inside the locked Nix shell and sets TEST_OUTPUT_DIR to
# $HOME/le-audio-test-results, so the retained output is uploaded as
# evidence and failures remain diagnosable.
#
# Required: NCS v3.3.0 dev shell (nix develop / direnv allow).
#   ZEPHYR_BASE must be set. BabbleSim dependencies must be provisioned;
#   scripts/bsim-stage1-run.sh derives BSIM_OUT_PATH via scripts/bsim-env.sh.
#
# Production firmware builds and dongle build are NOT included — they are
# run separately via fw-build-5340, fw-build-54l15, fw-build-dongle.
# The resolved build-contract checker also runs separately after those
# builds (scripts/check-build-contract.py) and does not depend on
# pre-existing build directories.
#
# Exits non-zero on any child failure.

set -ueo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

FAILURES=0
PASSES=0
TOTAL=0
TMP_ROOT=""
TEST_OUTPUT_DIR="${TEST_OUTPUT_DIR:-}"
COVERAGE_DIR=""

die() { echo "FATAL: $*" >&2; exit 1; }

validate_test_output_dir() {
    # TEST_OUTPUT_DIR (optional): absolute directory outside the repository
    # for retained canonical gate output; coverage lands in
    # $TEST_OUTPUT_DIR/coverage.  Never accept empty/root/home/relative
    # paths, paths inside the repository, paths that contain it, or paths
    # that alias it.  Runs before any suite, so a bad root fails fast.
    [ -n "$TEST_OUTPUT_DIR" ] || return 0
    case "$TEST_OUTPUT_DIR" in
        /*) : ;;
        *) die "TEST_OUTPUT_DIR must be an absolute path: $TEST_OUTPUT_DIR" ;;
    esac
    [ "$TEST_OUTPUT_DIR" != "/" ] || die "TEST_OUTPUT_DIR must not be /"
    [ "$TEST_OUTPUT_DIR" != "$HOME" ] || \
        die "TEST_OUTPUT_DIR must not be the home directory: $TEST_OUTPUT_DIR"
    local canon repo_canon
    canon="$(realpath -m "$TEST_OUTPUT_DIR")" || \
        die "cannot canonicalize TEST_OUTPUT_DIR: $TEST_OUTPUT_DIR"
    repo_canon="$(realpath -m "$REPO_ROOT")" || \
        die "cannot canonicalize repository root: $REPO_ROOT"
    [ "$canon" != "/" ] || die "TEST_OUTPUT_DIR must not be /"
    [ "$canon" != "$repo_canon" ] || \
        die "TEST_OUTPUT_DIR must not be the repository root: $TEST_OUTPUT_DIR"
    case "$canon" in
        "$repo_canon"/*) die "TEST_OUTPUT_DIR must not be inside the repository: $TEST_OUTPUT_DIR" ;;
    esac
    case "$repo_canon" in
        "$canon"/*) die "TEST_OUTPUT_DIR must not contain the repository root: $TEST_OUTPUT_DIR" ;;
    esac
}

inventory() { # flag -> stdout lines (one per line)
    python3 "$SCRIPT_DIR/test_inventory.py" "$@" || die "test_inventory.py $* failed"
}

resolve_ncs() {
    # Resolve ZEPHYR_BASE if not set — prefer nrfutil toolchain env.
    if [ -z "${ZEPHYR_BASE:-}" ]; then
        if command -v nrfutil &>/dev/null; then
            eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" 2>/dev/null || true
        fi
    fi
    : "${ZEPHYR_BASE:?ZEPHYR_BASE must be set or nrfutil must be in PATH}"
    echo "ZEPHYR_BASE=${ZEPHYR_BASE}"
}

run_one() {
    local label="$1"
    shift
    TOTAL=$((TOTAL + 1))
    printf '\n=== [%d] %s ===\n' "$TOTAL" "$label"
    if "$@"; then
        PASSES=$((PASSES + 1))
        printf '  PASS: %s\n' "$label"
        return 0
    else
        local status=$?
        FAILURES=$((FAILURES + 1))
        printf '  FAIL: %s (exit=%d)\n' "$label" "$status"
        return 1
    fi
}

# ---------- twister C suites ----------
run_twister_suites() {
    # Collect all testcase.yaml-based suites from tests/unit/ via the
    # shared inventory module.
    local suites=() line
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        suites+=("$line")
    done <<<"$(inventory --twister)"

    if [ ${#suites[@]} -eq 0 ]; then
        die "No twister suites found in tests/unit/"
    fi

    printf 'Twister suites: %s\n' "${suites[*]}"

    for suite in "${suites[@]}"; do
        run_one "twister: $suite" \
            env NIX_HARDENING_ENABLE="" \
            west build --no-sysbuild -b native_sim/native/64 \
                -d "$TMP_ROOT/twister_$suite" \
                "$REPO_ROOT/tests/unit/$suite" \
                -p -t run || true
    done
}

# ---------- exec-only C suites (no testcase.yaml) ----------
run_exec_suites() {
    local suites=() line
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        suites+=("$line")
    done <<<"$(inventory --exec-only)"

    for suite in "${suites[@]}"; do
        run_one "exec: $suite" \
            env NIX_HARDENING_ENABLE="" \
            west build --no-sysbuild -b native_sim/native/64 \
                -d "$TMP_ROOT/exec_$suite" \
                "$REPO_ROOT/tests/unit/$suite" \
                -p -t run || true
    done
}

# ---------- Python suites ----------
run_python_suites() {
    # One run_one child per discovered python file (label<TAB>relpath).
    local line label path
    while IFS=$'\t' read -r label path; do
        [ -n "$label" ] || continue
        run_one "python: $label" \
            env PYTHONPATH="$REPO_ROOT/scripts:${PYTHONPATH:-}" \
            python3 "$REPO_ROOT/$path" || true
    done <<<"$(inventory --python)"
}

# ---------- coverage (T7): rebuilds all native C suites, enforces baseline ----------
run_coverage() {
    # Enforces the committed tests/coverage-baseline.json (default mode);
    # requires a clean worktree.  Writes reports into $COVERAGE_DIR (the
    # TEST_OUTPUT_DIR/coverage subtree when TEST_OUTPUT_DIR is set, else
    # the private mktemp root).
    run_one "coverage: native suites + baseline" \
        bash "$SCRIPT_DIR/test-coverage.sh" --output "$COVERAGE_DIR" || true
}

# ---------- test-matrix checker (T7): consumes the coverage run ----------
run_matrix_check() {
    run_one "matrix: manifest + coverage.json" \
        python3 "$SCRIPT_DIR/check-test-matrix.py" --repo-root "$REPO_ROOT" \
            --coverage-json "$COVERAGE_DIR/coverage.json" || true
}

# ---------- BSim Stage 1 ----------
run_bsim_stage1() {
    # Stage 1 runner checks its own env (BSIM_OUT_PATH, nrfutil, etc.)
    # and exits non-zero on failure.
    run_one "bsim: stage1" \
        env NIX_HARDENING_ENABLE="" \
        bash "$SCRIPT_DIR/bsim-stage1-run.sh" || true
}

# ================================================================
printf '=== le-audio-receiver full local gate ===\n'
printf 'Repo: %s\n' "$REPO_ROOT"
printf '\n'

validate_test_output_dir
if [ -n "$TEST_OUTPUT_DIR" ]; then
    printf 'TEST_OUTPUT_DIR=%s (coverage -> %s/coverage)\n' \
        "$TEST_OUTPUT_DIR" "$TEST_OUTPUT_DIR"
fi
resolve_ncs
TMP_ROOT="$(mktemp -d)" || die "mktemp failed"
trap 'rm -rf "$TMP_ROOT"' EXIT

if [ -n "$TEST_OUTPUT_DIR" ]; then
    COVERAGE_DIR="$TEST_OUTPUT_DIR/coverage"
else
    COVERAGE_DIR="$TMP_ROOT/coverage"
fi

cd "$REPO_ROOT"

run_twister_suites
run_exec_suites
run_python_suites
run_coverage
run_matrix_check

# BSim is an accepted regular gate, not an optional smoke test. Missing
# prerequisites therefore fail the gate through the runner's own checks.
run_bsim_stage1

printf '\n============================================================\n'
printf 'Gate complete: %d PASS / %d FAIL / %d TOTAL\n' "$PASSES" "$FAILURES" "$TOTAL"

if [ "$FAILURES" -gt 0 ]; then
    echo "FAIL"
    exit 1
else
    echo "PASS"
    exit 0
fi
