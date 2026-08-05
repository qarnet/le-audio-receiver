cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_shell_noperf)

# T6: same production shell command bodies as the audio_shell suite, but
# built with CONFIG_AUDIO_PERF_MEASUREMENT=n so the disabled-perf branch
# of the production file is proven: truthful unavailable (zero) deadline
# percentage, zeroed queue fields, no division by zero.  The test source
# and subsystem fakes are shared with ../audio_shell (same suite family).
set(SHELL_SUITE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../audio_shell)

target_sources(app PRIVATE
  ${SHELL_SUITE_DIR}/src/test_audio_shell.c
  ${SHELL_SUITE_DIR}/src/fake_audio_shell_deps.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_shell.c
)

target_include_directories(app PRIVATE
  ${SHELL_SUITE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
  AUDIO_SHELL_TEST=1
  CONFIG_AUDIO_RESAMPLER_IDENTITY=1
)
