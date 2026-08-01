cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_apll_nohfclk)

# T5: no-HFCLKAUDIO variant of the production APLL actuator suite.
# Compiles the same src/audio_clock_actuator_apll.c with
# NRF_CLOCK_HAS_HFCLKAUDIO=0 (the shadow <hal/nrf_clock.h> defaults to
# 1, so the -D wins) and proves all no-op returns with zero register
# writes.  The conversion logic itself is tested only in
# tests/unit/actuator_apll.
target_sources(app PRIVATE
  src/test_actuator_apll_nohfclk.c
  src/mock_clock.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_clock_actuator_apll.c
)

target_include_directories(app PRIVATE
  include
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
  NRF_CLOCK_HAS_HFCLKAUDIO=0
)
