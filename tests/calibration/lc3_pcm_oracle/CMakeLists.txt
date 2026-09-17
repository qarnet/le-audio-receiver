cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pb031_lc3_pcm_oracle)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3)
set(PCM_ORACLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../support)
set(PCM_ORACLE_SOURCE ${PCM_ORACLE_DIR}/pcm_oracle.c)
set(PCM_ORACLE_HEADER ${PCM_ORACLE_DIR}/pcm_oracle.h)
set(STATEFUL_RECIPE_SOURCE ${PCM_ORACLE_DIR}/lc3_stateful_recipes.c)
set(STATEFUL_RECIPE_HEADER ${PCM_ORACLE_DIR}/lc3_stateful_recipes.h)
set(PB031_MANIFEST ${FIXTURES}/portable-oracle-manifest.json)
set(PB031_STATEFUL_MANIFEST ${FIXTURES}/stateful-reference-manifest.json)
set(PB031_PORTABLE_PCM_REFERENCES
  ${FIXTURES}/bsim_48k_10ms_120b_l.pcm
  ${FIXTURES}/bsim_48k_10ms_120b_r.pcm
  ${FIXTURES}/bsim_48k_7p5ms_90b_l.pcm
  ${FIXTURES}/bsim_48k_7p5ms_90b_r.pcm
)
set(PB031_PORTABLE_LC3_REFERENCES
  ${FIXTURES}/bsim_48k_10ms_120b_l.lc3
  ${FIXTURES}/bsim_48k_10ms_120b_r.lc3
  ${FIXTURES}/bsim_48k_7p5ms_90b_l.lc3
  ${FIXTURES}/bsim_48k_7p5ms_90b_r.lc3
)
set(PB031_STATEFUL_GENERATED_REFERENCES
  ${FIXTURES}/stateful_48k_10ms_skip20_l.pcm
  ${FIXTURES}/stateful_48k_10ms_loss48x18_r.pcm
)

set(PB031_NCS_VERSION "v3.3.0")
set(PB031_NRF_REVISION "ba167d9f3db4abbdc9b67887ca3ea66c64f2d956")
set(PB031_ZEPHYR_REVISION "fd9204a02d52630660ce8d729945a4dd743feabf")
set(PB031_LIBLC3_REVISION "48bbd3eacd36e99a57317a0a4867002e0b09e183")

find_package(Git REQUIRED)

function(pb031_require_git_repository directory expected_revision label status_mode)
  if(NOT IS_DIRECTORY "${directory}")
    message(FATAL_ERROR "PB-031 ${label} directory is missing: ${directory}")
  endif()
  get_filename_component(_directory "${directory}" REALPATH)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_directory}" rev-parse HEAD
    RESULT_VARIABLE _revision_result
    OUTPUT_VARIABLE _revision
    ERROR_VARIABLE _revision_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT "${_revision_result}" STREQUAL "0" OR
     NOT "${_revision}" STREQUAL "${expected_revision}")
    message(FATAL_ERROR
      "PB-031 ${label} at '${_directory}' is not pinned to ${expected_revision}: "
      "${_revision_error}")
  endif()
  if("${status_mode}" STREQUAL "all")
    set(_status_arguments status --porcelain --untracked-files=all --ignored)
  else()
    set(_status_arguments status --porcelain --untracked-files=no)
  endif()
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${_directory}" ${_status_arguments}
    RESULT_VARIABLE _status_result
    OUTPUT_VARIABLE _status
    ERROR_VARIABLE _status_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT "${_status_result}" STREQUAL "0")
    message(FATAL_ERROR
      "PB-031 cannot verify ${label} working tree at '${_directory}': ${_status_error}")
  endif()
  if(NOT "${_status}" STREQUAL "")
    message(FATAL_ERROR "PB-031 ${label} working tree is dirty at '${_directory}'")
  endif()
endfunction()

function(pb031_require_west_config config_path key expected_value)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" config --file "${config_path}" --get "${key}"
    RESULT_VARIABLE _config_result
    OUTPUT_VARIABLE _config_value
    ERROR_VARIABLE _config_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT "${_config_result}" STREQUAL "0" OR
     NOT "${_config_value}" STREQUAL "${expected_value}")
    message(FATAL_ERROR
      "PB-031 NCS workspace config ${key} is not ${expected_value}: ${_config_error}")
  endif()
endfunction()

get_filename_component(PB031_ZEPHYR_DIR "${ZEPHYR_BASE}" REALPATH)
get_filename_component(PB031_NCS_WORKSPACE "${PB031_ZEPHYR_DIR}/.." REALPATH)
set(PB031_WEST_CONFIG "${PB031_NCS_WORKSPACE}/.west/config")
if(NOT EXISTS "${PB031_WEST_CONFIG}")
  message(FATAL_ERROR "PB-031 NCS workspace config is missing: ${PB031_WEST_CONFIG}")
