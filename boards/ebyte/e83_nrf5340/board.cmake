# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_EBYTE_E83_NRF5340_NRF5340_CPUAPP)
  board_set_flasher(openocd)

  # Probe serial is injected via the _PROBE_SERIAL CMake var set in
  # CMakeLists.txt (read from scripts/probe-serial.local). Empty → auto-detect.
  if(_PROBE_SERIAL)
    board_runner_args(openocd
      "--config=interface/cmsis-dap.cfg"
      "--config=target/nordic/nrf53.cfg"
      "--config=${BOARD_DIR}/../../support/flash_nrf5340.tcl"
      "--cmd-pre-init=cmsis_dap_serial ${_PROBE_SERIAL}"
      "--cmd-pre-init=transport select swd"
      "--cmd-pre-init=adapter speed 100"
      "--cmd-pre-init=set NET_CORE_HEX {${_NET_CORE_HEX}}"
      "--cmd-pre-load=check_approtect"
      "--cmd-load=flash_west"
    )
  else()
    board_runner_args(openocd
      "--config=interface/cmsis-dap.cfg"
      "--config=target/nordic/nrf53.cfg"
      "--config=${BOARD_DIR}/../../support/flash_nrf5340.tcl"
      "--cmd-pre-init=transport select swd"
      "--cmd-pre-init=adapter speed 100"
      "--cmd-pre-init=set NET_CORE_HEX {${_NET_CORE_HEX}}"
      "--cmd-pre-load=check_approtect"
      "--cmd-load=flash_west"
    )
  endif()
endif()

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
