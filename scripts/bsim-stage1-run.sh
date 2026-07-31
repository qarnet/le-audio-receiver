#!/usr/bin/env bash
# BabbleSim Stage 1 — receiver + custom valid-LC3 client (10 ms + 7.5 ms)
#
# Compiles repo receiver (sink stub) and custom 48_4_1 client (10 ms),
# then runs dual-core simulations with real per-process log capture.
# Both 10 ms (48_4_1) and 7.5 ms (48_3_1) frame durations tested,
# each run twice.  Pairwise hash equality and known accepted values
# enforced:
#   - 10 ms: 0xFE0D4245
#   - 7.5 ms: 0x5853F445
#
# All processes must exit 0 AND logs must contain expected PASS
# markers with correct counters and deterministic hashes.
#
# Usage: bash scripts/bsim-stage1-run.sh
#
# Prerequisites:
#   - ZEPHYR_BASE exported
#   - nrfutil in PATH
#   - BabbleSim built at BSIM_OUT_PATH
#   - Source scripts/bsim-env.sh first or set BSIM_OUT_PATH

set -ueo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# Source BSIM environment (derives BSIM_OUT_PATH, BOARD defaults)
source "${SCRIPT_DIR}/bsim-env.sh"

BOARD_TS="${BOARD//\//_}"

# Known accepted hash values for each frame-duration scenario.
KNOWN_HASH_10MS="0xFE0D4245"
KNOWN_HASH_7MS="0x5853F445"

# --- Toolchain ---
if ! command -v nrfutil &>/dev/null; then
    echo "ERROR: nrfutil not in PATH — source NCS toolchain environment first" >&2
    exit 1
fi
# Work around set -u: nrfutil env exports appending to $LD_LIBRARY_PATH,
# which is unset under Nix (uses rpath instead).
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" || {
    echo "ERROR: Failed to source NCS toolchain environment" >&2
    exit 1
}

# --- Compile options ---
# Disable -Werror to survive glibc _FORTIFY_SOURCE false positive at -O0
export cmake_args="-DCONFIG_COVERAGE=y -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCONFIG_ASSERT=y -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=n"
export ORIG_CMAKE_ARGS="$cmake_args"
export WORK_DIR="${ZEPHYR_BASE}/bsim_out"
sysbuild=1

# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/tests/bsim/compile.source"

# ---- Compile receiver (device 0) ----
echo "=== Compile receiver (tests/bsim) ==="
app="tests/bsim"
app_root="${REPO_ROOT}"
BOARD_ROOT="${REPO_ROOT}"
conf_file="prj.conf"
exe_name="bs_${BOARD_TS}_le_audio_receiver_bsim_prj_conf"
export app app_root BOARD_ROOT conf_file exe_name
compile
wait_for_background_jobs

RECV_BIN="${BSIM_OUT_PATH}/bin/${exe_name}"
if [ ! -x "$RECV_BIN" ]; then
    echo "ERROR: Receiver binary not found: $RECV_BIN" >&2
    exit 1
fi
echo "Receiver: $RECV_BIN"

# ---- Compile client (device 1) ----
echo ""
echo "=== Compile client (tests/bsim/client) ==="
app="tests/bsim/client"
BOARD_ROOT="${REPO_ROOT}"
exe_name="bs_${BOARD_TS}_valid_lc3_client_bsim_prj_conf"
export app BOARD_ROOT exe_name
compile
wait_for_background_jobs

CLIENT_BIN="${BSIM_OUT_PATH}/bin/${exe_name}"
if [ ! -x "$CLIENT_BIN" ]; then
    echo "ERROR: Client binary not found: $CLIENT_BIN" >&2
    exit 1
fi
echo "Client: $CLIENT_BIN"

