#!/usr/bin/env bash
# BabbleSim Stage 1 — T4 BAP scenario matrix (canonical gate entry).
#
# Compiles the repo receiver and the repo parameterized client once per
# gate, then runs the 15-scenario BAP matrix:
#   scenarios 1–7 (normal mono / Mode A / Mode B, 10 ms + 7.5 ms, plus
#   reverse start) run twice, scenarios 8–15 (malformed SDU, lifecycle,
#   reconnect, source rejection, NO_MEM, invalid codec fields) run once.
#
# Every run is checked by scripts/bsim_stage1_parse.py, which parses
# every named field of the receiver/client PASS records and asserts the
# scenario contract (exact counts, responses, hashes, channel-hash
# relations, no fault markers).  Known full/L/R hashes are pinned below;
# they were baselined from two identical workstation runs of the
# deterministic multi-channel TX pattern (see docs/testing/t4-bap-bsim-matrix.md).
#
# A flock around the shared ${ZEPHYR_BASE}/bsim_out tree stops concurrent
# gates from corrupting shared generated build files.  Logs go to one
# private mktemp root: removed on success, preserved (with a printed
# path) on failure or when BSIM_KEEP_LOGS=1.
#
# Usage: bash scripts/bsim-stage1-run.sh
#        BSIM_BASELINE=1 bash scripts/bsim-stage1-run.sh   (print hashes, skip known asserts)
#        BSIM_KEEP_LOGS=1 bash scripts/bsim-stage1-run.sh  (preserve logs on success)
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

# ── Pinned known hashes (full / left / right) ────────────────────────
# Baselined 2026-08-01 from two identical workstation runs of the
# deterministic multi-channel TX pattern.  0x00000000 = not yet pinned.
declare -A KNOWN_FULL=(
    [mono_10ms]=0x00000000
    [mono_7p5ms]=0x00000000
    [modea_10ms]=0x00000000
    [modea_7p5ms]=0x00000000
    [modea_reverse_start_10ms]=0x00000000
    [modeb_10ms]=0x00000000
    [modeb_7p5ms]=0x00000000
    [invalid_sdu_resume_10ms]=0x00000000
)
declare -A KNOWN_L=(
    [mono_10ms]=0x00000000
    [mono_7p5ms]=0x00000000
    [modea_10ms]=0x00000000
    [modea_7p5ms]=0x00000000
    [modea_reverse_start_10ms]=0x00000000
    [modeb_10ms]=0x00000000
    [modeb_7p5ms]=0x00000000
)
declare -A KNOWN_R=(
    [mono_10ms]=0x00000000
    [mono_7p5ms]=0x00000000
    [modea_10ms]=0x00000000
    [modea_7p5ms]=0x00000000
    [modea_reverse_start_10ms]=0x00000000
    [modeb_10ms]=0x00000000
    [modeb_7p5ms]=0x00000000
)

# Pinned post-start PLC delta per scenario (0 = none; Mode A/B carry the
# deterministic CIS-sync boundary PLC).  -1 = not yet pinned.
declare -A KNOWN_PLC_DELTA=(
    [mono_10ms]=-1
    [mono_7p5ms]=-1
    [modea_10ms]=-1
    [modea_7p5ms]=-1
    [modea_reverse_start_10ms]=-1
    [modeb_10ms]=-1
    [modeb_7p5ms]=-1
)

# Pinned exact total decoder invocations per scenario.  -1 = not yet pinned.
declare -A KNOWN_TOTAL=(
    [mono_10ms]=-1
    [mono_7p5ms]=-1
    [modea_10ms]=-1
    [modea_7p5ms]=-1
    [modea_reverse_start_10ms]=-1
    [modeb_10ms]=-1
    [modeb_7p5ms]=-1
    [invalid_sdu_resume_10ms]=-1
    [modea_first_stop_10ms]=-1
    [release_without_disable_10ms]=-1
    [disconnect_streaming_10ms]=-1
    [reconnect_second_stream_10ms]=-1
)

