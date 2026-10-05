cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ltv_bounds)
target_sources(app PRIVATE test_ltv.c ${ZEPHYR_BASE}/subsys/bluetooth/audio/audio.c)
option(PB051_LTV_GUARD "Exercise production per-entry LTV guard" ON)
if(PB051_LTV_GUARD)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_audio_ltv_guard.c)
  target_link_options(app INTERFACE -Wl,--wrap=bt_audio_data_parse)
endif()
# Header-only buffer arithmetic needs these when Bluetooth is deliberately
# disabled. No controller/radio or fabricated parser implementation is linked.
target_compile_definitions(app PRIVATE CONFIG_BT_AUDIO_LOG_LEVEL=0
  CONFIG_BT_BUF_EVT_RX_COUNT=16 CONFIG_BT_BUF_ACL_TX_COUNT=3)
