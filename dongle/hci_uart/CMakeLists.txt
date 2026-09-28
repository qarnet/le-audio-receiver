cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_hci_uart)

target_sources(app PRIVATE src/main.c src/h4_rx.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json" SYMBOLIC)

# NCS v3.4.1 initializes only byte 0 and the tail of the next bounce buffer.
# Generate an audited copy with full sentinel initialization. Defer the old/new
# boundary for every successful swap until count and DMA pointer agree across
# a profile-specific 1 us quiet interval; settle first-byte anomaly recovery
# the same way. The guarded SDK source remains unchanged on disk. Host replay
# and diagnostic hardware passes do not qualify the HCI transport.
# The audited source also has an overflowing static swap-threshold initializer.
# main.c calls uart_configure() before RX, replacing that unused threshold
# with 112. Keep assertions; suppress -Woverflow only on this generated copy.
# See docs/development/pb-019-hci-resume-results.md.
set(_uarte_source "${ZEPHYR_BASE}/drivers/serial/uart_nrfx_uarte.c")
file(SHA256 "${_uarte_source}" _uarte_sha256)
if(NOT _uarte_sha256 STREQUAL
    "d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3")
  message(FATAL_ERROR "UARTE source changed: review the HCI compatibility repair")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_uarte_compat "${CMAKE_BINARY_DIR}/uart_nrfx_uarte_compat.c")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/patch_ncs_uarte.py"
  "${_uarte_source}" "${_uarte_compat}"
  RESULT_VARIABLE _uarte_result ERROR_VARIABLE _uarte_error)
if(NOT _uarte_result EQUAL 0)
  message(FATAL_ERROR "UARTE compatibility generation failed: ${_uarte_error}")
endif()

get_target_property(_uarte_sources drivers__serial SOURCES)
set(_uarte_new_sources "")
set(_uarte_found 0)
foreach(_source IN LISTS _uarte_sources)
  if(_source MATCHES "(^|/)uart_nrfx_uarte\\.c$")
    math(EXPR _uarte_found "${_uarte_found} + 1")
    list(APPEND _uarte_new_sources "${_uarte_compat}")
  else()
    list(APPEND _uarte_new_sources "${_source}")
  endif()
endforeach()
if(NOT _uarte_found EQUAL 1)
  message(FATAL_ERROR "Expected one UARTE driver in drivers__serial, found ${_uarte_found}")
endif()
set_target_properties(drivers__serial PROPERTIES SOURCES "${_uarte_new_sources}")
target_include_directories(drivers__serial PRIVATE "${ZEPHYR_BASE}/drivers/serial")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties("${_uarte_compat}" TARGET_DIRECTORY drivers__serial
    PROPERTIES COMPILE_OPTIONS -Wno-overflow)
endif()
