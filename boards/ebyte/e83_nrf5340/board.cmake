# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP)
  board_set_flasher(openocd)

  # The probe serial is intentionally NOT baked in here. fw-flash-5340
  # resolves the probe at flash time (by target identity via nrf-probes, or
  # the scripts/probe-serial.local override) and passes it as an extra
  # runner arg: west flash -- --cmd-pre-init="adapter serial <SER>".
  # With no serial arg, OpenOCD auto-detects (single-probe setups).
  board_runner_args(openocd
    "--config=interface/cmsis-dap.cfg"
    "--config=target/nordic/nrf53.cfg"
    "--config=${BOARD_DIR}/support/flash_nrf5340.tcl"
    "--cmd-pre-init=transport select swd"
    "--cmd-pre-init=adapter speed 1000"
    "--cmd-pre-init=set NET_CORE_HEX {${_NET_CORE_HEX}}"
    "--cmd-pre-load=check_approtect"
    "--cmd-load=flash_west"
  )
endif()

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