# Scenario matrix: name runs
MATRIX=(
    "mono_10ms 2"
    "mono_7p5ms 2"
    "modea_10ms 2"
    "modea_7p5ms 2"
    "modea_reverse_start_10ms 2"
    "modeb_10ms 2"
    "modeb_7p5ms 2"
    "invalid_sdu_resume_10ms 2"
    "modea_first_stop_10ms 1"
    "release_without_disable_10ms 1"
    "disconnect_streaming_10ms 1"
    "reconnect_second_stream_10ms 1"
    "unsupported_source_direction 1"
    "no_free_sink_slot 1"
    "invalid_codec_fields 1"
)

BASELINE="${BSIM_BASELINE:-0}"
KEEP_LOGS="${BSIM_KEEP_LOGS:-0}"

# --- Toolchain ---
if ! command -v nrfutil &>/dev/null; then
    echo "ERROR: nrfutil not in PATH — source NCS toolchain environment first" >&2
    exit 1
fi
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
eval "$(nrfutil sdk-manager toolchain env --ncs-version v3.3.0 --as-script sh)" || {
    echo "ERROR: Failed to source NCS toolchain environment" >&2
    exit 1
}

# --- Lock the shared bsim_out tree ---
BSIM_LOCK="${ZEPHYR_BASE}/bsim_out/.bsim_stage1.lock"
mkdir -p "$(dirname "$BSIM_LOCK")"
exec 9>"$BSIM_LOCK"
if ! flock -w 3600 9; then
    echo "ERROR: another gate holds ${BSIM_LOCK} — bsim_out is in use" >&2
    exit 2
fi
echo "bsim_out lock acquired"

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
exe_name="bs_${BOARD_TS}_bsim_client_bsim_prj_conf"
export app BOARD_ROOT exe_name
compile
wait_for_background_jobs

CLIENT_BIN="${BSIM_OUT_PATH}/bin/${exe_name}"
if [ ! -x "$CLIENT_BIN" ]; then
    echo "ERROR: Client binary not found: $CLIENT_BIN" >&2
    exit 1
fi
echo "Client: $CLIENT_BIN"

# ---- Private log root ----
LOGROOT="$(mktemp -d "${TMPDIR:-/tmp}/bsim_t4_XXXXXX")"
OVERALL_FAIL=0

cleanup() {
    if [ "$OVERALL_FAIL" -eq 0 ] && [ "$KEEP_LOGS" != "1" ]; then
        rm -rf "$LOGROOT"
    else
        echo ""
        echo "Logs preserved at: $LOGROOT"
        echo "  (set BSIM_KEEP_LOGS=1 to keep logs on success too)"
    fi
}
trap cleanup EXIT

