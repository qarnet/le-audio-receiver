cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_pcm_oracle)

target_sources(app PRIVATE
  src/test_pcm_oracle.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../support/pcm_oracle.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../support
)
