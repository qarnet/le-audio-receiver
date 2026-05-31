# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Explicit overlay: auto-discovery expects nrf5340_audio_dk_nrf5340_cpuapp.overlay
# but this project uses the short name nrf5340dk_nrf5340_cpuapp.overlay.
list(APPEND DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/boards/nrf5340dk_nrf5340_cpuapp.overlay")

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
