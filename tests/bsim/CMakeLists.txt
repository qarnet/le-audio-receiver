cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver_bsim)

target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bt_bap.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bt_pairing_policy.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_decode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_stream_session.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_modea.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_iso_seq.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_stats.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_drift.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_rate_convert.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_timing_math.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_timing_none.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/stream_lifecycle.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_volume.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_offload.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_sink_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_test_main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_observer.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# BSIM component headers
zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
