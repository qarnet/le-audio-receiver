cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_offload)

target_sources(app PRIVATE
  src/test_audio_offload.c
  src/mock_ring_mgr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_offload.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# Force CONFIG_SOC_NRF54L15 so audio_offload.c uses the FLPR path.
target_compile_definitions(app PRIVATE CONFIG_SOC_NRF54L15=1)
