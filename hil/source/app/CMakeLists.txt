# SPDX-License-Identifier: Apache-2.0
#
# Dedicated nRF5340DK LE Audio source fixture (RH1B): BAP unicast client
# that boots with persistent identity/bonds, accepts strict RH1A JSON via
# one quiet serial shell command, and runs a bounded connect/stream/teardown
# lifecycle with deterministic HIL1 records.
#
# Compiles the exact RH1A core modules (protocol, record, signal, state)
# plus the app coordinator, output queue/writer, TX send driver, and the
# real Bluetooth/BAP backend.  Production build has no test seam.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hil_source_app)

target_sources(app PRIVATE
  src/main.c
  src/hil_source_app.c
  src/hil_source_controller_time.c
  src/hil_source_output.c
  src/hil_source_tx.c
  src/hil_source_bap.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/hil_source_protocol.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/hil_source_record.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/hil_source_signal.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../core/hil_source_state.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../core
)

# LSP compile-database link: expose this app's compile_commands.json one
# directory up (hil/source/) so clangd's closest-ancestor discovery serves
# both this app tree and the ../core RH1A modules compiled into it.
# Idempotent; re-created on every reconfigure.
file(CREATE_LINK
     "${CMAKE_BINARY_DIR}/compile_commands.json"
     "${CMAKE_CURRENT_SOURCE_DIR}/../compile_commands.json"
     SYMBOLIC)
