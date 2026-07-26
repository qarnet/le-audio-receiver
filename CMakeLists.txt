# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Expose project's boards/ dir to Zephyr for custom board discovery.
list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Probe selection happens at flash time (fw-flash-5340 → fw-probes), not at
# configure time — a serial baked into runners.yaml goes stale when probes
# are replugged.

# Net core hex path: sysbuild top-level merged_CPUNET.hex.
# CMAKE_BINARY_DIR at this point is the app domain build dir
# (build/<target>/le-audio-receiver), so ../merged_CPUNET.hex resolves to
# build/<target>/merged_CPUNET.hex.
get_filename_component(_NET_CORE_HEX "${CMAKE_BINARY_DIR}/../merged_CPUNET.hex" ABSOLUTE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver)

target_sources(app PRIVATE
  src/main.c
  src/bt_bap.c
  src/audio_decode.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_rate_convert.c
  src/audio_stats.c
  src/audio_volume.c
)

zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_APLL src/audio_clock_actuator_apll.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_NONE src/audio_clock_actuator_none.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_SAMPLE_ADJUST src/audio_clock_actuator_sample_adjust.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/audio_timing_nrf54.c)
zephyr_sources_ifndef(CONFIG_SOC_NRF54L15 src/audio_timing_none.c)
zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
