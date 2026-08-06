cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_pairing_mode)

# P1: execute the REAL production pairing-mode transition owner
# (src/pairing_mode.c) against fake injected operations with an operation
# ledger, per-op event semaphores, per-op blocking gates, and short Kconfig
# timing values that preserve the production ratios (bond half 40 ms,
# reset half 8 ms, feedback 80 ms = 10 half-periods; bond hold 100 ms,
# reset hold 200 ms).
target_sources(app PRIVATE
  src/test_pairing_mode.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/pairing_mode.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# The root Kconfig symbols (USER_PAIRING_*) are not part of the test app's
# Kconfig tree (same pattern as audio_shell_nrf54 forcing
# CONFIG_SOC_NRF54L15 / CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS), so the values
# are forced for this translation unit.  PAIRING_MODE_TEST exposes the
# state-reset seam only to tests.  Timing values preserve the production
# ratios (bond half 100 ms, reset half 20 ms, feedback 200 ms = ten
# half-periods = five complete flashes; bond hold 250 ms, reset hold
# 500 ms) and are multiples of the native_sim 10 ms tick so every delayed
# work deadline lands on a tick boundary deterministically.
target_compile_definitions(app PRIVATE
  PAIRING_MODE_TEST=1
  CONFIG_USER_PAIRING_CONTROL=1
  CONFIG_USER_PAIRING_BOND_HOLD_MS=250
  CONFIG_USER_PAIRING_RESET_HOLD_MS=500
  CONFIG_USER_PAIRING_BOND_LED_HALF_PERIOD_MS=100
  CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS=20
  CONFIG_USER_PAIRING_RESET_FEEDBACK_MS=200
  CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=2048
  CONFIG_USER_PAIRING_WORKQ_PRIORITY=5
)
