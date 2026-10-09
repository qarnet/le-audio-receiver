cmake_minimum_required(VERSION 3.20.0)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/dts)
list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/fake_i2s.overlay)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../../boards/nrf54l15dk_nrf54l15_cpuapp.conf
  TUNING REGEX "^CONFIG_AUDIO_DRIFT_(OUTPUT_CLAMP|PHASE_INTEGRAL_CLAMP)=")
list(LENGTH TUNING TUNING_COUNT)
if(NOT TUNING_COUNT EQUAL 2)
  message(FATAL_ERROR "Production drift tuning assignments missing or duplicated")
endif()
string(JOIN "\n" TUNING_TEXT ${TUNING})
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/production-tuning.conf "${TUNING_TEXT}\n")
list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_BINARY_DIR}/production-tuning.conf)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(clock_loop)
target_sources(app PRIVATE clock_i2s.c peer.c test_loop.c
  ${SRC}/audio_i2s.c ${SRC}/audio_drift.c ${SRC}/audio_asrc.c
  ${SRC}/audio_offload.c ${SRC}/flpr_audio_process.c ${SRC}/flpr_ring.c
  ${SRC}/flpr_cache.c ${SRC}/audio_rate_convert.c ${SRC}/audio_clock_actuator_none.c)
target_include_directories(app PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR} ../audio_i2s_common)
target_compile_definitions(app PRIVATE CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=1
  CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=1 CONFIG_AUDIO_OFFLOAD_ASRC=1
  CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619 CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS=20
  CONFIG_I2S_NRFX_TX_BLOCK_COUNT=15 AUDIO_I2S_NATIVE_TEST=1)
set_source_files_properties(${SRC}/audio_offload.c PROPERTIES COMPILE_DEFINITIONS CONFIG_SOC_NRF54L15=1)
target_link_options(app INTERFACE -Wl,--wrap=audio_drift_controller_update)
file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json" SYMBOLIC)
