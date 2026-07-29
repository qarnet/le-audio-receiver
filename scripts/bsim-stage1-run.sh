#!/usr/bin/env bash
# BabbleSim Stage 1 — receiver + custom valid-LC3 client
#
# Compiles repo receiver (sink stub) and custom 48_4_1 client,
# then runs dual-core simulation. All three processes must exit 0.
# Two consecutive runs required for acceptance.
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

echo ""
echo "=== Run simulation $SIMULATION_ID ==="
echo "Receiver: $RECV_BIN"
echo "Client:   $CLIENT_BIN"

cd "${BSIM_OUT_PATH}/bin"

# Device 0: repo receiver (peripheral sink)
Execute "${RECV_BIN}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=0 \
    -testid=le_audio_receiver \
    -RealEncryption=1 -rs=23

# Device 1: custom client (central source, 48_4_1 preset)
Execute "${CLIENT_BIN}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=1 \
    -testid=valid_lc3_client \
    -RealEncryption=1 -rs=28

# PHY
Execute ./bs_2G4_phy_v1 \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" \
    -D=2 -sim_length=40e6

# Capture per-process exit codes; collect logs
RECV_LOG="/tmp/bsim_stage1_receiver_$$.log"
CLIENT_LOG="/tmp/bsim_stage1_client_$$.log"
PHY_LOG="/tmp/bsim_stage1_phy_$$.log"

_stage1_rc=0
_pid_idx=0
for _pid in $_process_ids; do
    _single_rc=0
    wait $_pid || _single_rc=$?
    _pid_idx=$((_pid_idx + 1))
    if [ "$_single_rc" -ne 0 ]; then
        _stage1_rc=$_single_rc
        echo "Process $_pid_idx (pid $_pid) exit $_single_rc" >&2
    fi
done

echo ""
echo "Simulation ID: $SIMULATION_ID"
echo "Logs (if console output captured):"
echo "  Receiver:  $RECV_LOG"
echo "  Client:    $CLIENT_LOG"
echo "  PHY:       $PHY_LOG"

if [ "$_stage1_rc" -eq 0 ]; then
    echo "=== ALL PROCESSES EXIT 0 — PASS ==="
else
    echo "=== FAILED: at least one process exit non-zero ($_stage1_rc) ==="
fi

exit $_stage1_rc
