cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_ring_mgr)

# T1B: execute the REAL ring manager on native_sim.
#
# FLPR_RING_MGR_NATIVE_TEST switches src/flpr_ring_mgr.c from the
# DT-derived shared SRAM pointers to two aligned host arrays and routes
# the cycle counter through a test-controlled source.  The production
# static IPC handlers run through the captured handlers of the handshake
# mock; flpr_ring.c and flpr_cache.c are real production sources.
target_sources(app PRIVATE
  src/test_flpr_ring_mgr.c
  src/flpr_ring_mgr_hooks.c
  src/mock_flpr_handshake.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring_mgr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_cache.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE FLPR_RING_MGR_NATIVE_TEST)
