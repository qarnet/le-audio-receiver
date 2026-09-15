cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pb031_lc3_pcm_oracle)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3)
set(PCM_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../support)
set(PCM_ORACLE_SOURCE ${PCM_ORACLE_DIR}/pcm_oracle.c)
set(PCM_ORACLE_HEADER ${PCM_ORACLE_DIR}/pcm_oracle.h)
set(PB031_MANIFEST ${FIXTURES}/portable-oracle-manifest.json)

set(PB031_NCS_VERSION "v3.3.0")
set(PB031_LIBLC3_REVISION "48bbd3eacd36e99a57317a0a4867002e0b09e183")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PB031_MANIFEST}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
  ${PCM_ORACLE_SOURCE}
  ${PCM_ORACLE_HEADER}
)

file(SHA256 ${PB031_MANIFEST} PB031_MANIFEST_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c PB031_MAIN_C_SHA256)
file(SHA256 ${PCM_ORACLE_SOURCE} PB031_PCM_ORACLE_C_SHA256)
file(SHA256 ${PCM_ORACLE_HEADER} PB031_PCM_ORACLE_H_SHA256)

configure_file(src/pb031_arm_build_info.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/pb031_arm_build_info.h
  @ONLY
)

target_sources(app PRIVATE
  src/main.c
  ${PCM_ORACLE_SOURCE}
)

target_include_directories(app PRIVATE
  ${PCM_ORACLE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

foreach(stem
    bsim_48k_10ms_120b_l
    bsim_48k_10ms_120b_r
    bsim_48k_7p5ms_90b_l
    bsim_48k_7p5ms_90b_r)
  generate_inc_file_for_target(
    app ${FIXTURES}/${stem}.lc3 ${CMAKE_CURRENT_BINARY_DIR}/${stem}_lc3.inc)
  generate_inc_file_for_target(
    app ${FIXTURES}/${stem}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${stem}_pcm.inc)
endforeach()
