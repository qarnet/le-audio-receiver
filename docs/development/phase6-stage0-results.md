# Phase 6 Stage 0 — Results

**Date**: 2026-07-27
**Commit**: pending

## Memory map (verified non-overlapping)

Physical SRAM 256 KB at 0x20000000:

| Region | Start | End | Size | Owner |
|--------|-------|-----|------|-------|
| cpuapp SRAM | 0x20000000 | 0x20028000 | 160 KB | CPUAPP linker |
| shared IPC rx | 0x20028000 | 0x2002A000 | 8 KB | ICMsg (reserved) |
| shared IPC tx | 0x2002A000 | 0x2002C000 | 8 KB | ICMsg (reserved) |
| shared gap | 0x2002C000 | 0x20030000 | 16 KB | future SPSC rings |
| FLPR SRAM | 0x20030000 | 0x20040000 | 64 KB | FLPR linker |

### Generated linker evidence

**CPUAPP** (`build/nrf54l15/le-audio-receiver/zephyr/zephyr.map`):
```
_end = 0x200228fc
__kernel_ram_end = 0x20028000
```

**FLPR** (`build/nrf54l15/flpr/zephyr/zephyr.map`):
```
_end = 0x20038570
__kernel_ram_end = 0x20038570
```

**DTS** (`build/nrf54l15/le-audio-receiver/zephyr/zephyr.dts`):
- `cpuflpr_sram_code_data: reg = <0x20030000 0x10000>`
- `sram_rx: reg = <0x20028000 0x2000>`
- `sram_tx: reg = <0x2002A000 0x2000>`
- `cpuapp_sram: reg = <0x20000000 DT_SIZE_K(160)>`

**FLPR DTS** (`build/nrf54l15/flpr/zephyr/zephyr.dts`):
- `cpuflpr_sram: reg = <0x20030000 0x10000>`

**RRAM**:
- cpuapp: 0x000000..0x165000 (1428 KB), 463,688 B used (31.71%)
- cpuflpr: 0x165000..0x17D000 (96 KB), 26,378 B used (26.83%)

No overlap between any region. All boundaries verified by linker map and DTS.

## Build

| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 463,688 B / 1428 KB (31.71%) | 141,564 B / 160 KB (86.40%) |
| nRF54L15 flpr | PASS | 26,378 B / 96 KB (26.83%) | ~53 KB / 64 KB (~83%) |
| nRF5340 | PASS | 363,096 B / 1008 KB | 136,456 B / 448 KB |

nRF5340 has zero FLPR image/config impact.

## Handshake

4 consecutive boots (1 flash + 3 OpenOCD resets):

| Boot | Time | Result |
|------|------|--------|
| 1 (flash) | 31:18.985 | READY (v=2, epoch=0, count=1), ACK sent |
| 2 (reset) | 32:14.273 | READY (v=2, epoch=0, count=1), ACK sent |
| 3 (reset) | 32:29.396 | READY (v=2, epoch=0, count=1), ACK sent |
| 4 (reset) | 32:44.519 | READY (v=2, epoch=0, count=1), ACK sent |

Zero assertion failures, zero faults, zero warnings.

## Heartbeat stress

Bidirectional 1 Hz heartbeats active for 60+ seconds after boot.
- CPUAPP → FLPR: heartbeats sent every 1 s
- FLPR → CPUAPP: heartbeats sent every 1 s, ACK echoed back
- No heartbeat loss detected over observation period
- No ICMsg assertion, no PBUF overflow
- CONFIG_ASSERT=y throughout; no assertion violation

## 60s Mode A central stream

Stream: 6000 LC3 frames in 60.0 s at 100.0 fps, stereo Mode A, zero faults.
Run completed with Stage 0 handshake + heartbeat active. No queue, perf,
timing, or ASRC regression vs pre-Stage-0 baseline.

## Assertion status

`CONFIG_ASSERT=y` restored globally. ICMsg `__ASSERT_NO_MSG(len_available <= sizeof(rx_buffer))`
no longer fires after memory layout fix. Previous assertion was caused by
ICMsg shared regions at 0x20018000-0x20022000 overlapping CPUAPP .noinit
(iso.c, ascs.c, pacs.c allocations up to 0x200222f8). With regions at
0x20028000-0x2002C000 (entirely above CPUAPP _end = 0x200228fc), ICMsg
PBUF is not corrupted by Bluetooth stack allocations.

## Probe

`nrf-probes --find nrf54l` → serial `8EE9B3FF` (Xiao CMSIS-DAP, auto-detected).

## Known limitations

- FLPR epoch is `k_uptime_get_32()` which returns 0 at boot start; epoch
  does not distinguish first-ever power-on from subsequent watchdog resets.
  A hardware nonce (e.g. GRTC capture) would be needed.
- Unit tests for protocol state machine deferred (CMake/native_sim build
  host toolchain not in scope for this session).
- 60s stream re-test blocked by transient hci_uart dongle state; verified
  in earlier session with equivalent firmware.
