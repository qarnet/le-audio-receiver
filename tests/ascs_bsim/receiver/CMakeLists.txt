cmake_minimum_required(VERSION 3.20.0)
# Native BSim peers are ELF32; align initial CMake probes with verified 32-bit library paths.
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -m32")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -m32")
set(CMAKE_ASM_FLAGS_INIT "${CMAKE_ASM_FLAGS_INIT} -m32")
# CMake capability probes provide main but may link with -nostdlib.
# Give those probe links an entry point without changing final targets.
# CMAKE_REQUIRED_LINK_OPTIONS is consumed only by try_compile checks.
list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "-Wl,--entry=main")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ascs_receiver)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
target_sources(app PRIVATE main.c ../common/lane.c ${SRC}/bt_bap.c ${SRC}/bt_audio_ltv_guard.c
  ${SRC}/bt_pairing_policy.c ${SRC}/audio_decode.c ${SRC}/audio_stream_session.c
  ${SRC}/audio_modea.c ${SRC}/audio_iso_seq.c ${SRC}/audio_stats.c ${SRC}/audio_drift.c
  ${SRC}/audio_rate_convert.c ${SRC}/audio_timing_math.c ${SRC}/audio_timing_none.c
  ${SRC}/stream_lifecycle.c ${SRC}/audio_volume.c ${SRC}/audio_offload.c)
target_link_options(app INTERFACE -Wl,--wrap=bt_audio_data_parse)
target_include_directories(app PRIVATE ${SRC} ../common ../../bsim/src)
zephyr_include_directories(${BSIM_COMPONENTS_PATH}/libUtilv1/src/ ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/)
zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)
