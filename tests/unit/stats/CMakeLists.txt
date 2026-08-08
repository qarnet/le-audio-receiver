cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_stats)

# T2D: execute the REAL production statistics implementation.
target_sources(app PRIVATE
  src/test_stats.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_stats.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
