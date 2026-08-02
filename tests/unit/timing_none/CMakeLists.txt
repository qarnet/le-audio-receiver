cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_timing_none)

# T7 Stage 1: production nRF5340 no-op timing suite.  Compiles the real
# src/audio_timing_none.c and proves every function executes: init == 0,
# repeated init, zero/extreme SDU timestamp and presentation-delay inputs,
# repeated reset, and arbitrary call order.  The no-op is intentionally
# side-effect-free; the tests prove the API contract (init returns 0, all
# calls safe) and give gcovr a real TU to measure.
target_sources(app PRIVATE
  src/test_timing_none.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_timing_none.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
