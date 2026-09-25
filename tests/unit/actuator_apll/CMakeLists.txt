cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_apll)

# Historical APLL actuator suite. Compiles the test-local implementation
# against test-owned include shadows of
# <hal/nrf_clock.h> (NRF_CLOCK_HAS_HFCLKAUDIO=1) and
# <nrfx_clock_hfclkaudio.h>, plus a mock capturing every register write.
target_sources(app PRIVATE
  src/test_actuator_apll.c
  src/mock_clock.c
  src/audio_clock_actuator_apll_historical.c
)

target_include_directories(app PRIVATE
  include
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
