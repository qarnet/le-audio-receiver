cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_volume)

# T2C: execute the REAL production volume implementation (VCP branch)
# against a test-local shadow of the NCS v3.3.0 VCP renderer types and a
# fake bt_vcp_vol_rend_register() backend, plus the REAL audio_perf.c for
# hook-balance proof.  The shadow lives under include/ (searched before
# the Zephyr includes for this suite only) and the VCP branch is enabled
# with test-only compile definitions — no invalid Kconfig assignments.
target_sources(app PRIVATE
  src/test_volume.c
  src/fake_vcp.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_volume.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src/audio_perf.c
)

target_include_directories(app PRIVATE
  include
  src
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)

target_compile_definitions(app PRIVATE
  CONFIG_BT_VCP_VOL_REND=1
  CONFIG_BT_AUDIO_VOL_DEFAULT=195
)
