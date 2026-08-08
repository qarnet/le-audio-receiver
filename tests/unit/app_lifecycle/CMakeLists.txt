cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_app_lifecycle)

# Execute the REAL production boot coordinator (src/app_lifecycle.c)
# against test-local operations. main.c is NOT compiled here — hardware
# wiring remains proven by the production builds.
target_sources(app PRIVATE
  src/test_app_lifecycle.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/app_lifecycle.c
)

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
