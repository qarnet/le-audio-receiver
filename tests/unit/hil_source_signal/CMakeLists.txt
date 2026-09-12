cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_hil_source_signal)

# RH1A native public-boundary suite for the deterministic signal
# generator, the real liblc3 encoder, and Mode A/B packing.  Compiles the
# exact repo-owned core signal module and the checked-in sine LUT.
target_sources(app PRIVATE
  src/test_hil_source_signal.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core/hil_source_signal.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../hil/source/core
)
