#!/usr/bin/env bash
# BabbleSim Stage 1 — T4+R7 BAP scenario matrix (canonical gate entry).
#
# Compiles the repo receiver and the repo parameterized client once per
# gate, then runs the 17-scenario BAP matrix:
#   scenarios 1–9 (mono / Mode A / Mode B at 10 ms + 7.5 ms, reverse
#   start, malformed-SDU resume, one-CIS-loss) run twice, scenarios 10–17
#   (first-ASE stop, release-without-disable, disconnect-while-streaming,
#   reconnect, source rejection, NO_MEM, invalid codec fields,
#   duplicate_release_10ms) run once — 26 runs total.
#
# The scenario matrix, run counts, and pinned totals all come from
# tests/bsim/stage1-scenarios.json (single versioned data source shared
# with scripts/bsim_stage1_parse.py) — the shell no longer copies any of
# those tables.
#
# Every run is checked by scripts/bsim_stage1_parse.py, which parses
# every named field of the receiver/client PASS records and asserts the
# scenario contract (exact counts, responses, numerical PCM limits and
# routing, exact client TX hashes, no fault markers; pins from versioned
# data file).
#
# A flock around the shared ${ZEPHYR_BASE}/bsim_out tree stops concurrent
# gates from corrupting shared generated build files.  Logs go to one
# private mktemp root: removed on success, preserved (with a printed
# path) on failure or when BSIM_KEEP_LOGS=1.  With BSIM_LOG_ROOT set to a
# caller-owned external directory, logs are written there instead, are
# always preserved, and that directory is never deleted.
#
# Usage: bash scripts/bsim-stage1-run.sh
#        BSIM_BASELINE=1 bash scripts/bsim-stage1-run.sh   (rejected: mode removed)
#        BSIM_KEEP_LOGS=1 bash scripts/bsim-stage1-run.sh  (preserve logs on success)
#        BSIM_LOG_ROOT=/abs/empty/dir bash scripts/bsim-stage1-run.sh
#                                                          (write logs to the caller's
#                                                          directory, always preserve,
#                                                          never delete caller-owned output)
#
# Prerequisites:
#   - ZEPHYR_BASE exported
#   - nrfutil in PATH
#   - BabbleSim built at BSIM_OUT_PATH
#   - Source scripts/bsim-env.sh first or set BSIM_OUT_PATH

set -ueo pipefail

SCRIPT_DIR="$(cd -- "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
DATA_FILE="$REPO_ROOT/tests/bsim/stage1-scenarios.json"

BASELINE="${BSIM_BASELINE:-0}"
KEEP_LOGS="${BSIM_KEEP_LOGS:-0}"

if [ "$BASELINE" = "1" ]; then
    echo "ERROR: BSIM_BASELINE=1 was removed with decoded PCM hash baselines; numerical PCM acceptance is mandatory" >&2
    exit 1
fi

