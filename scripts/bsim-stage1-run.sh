#!/usr/bin/env bash
# BabbleSim Stage 1 — receiver + custom valid-LC3 client
#
# Compiles repo receiver (sink stub) and custom 48_4_1 client,
# then runs dual-core simulation with real per-process log capture.
# All three processes must exit 0 AND logs must contain expected
# PASS markers with correct counters. Two consecutive runs required
# for acceptance.
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

NCS_ROOT="$(dirname "$(dirname "$ZEPHYR_BASE")")"
BOARD_TS="${BOARD//\//_}"

# --- Toolchain ---
if ! command -v nrfutil &>/dev/null; then
    echo "ERROR: nrfutil not in PATH — source NCS toolchain environment first" >&2
    exit 1
fi
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" || {
    echo "ERROR: Failed to source NCS toolchain environment" >&2
    exit 1
}

# --- Compile options ---
# Disable -Werror to survive glibc _FORTIFY_SOURCE false positive at -O0
export cmake_args="-DCONFIG_COVERAGE=y -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCONFIG_ASSERT=y -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=n"
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

# ---- Simulation ----
# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/tests/bsim/sh_common.source"

SIMULATION_ID="bsim_stage1_$$"
VERBOSITY_LEVEL=2

cd "${BSIM_OUT_PATH}/bin"

# Log files — printed before execution so caller knows paths
RECV_LOG="/tmp/bsim_stage1_receiver_$$.log"
CLIENT_LOG="/tmp/bsim_stage1_client_$$.log"
PHY_LOG="/tmp/bsim_stage1_phy_$$.log"

echo ""
echo "=== Run simulation $SIMULATION_ID ==="
echo "Receiver: $RECV_BIN  →  $RECV_LOG"
echo "Client:   $CLIENT_BIN  →  $CLIENT_LOG"
echo "PHY log:  $PHY_LOG"

# Device 0: repo receiver (peripheral sink)
Execute "${RECV_BIN}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=0 \
    -testid=le_audio_receiver \
    -RealEncryption=1 -rs=23 \
    >"$RECV_LOG" 2>&1

# Device 1: custom client (central source, 48_4_1 preset)
Execute "${CLIENT_BIN}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=1 \
    -testid=valid_lc3_client \
    -RealEncryption=1 -rs=28 \
    >"$CLIENT_LOG" 2>&1

# PHY — sim_length=40e6 (~40 s simulated)
Execute ./bs_2G4_phy_v1 \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" \
    -D=2 -sim_length=40e6 \
    >"$PHY_LOG" 2>&1

# ---- Wait for all processes; record per-process exits ----
echo "Waiting for background processes..."

_recv_rc=0
_client_rc=0
_phy_rc=0
_any_nonzero=0

_pid_idx=0
for _pid in $_process_ids; do
    _single_rc=0
    wait $_pid || _single_rc=$?
    _pid_idx=$((_pid_idx + 1))

    case $_pid_idx in
        1)
            _recv_rc=$_single_rc
            echo "RECEIVER (pid $_pid) exit $_recv_rc"
            ;;
        2)
            _client_rc=$_single_rc
            echo "CLIENT   (pid $_pid) exit $_client_rc"
            ;;
        3)
            _phy_rc=$_single_rc
            echo "PHY      (pid $_pid) exit $_phy_rc"
            ;;
    esac

    if [ "$_single_rc" -ne 0 ]; then
        _any_nonzero=$_single_rc
    fi
done

echo ""
echo "Log files:"
echo "  Receiver:  $RECV_LOG ($(wc -c <"$RECV_LOG" 2>/dev/null || echo 0) bytes)"
echo "  Client:    $CLIENT_LOG ($(wc -c <"$CLIENT_LOG" 2>/dev/null || echo 0) bytes)"
echo "  PHY:       $PHY_LOG ($(wc -c <"$PHY_LOG" 2>/dev/null || echo 0) bytes)"

