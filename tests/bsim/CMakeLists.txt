cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver_bsim)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_drift.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_i2s_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_test_main.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
