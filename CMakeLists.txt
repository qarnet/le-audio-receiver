# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Expose project's boards/ dir to Zephyr for custom board discovery.
list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Read probe serial from local (gitignored) file if present.
set(_PROBE_SERIAL "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-serial.local" _probe_serial_raw)
  string(STRIP "${_probe_serial_raw}" _PROBE_SERIAL)
endif()
if(_PROBE_SERIAL)
  message(STATUS "Using CMSIS-DAP probe serial: ${_PROBE_SERIAL}")
else()
  message(STATUS "No probe-serial.local — OpenOCD will auto-detect CMSIS-DAP probe")
endif()

# Net core hex path: sysbuild top-level merged_CPUNET.hex.
# CMAKE_BINARY_DIR at this point is the app domain build dir
# (build/<target>/le-audio-receiver), so ../merged_CPUNET.hex resolves to
# build/<target>/merged_CPUNET.hex.
get_filename_component(_NET_CORE_HEX "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)

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

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
