cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_bt_bap_pairing_adapter)

# Execute the REAL production Bluetooth pairing adapter
# (src/bt_bap_pairing_adapter.c) against fake backend functions with an
# operation ledger, per-slot result injection, snapshot control, and the
# BT_BAP_PAIRING_ADAPTER_TEST state-reset seam.
target_sources(app PRIVATE
  src/test_bt_bap_pairing_adapter.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_bap_pairing_adapter.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
  BT_BAP_PAIRING_ADAPTER_TEST=1
)
