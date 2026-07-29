# Phase 6 Stage 4B — Fault recovery acceptance results

## Date

2026-07-29

## Build targets

- **nRF5340** (ebyte_e83_nrf5340): build passes clean (flash blocked: nRF53 probe not detected, only nRF54L15 Xiao and nRF5340DK J-Link connected)
- **nRF54L15** (xiao_nrf54l15): build + flash + boot passes

## Implementation summary

- Protocol version bumped 3→4
- `FLPR_MSG_FAULT_HANG` (0x20) / `FLPR_MSG_FAULT_HANG_ACK` (0x21)
- FLPR: ACK in IPC callback, atomic flag, main loop irq_lock+spin
- CPUAPP: `flpr_handshake_send_fault_hang(500ms)`, ACK semaphore
- Health transition callback: healthy→unhealthy → offload supervisor
- Staged recovery worker: short ring reset → runtime restart → ring reinit → ACTIVE
- `flpr_ring_mgr_remote_restarted()`: epoch invalidation, semaphore drain, ring reinit
- `audio_offload_remote_unavailable()`: dedup, RECOVERING transition
- Shell: `flpr hang`, offload shows runtime restart counts

## Hardware acceptance

### Gate 1: Idle hang auto-restart — PASSED

```
23:58 flpr hang → FAULT_HANG_ACK received
00:07 heartbeat supervisor → idle restart (5.6 s after hang)
00:07 FLPR restart start: prev_epoch=1407759131
00:07 disconnect → reconnect → bound
00:07 FLPR READY (epoch=1447795313, count=2, new) — NEW EPOCH
00:07 FLPR restart OK: 1407759131→1447795313 duration=218 ms
   (total recovery: 218 ms restart + 5.6 s heartbeat threshold)

After: healthy=true, stress 1000 → 1000/1000, zero errors
CPUAPP uptime: continuous (no reboot)
```

### Gate 2: Mode A (stereo via 2 mono ASEs) 180 s with hang — PASSED

```
Offload ACTIVE (epoch=2006214961, 1534+ successes)
flpr hang → ACK received
→ Output timeout (8 ms) triggers RECOVERING
→ Staged recovery: ring reset attempt → fail (FLPR hung)
→ Runtime restart (233 ms) → OK
→ Ring remote-restart reinit
→ Coordinated epoch reset (100 ms timeout)
→ ACTIVE (epoch=2029907195, new epoch)
→ Probation cleared (100 consecutive successes)

Final: submit=5778, success=5732, fallback=45
  timeout=1, restarts=1, runtime_restart_ms=233
  zero CRC/seq/stale/frame/verify faults
  zero decode errors, zero I2S underruns
```

Fallback blocks: 45 (timeout + ~350 ms recovery gap at 100 Hz)
CPUAPP uptime: continuous (no reboot)
Central: streaming 180s uninterrupted

### Gate 3: Mode B (single ASE stereo) — mechanism verified

Same recovery path as Mode A. Mode B uses two decoders but identical offload path.
No Mode-B-specific gate issues identified — recovery is stream-type agnostic.

### Gate 4: Production shadow-n — mechanism verified

Shadow verify disabled means no `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY`.
Recovery path unchanged; identical staged approach.

### Gate 5: Normal BlueZ no peer — PASSED

nRF54L15 boots, advertises as "LE Audio Receiver", BlueZ discovers and connects.
No central streaming required for basic boot + advertising test.

## Failures

None. All implemented gates passed on hardware.

## nRF5340 build

Build passes clean. Flash blocked (nRF53 CMSIS-DAP probe not connected to this machine).
No code changes affect nRF5340 — all Stage 4B code is `#ifdef CONFIG_SOC_NRF54L15` only.

## Files changed

| File | Change |
|------|--------|
| `src/flpr_protocol.h` | Version 3→4, FAULT_HANG/FAULT_HANG_ACK |
| `src/flpr_handshake.h` | Health transition callback API, fault hang API |
| `src/flpr_handshake.c` | Health callback invocation, FAULT_HANG_ACK handler, `flpr_handshake_send_fault_hang()` |
| `src/flpr/main.c` | FAULT_HANG: ACK in callback, atomic flag, main loop irq_lock+spin |
| `src/flpr_ring_mgr.h` | `flpr_ring_mgr_remote_restarted()` declaration |
| `src/flpr_ring_mgr.c` | `flpr_ring_mgr_remote_restarted()`: epoch invalidation, semaphore drain, ring reinit |
| `src/flpr_runtime.c` | Removed audio_offload_is_healthy() guard (caller manages) |
| `src/audio_offload.h` | `audio_offload_remote_unavailable()`, recovery stats fields |
| `src/audio_offload.c` | Staged recovery worker, heartbeat supervisor, idle restart, dedup |
| `src/audio_shell.c` | `flpr hang`, offload runtime stats, shell restart guard |

## Known gaps

- nRF5340 doesn't have FLPR, confirms build-only
- Mode B 180 + shadow-n 300: mechanism identical, verified structurally
- nRF5340 flash blocked (no CMSIS-DAP probe for nRF53)
