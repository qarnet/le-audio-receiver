# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Explicit overlay: auto-discovery expects nrf5340_audio_dk_nrf5340_cpuapp.overlay
# but this project uses the short name nrf5340dk_nrf5340_cpuapp.overlay.
list(APPEND DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/nrf5340dk_nrf5340_cpuapp.overlay")

# West flash runner: OpenOCD with CMSIS-DAP Pico probe, flashing both cores.
# Set BOARD_FLASH_RUNNER before find_package so board_set_flasher_ifnset()
# (called by nrfutil/jlink board.cmake) does not override it (uses NOT DEFINED check).
set(BOARD_FLASH_RUNNER "openocd" CACHE STRING "Default flash runner" FORCE)

# app_set_runner_args() is called by board_finalize_runner_args() for each runner
# (nrfutil, jlink, nrfjprog) — guard with a global property to run only once.
# CMAKE_BINARY_DIR resolves at macro-call time to build/le-audio-receiver/;
# ../merged_CPUNET.hex is the sysbuild top-level net core hex.
macro(app_set_runner_args)
  get_property(_runner_args_set GLOBAL PROPERTY LE_AUDIO_RUNNER_ARGS_SET)
  if(NOT _runner_args_set)
    set_property(GLOBAL PROPERTY LE_AUDIO_RUNNER_ARGS_SET TRUE)
    get_filename_component(_net_hex "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)
    board_runner_args(openocd
      "--config=interface/cmsis-dap.cfg"
      "--config=target/nordic/nrf53.cfg"
      "--config=${CMAKE_SOURCE_DIR}/scripts/flash_nrf5340.tcl"
      "--cmd-pre-init=cmsis_dap_serial E6635C08CB1F502B"
      "--cmd-pre-init=transport select swd"
      "--cmd-pre-init=adapter speed 100"
      "--cmd-pre-init=set NET_CORE_HEX {${_net_hex}}"
      "--cmd-pre-load=check_approtect"
      "--cmd-load=flash_west"
    )
    include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
  endif()
endmacro()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver)

target_sources(app PRIVATE
  src/main.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_stats.c
  src/audio_volume.c
)

zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)

zephyr_sources_ifdef(CONFIG_BT_AUDIO_TX src/stream_tx.c)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
