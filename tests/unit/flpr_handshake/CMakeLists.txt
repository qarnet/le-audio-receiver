cmake_minimum_required(VERSION 3.20.0)

# The boards/<board>.overlay auto-discovery does not match the
# native_sim/native/64 variant board string, so apply the fake-backend
# overlay explicitly.
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/boards/native_sim.overlay
    CACHE STRING "Devicetree overlay for the fake IPC backend" FORCE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_flpr_handshake)

# T1C: execute the REAL handshake implementation on native_sim against a
# fake IPC service backend (patterned on the NCS v3.3.0 ipc_service test
# backend).  Production src/flpr_handshake.c uses the real Zephyr
# ipc_service_* APIs; the fake backend captures the endpoint config,
# invokes the production callbacks, records sent messages, and exposes
# test controls.  FLPR_HANDSHAKE_NATIVE_TEST adds the test-only hooks.
target_sources(app PRIVATE
  src/test_flpr_handshake.c
  src/fake_ipc_backend.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_handshake.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE FLPR_HANDSHAKE_NATIVE_TEST)