# ---- Run one simulation with given client binary and label ----
# Sets global BSIM_LAST_HASH to the extracted receiver hash (uppercase hex,
# including 0x prefix) on success.  On failure, BSIM_LAST_HASH is empty
# and function returns non-zero.
#
# Usage: run_sim <label> <client_bin> <client_testid>
run_sim() {
    local _label="$1"
    local _client_bin="$2"
    local _client_testid="$3"

    BSIM_LAST_HASH=""

    source "${ZEPHYR_BASE}/tests/bsim/sh_common.source"

    local _sid="bsim_stage1_${_label}_$$"
    local _RLOG="/tmp/bsim_stage1_${_label}_receiver_$$.log"
    local _CLOG="/tmp/bsim_stage1_${_label}_client_$$.log"
    local _PLOG="/tmp/bsim_stage1_${_label}_phy_$$.log"

    echo ""
    echo "=== Run ${_label} simulation $_sid ==="
    echo "Receiver: $RECV_BIN  →  $_RLOG"
    echo "Client:   $_client_bin  →  $_CLOG"
    echo "PHY log:  $_PLOG"

    cd "${BSIM_OUT_PATH}/bin"

    # Device 0: repo receiver (peripheral sink)
    Execute "${RECV_BIN}" \
        -v=2 -s="$_sid" -d=0 \
        -testid=le_audio_receiver \
        -RealEncryption=1 -rs=23 \
        >"$_RLOG" 2>&1

    # Device 1: client (central source)
    Execute "$_client_bin" \
        -v=2 -s="$_sid" -d=1 \
        -testid="$_client_testid" \
        -RealEncryption=1 -rs=28 \
        >"$_CLOG" 2>&1

    # PHY — sim_length=40e6 (~40 s simulated)
    Execute ./bs_2G4_phy_v1 \
        -v=2 -s="$_sid" \
        -D=2 -sim_length=40e6 \
        >"$_PLOG" 2>&1

    echo "Waiting for background processes..."

    local _recv_rc=0 _client_rc=0 _phy_rc=0 _any_nonzero=0
    local _pid_idx=0
    for _pid in $_process_ids; do
        local _single_rc=0
        wait $_pid || _single_rc=$?
        _pid_idx=$((_pid_idx + 1))
        case $_pid_idx in
            1) _recv_rc=$_single_rc; echo "RECEIVER (pid $_pid) exit $_recv_rc" ;;
            2) _client_rc=$_single_rc; echo "CLIENT   (pid $_pid) exit $_client_rc" ;;
            3) _phy_rc=$_single_rc; echo "PHY      (pid $_pid) exit $_phy_rc" ;;
        esac
        [ "$_single_rc" -ne 0 ] && _any_nonzero=$_single_rc
    done

    echo ""
    echo "Log files ($_label):"
    echo "  Receiver:  $_RLOG ($(wc -c <"$_RLOG" 2>/dev/null || echo 0) bytes)"
    echo "  Client:    $_CLOG ($(wc -c <"$_CLOG" 2>/dev/null || echo 0) bytes)"
    echo "  PHY:       $_PLOG ($(wc -c <"$_PLOG" 2>/dev/null || echo 0) bytes)"

    if [ "$_any_nonzero" -ne 0 ]; then
        echo ""
        echo "=== ${_label} FAILED: process exit non-zero ==="
        [ "$_recv_rc" -ne 0 ] && echo "  RECEIVER exit $_recv_rc"
        [ "$_client_rc" -ne 0 ] && echo "  CLIENT exit $_client_rc"
        [ "$_phy_rc" -ne 0 ] && echo "  PHY exit $_phy_rc"
        return 1
    fi

    echo "All three processes exited 0."

    # ---- Validate PASS markers ----
    local _fail=0

    local _grep_pass="INFO: le_audio_receiver:"
    if ! grep -q "$_grep_pass" "$_RLOG" 2>/dev/null; then
        echo "FAIL: Receiver log missing PASS marker" >&2
        _fail=1
    else
        local _pass_line
        _pass_line=$(grep "$_grep_pass" "$_RLOG" | tail -1)
        echo "Receiver PASS line: $_pass_line"

        if ! echo "$_pass_line" | grep -q "errors=0"; then
            echo "FAIL: Receiver decode_errors != 0" >&2; _fail=1
        fi
        if ! echo "$_pass_line" | grep -q "malformed=0"; then
            echo "FAIL: Receiver malformed_count != 0" >&2; _fail=1
        fi
        if ! echo "$_pass_line" | grep -q "after_stop=0"; then
            echo "FAIL: Receiver pushes_after_stop != 0" >&2; _fail=1
        fi
        if ! echo "$_pass_line" | grep -q "nonzero=1"; then
            echo "FAIL: Receiver no nonzero samples" >&2; _fail=1
        fi

        local _plc _splc _total _pushes _szero
        _plc=$(echo "$_pass_line" | grep -oP 'plc=\K\d+' | head -1)
        _splc=$(echo "$_pass_line" | grep -oP 'startup_plc=\K\d+' | head -1)
        if [ -n "$_plc" ] && [ -n "$_splc" ]; then
            if [ "$_plc" -ne "$_splc" ]; then
                echo "FAIL: plc=$_plc != startup_plc=$_splc" >&2; _fail=1
            fi
        fi

        _total=$(echo "$_pass_line" | grep -oP 'total=\K\d+' | head -1)
        _pushes=$(echo "$_pass_line" | grep -oP '[0-9]+ pushes' | grep -oP '\d+' | head -1)
        _szero=$(echo "$_pass_line" | grep -oP 'startup_zero=\K\d+' | head -1)
        if [ -n "$_total" ] && [ -n "$_pushes" ] && [ -n "$_szero" ]; then
            if [ "$_total" -ne $((_pushes + _szero)) ]; then
                echo "FAIL: total=$_total != pushes=$_pushes + startup_zero=$_szero" >&2; _fail=1
            fi
        fi

        local _emax
        _emax=$(echo "$_pass_line" | grep -oP 'energy_max=\K\d+')
        if [ -z "$_emax" ] || [ "$_emax" -le 0 ]; then
            echo "FAIL: energy_max zero or missing" >&2; _fail=1
        fi

        local _hash _hash_val
        _hash=$(echo "$_pass_line" | grep -oP 'hash=0x[0-9A-Fa-f]+' | head -1)
        if [ -z "$_hash" ]; then
            echo "FAIL: PASS line missing hash=" >&2; _fail=1
        else
            _hash_val=$(echo "$_hash" | cut -d= -f2 | tr 'a-f' 'A-F')
            if [ "$_hash_val" = "0x00000000" ] || [ "$_hash_val" = "0x811C9DC5" ]; then
                echo "FAIL: hash is zero or FNV seed: $_hash_val" >&2; _fail=1
            fi
        fi
    fi

    # Client PASS check
    if ! grep -q "INFO: ${_client_testid}:" "$_CLOG" 2>/dev/null; then
        echo "FAIL: Client log missing PASS marker" >&2; _fail=1
    else
        local _cpass _tx
        _cpass=$(grep "INFO: ${_client_testid}:" "$_CLOG" | tail -1)
        echo "Client PASS line: $_cpass"
        _tx=$(echo "$_cpass" | grep -oP '\d+ successful sends' | grep -oP '\d+')
        if [ -z "$_tx" ] || [ "$_tx" -lt 100 ]; then
            echo "FAIL: Client TX count < 100 (got $_tx)" >&2; _fail=1
        fi
    fi

    echo ""
    echo "Simulation ID: $_sid"

    if [ "$_fail" -eq 0 ]; then
        # Export hash to global for pairwise/known-value checks
        BSIM_LAST_HASH="${_hash_val:-}"
        echo "=== ${_label} PASS  (hash=${BSIM_LAST_HASH}) ==="
        echo "  Receiver log: $_RLOG"
        return 0
    else
        echo "=== ${_label} FAIL ==="
        return 1
    fi
}

