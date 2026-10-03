cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/dts)
list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../audio_i2s_common/fake_i2s.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(audio_i2s_asrc_oracle)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_vectors.py
  ${CMAKE_CURRENT_BINARY_DIR}/oracle_vectors.h COMMAND_ERROR_IS_FATAL ANY)
target_sources(app PRIVATE test_sink.c peer.c
  ../audio_i2s_common/fake_i2s.c
  ${SRC}/audio_i2s.c ${SRC}/audio_asrc.c ${SRC}/audio_offload.c
  ${SRC}/flpr_audio_process.c ${SRC}/flpr_ring.c ${SRC}/flpr_cache.c
  ${SRC}/audio_rate_convert.c ${SRC}/audio_clock_actuator_none.c)
target_include_directories(app PRIVATE ${SRC} ../audio_i2s_common ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(app PRIVATE CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=1
  CONFIG_AUDIO_CLOCK_ACTUATOR_NONE=1 CONFIG_AUDIO_OFFLOAD_ASRC=1
  CONFIG_AUDIO_I2S_OUTPUT_SAMPLE_RATE_HZ=47619 CONFIG_AUDIO_I2S_WRITE_TIMEOUT_MS=20
  AUDIO_I2S_NATIVE_TEST=1)
# Select the real offload manager branch only, not the native Zephyr platform.
set_source_files_properties(${SRC}/audio_offload.c PROPERTIES
  COMPILE_DEFINITIONS CONFIG_SOC_NRF54L15=1)