# ── BSIM_LOG_ROOT (optional external output root) ──────────────────────
# When set, every per-run log is written to the caller's directory instead
# of a private mktemp root, is always preserved (even on success), and the
# caller's directory is never deleted.  Refuse relative paths, root, the
# home directory, the repository root or anything inside it, and any
# existing nonempty destination.  Runs before bsim-env.sh so a bad root
# fails fast without requiring ZEPHYR_BASE or toolchain presence.
BSIM_LOG_ROOT="${BSIM_LOG_ROOT:-}"
validate_bsim_log_root() {
    [ -n "$BSIM_LOG_ROOT" ] || return 0
    local canon repo_canon home_canon
    case "$BSIM_LOG_ROOT" in
        /*) : ;;
        *) echo "ERROR: BSIM_LOG_ROOT must be an absolute path: $BSIM_LOG_ROOT" >&2; exit 1 ;;
    esac
    canon="$(realpath -m "$BSIM_LOG_ROOT")" || \
        { echo "ERROR: cannot canonicalize BSIM_LOG_ROOT: $BSIM_LOG_ROOT" >&2; exit 1; }
    repo_canon="$(realpath -m "$REPO_ROOT")" || \
        { echo "ERROR: cannot canonicalize repository root: $REPO_ROOT" >&2; exit 1; }
    home_canon="$(realpath -m "$HOME" 2>/dev/null)" || home_canon="$HOME"
    [ "$canon" != "/" ] || { echo "ERROR: BSIM_LOG_ROOT must not be /" >&2; exit 1; }
    [ "$canon" != "$home_canon" ] || \
        { echo "ERROR: BSIM_LOG_ROOT must not be the home directory: $BSIM_LOG_ROOT" >&2; exit 1; }
    [ "$canon" != "$repo_canon" ] || \
        { echo "ERROR: BSIM_LOG_ROOT must not be the repository root: $BSIM_LOG_ROOT" >&2; exit 1; }
    case "$canon" in
        "$repo_canon"/*) \
            echo "ERROR: BSIM_LOG_ROOT must not be inside the repository: $BSIM_LOG_ROOT" >&2; exit 1 ;;
    esac
    if [ -e "$BSIM_LOG_ROOT" ]; then
        [ -d "$BSIM_LOG_ROOT" ] || \
            { echo "ERROR: BSIM_LOG_ROOT is not a directory: $BSIM_LOG_ROOT" >&2; exit 1; }
        [ -z "$(ls -A "$BSIM_LOG_ROOT" 2>/dev/null)" ] || \
            { echo "ERROR: BSIM_LOG_ROOT must be an empty directory: $BSIM_LOG_ROOT" >&2; exit 1; }
    fi
    mkdir -p "$BSIM_LOG_ROOT"
}
validate_bsim_log_root

# Source BSIM environment (derives BSIM_OUT_PATH, BOARD defaults)
source "${SCRIPT_DIR}/bsim-env.sh"

BOARD_TS="${BOARD//\//_}"

# ── Versioned scenario data (single source: stage1-scenarios.json) ─────
# Validate schema early and fail clearly; never proceed on a malformed,
# missing, or duplicate matrix.

if ! python3 -c '
import sys
sys.path.insert(0, sys.argv[1])
from bsim_stage1_parse import load_scenarios
load_scenarios(sys.argv[2])
' "$SCRIPT_DIR" "$DATA_FILE" 2>/dev/null; then
    echo "ERROR: invalid stage1-scenarios.json — run python3 scripts/bsim_stage1_parse.py --help for the schema" >&2
    echo "  $(python3 -c '
import sys
sys.path.insert(0, sys.argv[1])
from bsim_stage1_parse import load_scenarios
try:
    load_scenarios(sys.argv[2])
except Exception as e:
    print(e)
' "$SCRIPT_DIR" "$DATA_FILE")" >&2
    exit 1
fi

# Matrix: "name runs" entries (names contain no spaces — no word-splitting
# risk). Capture producer status explicitly: process-substitution failures
# otherwise let mapfile appear successful with a partial matrix.
if ! MATRIX_TEXT=$(python3 - "$DATA_FILE" <<'PY'
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
for s in data["scenarios"]:
    print("%s %d" % (s["name"], s["runs"]))
PY
); then
    echo "ERROR: cannot derive Stage 1 matrix from $DATA_FILE" >&2
    exit 1
fi
mapfile -t MATRIX <<<"$MATRIX_TEXT"
MATRIX_RUNS=0
for entry in "${MATRIX[@]}"; do
    scn="${entry%% *}"
    runs="${entry##* }"
    case "$runs" in
        ''|*[!0-9]*)
            echo "ERROR: invalid Stage 1 run count: $entry" >&2
            exit 1
            ;;
    esac
    MATRIX_RUNS=$((MATRIX_RUNS + runs))
done
if [ "${#MATRIX[@]}" -ne 17 ] || [ "$MATRIX_RUNS" -ne 26 ]; then
    echo "ERROR: Stage 1 matrix must contain 17 scenarios and 26 runs" >&2
    exit 1
fi

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
# Warnings are errors (Zephyr default): the repo compiles warning-free.
# If the toolchain's glibc _FORTIFY_SOURCE diagnostic recurs it is
# captured and suppressed with the narrowest flag and a recorded reason —
# never by disabling warning errors for repo code.
export cmake_args="-DCONFIG_COVERAGE=y -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCONFIG_ASSERT=y"
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
if [ -n "$BSIM_LOG_ROOT" ]; then
    LOGROOT="$BSIM_LOG_ROOT"
else
    LOGROOT="$(mktemp -d "${TMPDIR:-/tmp}/bsim_t4_XXXXXX")"
fi
OVERALL_FAIL=0

cleanup() {
    local status=$?

    # A caller-provided BSIM_LOG_ROOT is never deleted: it is caller-owned
    # output and is always preserved, success or failure.
    if [ -z "$BSIM_LOG_ROOT" ] && [ "$status" -eq 0 ] && [ "$OVERALL_FAIL" -eq 0 ] && \
       [ "$KEEP_LOGS" != "1" ]; then
        rm -rf "$LOGROOT"
    else
        echo ""
        echo "Logs preserved at: $LOGROOT"
        if [ -z "$BSIM_LOG_ROOT" ]; then
            echo "  (set BSIM_KEEP_LOGS=1 to keep logs on success too)"
        fi
    fi
}
trap cleanup EXIT

# ---- Run one simulation with strict per-scenario parsing ----
# Sets concise global numerical metrics and LAST_PUSHES on success.
# Returns non-zero on any failure.
run_one() {
    local _scn="$1"
    local _run="$2"
    local _dir="$LOGROOT/${_scn}-run${_run}"

    LAST_PUSHES=""
    LAST_LMAX=""; LAST_LRMS=""; LAST_LCORR=""
    LAST_RMAX=""; LAST_RRMS=""; LAST_RCORR=""
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

    local _parse_out
    if ! _parse_out=$(python3 "$SCRIPT_DIR/bsim_stage1_parse.py" check \
            --scenario "$_scn" \
            --receiver "$_dir/receiver.log" \
            --client "$_dir/client.log" 2>&1); then
        echo "FAIL: ${_scn} run ${_run} — strict parse rejected" >&2
        echo "$_parse_out" >&2
        return 1
    fi
    echo "  $_parse_out"

    # Extract concise numerical evidence from parser output for summary only.
    # `run_one` executes inside an `if`, so explicitly fail on missing fields
    # rather than relying on errexit in a grep pipeline.
    extract_parser_field() {
        local field="$1"
        local output="$2"
        local value

        value="$(printf '%s\n' "$output" | grep -oP "(?<![[:alnum:]_])${field}=-?[0-9]+" | \
            head -1 | cut -d= -f2)"
        [ -n "$value" ] || return 1
        printf '%s' "$value"
    }

    if ! LAST_PUSHES="$(extract_parser_field pushes1 "$_parse_out")" || \
       ! LAST_LMAX="$(extract_parser_field lmax1 "$_parse_out")" || \
       ! LAST_LRMS="$(extract_parser_field lrms1 "$_parse_out")" || \
       ! LAST_LCORR="$(extract_parser_field lcorr1 "$_parse_out")" || \
       ! LAST_RMAX="$(extract_parser_field rmax1 "$_parse_out")" || \
       ! LAST_RRMS="$(extract_parser_field rrms1 "$_parse_out")" || \
       ! LAST_RCORR="$(extract_parser_field rcorr1 "$_parse_out")"; then
        echo "FAIL: ${_scn} run ${_run} — parser PASS missing numerical summary field" >&2
        return 1
    fi

    echo "=== ${_scn} run ${_run} PASS (L max/rms/corr=${LAST_LMAX}/${LAST_LRMS}/${LAST_LCORR}, R max/rms/corr=${LAST_RMAX}/${LAST_RRMS}/${LAST_RCORR}) ==="
    return 0
}

# ── Run the matrix ──────────────────────────────────────────────────

declare -A SUMMARY_PUSHES SUMMARY_LMAX SUMMARY_LRMS SUMMARY_LCORR
declare -A SUMMARY_RMAX SUMMARY_RRMS SUMMARY_RCORR
FAILED_SCNS=""

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  T4 BAP scenario matrix ($((${#MATRIX[@]})) scenarios, "
echo "  ${MATRIX_RUNS} runs)"
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
            SUMMARY_PUSHES[$scn]="$LAST_PUSHES"
            SUMMARY_LMAX[$scn]="$LAST_LMAX"
            SUMMARY_LRMS[$scn]="$LAST_LRMS"
            SUMMARY_LCORR[$scn]="$LAST_LCORR"
            SUMMARY_RMAX[$scn]="$LAST_RMAX"
            SUMMARY_RRMS[$scn]="$LAST_RRMS"
            SUMMARY_RCORR[$scn]="$LAST_RCORR"
        fi
    done
done

# ── Final summary ───────────────────────────────────────────────────

echo ""
echo "══════════════════════════════════════════════════════════════════"
echo "  T4 BAP SCENARIO MATRIX SUMMARY"
echo "══════════════════════════════════════════════════════════════════"
printf "  %-28s %-4s %-7s %-18s %-18s\n" "scenario" "runs" "pushes" "left max/rms/corr" "right max/rms/corr"
printf "  %-28s %-4s %-7s %-18s %-18s\n" "--------" "----" "------" "-----------------" "------------------"
for entry in "${MATRIX[@]}"; do
    scn="${entry%% *}"
    runs="${entry##* }"
    pushes="${SUMMARY_PUSHES[$scn]:--}"
    left="${SUMMARY_LMAX[$scn]:--}/${SUMMARY_LRMS[$scn]:--}/${SUMMARY_LCORR[$scn]:--}"
    right="${SUMMARY_RMAX[$scn]:--}/${SUMMARY_RRMS[$scn]:--}/${SUMMARY_RCORR[$scn]:--}"
    printf "  %-28s %-4s %-7s %-18s %-18s\n" "$scn" "$runs" "$pushes" "$left" "$right"
done

if [ "$OVERALL_FAIL" -eq 0 ]; then
    echo ""
    echo "=== STAGE1 (T4 matrix) PASS — all scenarios strict-checked ==="
    exit 0
else
    echo ""
    echo "=== STAGE1 (T4 matrix) FAIL — failed scenarios:${FAILED_SCNS} ==="
    exit 1
fi
