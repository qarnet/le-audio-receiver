# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_unicast_client_bsim)

# ── BSIM entry point ────────────────────────────────────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

# ── Repo-owned deterministic multi-channel TX (replaces the upstream
#    repeated-tone stream_tx/stream_lc3 helpers; real BAP send + liblc3) ─
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_tx.c
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
