cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_timing_nrf54)

# Production nRF54 timing suite.  Compiles the real
# src/audio_timing_nrf54.c and src/audio_timing_math.c against
# test-owned include shadows of the installed nrfx_grtc / nrfx_gppi /
# nrf_grtc / nrf_timer HALs (see include/), plus a mock of
# audio_drift_frequency_error_update().  The narrow production seams
# under AUDIO_TIMING_NRF54_TEST switch TIMER20_BASE to a test-owned
# register object and capture deferred work instead of dispatching it.
target_sources(app PRIVATE
  src/test_timing_nrf54.c
  src/mock_hal.c
  src/mock_drift.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_timing_nrf54.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_timing_math.c
)

target_include_directories(app PRIVATE
  include
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
  AUDIO_TIMING_NRF54_TEST=1
)
