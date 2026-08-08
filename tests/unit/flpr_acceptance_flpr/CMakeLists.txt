cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_acceptance_flpr)

# Direct native suite for the FLPR-image acceptance module
# (src/flpr/acceptance.c).  The module takes an injected send/wake
# dependency table (no ipc_service/device/DT/irq references), so its
# message/state logic — RING_TEST_START/STOP report cascade, RING_STALL
# timer + ACK echo, STRESS_PING/PONG, FAULT_HANG ACK-before-spin,
# diagnostic note hooks — is compiled and executed directly on
# native_sim against a fake transport.
target_sources(app PRIVATE
  src/test_flpr_acceptance_flpr.c
  src/fake_flpr_deps.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr/acceptance.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr
)
