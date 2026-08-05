cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_stream_session)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3)

# Direct production-source suite for src/audio_stream_session.c: compiles
# the real session, decoder, Mode A assembler, ISO sequence tracker,
# statistics, perf, and volume modules against a faithful fake sink and
# fake observer.  lc3_decode() is linker-wrapped (src/lc3_wrap.c) only to
# inject hard failures for the production error-accounting path — all
# other calls pass through to the real liblc3 (same seam as tests/unit/decode).
target_sources(app PRIVATE
  src/test_audio_stream_session.c
  src/fake_sink.c
  src/fake_observer.c
  src/lc3_wrap.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_stream_session.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_decode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_modea.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_iso_seq.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_stats.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_perf.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_volume.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/bsim/src
  ${CMAKE_CURRENT_BINARY_DIR}
)

foreach(f mono_48k_10ms_60b modeb_48k_10ms_60b)
  generate_inc_file_for_target(app ${FIXTURES}/${f}.lc3 ${CMAKE_CURRENT_BINARY_DIR}/${f}_lc3.inc)
  generate_inc_file_for_target(app ${FIXTURES}/${f}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${f}_pcm.inc)
endforeach()

target_link_options(app PUBLIC -Wl,--wrap=lc3_decode)
