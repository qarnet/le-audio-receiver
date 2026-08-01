cmake_minimum_required(VERSION 3.20.0)

# Shared fake I2S driver, mocks, hooks, binding, and overlay live in
# ../audio_i2s_common.  DTS_ROOT/EXTRA_DTC_OVERLAY_FILE must be set
# before find_package(Zephyr).
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/dts)
list(APPEND EXTRA_DTC_OVERLAY_FILE
     ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/fake_i2s.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_i2s_identity)

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common)
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)

target_sources(app PRIVATE
  src/main.c
  ${COMMON_DIR}/test_sink_common.c
  ${COMMON_DIR}/test_sink_startup.c
  ${COMMON_DIR}/test_sink_stop.c
  ${COMMON_DIR}/test_support.c
  ${SRC_DIR}/audio_i2s.c
  ${COMMON_DIR}/fake_i2s.c
  ${COMMON_DIR}/mock_audio.c
)

target_include_directories(app PRIVATE
  ${COMMON_DIR}
  ${SRC_DIR}
)

# Test-only compile definitions: variant selection (IDENTITY + APLL
# actuator, 48000 Hz output) and the narrow production test hooks.
# The variant Kconfig symbols do not exist in this app's Kconfig, so
# these are plain compile definitions — no invalid Kconfig assignments
# (same pattern as tests/unit/volume).
target_compile_definitions(app PRIVATE
  CONFIG_AUDIO_RESAMPLER_IDENTITY=1
  CONFIG_AUDIO_CLOCK_ACTUATOR_APLL=1
  CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=48000
  AUDIO_I2S_NATIVE_TEST=1
)
