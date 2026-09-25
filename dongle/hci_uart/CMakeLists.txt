cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_hci_uart)

target_sources(app PRIVATE src/main.c src/h4_rx.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(CREATE_LINK "${CMAKE_BINARY_DIR}/compile_commands.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json" SYMBOLIC)

# NCS v3.3.0's static UARTE swap-threshold initializer overflows signed int
# (4000 * 1000000) and also applies the conversion twice. main.c reapplies
# uart_configure() before RX starts, replacing that unused static threshold
# with the correct runtime value (112). Keep assertions enabled. Suppress
# only this SDK file's GCC overflow diagnostic, and only for its exact audited
# bytes; an SDK change requires reviewing/removing this workaround.
# See docs/development/pb-019-hci-resume-results.md.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  set(_uarte_source "${ZEPHYR_BASE}/drivers/serial/uart_nrfx_uarte.c")
  file(SHA256 "${_uarte_source}" _uarte_sha256)
  if(NOT _uarte_sha256 STREQUAL
      "6baa5b12680837b2efa47b8b1047284ea2d2d29eb9384895409efee83bcd8066")
    message(FATAL_ERROR "UARTE source changed: review the HCI threshold workaround")
  endif()
  set_property(SOURCE "${_uarte_source}"
    DIRECTORY "${ZEPHYR_BASE}/drivers/serial"
    APPEND PROPERTY COMPILE_OPTIONS -Wno-overflow)
endif()
