# Phase 6 Stage 0 — FLPR boot, memory map, handshake

## Goal

Add nRF54L15-only custom Zephyr FLPR image executed from reserved SRAM, stored
in FLPR RRAM, launched by CPUAPP, and prove repeatable boot handshake without
changing audio behavior. Keep nRF5340 untouched.

## Ground truth

- NCS v3.3.0 target: `nrf54l15dk/nrf54l15/cpuflpr` (SRAM execution).
- Reference: `nrf/samples/ipc/ipc_service/` and `nordic-flpr` launcher pattern.
- Stock FLPR RRAM base `0x165000`; generated DTS/HEX addresses must be inspected,
  not assumed.
- FLPR RV32E e/m/c; no FPU, no atomic extension.
- Existing Xiao CPUAPP overlay owns UART20/I2S20/TIMER20. Do not disturb pins,
  RADIO, MPSL resources, or I2S DMA memory.

## Scope

1. Add custom FLPR application directory with minimal Zephyr config, no console,
   deterministic startup, fixed protocol-version constant.
2. Add nRF54-only sysbuild image with explicit configure/flash dependency. Do
   not add FLPR image to nRF5340.
3. Define CPUAPP source-memory RRAM partition, FLPR execution SRAM, and minimal
   shared-memory/control area using project overlays. Prove regions do not
   overlap CPUAPP linker RAM, I2S slab, settings, or other partitions by reading
   generated DTS/map/HEX.
4. Use official VPR launcher and IPC service ICMsg/VEVIF pattern. Stage 0 only
   sends versioned READY and CPUAPP ACK/heartbeat counters; no PCM.
5. CPUAPP startup must tolerate absent/mismatched FLPR and keep BLE/audio running,
   while logging one actionable error and exposing handshake status/counters via
   shell/status. No boot hang.
6. Extend `fw-build-54l15` and `fw-flash-54l15`/OpenOCD scripts to discover and
   program both generated images at addresses encoded in HEX. Verify each image.
   Do not hardcode stale probe IDs. Do not mass erase/recover.
7. Add host/native unit tests for protocol structs/state machine where possible:
   version mismatch, duplicate READY, timeout, ACK, reset epoch.

## Gates

- Both target builds pass; nRF5340 has no FLPR image/config impact.
- Generated memory maps and HEX ranges documented exactly.
- nRF54 normal flash writes/verifies CPUAPP and FLPR source image.
- Serial capture before reset shows FLPR READY + CPUAPP ACK on three normal
  resets; no warning/error/fault.
- 60 s Mode A central stream remains stable with Stage 0 image active; no queue,
  perf, timing, or ASRC regression.
- Commit code, scripts, handoff, and result evidence only after gates.

## Non-scope

- No PCM rings, ASRC offload, HPF, ICBmsg comparison, BabbleSim, package install,
  direct RADIO access, destructive recovery, push/release.
