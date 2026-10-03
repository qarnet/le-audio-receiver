#!/usr/bin/env bash
# BabbleSim environment for NCS v3.4.1
# Derives BSIM paths from ZEPHYR_BASE. Source this script (not execute)
# to export BSIM_OUT_PATH, BSIM_COMPONENTS_PATH, BOARD.
#
# Fails with clear message when component binaries are absent.
#
# Usage: source scripts/bsim-env.sh

set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"

_NCS_ROOT="$(realpath "$ZEPHYR_BASE/..")"

BSIM_OUT_PATH="${BSIM_OUT_PATH:-${_NCS_ROOT}/tools/bsim}"
BSIM_COMPONENTS_PATH="${BSIM_COMPONENTS_PATH:-${BSIM_OUT_PATH}/components}"
BOARD="${BOARD:-nrf54l15bsim/nrf54l15/cpuapp}"

export BSIM_OUT_PATH
export BSIM_COMPONENTS_PATH
export BOARD

# These default models are loaded with dlopen, not linked into the PHY.
# Check actual loader/model startup before peers can wait on a dead PHY.
_BSIM_SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! python3 "$_BSIM_SCRIPT_DIR/check-bsim-runtime.py" --root "$BSIM_OUT_PATH"; then
    printf "Build the required closure with: bash %s/build-bsim-components.sh\n" "$_BSIM_SCRIPT_DIR"
    return 1 2>/dev/null || exit 1
fi

printf "BabbleSim environment ready (NCS root: %s)\n" "$_NCS_ROOT"
