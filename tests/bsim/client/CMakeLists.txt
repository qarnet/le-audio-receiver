# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bap_unicast_client_bsim)

# ── BSIM entry point ────────────────────────────────────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

# ── Upstream sample sources (reuse, no modification) ───────────────
target_sources(app PRIVATE
  ${ZEPHYR_BASE}/samples/bluetooth/bap_unicast_client/src/stream_tx.c
)

zephyr_sources_ifdef(CONFIG_LIBLC3
  ${ZEPHYR_BASE}/samples/bluetooth/bap_unicast_client/src/stream_lc3.c)

# ── BSIM test installer (test_id=valid_lc3_client) ──────────────────
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bsim_client_main.c
)

# ── Forced-include: preset override + send wrapper ──────────────────
target_compile_options(app PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/src/preset_override.h
)

# ── Include paths ──────────────────────────────────────────────────
target_include_directories(app PRIVATE
  ${ZEPHYR_BASE}/samples/bluetooth
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# BSIM component headers — needed for bs_types.h, bs_tracing.h, etc.
zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
)
