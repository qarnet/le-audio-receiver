cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_bt_shell_pairing)

# P5: execute the REAL production feature-on `bt unpair` command body
# (src/bt_shell.c compiled under CONFIG_USER_PAIRING_INPUT) through the
# real Zephyr dummy backend, with a fake link implementation of
# pairing_mode_request_reset_sync that records the exact timeout argument
# and returns scripted results.  bt_bap_pairing_reset() is deliberately
# NOT linked: if the legacy branch ever compiled into this build, the
# link fails — a structural proof that the feature-on shell path has no
# legacy API call.
target_sources(app PRIVATE
  src/test_bt_shell_pairing.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_shell.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# The root Kconfig symbols (USER_PAIRING_*) are not part of the test app's
# Kconfig tree (same pattern as the pairing_mode / user_pairing_io direct
# suites), so the values are forced for this translation unit.
# AUDIO_SHELL_TEST exposes the narrow seam wrapper.  The shell reset
# timeout is a distinctive value so the tests can prove the exact argument
# handed to the fake (CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000 makes ticks and
# milliseconds numerically equal).
target_compile_definitions(app PRIVATE
  AUDIO_SHELL_TEST=1
  CONFIG_USER_PAIRING_INPUT=1
  CONFIG_USER_PAIRING_CONTROL=1
  CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS=2000
)