# assert_hash <label> <run_number> <actual_hash> <known_hash>
# Checks pairwise equality against previous run and known accepted value.
# Uses global ASSERTS_FAILED counter.
assert_hash() {
    local _label="$1"
    local _run="$2"
    local _actual="$3"
    local _known="$4"

    if [ -z "$_actual" ]; then
        echo "FAIL: ${_label} run ${_run} — no hash extracted" >&2
        ASSERTS_FAILED=$((ASSERTS_FAILED + 1))
        return 1
    fi

    # Known-value check
    if [ "$_actual" != "$_known" ]; then
        echo "FAIL: ${_label} run ${_run} — hash ${_actual} != known ${_known}" >&2
        ASSERTS_FAILED=$((ASSERTS_FAILED + 1))
        return 1
    fi
    echo "  ${_label} run ${_run} hash=${_actual} == known ${_known} ✓"

    # Pairwise check (run > 1)
    if [ "$_run" -gt 1 ]; then
        local _prev_var="PREV_${_label//[^a-zA-Z0-9]/_}_HASH"
        local _prev="${!_prev_var:-}"
        if [ -n "$_prev" ]; then
            if [ "$_actual" != "$_prev" ]; then
                echo "FAIL: ${_label} run ${_run} — hash ${_actual} != run $((_run - 1)) hash ${_prev}" >&2
                ASSERTS_FAILED=$((ASSERTS_FAILED + 1))
                return 1
            fi
            echo "  ${_label} run ${_run} hash=${_actual} == run $((_run - 1)) hash=${_prev} ✓ (pairwise deterministic)"
        fi
    fi

    # Store for next pairwise check
    eval "PREV_${_label//[^a-zA-Z0-9]/_}_HASH=${_actual}"
    return 0
}

