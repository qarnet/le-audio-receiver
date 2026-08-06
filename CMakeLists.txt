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
  src/app_lifecycle.c
  src/bt_bap.c
  src/bt_pairing_policy.c
  src/audio_decode.c
  src/audio_stream_session.c
  src/audio_i2s.c
  src/audio_drift.c
  src/audio_rate_convert.c
  src/audio_modea.c
  src/audio_iso_seq.c
  src/audio_stats.c
  src/audio_volume.c
  src/audio_timing_math.c
  src/stream_lifecycle.c
  src/audio_offload.c
)

zephyr_sources_ifdef(CONFIG_AUDIO_PERF_MEASUREMENT src/audio_perf.c)

zephyr_sources_ifdef(CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR src/audio_asrc.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_APLL src/audio_clock_actuator_apll.c)
zephyr_sources_ifdef(CONFIG_AUDIO_CLOCK_ACTUATOR_NONE src/audio_clock_actuator_none.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/audio_timing_nrf54.c)
zephyr_sources_ifndef(CONFIG_SOC_NRF54L15 src/audio_timing_none.c)
zephyr_sources_ifdef(CONFIG_SHELL src/audio_shell.c)
zephyr_sources_ifdef(CONFIG_SHELL src/bt_shell.c)
zephyr_sources_ifdef(CONFIG_USER_PAIRING_CONTROL src/pairing_mode.c)
# flpr_shell.c needs both SHELL and the nRF54L15 FLPR stack;
# zephyr_sources_ifdef accepts a single condition, so use an explicit
# if() with target_sources for the two-condition gate.
if(CONFIG_SHELL AND CONFIG_SOC_NRF54L15)
  target_sources(app PRIVATE src/flpr_shell.c)
endif()
zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS src/flpr_acceptance_shell.c)
# R8: FLPR acceptance orchestration (ring test, stalls, stale produce,
# stress, fault hang, gates 1-6) — compiled only with the acceptance
# diagnostics config.  The shared control-ACK engine is production-neutral
# (used by the core coordinated reset) and compiles on every nRF54L15
# build, exactly like flpr_ring_mgr.c.
zephyr_sources_ifdef(CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS src/flpr_acceptance.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_control_ack.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_handshake.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_ring.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_cache.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_ring_mgr.c)
zephyr_sources_ifdef(CONFIG_SOC_NRF54L15 src/flpr_runtime.c)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
