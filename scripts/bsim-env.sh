#!/usr/bin/env bash
# BabbleSim environment for NCS v3.3.0
# Derives BSIM paths from ZEPHYR_BASE. Source this script (not execute)
# to export BSIM_OUT_PATH, BSIM_COMPONENTS_PATH, BOARD.
#
# Fails with clear message when component binaries are absent.
#
# Usage: source scripts/bsim-env.sh

set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"

_NCS_ROOT="$(dirname "$(dirname "$ZEPHYR_BASE")")"

BSIM_OUT_PATH="${BSIM_OUT_PATH:-${_NCS_ROOT}/tools/bsim}"
BSIM_COMPONENTS_PATH="${BSIM_COMPONENTS_PATH:-${BSIM_OUT_PATH}/components}"
BOARD="${BOARD:-nrf5340bsim/nrf5340/cpuapp}"

export BSIM_OUT_PATH
export BSIM_COMPONENTS_PATH
export BOARD

# Verify BabbleSim component binaries exist
_BSIM_BINARIES=(
    "${BSIM_OUT_PATH}/bin/bs_2G4_phy_v1"
    "${BSIM_OUT_PATH}/bin/bs_device_2G4_burst_interf"
    "${BSIM_OUT_PATH}/bin/bs_device_2G4_playback"
    "${BSIM_OUT_PATH}/bin/bs_device_2G4_playbackv2"
    "${BSIM_OUT_PATH}/bin/bs_device_2G4_WLAN_actmod"
    "${BSIM_OUT_PATH}/bin/bs_device_empty"
    "${BSIM_OUT_PATH}/bin/bs_device_handbrake"
    "${BSIM_OUT_PATH}/bin/bs_device_pause_simu"
    "${BSIM_OUT_PATH}/bin/bs_device_time_monitor"
)

_MISSING=""
for _bin in "${_BSIM_BINARIES[@]}"; do
    if [ ! -x "$_bin" ]; then
        _MISSING="${_MISSING}  ${_bin}\n"
    fi
done

if [ -n "$_MISSING" ]; then
    printf "ERROR: BabbleSim component binaries missing:\n%b" "$_MISSING"
    printf "Build them with: make -C %s everything\n" "$BSIM_OUT_PATH"
    return 1 2>/dev/null || exit 1
fi

printf "BabbleSim environment ready (NCS root: %s)\n" "$_NCS_ROOT"
