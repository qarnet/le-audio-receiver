cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_runtime)

target_sources(app PRIVATE
  src/test_flpr_runtime.c
  src/mock_nrf_vpr.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# Provide mock implementations for flpr_handshake, flpr_ring, audio_offload
target_sources(app PRIVATE
  src/mock_flpr_deps.c
)

# Compile the real flpr_runtime.c stub (non-nRF54L stubs for structural tests)
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_runtime.c
)
