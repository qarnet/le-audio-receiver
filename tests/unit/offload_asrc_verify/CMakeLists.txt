cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_offload_asrc_verify)

target_sources(app PRIVATE
  src/test_offload_asrc_verify.c
  src/mock_ring_mgr_verify.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_offload.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_asrc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../audio_offload/src
)

# Force CONFIG_SOC_NRF54L15 so audio_offload.c uses the FLPR path.
target_compile_definitions(app PRIVATE CONFIG_SOC_NRF54L15=1)

# Enable ASRC offload WITH shadow verification (this suite's purpose).
target_compile_definitions(app PRIVATE CONFIG_AUDIO_OFFLOAD_ASRC=1)
target_compile_definitions(app PRIVATE CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=1)
