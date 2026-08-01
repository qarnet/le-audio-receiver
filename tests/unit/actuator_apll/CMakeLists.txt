cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_apll)

# T5: production APLL actuator suite.  Compiles the real
# src/audio_clock_actuator_apll.c against test-owned include shadows of
# <hal/nrf_clock.h> (NRF_CLOCK_HAS_HFCLKAUDIO=1) and
# <nrfx_clock_hfclkaudio.h>, plus a mock capturing every register write.
target_sources(app PRIVATE
  src/test_actuator_apll.c
  src/mock_clock.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_clock_actuator_apll.c
)

target_include_directories(app PRIVATE
  include
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
