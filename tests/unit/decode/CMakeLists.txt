cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_decode)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3)

# T2B: execute the REAL production decoder and stats against deterministic
# checked-in 48 kHz LC3 fixtures.  The fixtures are embedded as byte
# arrays at build time (never regenerated during tests).  lc3_decode() is
# linker-wrapped (src/lc3_wrap.c) only to inject hard failures for the
# production error-accounting path — all other calls pass through to the
# real liblc3.
target_sources(app PRIVATE
  src/test_decode.c
  src/lc3_wrap.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_decode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_stats.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
  ${CMAKE_CURRENT_BINARY_DIR}
)

foreach(f mono_48k_7p5ms_60b mono_48k_10ms_60b modeb_48k_7p5ms_60b modeb_48k_10ms_60b)
  generate_inc_file_for_target(app ${FIXTURES}/${f}.lc3 ${CMAKE_CURRENT_BINARY_DIR}/${f}_lc3.inc)
  generate_inc_file_for_target(app ${FIXTURES}/${f}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${f}_pcm.inc)
endforeach()

target_link_options(app PUBLIC -Wl,--wrap=lc3_decode)
