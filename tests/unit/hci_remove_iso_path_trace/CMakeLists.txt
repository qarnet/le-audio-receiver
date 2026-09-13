cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hci_remove_iso_path_trace)

target_sources(app PRIVATE
  src/test_hci_remove_iso_path_trace.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/hci_remove_iso_path_trace.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
