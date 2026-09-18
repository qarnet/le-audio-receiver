cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(le_audio_receiver_bsim)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/lc3)
set(PCM_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../support)
set(PCM_ORACLE_SOURCE ${PCM_ORACLE_DIR}/pcm_oracle.c)
set(PCM_ORACLE_HEADER ${PCM_ORACLE_DIR}/pcm_oracle.h)
set(STATEFUL_RECIPE_SOURCE ${PCM_ORACLE_DIR}/lc3_stateful_recipes.c)
set(STATEFUL_RECIPE_HEADER ${PCM_ORACLE_DIR}/lc3_stateful_recipes.h)
set(PB031_MANIFEST ${FIXTURES}/portable-oracle-manifest.json)
set(PB031_STATEFUL_MANIFEST ${FIXTURES}/stateful-reference-manifest.json)

set(PB031_CORPUS_STEMS
  bsim_48k_10ms_120b_l
  bsim_48k_10ms_120b_r
  bsim_48k_7p5ms_90b_l
  bsim_48k_7p5ms_90b_r
)
set(PB031_STATEFUL_REFERENCE_STEMS
  stateful_48k_10ms_skip20_l
  stateful_48k_10ms_loss48x18_r
  stateful_48k_7p5ms_modea_start_r
)

file(READ ${PB031_MANIFEST} PB031_MANIFEST_JSON)
string(JSON PB031_MANIFEST_SCHEMA_VERSION ERROR_VARIABLE
  PB031_MANIFEST_SCHEMA_VERSION_ERROR GET "${PB031_MANIFEST_JSON}" schema_version)
string(JSON PB031_MANIFEST_SCHEMA_VERSION_TYPE ERROR_VARIABLE
  PB031_MANIFEST_SCHEMA_VERSION_TYPE_ERROR TYPE "${PB031_MANIFEST_JSON}" schema_version)
if(NOT "${PB031_MANIFEST_SCHEMA_VERSION_ERROR}" STREQUAL "NOTFOUND" OR
   NOT "${PB031_MANIFEST_SCHEMA_VERSION_TYPE_ERROR}" STREQUAL "NOTFOUND" OR
   NOT "${PB031_MANIFEST_SCHEMA_VERSION_TYPE}" STREQUAL "NUMBER" OR
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
  ${PB031_STATEFUL_MANIFEST}
  ${PCM_ORACLE_SOURCE}
  ${PCM_ORACLE_HEADER}
  ${STATEFUL_RECIPE_SOURCE}
  ${STATEFUL_RECIPE_HEADER}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_pcm_limits.h.in
)

foreach(stem IN LISTS PB031_CORPUS_STEMS)
  list(APPEND PB031_EMBEDDED_BINARIES ${FIXTURES}/${stem}.lc3 ${FIXTURES}/${stem}.pcm)
endforeach()
foreach(stem IN LISTS PB031_STATEFUL_REFERENCE_STEMS)
  list(APPEND PB031_EMBEDDED_BINARIES ${FIXTURES}/${stem}.pcm)
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PB031_EMBEDDED_BINARIES})

configure_file(src/bsim_pcm_limits.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/bsim_pcm_limits.h
  @ONLY
)

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
  ${PCM_ORACLE_SOURCE}
  ${STATEFUL_RECIPE_SOURCE}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/audio_offload.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_sink_stub.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_test_main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_observer.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${PCM_ORACLE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

foreach(stem IN LISTS PB031_CORPUS_STEMS)
  generate_inc_file_for_target(
    app ${FIXTURES}/${stem}.lc3 ${CMAKE_CURRENT_BINARY_DIR}/${stem}_lc3.inc)
  generate_inc_file_for_target(
    app ${FIXTURES}/${stem}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${stem}_pcm.inc)
endforeach()

foreach(stem IN LISTS PB031_STATEFUL_REFERENCE_STEMS)
  generate_inc_file_for_target(
    app ${FIXTURES}/${stem}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${stem}_pcm.inc)
endforeach()

# BSIM component headers
zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)

zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

# LSP compile-database link: expose this BSim image's compile_commands.json
# at the app root for clangd closest-ancestor discovery.  The link target is
# absolute because the BSim build tree lives in the shared SDK bsim_out,
# outside this repo; scripts/gen-lsp-links.sh can also repair it.
file(CREATE_LINK
     "${CMAKE_BINARY_DIR}/compile_commands.json"
     "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
     SYMBOLIC)
