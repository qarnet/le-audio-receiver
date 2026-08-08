cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_acceptance)

# Direct suite for the cpuapp FLPR acceptance module.
#
# Compiles the REAL acceptance module (src/flpr_acceptance.c), the REAL
# core ring manager (src/flpr_ring_mgr.c with host ring arrays via
# FLPR_RING_MGR_NATIVE_TEST), the shared control-ACK engine, and the real
# flpr_ring.c/flpr_cache.c, against a faithful handshake mock that
# implements BOTH the production handler slot (reset ACK + consumer) and
# the diagnostic slot (report/stall-ack/pong/hang).  The production
# static handlers run through the captured handlers of the mock.
set(RING_MGR_SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../flpr_ring_mgr)

target_sources(app PRIVATE
  src/test_flpr_acceptance.c
  src/mock_flpr_handshake.c
  ${RING_MGR_SUITE_DIR}/src/flpr_ring_mgr_hooks.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_acceptance.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_control_ack.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring_mgr.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_ring.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_cache.c
)

target_include_directories(app PRIVATE
  src
  ${RING_MGR_SUITE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS is a production Kconfig symbol; the
# test app Kconfig does not define it, so it is forced for this
# translation unit so the core ring manager's config-gated acceptance
# hooks (counters, stall check, test-active) compile in, and the
# acceptance module is compiled as production code.
target_compile_definitions(app PRIVATE
  CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=1
  FLPR_RING_MGR_NATIVE_TEST
  FLPR_ACCEPTANCE_NATIVE_TEST
  FLPR_CONTROL_ACK_NATIVE_TEST
)