# ---- Run 10 ms simulations (twice) ----
ASSERTS_FAILED=0

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  10 ms (48_4_1) — Run 1 of 2"
echo "══════════════════════════════════════════════════════════════════"
run_sim "10ms-run1" "$CLIENT_BIN" "valid_lc3_client" || exit $?
HASH_10MS_R1="$BSIM_LAST_HASH"
assert_hash "10ms" 1 "$HASH_10MS_R1" "$KNOWN_HASH_10MS"

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  10 ms (48_4_1) — Run 2 of 2"
echo "══════════════════════════════════════════════════════════════════"
run_sim "10ms-run2" "$CLIENT_BIN" "valid_lc3_client" || exit $?
HASH_10MS_R2="$BSIM_LAST_HASH"
assert_hash "10ms" 2 "$HASH_10MS_R2" "$KNOWN_HASH_10MS"

# ---- Compile 7.5 ms client ----
echo ""
echo "=== Compile 7.5ms client (tests/bsim/client) ==="
app="tests/bsim/client"
BOARD_ROOT="${REPO_ROOT}"
exe_name="bs_${BOARD_TS}_valid_lc3_client_7ms_bsim_prj_conf"
export app BOARD_ROOT exe_name
# Preserve original cmake_args, add 7.5ms preset
export cmake_args="${cmake_args} -DCONFIG_BSIM_CLIENT_PRESET_48_3_1=y"
compile
wait_for_background_jobs
# Restore cmake_args
export cmake_args="${ORIG_CMAKE_ARGS:-${cmake_args/-DCONFIG_BSIM_CLIENT_PRESET_48_3_1=y/}}"

CLIENT_7MS_BIN="${BSIM_OUT_PATH}/bin/${exe_name}"
if [ ! -x "$CLIENT_7MS_BIN" ]; then
    echo "ERROR: 7.5ms client binary not found: $CLIENT_7MS_BIN" >&2
    exit 1
fi
echo "7.5ms Client: $CLIENT_7MS_BIN"

# ---- Run 7.5 ms simulations (twice) ----
echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  7.5 ms (48_3_1) — Run 1 of 2"
echo "══════════════════════════════════════════════════════════════════"
run_sim "7.5ms-run1" "$CLIENT_7MS_BIN" "valid_lc3_client" || exit $?
HASH_7MS_R1="$BSIM_LAST_HASH"
assert_hash "7.5ms" 1 "$HASH_7MS_R1" "$KNOWN_HASH_7MS"

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  7.5 ms (48_3_1) — Run 2 of 2"
echo "══════════════════════════════════════════════════════════════════"
run_sim "7.5ms-run2" "$CLIENT_7MS_BIN" "valid_lc3_client" || exit $?
HASH_7MS_R2="$BSIM_LAST_HASH"
assert_hash "7.5ms" 2 "$HASH_7MS_R2" "$KNOWN_HASH_7MS"

# ---- Final summary ----
echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  STAGE 1 — REPEATED-RUN HASH SUMMARY"
echo "══════════════════════════════════════════════════════════════════"
echo ""
echo "  10 ms  run 1:  $HASH_10MS_R1"
echo "  10 ms  run 2:  $HASH_10MS_R2  (pairwise match: $([ "$HASH_10MS_R1" = "$HASH_10MS_R2" ] && echo YES || echo NO))"
echo "  10 ms  known:  $KNOWN_HASH_10MS  (known match: $([ "$HASH_10MS_R1" = "$KNOWN_HASH_10MS" ] && echo YES || echo NO))"
echo ""
echo "  7.5 ms run 1:  $HASH_7MS_R1"
echo "  7.5 ms run 2:  $HASH_7MS_R2  (pairwise match: $([ "$HASH_7MS_R1" = "$HASH_7MS_R2" ] && echo YES || echo NO))"
echo "  7.5 ms known:  $KNOWN_HASH_7MS  (known match: $([ "$HASH_7MS_R1" = "$KNOWN_HASH_7MS" ] && echo YES || echo NO))"
echo ""

if [ "$ASSERTS_FAILED" -eq 0 ]; then
    echo "=== STAGE1 PASS — 10ms+7.5ms repeated-run hash assertions all correct ==="
    exit 0
else
    echo "=== STAGE1 FAIL — $ASSERTS_FAILED hash assertion(s) failed ==="
    exit 1
fi
