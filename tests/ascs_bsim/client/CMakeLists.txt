cmake_minimum_required(VERSION 3.20.0)
set(CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../bsim/client/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/prj.conf")
# Native BSim peers are ELF32; align initial CMake probes with verified 32-bit library paths.
set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -m32")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -m32")
set(CMAKE_ASM_FLAGS_INIT "${CMAKE_ASM_FLAGS_INIT} -m32")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ascs_client)
target_sources(app PRIVATE ../../bsim/client/src/main.c ../../bsim/client/src/bsim_tx.c
  client.c ../common/lane.c)
target_include_directories(app PRIVATE ../../bsim/client/src ../common)
foreach(stem bsim_48k_10ms_120b_l bsim_48k_10ms_120b_r bsim_48k_7p5ms_90b_l bsim_48k_7p5ms_90b_r)
  generate_inc_file_for_target(app ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3/${stem}.lc3
    ${ZEPHYR_BINARY_DIR}/include/generated/${stem}.lc3.inc)
endforeach()
zephyr_include_directories(${BSIM_COMPONENTS_PATH}/libUtilv1/src/ ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/)