# ---- Validate exit codes ----
if [ "$_any_nonzero" -ne 0 ]; then
    echo ""
    echo "=== FAILED: at least one process exit non-zero ==="
    if [ "$_recv_rc" -ne 0 ]; then echo "  RECEIVER exit $_recv_rc"; fi
    if [ "$_client_rc" -ne 0 ]; then echo "  CLIENT exit $_client_rc"; fi
    if [ "$_phy_rc" -ne 0 ]; then echo "  PHY exit $_phy_rc"; fi
    exit $_any_nonzero
fi

echo ""
echo "All three processes exited 0."

# ---- Validate PASS markers in logs regardless of exit code ----
_fail_markers=0

# Receiver must contain:
#   "INFO: le_audio_receiver:" with "pushes" and correct counters
_grep_pass="INFO: le_audio_receiver:"
if ! grep -q "$_grep_pass" "$RECV_LOG" 2>/dev/null; then
    echo "FAIL: Receiver log missing PASS marker" >&2
    _fail_markers=1
else
    # Extract the PASS line and verify counters
    _pass_line=$(grep "$_grep_pass" "$RECV_LOG" | tail -1)
    echo "Receiver PASS line: $_pass_line"

    if ! echo "$_pass_line" | grep -q "errors=0"; then
        echo "FAIL: Receiver decode_errors != 0" >&2
        _fail_markers=1
    fi
    if ! echo "$_pass_line" | grep -q "plc=0"; then
        echo "FAIL: Receiver plc_frames != 0" >&2
        _fail_markers=1
    fi
    if ! echo "$_pass_line" | grep -q "malformed=0"; then
        echo "FAIL: Receiver malformed_count != 0" >&2
        _fail_markers=1
    fi
    if ! echo "$_pass_line" | grep -q "after_stop=0"; then
        echo "FAIL: Receiver pushes_after_stop != 0" >&2
        _fail_markers=1
    fi
    if ! echo "$_pass_line" | grep -q "nonzero=1"; then
        echo "FAIL: Receiver no nonzero samples" >&2
        _fail_markers=1
    fi
    # energy_max > 0 (positive integer)
    _emax=$(echo "$_pass_line" | grep -oP 'energy_max=\K\d+')
    if [ -z "$_emax" ] || [ "$_emax" -le 0 ]; then
        echo "FAIL: Receiver energy_max is zero or missing" >&2
        _fail_markers=1
    fi
    # Hash must be nonzero and not 0x811C9DC5 (FNV seed)
    _hash=$(echo "$_pass_line" | grep -oP 'hash=0x[0-9A-Fa-f]+' | head -1)
    if [ -z "$_hash" ]; then
        echo "FAIL: Receiver PASS line missing hash=" >&2
        _fail_markers=1
    else
        _hash_val=$(echo "$_hash" | cut -d= -f2 | tr 'a-f' 'A-F')
        if [ "$_hash_val" = "0x00000000" ] || [ "$_hash_val" = "0x811C9DC5" ]; then
            echo "FAIL: Receiver hash is zero or FNV seed: $_hash_val" >&2
            _fail_markers=1
        fi
    fi
fi

# Client must contain "INFO: valid_lc3_client:" with >=100 sends
if ! grep -q "INFO: valid_lc3_client:" "$CLIENT_LOG" 2>/dev/null; then
    echo "FAIL: Client log missing PASS marker" >&2
    _fail_markers=1
else
    _client_pass=$(grep "INFO: valid_lc3_client:" "$CLIENT_LOG" | tail -1)
    echo "Client PASS line: $_client_pass"
    _tx_count=$(echo "$_client_pass" | grep -oP '\d+ successful sends' | grep -oP '\d+')
    if [ -z "$_tx_count" ] || [ "$_tx_count" -lt 100 ]; then
        echo "FAIL: Client TX count < 100 (got $_tx_count)" >&2
        _fail_markers=1
    fi
fi

echo ""
echo "Simulation ID: $SIMULATION_ID"

if [ "$_fail_markers" -eq 0 ]; then
    echo "=== STAGE1 PASS — all exits 0, all markers correct ==="
    exit 0
else
    echo "=== STAGE1 FAIL — markers/checks absent or wrong ==="
    exit 1
fi
