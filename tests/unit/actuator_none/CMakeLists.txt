cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_none)

# T5: production NONE actuator suite.  Compiles the real
# src/audio_clock_actuator_none.c and proves all no-op returns.
target_sources(app PRIVATE
  src/test_actuator_none.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_clock_actuator_none.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
