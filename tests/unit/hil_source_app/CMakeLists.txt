cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hil_source_app)

# RH1B native public-boundary suite for the source app coordinator.
# Compiles the coordinator + output formatter with the scripted fake
# backend and the exact RH1A core modules (protocol, record, signal,
# state); it does NOT compile the real Bluetooth backend or TX driver.
target_sources(app PRIVATE
  src/test_hil_source_app.c
  src/fake_hil_source_backend.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/app/src/hil_source_app.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/app/src/hil_source_output.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_protocol.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_record.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_signal.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_state.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/app/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core
)

# Backend/output injection seams: only the test build defines
# CONFIG_HIL_SOURCE_APP_TEST (the production app never sets it).
target_compile_definitions(app PRIVATE
  CONFIG_HIL_SOURCE_APP_TEST=1
)