# ---- Run one simulation with strict per-scenario parsing ----
# Sets global LAST_H, LAST_LH, LAST_RH (uppercase hex incl. 0x) and
# LAST_PUSHES on success.  Returns non-zero on any failure.
run_one() {
    local _scn="$1"
    local _run="$2"
    local _dir="$LOGROOT/${_scn}-run${_run}"

    LAST_H=""; LAST_LH=""; LAST_RH=""; LAST_PUSHES=""
    mkdir -p "$_dir"

    source "${ZEPHYR_BASE}/tests/bsim/sh_common.source"

    # sh_common's Execute() wraps processes in `timeout EXECUTE_TIMEOUT`;
    # the default 30 s is far below the T4 matrix's 80 s sim length.
    export EXECUTE_TIMEOUT=300

    local _sid="bsim_t4_${_scn}_${_run}_$$"
    echo ""
    echo "=== Run ${_scn} (${_run}) — $_sid ==="

    cd "${BSIM_OUT_PATH}/bin"

    Execute "${RECV_BIN}" \
        -v=2 -s="$_sid" -d=0 \
        -testid="$_scn" \
        -RealEncryption=1 -rs=23 \
        >"$_dir/receiver.log" 2>&1

    Execute "$CLIENT_BIN" \
        -v=2 -s="$_sid" -d=1 \
        -testid="$_scn" \
        -RealEncryption=1 -rs=28 \
        >"$_dir/client.log" 2>&1

    Execute ./bs_2G4_phy_v1 \
        -v=2 -s="$_sid" \
        -D=2 -sim_length=80e6 \
        >"$_dir/phy.log" 2>&1

    local _any_nonzero=0 _pid_idx=0
    for _pid in $_process_ids; do
        local _single_rc=0
        wait $_pid || _single_rc=$?
        _pid_idx=$((_pid_idx + 1))
        [ "$_single_rc" -ne 0 ] && _any_nonzero=$_single_rc
        case $_pid_idx in
            1) echo "  RECEIVER (pid $_pid) exit $_single_rc" ;;
            2) echo "  CLIENT   (pid $_pid) exit $_single_rc" ;;
            3) echo "  PHY      (pid $_pid) exit $_single_rc" ;;
        esac
    done

    if [ "$_any_nonzero" -ne 0 ]; then
        echo "FAIL: ${_scn} run ${_run} — process exit non-zero" >&2
        return 1
    fi

    # Strict scenario check via the Python parser.
    local _known_args=()
    if [ "$BASELINE" != "1" ]; then
        [ "${KNOWN_FULL[$_scn]:-0x00000000}" != "0x00000000" ] && \
            _known_args+=(--known-full "${KNOWN_FULL[$_scn]}")
        [ "${KNOWN_L[$_scn]:-0x00000000}" != "0x00000000" ] && \
            _known_args+=(--known-l "${KNOWN_L[$_scn]}")
        [ "${KNOWN_R[$_scn]:-0x00000000}" != "0x00000000" ] && \
            _known_args+=(--known-r "${KNOWN_R[$_scn]}")
        [ "${KNOWN_PLC_DELTA[$_scn]:--1}" != "-1" ] && \
            _known_args+=(--known-plc-delta "${KNOWN_PLC_DELTA[$_scn]}")
        [ "${KNOWN_TOTAL[$_scn]:--1}" != "-1" ] && \
            _known_args+=(--known-total "${KNOWN_TOTAL[$_scn]}")
    fi

    local _parse_out
    if ! _parse_out=$(python3 "$SCRIPT_DIR/bsim_stage1_parse.py" check \
            --scenario "$_scn" \
            --receiver "$_dir/receiver.log" \
            --client "$_dir/client.log" \
            "${_known_args[@]}" 2>&1); then
        echo "FAIL: ${_scn} run ${_run} — strict parse rejected" >&2
        echo "$_parse_out" >&2
        return 1
    fi
    echo "  $_parse_out"

    # Extract hashes for pairwise + table (from the parser's PASS line).
    LAST_H="$(echo "$_parse_out" | grep -oP 'h1=0x[0-9A-Fa-f]+' | head -1 | cut -d= -f2 | tr 'a-f' 'A-F')"
    LAST_LH="$(echo "$_parse_out" | grep -oP 'lh1=0x[0-9A-Fa-f]+' | head -1 | cut -d= -f2 | tr 'a-f' 'A-F')"
    LAST_RH="$(echo "$_parse_out" | grep -oP 'rh1=0x[0-9A-Fa-f]+' | head -1 | cut -d= -f2 | tr 'a-f' 'A-F')"
    LAST_PUSHES="$(echo "$_parse_out" | grep -oP 'pushes1=\d+' | head -1 | cut -d= -f2)"

    echo "=== ${_scn} run ${_run} PASS (h=${LAST_H} lh=${LAST_LH} rh=${LAST_RH}) ==="
    return 0
}

# ── Run the matrix ──────────────────────────────────────────────────

