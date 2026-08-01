cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_actuator_sample_adjust_historical)

target_sources(app PRIVATE
  src/test_actuator.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_clock_actuator_sample_adjust.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