endif()
pb031_require_west_config("${PB031_WEST_CONFIG}" manifest.path nrf)
pb031_require_west_config("${PB031_WEST_CONFIG}" manifest.file west.yml)
pb031_require_west_config("${PB031_WEST_CONFIG}" zephyr.base zephyr)
file(STRINGS "${PB031_NCS_WORKSPACE}/nrf/VERSION" PB031_NRF_VERSION LIMIT_COUNT 1)
if(NOT "${PB031_NRF_VERSION}" STREQUAL "3.3.0")
  message(FATAL_ERROR "PB-031 NCS VERSION is not 3.3.0")
endif()
pb031_require_git_repository("${PB031_NCS_WORKSPACE}/nrf" "${PB031_NRF_REVISION}"
  "NCS nrf" tracked)
pb031_require_git_repository("${PB031_ZEPHYR_DIR}" "${PB031_ZEPHYR_REVISION}"
  "NCS zephyr" tracked)

set(PB031_EXPECTED_LIBLC3_DIR "${PB031_NCS_WORKSPACE}/modules/lib/liblc3")
if(NOT DEFINED ZEPHYR_LIBLC3_MODULE_DIR OR
   "${ZEPHYR_LIBLC3_MODULE_DIR}" STREQUAL "" OR
   NOT IS_DIRECTORY "${ZEPHYR_LIBLC3_MODULE_DIR}")
  message(FATAL_ERROR "PB-031 Zephyr selected no liblc3 module")
endif()
get_filename_component(PB031_LIBLC3_MODULE_DIR "${ZEPHYR_LIBLC3_MODULE_DIR}" REALPATH)
get_filename_component(PB031_EXPECTED_LIBLC3_DIR "${PB031_EXPECTED_LIBLC3_DIR}" REALPATH)
if(NOT "${PB031_LIBLC3_MODULE_DIR}" STREQUAL "${PB031_EXPECTED_LIBLC3_DIR}")
  message(FATAL_ERROR
    "PB-031 Zephyr selected liblc3 outside NCS workspace: ${PB031_LIBLC3_MODULE_DIR}")
endif()
pb031_require_git_repository("${PB031_LIBLC3_MODULE_DIR}" "${PB031_LIBLC3_REVISION}"
  liblc3 all)
if(NOT CONFIG_FPU OR NOT CONFIG_LIBLC3)
  message(FATAL_ERROR "PB-031 calibration requires CONFIG_FPU=y and CONFIG_LIBLC3=y")
endif()
if(CONFIG_USE_DT_CODE_PARTITION)
  message(FATAL_ERROR "PB-031 direct calibration requires CONFIG_USE_DT_CODE_PARTITION=n")
endif()

file(READ ${PB031_MANIFEST} PB031_MANIFEST_JSON)
file(READ ${PB031_STATEFUL_MANIFEST} PB031_STATEFUL_MANIFEST_JSON)
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

