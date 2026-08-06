cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_user_pairing_io)

# P2: execute the REAL production user-pairing I/O adapter
# (src/user_pairing_io.c) against the real input subsystem, the real
# gpio-keys driver, and the real gpio-emul controller on native_sim, with
# fake link implementations of the two P1 request APIs that record public
# calls/results.
target_sources(app PRIVATE
  src/test_user_pairing_io.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/user_pairing_io.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# The root Kconfig symbols (USER_PAIRING_*) are not part of the test app's
# Kconfig tree (same pattern as audio_shell_nrf54 forcing
# CONFIG_SOC_NRF54L15 / CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS), so the values
# are forced for this translation unit.  USER_PAIRING_IO_TEST exposes the
# state-reset and LED fault-injection seams only to tests.  Hold thresholds
# preserve the production ordering (reset > bonding) and are multiples of
# the native_sim 1 ms tick so every delayed-work deadline lands
# deterministically; the debounce equals the overlay's
# debounce-interval-ms (BUILD_ASSERT in the production source).
target_compile_definitions(app PRIVATE
  USER_PAIRING_IO_TEST=1
  CONFIG_USER_PAIRING_INPUT=1
  CONFIG_USER_PAIRING_CONTROL=1
  CONFIG_USER_PAIRING_BOND_HOLD_MS=100
  CONFIG_USER_PAIRING_RESET_HOLD_MS=200
  CONFIG_USER_PAIRING_DEBOUNCE_MS=30
)
