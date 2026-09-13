#
# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0
#
# FLPR (RISC-V VPR) application for nRF54L15.
# Minimal Zephyr: IPC service + handshake/ heartbeat protocol. No console.
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flpr_handshake)

# Shared protocol header lives one directory up.
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

target_sources(app PRIVATE
  main.c
  ../flpr_ring.c
  ../flpr_cache.c
  ../flpr_audio_process.c
  ../audio_asrc.c
)

# FLPR-image acceptance handlers (RING_TEST/STALL/STRESS/FAULT_HANG +
# diagnostic counters) gated behind CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS
# (src/flpr/Kconfig; enabled in src/flpr/prj.conf for the current lab
# build).  Config-off images contain no acceptance symbols.
zephyr_sources_ifdef(CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS acceptance.c)

# LSP compile-database link: expose the RISC-V image's compile_commands.json
# at this app root (src/flpr/) for clangd closest-ancestor discovery, so the
# FLPR firmware files parse with the rv32emc toolchain instead of inheriting
# the repo-root ARM database.  Idempotent; re-created on reconfigure.
file(CREATE_LINK
     "${CMAKE_BINARY_DIR}/compile_commands.json"
     "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
     SYMBOLIC)
