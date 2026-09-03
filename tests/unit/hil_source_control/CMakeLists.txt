cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hil_source_control)

# RH1A native public-boundary suite for the strict inbound command schema,
# the HIL1 record envelope, and the pure run-state machine.  Compiles the
# exact repo-owned core modules, no liblc3 needed.
target_sources(app PRIVATE
  src/test_hil_source_control.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_protocol.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_record.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_state.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core
)
