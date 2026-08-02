#!/usr/bin/env bash
# Canonical full local gate script for le-audio-receiver.
#
# Runs every test suite:
#   1. Twister C unit suites (25 suites with testcase.yaml: 21 prior +
#      app_lifecycle, audio_shell, audio_shell_noperf, audio_shell_nrf54,
#      timing_none)
#   2. Exec-only C unit suites (4 suites: audio_offload, flpr_audio_process,
#      flpr_ring, offload_asrc)
#   3. Python unit suites (9: gate/test_gate.py, flpr_stall_gate/test_flpr_stall_gate.py,
#      flpr_hang_gate/test_flpr_hang_gate.py,
#      bluez_wp_gate/test_bluez_wireplumber_gate.py, bluez_wp_phase3_gate/test_bluez_wireplumber_phase3_gate.py,
#      bsim_runner/test_bsim_stage1_parse.py, build_contract/test_build_contract.py,
#      test_matrix/test_check_test_matrix.py, test_coverage_runner/test_test_coverage_runner.py)
#   4. Coverage (T7): rebuilds all native C suites with CONFIG_COVERAGE=y
#      and enforces the committed tests/coverage-baseline.json
#   5. Test-matrix checker (T7): consumes the coverage run's coverage.json
#      — zero-hit function enforcement, public API inventory, outcome ledger
#   6. BabbleSim Stage 1 (sink-only scenario, deterministic across runs)
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

die() { echo "FATAL: $*" >&2; exit 1; }

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
    # Collect all testcase.yaml-based suites from tests/unit/
    local suites=()
    for d in "$REPO_ROOT"/tests/unit/*/; do
        [ -f "$d/testcase.yaml" ] || continue
        suites+=("$(basename "$d")")
    done

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
    local suites=(audio_offload flpr_audio_process flpr_ring offload_asrc)

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
    run_one "python: gate" \
        env PYTHONPATH="$REPO_ROOT/scripts:$PYTHONPATH" \
        python3 "$REPO_ROOT/tests/unit/gate/test_gate.py" || true
    run_one "python: flpr_stall_gate" \
        env PYTHONPATH="$REPO_ROOT/scripts:$PYTHONPATH" \
        python3 "$REPO_ROOT/tests/unit/flpr_stall_gate/test_flpr_stall_gate.py" || true
    run_one "python: flpr_hang_gate" \
        python3 "$REPO_ROOT/tests/unit/flpr_hang_gate/test_flpr_hang_gate.py" || true
    run_one "python: bluez_wp_gate" \
        env PYTHONPATH="$REPO_ROOT/scripts:$PYTHONPATH" \
        python3 "$REPO_ROOT/scripts/test_bluez_wireplumber_gate.py" || true
    run_one "python: bluez_wp_phase3_gate" \
        env PYTHONPATH="$REPO_ROOT/scripts:$PYTHONPATH" \
        python3 "$REPO_ROOT/scripts/test_bluez_wireplumber_phase3_gate.py" || true
    run_one "python: bsim_runner" \
        env PYTHONPATH="$REPO_ROOT/scripts:$PYTHONPATH" \
        python3 "$REPO_ROOT/tests/unit/bsim_runner/test_bsim_stage1_parse.py" || true
    run_one "python: build_contract" \
        python3 "$REPO_ROOT/tests/unit/build_contract/test_build_contract.py" || true
    run_one "python: test_matrix" \
        python3 "$REPO_ROOT/tests/unit/test_matrix/test_check_test_matrix.py" || true
    run_one "python: test_coverage_runner" \
        python3 "$REPO_ROOT/tests/unit/test_coverage_runner/test_test_coverage_runner.py" || true
}

# ---------- coverage (T7): rebuilds all native C suites, enforces baseline ----------
run_coverage() {
    # Enforces the committed tests/coverage-baseline.json (default mode);
    # requires a clean worktree.  Writes reports into $TMP_ROOT/coverage.
    run_one "coverage: native suites + baseline" \
        bash "$SCRIPT_DIR/test-coverage.sh" --output "$TMP_ROOT/coverage" || true
}

# ---------- test-matrix checker (T7): consumes the coverage run ----------
run_matrix_check() {
    run_one "matrix: manifest + coverage.json" \
        python3 "$SCRIPT_DIR/check-test-matrix.py" --repo-root "$REPO_ROOT" \
            --coverage-json "$TMP_ROOT/coverage/coverage.json" || true
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

resolve_ncs
TMP_ROOT="$(mktemp -d)" || die "mktemp failed"
trap 'rm -rf "$TMP_ROOT"' EXIT

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
