cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_timing)

target_sources(app PRIVATE
  src/test_timing.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_timing_math.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
