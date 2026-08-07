#!/usr/bin/env bash
# BabbleSim official smoke — BAP unicast audio baseline
#
# Compiles the official Zephyr BAP unicast audio test for nrf5340bsim,
# then runs the full-lifecycle test (unicast_client / unicast_server).
#
# The full lifecycle test streams 100 SDUs successfully through ACL,
# encryption, discovery, codec config, QoS config, enable, CIS, and
# streaming phases. It has a known teardown disable-race in NCS v3.3.0
# that causes the client to report ISO receive lost after the server
# disables its source ASE — documented in docs/development/bsim-stage0-results.md.
# This is a test-script ordering issue, not a BAP stack failure.
#
# Acceptance is EVIDENCE-BASED, not assumed: every device log is captured
# and parsed by scripts/bsim_official_smoke_parse.py, which must prove
# >=100 valid RX SDUs (the test's MIN_SEND_COUNT, exact "Incoming audio on
# stream [valid|rx]" marker from bap_stream_rx.c) before any outcome is
# accepted.  With progress proven, a nonzero process exit is accepted ONLY
# when the logs carry the known teardown race ("ISO receive lost",
# bap_stream_rx.c:104); missing, malformed, or short progress and any
# unrelated failure exit nonzero with the reason.
#
# The ACL-disconnect sub-test (unicast_client_acl_disconnect /
# unicast_server_acl_disconnect) suffers from a separate host-side
# -ENOMEM bug in this NCS version when creating >1 extended advertising
# set; it cannot serve as a gate. See results doc for details.
#
# Prerequisites:
#   - ZEPHYR_BASE, BSIM_OUT_PATH, BSIM_COMPONENTS_PATH exported
#   - nrfutil in PATH (toolchain already sourced)
#   - BabbleSim binaries built (bs_2G4_phy_v1)
#
# Usage: bash scripts/bsim-official-smoke.sh

set -ueo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"

# Source BSIM environment (also validates PHY binary)
source "${SCRIPT_DIR}/bsim-env.sh"

BOARD_TS="${BOARD//\//_}"

# --- Toolchain: require nrfutil in PATH, check command succeeds ---
if ! command -v nrfutil &>/dev/null; then
    echo "ERROR: nrfutil not in PATH — source NCS toolchain environment first" >&2
    exit 1
fi
if ! nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh >/dev/null 2>&1; then
    echo "ERROR: nrfutil toolchain env command failed" >&2
    exit 1
fi
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" || {
    echo "ERROR: Failed to source NCS toolchain environment" >&2
    exit 1
}

echo "=== Step 1: Compile official BAP unicast audio test ==="
echo "BOARD=$BOARD  BOARD_TS=$BOARD_TS"

# Compile options as scalar string (compile.source reads ${cmake_args} as scalar).
# CONFIG_COMPILER_WARNINGS_AS_ERRORS=n avoids glibc _FORTIFY_SOURCE false
# positive at -O0 (bsim debug default).
export WORK_DIR="${ZEPHYR_BASE}/bsim_out"

cmake_args="-DCONFIG_COVERAGE=y -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCONFIG_ASSERT=y -DCONFIG_COMPILER_WARNINGS_AS_ERRORS=n"
export cmake_args

app=tests/bsim/bluetooth/audio
exe_name="bs_${BOARD_TS}_${app}_prj_conf"
sysbuild=1

# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/tests/bsim/compile.source"
compile
wait_for_background_jobs

echo ""
echo "=== Step 2: Run full unicast audio lifecycle ==="

# Validate test binary exists
TEST_BIN="${BSIM_OUT_PATH}/bin/${exe_name//\//_}"
if [ ! -x "$TEST_BIN" ]; then
    echo "ERROR: Test binary not found: $TEST_BIN" >&2
    exit 1
fi

SIMULATION_ID="bsim_smoke_unicast_audio_$$"
VERBOSITY_LEVEL=2

# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/tests/bsim/sh_common.source"

cd "${BSIM_OUT_PATH}/bin"

# Per-device log capture dir so acceptance can be verified from evidence.
SMOKE_LOG_DIR="${BSIM_OUT_PATH}/bsim_official_smoke_$$"
mkdir -p "$SMOKE_LOG_DIR"

# Full lifecycle test (unicast_client / unicast_server).
# sim_length=110e6 must exceed WAIT_TIME in tests/bsim/bluetooth/audio/src/common.h
run_in_background timeout --kill-after=5 -v "${EXECUTE_TIMEOUT:-30}" \
    "./${exe_name//\//_}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=0 \
    -testid=unicast_client \
    -RealEncryption=1 -rs=23 -D=2 >"$SMOKE_LOG_DIR/client.log" 2>&1

run_in_background timeout --kill-after=5 -v "${EXECUTE_TIMEOUT:-30}" \
    "./${exe_name//\//_}" \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=1 \
    -testid=unicast_server \
    -RealEncryption=1 -rs=28 -D=2 >"$SMOKE_LOG_DIR/server.log" 2>&1

run_in_background timeout --kill-after=5 -v "${EXECUTE_TIMEOUT:-30}" \
    ./bs_2G4_phy_v1 \
    -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" \
    -D=2 -sim_length=110e6 >"$SMOKE_LOG_DIR/phy.log" 2>&1

# Full test can exit non-zero due to the known disable-race (see docs).
# wait_for_background_jobs() calls exit() on failure, so we wait directly
# to capture exit codes without terminating the script.
_smoke_rc=0
for _pid in $_process_ids; do
    wait $_pid || _smoke_rc=$?
done

echo ""
echo "Simulation ID: $SIMULATION_ID"
echo "Full lifecycle test exit code: $_smoke_rc"
echo "Logs: $SMOKE_LOG_DIR"
echo ""

# Evidence-based acceptance: prove >=100 valid RX SDUs from the captured
# logs before accepting anything.  The parser rejects missing/malformed/
# short progress and unrelated failures.
if ! _acceptance="$(python3 "$SCRIPT_DIR/bsim_official_smoke_parse.py" check \
    --client "$SMOKE_LOG_DIR/client.log" \
    --server "$SMOKE_LOG_DIR/server.log" \
    --min-sdus 100 \
    --smoke-rc "$_smoke_rc" 2>&1)"; then
    echo "=== OFFICIAL BSIM SMOKE FAIL — no accepted outcome ==="
    echo "$_acceptance"
    exit 1
fi

echo "$_acceptance"

if [ "$_smoke_rc" -ne 0 ]; then
    echo ""
    echo "=== Baseline PARTIAL — accepted with evidence ==="
    echo ">=100 SDUs proven from logs; nonzero exit is the documented"
    echo "teardown disable-race (NCS v3.3.0), not a BAP stack defect."
    echo "See docs/development/bsim-stage0-results.md for details."
    exit 0
fi

echo ""
echo "=== Baseline PASS — all processes exit 0 with >=100 SDUs proven ==="
exit 0
