# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_unicast_client_bsim)

# ── BSIM entry point ────────────────────────────────────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

# ── Repo-owned deterministic multi-channel TX (real BAP send with
#    checked-in LC3 corpus frames) ─────────────────────────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_tx.c
)

# Embed only the four fixed BSim LC3 corpus streams.  file2hex emits raw
# initializer bytes; bsim_tx.c owns the array names and selects frames by
# logical sequence.
set(lc3_fixture_dir ${CMAKE_CURRENT_SOURCE_DIR}/../../fixtures/lc3)
set(lc3_fixture_inc_dir ${ZEPHYR_BINARY_DIR}/include/generated)
generate_inc_file_for_target(
  app
  ${lc3_fixture_dir}/bsim_48k_10ms_120b_l.lc3
  ${lc3_fixture_inc_dir}/bsim_48k_10ms_120b_l.lc3.inc
)
generate_inc_file_for_target(
  app
  ${lc3_fixture_dir}/bsim_48k_10ms_120b_r.lc3
  ${lc3_fixture_inc_dir}/bsim_48k_10ms_120b_r.lc3.inc
)
generate_inc_file_for_target(
  app
  ${lc3_fixture_dir}/bsim_48k_7p5ms_90b_l.lc3
  ${lc3_fixture_inc_dir}/bsim_48k_7p5ms_90b_l.lc3.inc
)
generate_inc_file_for_target(
  app
  ${lc3_fixture_dir}/bsim_48k_7p5ms_90b_r.lc3
  ${lc3_fixture_inc_dir}/bsim_48k_7p5ms_90b_r.lc3.inc
)

# ── BSIM test installer (one test_id per T4 scenario) ────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_client_main.c
)

# ── Include paths ──────────────────────────────────────────────────
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# BSIM component headers — needed for bs_types.h, bs_tracing.h, etc.
zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)

# LSP compile-database link: expose this BSim client image's
# compile_commands.json at the app root for clangd closest-ancestor
# discovery.  Absolute target (build tree lives in the shared SDK
# bsim_out); scripts/gen-lsp-links.sh can also repair it.
file(CREATE_LINK
     "${CMAKE_BINARY_DIR}/compile_commands.json"
     "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
     SYMBOLIC)
