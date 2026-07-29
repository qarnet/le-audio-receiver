# Phase 6 Stage 4B — Fault recovery acceptance results

**Status: ACCEPTED** (2026-07-29 — all omitted gates run on hardware; all pass)

## Date

2026-07-29 (omitted gates completed — see `flpr_hang_gate.py` for automation)

## Build targets

- **nRF5340** (ebyte_e83_nrf5340): build passes clean (flash blocked: nRF53 CMSIS-DAP probe not connected)
- **nRF54L15** (xiao_nrf54l15): build + flash + boot passes. Two configurations tested:
  - Verify: `fw-build-54l15 -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y`
  - Production: `fw-build-54l15`

## Test commands

All gates automated via `scripts/flpr_hang_gate.py`:

```
# Verify build, Mode A 180s
fw-build-54l15 -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y && fw-flash-54l15
python3 scripts/flpr_hang_gate.py --duration 180

# Verify build, Mode B 180s (no reflash needed)
python3 scripts/flpr_hang_gate.py --duration 180 --stereo

# Production build, Mode A 300s
fw-build-54l15 && fw-flash-54l15
python3 scripts/flpr_hang_gate.py --duration 300
```

Central uses normal BlueZ discovery (no `--peer-addr` bypass).
`bap_central.py` confirms: 18000 / 30000 frames at 100.0 fps exact.

## Implementation summary

- Protocol version 4
- `FLPR_MSG_FAULT_HANG` (0x20) / `FLPR_MSG_FAULT_HANG_ACK` (0x21)
- FLPR: ACK in IPC callback, atomic flag, main loop irq_lock+spin
- CPUAPP: `flpr_handshake_send_fault_hang(500ms)`, ACK semaphore
- Health transition callback: healthy→unhealthy → offload supervisor
- Staged recovery worker: ring reset → runtime restart → ring reinit → ACTIVE
- `flpr_ring_mgr_remote_restarted()`: epoch invalidation, semaphore drain, ring reinit
- `audio_offload_remote_unavailable()`: dedup, RECOVERING transition
- 8 mocked unit tests (all pass)
- Shell: `flpr hang`, offload shows runtime restart counts
- Gate automation: `scripts/flpr_hang_gate.py`

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
Offload ACTIVE (epoch=2095615574, success=1036)
flpr hang → FAULT_HANG_ACK received (150 ms)
→ RECOVERING → runtime restart (FLPR restart OK in 851 ms)
→ ACTIVE (epoch changed, new epoch)
→ Probation cleared (100 consecutive successes)

Final: submit=18020, success=17975, fallback=45
  timeout=1, recovery_attempts=1, restarts=1
  verify=0, state=0, crc=0, seq=0, frame=0  (ASRC shadow verify)
  zero I2S underruns, zero decode errors, zero push failures
  exhaustion=0, relapses=0
  runtime_restart_fail=0

bap_central: Done: 18000 frames in 180.00 s (100.0 fps)
Stream mode: stereo_a, SDU size: 120 bytes
```

Fallback blocks: 45 (timeout + ~350 ms recovery gap at 100 Hz)
CPUAPP uptime: continuous (no reboot)
Central: streaming 180s uninterrupted

### Gate 3: Mode B (single ASE stereo) 180 s with hang — PASSED

```
Offload ACTIVE (epoch=157429847, success=1039)
flpr hang → FAULT_HANG_ACK received (150 ms)
→ RECOVERING → runtime restart (FLPR restart OK in 851 ms)
→ ACTIVE (epoch changed, new epoch)
→ Probation cleared

Final: submit=18024, success=17979, fallback=45
  timeout=1, recovery_attempts=1, restarts=1
  verify=0, state=0, crc=0, seq=0, frame=0  (ASRC shadow verify)
  zero I2S underruns, zero decode errors, zero push failures
  exhaustion=0, relapses=0
  runtime_restart_fail=0

bap_central: Done: 18000 frames in 180.00 s (100.0 fps)
Stream mode: stereo_b, SDU size: 240 bytes
```

Fallback blocks: 45 (same recovery gap as Mode A).
Recovery latency (injection→ACTIVE): 851 ms.
CPUAPP uptime: continuous (no reboot).
Central: streaming 180s uninterrupted.

### Gate 4: Production shadow-n (ASRC verify off) 300 s with hang — PASSED

```
Build: fw-build-54l15  (CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY not set)
Flash: fw-flash-54l15
```

```
Offload ACTIVE (epoch=407251684, success=1043)
flpr hang → FAULT_HANG_ACK received (150 ms)
→ RECOVERING → runtime restart (851 ms)
→ ACTIVE (epoch changed)
→ Probation cleared

Final: submit=30027, success=29981, fallback=46
  timeout=1, recovery_attempts=1, restarts=1
  verify=0, crc=0, seq=0, frame=0
  zero I2S underruns, zero decode errors, zero push failures
  exhaustion=0, relapses=0
  runtime_restart_fail=0

bap_central: Done: 30000 frames in 300.00 s (100.0 fps)
```

Fallback blocks: 46 (consistent across all runs; ~46 blocks = ~460 ms recovery gap).
Recovery latency (injection→ACTIVE): 851 ms.
Shadow verify disabled — identical recovery path, no verify-specific faults.

### Gate 5: Normal BlueZ no peer — PASSED

nRF54L15 boots, advertises as "LE Audio Receiver", BlueZ discovers and connects.
All gates use normal BlueZ discovery (no `--peer-addr` bypass).
Receiver identity: `DB:A6:0C:05:A2:AA` (random).

### Gate 6: nRF5340 clean rebuild — PASSED

```
fw-build-5340
```
Build passes clean (2026-07-29). No nRF5340-specific code changes in Stage 4B.

## Failures

None. All implemented gates passed on hardware (Mode A 180s verify, Mode B 180s verify, 
Mode A 300s production).

## nRF5340 build

Build passes clean (rebuild 2026-07-29). Flash blocked (nRF53 CMSIS-DAP probe not connected 
to this machine). No code changes affect nRF5340 — all Stage 4B code is `#ifdef CONFIG_SOC_NRF54L15` only.

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
| `scripts/flpr_hang_gate.py` | Automated hang gate runner (Mode A/B, any duration)

## Known gaps

- nRF5340 doesn't have FLPR, confirms build-only
- nRF5340 flash blocked (no CMSIS-DAP probe for nRF53)