declare -A H_RUN1 H_RUN2 H_L1 H_R1 H_PUSHES
FAILED_SCNS=""

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  T4 BAP scenario matrix ($((${#MATRIX[@]})) scenarios, "
echo "  $(awk '{s+=$2} END {print s}' <<<"$(printf '%s\n' "${MATRIX[@]}")") runs)"
if [ "$BASELINE" = "1" ]; then
    echo "  BASELINE MODE — known-hash asserts skipped, hashes printed"
fi
echo "══════════════════════════════════════════════════════════════════"

for entry in "${MATRIX[@]}"; do
    scn="${entry%% *}"
    runs="${entry##* }"

    for ((rn = 1; rn <= runs; rn++)); do
        if ! run_one "$scn" "$rn"; then
            OVERALL_FAIL=1
            FAILED_SCNS="${FAILED_SCNS} ${scn}"
            break
        fi
        if [ "$rn" -eq 1 ]; then
            H_RUN1[$scn]="$LAST_H"
            H_L1[$scn]="$LAST_LH"
            H_R1[$scn]="$LAST_RH"
            H_PUSHES[$scn]="$LAST_PUSHES"
        else
            H_RUN2[$scn]="$LAST_H"
            # Pairwise determinism: run 2 must be identical to run 1.
            if [ "$LAST_H" != "${H_RUN1[$scn]}" ] || \
               [ "$LAST_LH" != "${H_L1[$scn]}" ] || \
               [ "$LAST_RH" != "${H_R1[$scn]}" ]; then
                echo "FAIL: ${scn} run 2 hash differs from run 1" >&2
                OVERALL_FAIL=1
                FAILED_SCNS="${FAILED_SCNS} ${scn}"
                break
            fi
            echo "  pairwise deterministic ✓"
        fi
    done
done

# ── Final summary ───────────────────────────────────────────────────

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  T4 BAP SCENARIO MATRIX SUMMARY"
echo "══════════════════════════════════════════════════════════════════"
printf "  %-28s %-4s %-12s %-12s %-12s %s\n" "scenario" "runs" "full" "left" "right" "pushes"
printf "  %-28s %-4s %-12s %-12s %-12s %s\n" "--------" "----" "----" "----" "-----" "------"
for entry in "${MATRIX[@]}"; do
    scn="${entry%% *}"
    runs="${entry##* }"
    h1="${H_RUN1[$scn]:-FAIL}"
    lh1="${H_L1[$scn]:--}"
    rh1="${H_R1[$scn]:--}"
    pushes="${H_PUSHES[$scn]:--}"
    printf "  %-28s %-4s %-12s %-12s %-12s %s\n" "$scn" "$runs" "$h1" "$lh1" "$rh1" "$pushes"
done

echo ""
echo "Known full/L/R hashes (pinned):"
for scn in mono_10ms mono_7p5ms modea_10ms modea_7p5ms modea_reverse_start_10ms \
           modeb_10ms modeb_7p5ms invalid_sdu_resume_10ms; do
    printf "  %-28s full=%-12s L=%-12s R=%-12s\n" "$scn" "${KNOWN_FULL[$scn]}" \
        "${KNOWN_L[$scn]}" "${KNOWN_R[$scn]}"
done

if [ "$BASELINE" = "1" ]; then
    echo ""
    echo "=== BASELINE HASHES (pin these into KNOWN_* after two identical runs) ==="
    for scn in mono_10ms mono_7p5ms modea_10ms modea_7p5ms modea_reverse_start_10ms \
               modeb_10ms modeb_7p5ms invalid_sdu_resume_10ms; do
        printf "  %-28s full=%-12s L=%-12s R=%-12s\n" "$scn" "${H_RUN1[$scn]:-FAIL}" \
            "${H_L1[$scn]:-FAIL}" "${H_R1[$scn]:-FAIL}"
    done
fi

if [ "$OVERALL_FAIL" -eq 0 ]; then
    echo ""
    echo "=== STAGE1 (T4 matrix) PASS — all scenarios strict-checked ==="
    exit 0
else
    echo ""
    echo "=== STAGE1 (T4 matrix) FAIL — failed scenarios:${FAILED_SCNS} ==="
    exit 1
fi
