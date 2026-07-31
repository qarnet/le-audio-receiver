cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_runtime)

# T1A: execute the REAL nRF54 restart body on native_sim.
#
# FLPR_RUNTIME_NATIVE_TEST switches src/flpr_runtime.c from the DT-derived
# production constants to test-provided VPR register storage, source and
# execution arrays, and time/cache/barrier hooks.  The real production
# restart function runs; the mock handshake + shadow VPR HAL only inject
# transport and register-file behavior.
target_sources(app PRIVATE
  src/test_flpr_runtime.c
  src/flpr_runtime_hooks.c
  src/mock_flpr_handshake.c
  src/mock_nrf_vpr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_runtime.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring.c   # real flpr_ring_crc32
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_cache.c  # native barrier hooks
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE FLPR_RUNTIME_NATIVE_TEST)
