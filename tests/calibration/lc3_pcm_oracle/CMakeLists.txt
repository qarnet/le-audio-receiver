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

file(READ ${PB031_MANIFEST} PB031_MANIFEST_JSON)
string(JSON PB031_MANIFEST_SCHEMA_VERSION ERROR_VARIABLE
  PB031_MANIFEST_SCHEMA_VERSION_ERROR GET "${PB031_MANIFEST_JSON}" schema_version)
if(NOT "${PB031_MANIFEST_SCHEMA_VERSION_ERROR}" STREQUAL "NOTFOUND" OR
   NOT "${PB031_MANIFEST_SCHEMA_VERSION}" STREQUAL "2")
  message(FATAL_ERROR "PB-031 manifest must provide schema_version 2")
endif()

string(JSON PB031_PCM_LIMITS_TYPE ERROR_VARIABLE PB031_PCM_LIMITS_TYPE_ERROR
  TYPE "${PB031_MANIFEST_JSON}" pcm_limits)
if(NOT "${PB031_PCM_LIMITS_TYPE_ERROR}" STREQUAL "NOTFOUND" OR
   NOT "${PB031_PCM_LIMITS_TYPE}" STREQUAL "OBJECT")
  message(FATAL_ERROR "PB-031 manifest pcm_limits must be an object")
endif()

function(pb031_manifest_limit variable member)
  string(JSON _type ERROR_VARIABLE _type_error TYPE "${PB031_MANIFEST_JSON}"
    pcm_limits ${member})
  string(JSON _value ERROR_VARIABLE _value_error GET "${PB031_MANIFEST_JSON}"
    pcm_limits ${member})
  if(NOT "${_type_error}" STREQUAL "NOTFOUND" OR
     NOT "${_value_error}" STREQUAL "NOTFOUND" OR
     NOT "${_type}" STREQUAL "NUMBER")
    message(FATAL_ERROR "PB-031 manifest pcm_limits.${member} must be numeric")
  endif()
  if("${member}" STREQUAL "min_correlation_q15")
    set(_integer_pattern "^-?[0-9]+$")
  else()
    set(_integer_pattern "^[0-9]+$")
  endif()
  if(NOT "${_value}" MATCHES "${_integer_pattern}")
    message(FATAL_ERROR "PB-031 manifest pcm_limits.${member} must be an integer")
  endif()
  set(${variable} "${_value}" PARENT_SCOPE)
endfunction()

pb031_manifest_limit(PB031_PCM_MAX_ABS_ERROR max_abs_error)
pb031_manifest_limit(PB031_PCM_MAX_RMS_ERROR max_rms_error)
pb031_manifest_limit(PB031_PCM_MIN_CORRELATION_Q15 min_correlation_q15)

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
