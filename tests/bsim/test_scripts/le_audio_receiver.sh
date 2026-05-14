#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Run le_audio_receiver BSIM test (device 0) against Zephyr unicast_client (device 1).
# Requires BabbleSim installed at $BSIM_OUT_PATH and unicast_client binary pre-built.
#
# Usage: ./le_audio_receiver.sh [extra bsim args]

SIMULATION_ID="le_audio_receiver"
VERBOSITY_LEVEL=2

# shellcheck source=/dev/null
source "${ZEPHYR_BASE}/tests/bsim/sh_common.source"

BOARD_TS="nrf5340bsim_nrf5340_cpuapp"
RECV_BIN="bs_${BOARD_TS}_tests_le_audio_receiver_bsim_prj_conf"
CLIENT_BIN="bs_${BOARD_TS}_tests_bsim_bluetooth_audio_prj_conf"

cd "${BSIM_OUT_PATH}/bin"

# Device 0: our receiver
Execute "./${RECV_BIN}" \
  -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=0 \
  -testid=le_audio_receiver -D=2

# Device 1: Zephyr unicast_client (simulated phone)
Execute "./${CLIENT_BIN}" \
  -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" -d=1 \
  -testid=unicast_client -RealEncryption=1 -D=2

# 2.4 GHz PHY
Execute ./bs_2G4_phy_v1 \
  -v="${VERBOSITY_LEVEL}" -s="${SIMULATION_ID}" \
  -D=2 -sim_length=30e6 "$@"

wait_for_background_jobs
