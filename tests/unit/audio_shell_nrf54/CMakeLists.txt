cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_shell_nrf54)

# Same production shell command bodies as the audio_shell suite, but
# with CONFIG_SOC_NRF54L15 forced for this translation unit so the FLPR
# command groups (production diagnostics in flpr_shell.c, acceptance
# harness in flpr_acceptance_shell.c, bt unpair in bt_shell.c) compile
# and execute against mocked FLPR APIs.  Common subsystem APIs are faked
# from ../audio_shell; audio_perf.c is production source.
set(SHELL_SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../audio_shell)

target_sources(app PRIVATE
  src/test_audio_shell_nrf54.c
  src/fake_flpr_deps.c
  ${SHELL_SUITE_DIR}/src/fake_audio_shell_deps.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/flpr_acceptance_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_perf.c
)

target_include_directories(app PRIVATE
  src
  ${SHELL_SUITE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# CONFIG_SOC_NRF54L15, CONFIG_AUDIO_OFFLOAD_ASRC, and
# CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS are production Kconfig symbols; the
# test app Kconfig does not define them, so they are forced for this
# translation unit to compile the FLPR command bodies (production +
# acceptance) and the ASRC offload section.  The resampler variant string
# is the nRF54 production choice (ASRC linear).
target_compile_definitions(app PRIVATE
  AUDIO_SHELL_TEST=1
  CONFIG_SOC_NRF54L15=1
  CONFIG_AUDIO_OFFLOAD_ASRC=1
  CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=1
  CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=1
  CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=2
)