function(pb031_json_require_object_keys json label)
  set(_expected_keys ${ARGN})
  string(JSON _type ERROR_VARIABLE _type_error TYPE "${json}")
  if(NOT "${_type_error}" STREQUAL "NOTFOUND" OR NOT "${_type}" STREQUAL "OBJECT")
    message(FATAL_ERROR "${label} must be an object")
  endif()
  string(JSON _actual_count ERROR_VARIABLE _actual_count_error LENGTH "${json}")
  if(NOT "${_actual_count_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "${label} field count is unreadable")
  endif()
  list(LENGTH _expected_keys _expected_count)
  if(NOT "${_actual_count}" STREQUAL "${_expected_count}")
    message(FATAL_ERROR "${label} has missing or unknown fields")
  endif()
  math(EXPR _last_member "${_actual_count} - 1")
  foreach(_member_index RANGE 0 ${_last_member})
    string(JSON _member ERROR_VARIABLE _member_error MEMBER "${json}" ${_member_index})
    if(NOT "${_member_error}" STREQUAL "NOTFOUND")
      message(FATAL_ERROR "${label} field lookup failed")
    endif()
    list(FIND _expected_keys "${_member}" _expected_index)
    if("${_expected_index}" STREQUAL "-1")
      message(FATAL_ERROR "${label} has unknown field ${_member}")
    endif()
  endforeach()
endfunction()

function(pb031_json_get json output label expected_type)
  string(JSON _type ERROR_VARIABLE _type_error TYPE "${json}" ${ARGN})
  string(JSON _value ERROR_VARIABLE _value_error GET "${json}" ${ARGN})
  if(NOT "${_type_error}" STREQUAL "NOTFOUND" OR
     NOT "${_value_error}" STREQUAL "NOTFOUND" OR
     NOT "${_type}" STREQUAL "${expected_type}")
    message(FATAL_ERROR "${label} must be ${expected_type}")
  endif()
  set(${output} "${_value}" PARENT_SCOPE)
endfunction()

function(pb031_json_require_string json label expected_value)
  pb031_json_get("${json}" _value "${label}" STRING ${ARGN})
  if(NOT "${_value}" STREQUAL "${expected_value}")
    message(FATAL_ERROR "${label} has unexpected value")
  endif()
endfunction()

function(pb031_json_require_integer json label expected_value)
  pb031_json_get("${json}" _value "${label}" NUMBER ${ARGN})
  if(NOT "${_value}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "${label} must be an integer")
  endif()
  if(NOT "${_value}" STREQUAL "${expected_value}")
    message(FATAL_ERROR "${label} has unexpected value")
  endif()
endfunction()

function(pb031_validate_sha256 value label)
  string(LENGTH "${value}" _length)
  if(NOT "${_length}" STREQUAL "64" OR NOT "${value}" MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "${label} must be lowercase SHA-256")
  endif()
endfunction()

function(pb031_json_require_sha256 json label expected_value)
  pb031_json_get("${json}" _value "${label}" STRING ${ARGN})
  pb031_validate_sha256("${_value}" "${label}")
  if(NOT "${_value}" STREQUAL "${expected_value}")
    message(FATAL_ERROR "${label} has unexpected value")
  endif()
endfunction()

function(pb031_verify_fixture path expected_size expected_sha256 label)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "${label} fixture is missing")
  endif()
  file(SIZE "${path}" _actual_size)
  if(NOT "${_actual_size}" STREQUAL "${expected_size}")
    message(FATAL_ERROR "${label} fixture size mismatch")
  endif()
  file(SHA256 "${path}" _actual_sha256)
  if(NOT "${_actual_sha256}" STREQUAL "${expected_sha256}")
    message(FATAL_ERROR "${label} fixture SHA-256 mismatch")
  endif()
endfunction()

set(PB031_EXPECTED_GENERATOR_FLAGS
  -O3
  -std=c11
  -ffast-math
  -Wall
  -Wextra
  -Wdouble-promotion
  -Wvla
  -pedantic
  -Werror
)

function(pb031_portable_pcm_authority source_stem output_path output_size output_sha256)
  if("${source_stem}" STREQUAL "bsim_48k_10ms_120b_l")
    set(_stream_index 0)
  elseif("${source_stem}" STREQUAL "bsim_48k_10ms_120b_r")
    set(_stream_index 1)
  elseif("${source_stem}" STREQUAL "bsim_48k_7p5ms_90b_l")
    set(_stream_index 2)
  elseif("${source_stem}" STREQUAL "bsim_48k_7p5ms_90b_r")
    set(_stream_index 3)
  else()
    message(FATAL_ERROR "PB-031 stateful recipe source is unknown")
  endif()
  string(JSON _stream ERROR_VARIABLE _stream_error GET "${PB031_MANIFEST_JSON}" streams
    ${_stream_index})
  if(NOT "${_stream_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 portable manifest stream is unreadable")
  endif()
  pb031_json_require_object_keys("${_stream}" "PB-031 portable manifest stream"
    stem duration_us frequency_hz channel frame_bytes samples_per_frame frame_count lc3 pcm)
  pb031_json_require_string("${_stream}" "PB-031 portable manifest stream.stem"
    "${source_stem}" stem)
  string(JSON _pcm ERROR_VARIABLE _pcm_error GET "${_stream}" pcm)
  if(NOT "${_pcm_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 portable manifest PCM authority is unreadable")
  endif()
  pb031_json_require_object_keys("${_pcm}" "PB-031 portable manifest PCM authority"
    path size sha256)
  pb031_json_get("${_pcm}" _path "PB-031 portable manifest PCM authority.path" STRING path)
  pb031_json_get("${_pcm}" _size "PB-031 portable manifest PCM authority.size" NUMBER size)
  if(NOT "${_size}" MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PB-031 portable manifest PCM authority.size must be an integer")
  endif()
  pb031_json_get("${_pcm}" _sha256 "PB-031 portable manifest PCM authority.sha256" STRING sha256)
  pb031_validate_sha256("${_sha256}" "PB-031 portable manifest PCM authority.sha256")
  pb031_verify_fixture("${FIXTURES}/${_path}" "${_size}" "${_sha256}"
    "PB-031 portable manifest PCM authority")
  set(${output_path} "${_path}" PARENT_SCOPE)
  set(${output_size} "${_size}" PARENT_SCOPE)
  set(${output_sha256} "${_sha256}" PARENT_SCOPE)
endfunction()

pb031_json_require_object_keys("${PB031_MANIFEST_JSON}" "PB-031 portable manifest"
  schema_version ncs_version liblc3 generator_flags source_formula_identifier corpus_frame_count
  pcm_limits streams)
pb031_json_require_integer("${PB031_MANIFEST_JSON}"
  "PB-031 portable manifest.schema_version" "2" schema_version)
pb031_json_require_string("${PB031_MANIFEST_JSON}"
  "PB-031 portable manifest.ncs_version" "${PB031_NCS_VERSION}" ncs_version)
pb031_json_require_string("${PB031_MANIFEST_JSON}"
  "PB-031 portable manifest.source_formula_identifier" "bsim_tx_hash_mix_seq_i_ch_v1"
  source_formula_identifier)
pb031_json_require_integer("${PB031_MANIFEST_JSON}"
  "PB-031 portable manifest.corpus_frame_count" "128" corpus_frame_count)
string(JSON _portable_pcm_limits ERROR_VARIABLE _portable_pcm_limits_error GET
  "${PB031_MANIFEST_JSON}" pcm_limits)
if(NOT "${_portable_pcm_limits_error}" STREQUAL "NOTFOUND")
  message(FATAL_ERROR "PB-031 portable manifest PCM limits are unreadable")
endif()
pb031_json_require_object_keys("${_portable_pcm_limits}" "PB-031 portable manifest PCM limits"
  max_abs_error max_rms_error min_correlation_q15)

string(JSON _portable_liblc3 ERROR_VARIABLE _portable_liblc3_error GET
  "${PB031_MANIFEST_JSON}" liblc3)
if(NOT "${_portable_liblc3_error}" STREQUAL "NOTFOUND")
  message(FATAL_ERROR "PB-031 portable manifest liblc3 is unreadable")
endif()
pb031_json_require_object_keys("${_portable_liblc3}" "PB-031 portable manifest liblc3"
  semantic_label west_revision)
pb031_json_require_string("${_portable_liblc3}"
  "PB-031 portable manifest liblc3.semantic_label" "1.1.2" semantic_label)
pb031_json_require_string("${_portable_liblc3}"
  "PB-031 portable manifest liblc3.west_revision" "${PB031_LIBLC3_REVISION}" west_revision)

string(JSON _portable_flags_type ERROR_VARIABLE _portable_flags_type_error TYPE
  "${PB031_MANIFEST_JSON}" generator_flags)
string(JSON _portable_flags_length ERROR_VARIABLE _portable_flags_length_error LENGTH
  "${PB031_MANIFEST_JSON}" generator_flags)
list(LENGTH PB031_EXPECTED_GENERATOR_FLAGS _expected_flags_length)
if(NOT "${_portable_flags_type_error}" STREQUAL "NOTFOUND" OR
   NOT "${_portable_flags_length_error}" STREQUAL "NOTFOUND" OR
   NOT "${_portable_flags_type}" STREQUAL "ARRAY" OR
   NOT "${_portable_flags_length}" STREQUAL "${_expected_flags_length}")
  message(FATAL_ERROR "PB-031 portable manifest generator_flags are invalid")
endif()
math(EXPR _last_flag_index "${_expected_flags_length} - 1")
foreach(_flag_index RANGE 0 ${_last_flag_index})
  list(GET PB031_EXPECTED_GENERATOR_FLAGS ${_flag_index} _expected_flag)
  pb031_json_require_string("${PB031_MANIFEST_JSON}"
    "PB-031 portable manifest generator_flags[${_flag_index}]" "${_expected_flag}"
    generator_flags ${_flag_index})
endforeach()

string(JSON _portable_streams_type ERROR_VARIABLE _portable_streams_type_error TYPE
  "${PB031_MANIFEST_JSON}" streams)
string(JSON _portable_streams_length ERROR_VARIABLE _portable_streams_length_error LENGTH
  "${PB031_MANIFEST_JSON}" streams)
set(PB031_EXPECTED_PORTABLE_STREAMS
  "bsim_48k_10ms_120b_l|10000|48000|left|120|480"
  "bsim_48k_10ms_120b_r|10000|48000|right|120|480"
  "bsim_48k_7p5ms_90b_l|7500|48000|left|90|360"
  "bsim_48k_7p5ms_90b_r|7500|48000|right|90|360"
)
list(LENGTH PB031_EXPECTED_PORTABLE_STREAMS _expected_streams_length)
if(NOT "${_portable_streams_type_error}" STREQUAL "NOTFOUND" OR
   NOT "${_portable_streams_length_error}" STREQUAL "NOTFOUND" OR
   NOT "${_portable_streams_type}" STREQUAL "ARRAY" OR
   NOT "${_portable_streams_length}" STREQUAL "${_expected_streams_length}")
  message(FATAL_ERROR "PB-031 portable manifest streams are invalid")
endif()

math(EXPR _last_stream_index "${_expected_streams_length} - 1")
foreach(_stream_index RANGE 0 ${_last_stream_index})
  list(GET PB031_EXPECTED_PORTABLE_STREAMS ${_stream_index} _expected_stream)
  string(REPLACE "|" ";" _stream_fields "${_expected_stream}")
  list(GET _stream_fields 0 _expected_stem)
  list(GET _stream_fields 1 _expected_duration_us)
  list(GET _stream_fields 2 _expected_frequency_hz)
  list(GET _stream_fields 3 _expected_channel)
  list(GET _stream_fields 4 _expected_frame_bytes)
  list(GET _stream_fields 5 _expected_samples_per_frame)
  string(JSON _portable_stream ERROR_VARIABLE _portable_stream_error GET
    "${PB031_MANIFEST_JSON}" streams ${_stream_index})
  if(NOT "${_portable_stream_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 portable manifest stream is unreadable")
  endif()
  pb031_json_require_object_keys("${_portable_stream}" "PB-031 portable manifest stream"
    stem duration_us frequency_hz channel frame_bytes samples_per_frame frame_count lc3 pcm)
  pb031_json_require_string("${_portable_stream}" "PB-031 portable manifest stream.stem"
    "${_expected_stem}" stem)
  pb031_json_require_integer("${_portable_stream}"
    "PB-031 portable manifest stream.duration_us" "${_expected_duration_us}" duration_us)
  pb031_json_require_integer("${_portable_stream}"
    "PB-031 portable manifest stream.frequency_hz" "${_expected_frequency_hz}" frequency_hz)
  pb031_json_require_string("${_portable_stream}" "PB-031 portable manifest stream.channel"
    "${_expected_channel}" channel)
  pb031_json_require_integer("${_portable_stream}"
    "PB-031 portable manifest stream.frame_bytes" "${_expected_frame_bytes}" frame_bytes)
  pb031_json_require_integer("${_portable_stream}"
    "PB-031 portable manifest stream.samples_per_frame" "${_expected_samples_per_frame}"
    samples_per_frame)
  pb031_json_require_integer("${_portable_stream}"
    "PB-031 portable manifest stream.frame_count" "128" frame_count)

  string(JSON _lc3 ERROR_VARIABLE _lc3_error GET "${_portable_stream}" lc3)
  if(NOT "${_lc3_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 portable manifest LC3 fixture is unreadable")
  endif()
  pb031_json_require_object_keys("${_lc3}" "PB-031 portable manifest LC3 fixture"
    path size sha256)
  pb031_json_require_string("${_lc3}" "PB-031 portable manifest LC3 fixture.path"
    "${_expected_stem}.lc3" path)
  math(EXPR _expected_lc3_size "${_expected_frame_bytes} * 128")
  pb031_json_require_integer("${_lc3}" "PB-031 portable manifest LC3 fixture.size"
    "${_expected_lc3_size}" size)
  pb031_json_get("${_lc3}" _lc3_sha256 "PB-031 portable manifest LC3 fixture.sha256"
    STRING sha256)
  pb031_validate_sha256("${_lc3_sha256}" "PB-031 portable manifest LC3 fixture.sha256")
  pb031_verify_fixture("${FIXTURES}/${_expected_stem}.lc3" "${_expected_lc3_size}"
    "${_lc3_sha256}" "PB-031 portable manifest LC3 fixture")

  string(JSON _pcm ERROR_VARIABLE _pcm_error GET "${_portable_stream}" pcm)
  if(NOT "${_pcm_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 portable manifest PCM fixture is unreadable")
  endif()
  pb031_json_require_object_keys("${_pcm}" "PB-031 portable manifest PCM fixture"
    path size sha256)
  pb031_json_require_string("${_pcm}" "PB-031 portable manifest PCM fixture.path"
    "${_expected_stem}.pcm" path)
  math(EXPR _expected_pcm_size "${_expected_samples_per_frame} * 128 * 2")
  pb031_json_require_integer("${_pcm}" "PB-031 portable manifest PCM fixture.size"
    "${_expected_pcm_size}" size)
  pb031_json_get("${_pcm}" _pcm_sha256 "PB-031 portable manifest PCM fixture.sha256"
    STRING sha256)
  pb031_validate_sha256("${_pcm_sha256}" "PB-031 portable manifest PCM fixture.sha256")
  pb031_verify_fixture("${FIXTURES}/${_expected_stem}.pcm" "${_expected_pcm_size}"
    "${_pcm_sha256}" "PB-031 portable manifest PCM fixture")
endforeach()

pb031_json_require_object_keys("${PB031_STATEFUL_MANIFEST_JSON}" "PB-031 stateful manifest"
  schema_version ncs_version liblc3 generator_flags source_portable_manifest recipes)
pb031_json_require_integer("${PB031_STATEFUL_MANIFEST_JSON}"
  "PB-031 stateful manifest.schema_version" "1" schema_version)
pb031_json_require_string("${PB031_STATEFUL_MANIFEST_JSON}"
  "PB-031 stateful manifest.ncs_version" "${PB031_NCS_VERSION}" ncs_version)

string(JSON _stateful_liblc3 ERROR_VARIABLE _stateful_liblc3_error GET
  "${PB031_STATEFUL_MANIFEST_JSON}" liblc3)
if(NOT "${_stateful_liblc3_error}" STREQUAL "NOTFOUND")
  message(FATAL_ERROR "PB-031 stateful manifest liblc3 is unreadable")
endif()
pb031_json_require_object_keys("${_stateful_liblc3}" "PB-031 stateful manifest liblc3"
  semantic_label west_revision)
pb031_json_require_string("${_stateful_liblc3}"
  "PB-031 stateful manifest liblc3.semantic_label" "1.1.2" semantic_label)
pb031_json_require_string("${_stateful_liblc3}"
  "PB-031 stateful manifest liblc3.west_revision" "${PB031_LIBLC3_REVISION}" west_revision)

string(JSON _stateful_flags_type ERROR_VARIABLE _stateful_flags_type_error TYPE
  "${PB031_STATEFUL_MANIFEST_JSON}" generator_flags)
string(JSON _stateful_flags_length ERROR_VARIABLE _stateful_flags_length_error LENGTH
  "${PB031_STATEFUL_MANIFEST_JSON}" generator_flags)
list(LENGTH PB031_EXPECTED_GENERATOR_FLAGS _expected_flags_length)
if(NOT "${_stateful_flags_type_error}" STREQUAL "NOTFOUND" OR
   NOT "${_stateful_flags_length_error}" STREQUAL "NOTFOUND" OR
   NOT "${_stateful_flags_type}" STREQUAL "ARRAY" OR
   NOT "${_stateful_flags_length}" STREQUAL "${_expected_flags_length}")
  message(FATAL_ERROR "PB-031 stateful manifest generator_flags are invalid")
endif()
math(EXPR _last_flag_index "${_expected_flags_length} - 1")
foreach(_flag_index RANGE 0 ${_last_flag_index})
  list(GET PB031_EXPECTED_GENERATOR_FLAGS ${_flag_index} _expected_flag)
  pb031_json_require_string("${PB031_STATEFUL_MANIFEST_JSON}"
    "PB-031 stateful manifest generator_flags[${_flag_index}]" "${_expected_flag}"
    generator_flags ${_flag_index})
endforeach()

string(JSON _source_portable_manifest ERROR_VARIABLE _source_portable_manifest_error GET
  "${PB031_STATEFUL_MANIFEST_JSON}" source_portable_manifest)
if(NOT "${_source_portable_manifest_error}" STREQUAL "NOTFOUND")
  message(FATAL_ERROR "PB-031 stateful source portable manifest is unreadable")
endif()
pb031_json_require_object_keys("${_source_portable_manifest}"
  "PB-031 stateful source portable manifest" path size sha256)
file(SIZE "${PB031_MANIFEST}" _portable_manifest_size)
file(SHA256 "${PB031_MANIFEST}" _portable_manifest_sha256)
pb031_json_require_string("${_source_portable_manifest}"
  "PB-031 stateful source portable manifest.path" "portable-oracle-manifest.json" path)
pb031_json_require_integer("${_source_portable_manifest}"
  "PB-031 stateful source portable manifest.size" "${_portable_manifest_size}" size)
pb031_json_require_sha256("${_source_portable_manifest}"
  "PB-031 stateful source portable manifest.sha256" "${_portable_manifest_sha256}" sha256)

string(JSON _stateful_recipes_type ERROR_VARIABLE _stateful_recipes_type_error TYPE
  "${PB031_STATEFUL_MANIFEST_JSON}" recipes)
string(JSON _stateful_recipes_length ERROR_VARIABLE _stateful_recipes_length_error LENGTH
  "${PB031_STATEFUL_MANIFEST_JSON}" recipes)
set(PB031_EXPECTED_STATEFUL_RECIPES
  "start8_10ms_l|bsim_48k_10ms_120b_l|portable-pcm|bsim_48k_10ms_120b_l.pcm|0|10000|120|480|108|100|plc:0:8,corpus:0:100"
  "start8_10ms_r|bsim_48k_10ms_120b_r|portable-pcm|bsim_48k_10ms_120b_r.pcm|0|10000|120|480|108|100|plc:0:8,corpus:0:100"
  "start11_7p5ms_l|bsim_48k_7p5ms_90b_l|portable-pcm|bsim_48k_7p5ms_90b_l.pcm|0|7500|90|360|111|100|plc:0:11,corpus:0:100"
  "start11_7p5ms_r|bsim_48k_7p5ms_90b_r|portable-pcm|bsim_48k_7p5ms_90b_r.pcm|0|7500|90|360|111|100|plc:0:11,corpus:0:100"
  "start13_7p5ms_l|bsim_48k_7p5ms_90b_l|portable-pcm|bsim_48k_7p5ms_90b_l.pcm|0|7500|90|360|113|100|plc:0:13,corpus:0:100"
  "start13_7p5ms_r|bsim_48k_7p5ms_90b_r|portable-pcm|bsim_48k_7p5ms_90b_r.pcm|0|7500|90|360|113|100|plc:0:13,corpus:0:100"
  "skip20_10ms_l|bsim_48k_10ms_120b_l|generated-pcm|stateful_48k_10ms_skip20_l.pcm|0|10000|120|480|108|100|plc:0:8,corpus:0:20,corpus:21:80"
  "loss48x18_10ms_r|bsim_48k_10ms_120b_r|generated-pcm|stateful_48k_10ms_loss48x18_r.pcm|0|10000|120|480|108|82|plc:0:8,corpus:0:48,plc:0:18,corpus:48:34"
)
list(LENGTH PB031_EXPECTED_STATEFUL_RECIPES _expected_recipes_length)
if(NOT "${_stateful_recipes_type_error}" STREQUAL "NOTFOUND" OR
   NOT "${_stateful_recipes_length_error}" STREQUAL "NOTFOUND" OR
   NOT "${_stateful_recipes_type}" STREQUAL "ARRAY" OR
   NOT "${_stateful_recipes_length}" STREQUAL "${_expected_recipes_length}")
  message(FATAL_ERROR "PB-031 stateful manifest recipes are invalid")
endif()

math(EXPR _last_recipe_index "${_expected_recipes_length} - 1")
foreach(_recipe_index RANGE 0 ${_last_recipe_index})
  list(GET PB031_EXPECTED_STATEFUL_RECIPES ${_recipe_index} _expected_recipe)
  string(REPLACE "|" ";" _recipe_fields "${_expected_recipe}")
  list(GET _recipe_fields 0 _expected_id)
  list(GET _recipe_fields 1 _expected_source_stem)
  list(GET _recipe_fields 2 _expected_reference_kind)
  list(GET _recipe_fields 3 _expected_reference_path)
  list(GET _recipe_fields 4 _expected_reference_first_frame)
  list(GET _recipe_fields 5 _expected_duration_us)
  list(GET _recipe_fields 6 _expected_frame_bytes)
  list(GET _recipe_fields 7 _expected_samples_per_frame)
  list(GET _recipe_fields 8 _expected_output_action_count)
  list(GET _recipe_fields 9 _expected_valid_frame_count)
  list(GET _recipe_fields 10 _expected_steps)
  string(JSON _recipe ERROR_VARIABLE _recipe_error GET "${PB031_STATEFUL_MANIFEST_JSON}"
    recipes ${_recipe_index})
  if(NOT "${_recipe_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 stateful manifest recipe is unreadable")
  endif()
  pb031_json_require_object_keys("${_recipe}" "PB-031 stateful recipe"
    id source_stem duration_us frame_bytes samples_per_frame output_action_count
    valid_frame_count steps reference)
  pb031_json_require_string("${_recipe}" "PB-031 stateful recipe.id" "${_expected_id}" id)
  pb031_json_require_string("${_recipe}" "PB-031 stateful recipe.source_stem"
    "${_expected_source_stem}" source_stem)
  pb031_json_require_integer("${_recipe}" "PB-031 stateful recipe.duration_us"
    "${_expected_duration_us}" duration_us)
  pb031_json_require_integer("${_recipe}" "PB-031 stateful recipe.frame_bytes"
    "${_expected_frame_bytes}" frame_bytes)
  pb031_json_require_integer("${_recipe}" "PB-031 stateful recipe.samples_per_frame"
    "${_expected_samples_per_frame}" samples_per_frame)
  pb031_json_require_integer("${_recipe}" "PB-031 stateful recipe.output_action_count"
    "${_expected_output_action_count}" output_action_count)
  pb031_json_require_integer("${_recipe}" "PB-031 stateful recipe.valid_frame_count"
    "${_expected_valid_frame_count}" valid_frame_count)

  string(JSON _steps_type ERROR_VARIABLE _steps_type_error TYPE "${_recipe}" steps)
  string(JSON _steps_length ERROR_VARIABLE _steps_length_error LENGTH "${_recipe}" steps)
  string(REPLACE "," ";" _expected_steps_list "${_expected_steps}")
  list(LENGTH _expected_steps_list _expected_steps_length)
  if(NOT "${_steps_type_error}" STREQUAL "NOTFOUND" OR
     NOT "${_steps_length_error}" STREQUAL "NOTFOUND" OR
     NOT "${_steps_type}" STREQUAL "ARRAY" OR
     NOT "${_steps_length}" STREQUAL "${_expected_steps_length}")
    message(FATAL_ERROR "PB-031 stateful recipe steps are invalid")
  endif()
  math(EXPR _last_step_index "${_expected_steps_length} - 1")
  foreach(_step_index RANGE 0 ${_last_step_index})
    list(GET _expected_steps_list ${_step_index} _expected_step)
    string(REPLACE ":" ";" _step_fields "${_expected_step}")
    list(GET _step_fields 0 _expected_action)
    list(GET _step_fields 1 _expected_first_sequence)
    list(GET _step_fields 2 _expected_count)
    string(JSON _step ERROR_VARIABLE _step_error GET "${_recipe}" steps ${_step_index})
    if(NOT "${_step_error}" STREQUAL "NOTFOUND")
      message(FATAL_ERROR "PB-031 stateful recipe step is unreadable")
    endif()
    pb031_json_require_object_keys("${_step}" "PB-031 stateful recipe step"
      action first_sequence count)
    pb031_json_require_string("${_step}" "PB-031 stateful recipe step.action"
      "${_expected_action}" action)
    pb031_json_require_integer("${_step}" "PB-031 stateful recipe step.first_sequence"
      "${_expected_first_sequence}" first_sequence)
    pb031_json_require_integer("${_step}" "PB-031 stateful recipe step.count"
      "${_expected_count}" count)
  endforeach()

  string(JSON _reference ERROR_VARIABLE _reference_error GET "${_recipe}" reference)
  if(NOT "${_reference_error}" STREQUAL "NOTFOUND")
    message(FATAL_ERROR "PB-031 stateful recipe reference is unreadable")
  endif()
  pb031_json_require_object_keys("${_reference}" "PB-031 stateful recipe reference"
    kind path first_frame frame_count size sha256)
  pb031_json_require_string("${_reference}" "PB-031 stateful recipe reference.kind"
    "${_expected_reference_kind}" kind)
  pb031_json_require_string("${_reference}" "PB-031 stateful recipe reference.path"
    "${_expected_reference_path}" path)
  pb031_json_require_integer("${_reference}" "PB-031 stateful recipe reference.first_frame"
    "${_expected_reference_first_frame}" first_frame)
  pb031_json_require_integer("${_reference}" "PB-031 stateful recipe reference.frame_count"
    "${_expected_valid_frame_count}" frame_count)
  pb031_json_get("${_reference}" _reference_size "PB-031 stateful recipe reference.size" NUMBER
    size)
  if(NOT "${_reference_size}" MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PB-031 stateful recipe reference.size must be an integer")
  endif()
  pb031_json_get("${_reference}" _reference_sha256
    "PB-031 stateful recipe reference.sha256" STRING sha256)
  pb031_validate_sha256("${_reference_sha256}" "PB-031 stateful recipe reference.sha256")

  if("${_expected_reference_kind}" STREQUAL "portable-pcm")
    pb031_portable_pcm_authority("${_expected_source_stem}" _authority_path _authority_size
      _authority_sha256)
    if(NOT "${_authority_path}" STREQUAL "${_expected_reference_path}" OR
       NOT "${_reference_size}" STREQUAL "${_authority_size}" OR
       NOT "${_reference_sha256}" STREQUAL "${_authority_sha256}")
      message(FATAL_ERROR "PB-031 stateful portable reference does not match authority")
    endif()
  else()
    math(EXPR _generated_reference_size
      "${_expected_valid_frame_count} * ${_expected_samples_per_frame} * 2")
    if(NOT "${_reference_size}" STREQUAL "${_generated_reference_size}")
      message(FATAL_ERROR "PB-031 stateful generated reference size is invalid")
    endif()
    pb031_verify_fixture("${FIXTURES}/${_expected_reference_path}" "${_reference_size}"
      "${_reference_sha256}" "PB-031 stateful generated reference")
  endif()
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${PB031_MANIFEST}
  ${PB031_STATEFUL_MANIFEST}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
  ${PCM_ORACLE_SOURCE}
  ${PCM_ORACLE_HEADER}
  ${STATEFUL_RECIPE_SOURCE}
  ${STATEFUL_RECIPE_HEADER}
  ${PB031_PORTABLE_PCM_REFERENCES}
  ${PB031_PORTABLE_LC3_REFERENCES}
  ${PB031_STATEFUL_GENERATED_REFERENCES}
)

file(SHA256 ${PB031_MANIFEST} PB031_MANIFEST_SHA256)
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c PB031_MAIN_C_SHA256)
file(SHA256 ${PCM_ORACLE_SOURCE} PB031_PCM_ORACLE_C_SHA256)
file(SHA256 ${PCM_ORACLE_HEADER} PB031_PCM_ORACLE_H_SHA256)
file(SHA256 ${PB031_STATEFUL_MANIFEST} PB031_STATEFUL_MANIFEST_SHA256)
file(SHA256 ${STATEFUL_RECIPE_SOURCE} PB031_STATEFUL_RECIPE_C_SHA256)
file(SHA256 ${STATEFUL_RECIPE_HEADER} PB031_STATEFUL_RECIPE_H_SHA256)

configure_file(src/pb031_arm_build_info.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/pb031_arm_build_info.h
  @ONLY
)

target_sources(app PRIVATE
  src/main.c
  ${PCM_ORACLE_SOURCE}
  ${STATEFUL_RECIPE_SOURCE}
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

foreach(reference
    stateful_48k_10ms_skip20_l
    stateful_48k_10ms_loss48x18_r)
  generate_inc_file_for_target(
    app ${FIXTURES}/${reference}.pcm ${CMAKE_CURRENT_BINARY_DIR}/${reference}_pcm.inc)
endforeach()
