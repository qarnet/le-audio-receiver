cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_audio_shell)

# Compile and execute the REAL production shell command bodies
# (src/audio_shell.c + src/bt_shell.c) registered normally and driven
# through the Zephyr dummy shell backend.  Subsystem APIs
# (stats/drift/volume/sink/unpair) are mocked; audio_perf.c is compiled
# for real so queue fields and the deadline percentage come from
# production instrumentation.
#
# The `unit.audio_shell_noperf` testcase disables CONFIG_AUDIO_PERF_MEASUREMENT
# so the same production file is proven without measurement enabled.
target_sources(app PRIVATE
  src/test_audio_shell.c
  src/fake_audio_shell_deps.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_shell.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/bt_shell.c
)

zephyr_sources_ifdef(CONFIG_AUDIO_PERF_MEASUREMENT
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_perf.c
)

target_include_directories(app PRIVATE
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

# AUDIO_SHELL_TEST exposes the narrow test seam wrappers.  The resampler
# variant string is chosen by test-only compile definition (production
# chooses it via the AUDIO_RESAMPLER Kconfig choice); the nRF54 suite
# compiles the ASRC linear variant instead.
target_compile_definitions(app PRIVATE
  AUDIO_SHELL_TEST=1
  CONFIG_AUDIO_RESAMPLER_IDENTITY=1
)
