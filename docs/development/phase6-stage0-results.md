# Phase 6 Stage 0 — Results

**Date**: 2026-07-27
**Commit**: 52abde1 (Stage 0 gate CLOSED — PASS)

## Central dongle compile-time identity fix (2026-07-27 — PASS)

The nRF5340DK hci_uart dongle FICR DEVICEADDR is unprogrammed (all zeros).
Prior workaround was runtime `btmgmt static-addr` which provided a connectable
address but did NOT restore LE scan support, blocking GATT ServicesResolved
and full BAP stream.

**Fix**: The hci_ipc netcore firmware now calls `bt_ctlr_set_public_addr()`
before `bt_enable_raw()`, setting the controller's public BD_ADDR to
`C0:AA:BB:CC:DD:EE` (lab-only, defined in `dongle/hci_identity.h`).
This replaces the upstream hci_ipc sample with a repo-owned copy under
`dongle/hci_ipc/`. The build script (`fw-build-dongle`) compiles the
custom hci_ipc standalone and merges it with the upstream hci_uart app core.

### Verification

| Check | Result |
|-------|--------|
| `btmgmt info` BD_ADDR | ✅ `addr C0:AA:BB:CC:DD:EE` — public address, no static-addr command needed |
| `btmgmt info` settings | ✅ `powered le secure-conn cis-central` |
| `hcitool lescan` / `btmgmt find` | ✅ Scanning works — discovers LE Audio Receiver immediately |
| `bluetoothctl devices` | ✅ Lists receiver DB:A6:0C:05:A2:AA after scan |
| `bluetoothctl pair` (SC Just Works) | ✅ ACL connects (receiver: "Connected: EE:DD:CC:BB:AA:C0", "Pairing accepted") — SMP handshake completes via D-Bus agent |
| HCI Reset / Read BD_ADDR | ✅ Standard HCI command returns C0:AA:BB:CC:DD:EE |
| Dongle flash | ✅ Both cores verified via OpenOCD |
| Build reproducibility | ✅ `fw-build-dongle` compiles both images and merges cleanly |

### Files changed

| File | Change |
|------|--------|
| `dongle/hci_identity.h` | New: lab-only BD_ADDR (on-air byte order: EE DD CC BB AA C0) |
| `dongle/hci_ipc/src/main.c` | Fork of upstream hci_ipc — adds `bt_ctlr_set_public_addr()` before `bt_enable_raw()` |
| `dongle/hci_ipc/CMakeLists.txt` | Standalone netcore project, includes `../hci_identity.h` |
| `dongle/hci_ipc/prj.conf` | Merged upstream hci_ipc + dongle ISO/controller tuning |
| `dongle/hci_ipc/dts/arm/nordic/override.dtsi` | IRQ priority override (copy from upstream) |
| `scripts/bin/fw-build-dongle` | Rewritten: standalone hci_ipc build + sysbuild hci_uart with NETCORE_EMPTY + hex merge |
| `AGENTS.md` | Central setup: removed `btmgmt static-addr` step, documented compile-time identity |
| `dongle/README.md` | Updated files table, build description, attach instructions |
| `docs/development/phase6-stage0-results.md` | This file — updated from BLOCKED to PASS |

### Byte order note

`bt_ctlr_set_public_addr()` expects on-air byte order (little-endian LAP first).
`dongle_bd_addr = {0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0xC0}` produces the expected
`btmgmt info addr C0:AA:BB:CC:DD:EE`. First attempt with display-order bytes
`{0xC0, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE}` produced reversed `addr EE:DD:CC:BB:AA:C0`
— corrected.

## Review defects fixed (after dc199ec)

### 1. Protocol production helpers

All state transitions now go through PURE helpers in `flpr_protocol.h`:
- `flpr_peer_handle_ready(peer, epoch)` — new epoch resets heartbeat seq/ack/health,
  increments reboot_count; duplicate same epoch preserves state
- `flpr_peer_handle_ready_ack(peer, data)` — FLPR side ACK transition
- `flpr_peer_handle_heartbeat_ack(peer, seq)` — validates (stale rejected, in-order accepted)
- `flpr_peer_reset(peer)` — zeroes all state (IPC unbound)
- `flpr_classify_stress_pong(cookie, expected, active)` — pure classifier (MATCH/STALE/FUTURE/INACTIVE)
- `flpr_msg_validate(msg, len, peer)` — shared validation (via IPC callback)

IPC callbacks call these helpers exclusively. No raw field assignment remains in the
`ep_received` handler or stress path.

Tests: all 40 tests call production helpers, not field assignment. Removed 3 fake
direct-assignment tests (`test_ready_updates_epoch`, `test_ready_duplicate_increments_count`,
`test_acked_persists`). Added 22 tests covering READY transition (new/same epoch),
READY_ACK, heartbeat ACK (advance/stale), stress PONG classifier (match/stale/future/inactive),
peer reset, OOO-then-inorder, health (single transition count, repeated stale poll
no-inflation, heartbeat restore).

### 2. Sequence OOO fix

`flpr_peer_rx_seq` OOO branch now COUNTS stale (`rx_ooo++`) but does NOT move
in-order `rx_seq` baseline or `rx_last_ms` timestamp. Natural wrap (diff > 0)
remains valid. Stale packet + next in-order produces NO fake huge gap (verified
by `test_rx_seq_ooo_then_inorder` and `test_rx_seq_complex`).

### 3. Health transition counting

`flpr_peer_check_health` increments `rx_missed_total` ONLY on healthy→unhealthy
transition (once per miss episode). No repeated polling inflation. New valid
heartbeat arrival (via `flpr_peer_rx_seq` updating `rx_last_ms`) restores `healthy`
on next check. Verified by `test_health_repeated_stale_poll` (3 stale polls → counter=1)
and `test_health_heartbeat_restores` (stale → heartbeat → healthy).

### 4. Stress late-PONG safety

- Semaphore drained before EVERY iteration (not just run start)
- Pure classifier `flpr_classify_stress_pong` validates: active + exact cookie = MATCH,
  cookie < expected = STALE, cookie > expected = FUTURE, !active = INACTIVE
- Late previous-cookie and future-cookie PONGs NEVER signal semaphore
- On timeout: cookie invalidated (incremented) BEFORE next iteration; late-PONG
  for timed-out iteration classified as stale
- Counters `stress_stale` / `stress_mismatch` populated via classifier return
- 100,000 stress: Sent=100000 Recv=100000 Timeout=0 Stale=0 Mismatch=0 ErrSend=0

### 5. READY new epoch resets state

`flpr_peer_handle_ready` detects new epoch (ready_count==0 or epoch != current):
- Resets: rx_seq, rx_lost, rx_dup, rx_ooo, rx_last_ms, rx_missed_total, tx_acked_seq
- Sets healthy=true
- Increments reboot_count
- Duplicate same epoch: no reset, only increment ready_count
- `reboot_count` exposed in `flpr_status` and shell `flpr status`

### 6. Pairing recovery without destructive erase

Added `bt unpair` shell command: calls `bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY)`.
Test-only, no confirmation. Confirmed working: `All bonds cleared.` on receiver.
Paired with `bluetoothctl remove` on BlueZ side. Both bond stores explicitly cleared.

**Result**: RESOLVED by commit 52abde1. Root cause was two-fold:
1. Dongle identity was `00:00:00:00:00:00` (unprogrammed FICR) — resolved
   by 87b8d36 (`bt_ctlr_set_public_addr()` before `bt_enable_raw()`).
2. `--peer-addr` raw-HCI bypass used `own_address_type=Random` (0x01) in the
   LE Extended Create Connection command, causing BlueZ SMP SC DHKey Check
   mismatch (peer reason 0x0C). Fixed in 52abde1: raw-HCI helper always uses
   `--addr-type public`, matching the dongle's compile-time public BD_ADDR.
   The `--peer-addr` path now holds the ACL open for `duration+120 s` instead of
   a brief connect+kill, allowing `Pair()` to succeed over the existing ACL.

Both `--peer-addr` bypass and scan-based discovery now work. Scan-based
discovery is the preferred path: GATT ServicesResolved fires normally, BlueZ
auto-configures ASEs, BAP stream runs end-to-end without manual steps.

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
- cpuapp: 0x000000..0x165000 (1428 KB), 467,896 B flash image
- cpuflpr: 0x165000..0x17D000 (96 KB), 26,634 B flash image

No overlap between any region.

## Build

| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 467,896 B / 1428 KB | 141,692 B / 160 KB |
| nRF54L15 flpr | PASS | 26,634 B / 96 KB | 34,464 B / 64 KB (52.59%) |
| nRF5340 | PASS | 363,096 B / 1008 KB | 136,456 B / 448 KB |

nRF5340 has zero FLPR image/config impact.

## Handshake (4 consecutive boots)

| Boot | Epoch | Result |
|------|-------|--------|
| 1 (flash) | 920157244 | READY v=2, new, ACK sent |
| 2 (reset) | 925278107 | READY v=2, new, ACK sent |
| 3 (reset) | 930401107 | READY v=2, new, ACK sent |
| 4 (reset) | 935525042 | READY v=2, new, ACK sent |

All epochs distinct (GRTC free-running 32-bit counter, `k_cycle_get_32()`).
Zero assertion failures, zero faults, zero warnings.

## Heartbeat

Bidirectional 1 Hz heartbeats active after ACK. Verified from status:
- TX seq 119, RX seq 118 after ~120 s runtime
- RX lost=0, dup=0, ooo=0, missed=0
- Errors: len=0, ver=0, unk=0, send=0
- Ready count=1, reboot count=1

## 100,000 stress test (stop-and-wait ping/pong)

```
Sent=100000 Recv=100000 Timeout=0 Stale=0 Mismatch=0 ErrSend=0
Duration: ~6.6 s (200 ms timeout per ping)
```

All counters exact: every ping received a matching PONG. Zero stale, zero mismatch,
zero send errors. Semaphore per-iteration drain, cookie validation via pure classifier,
cookie invalidation on timeout, and lock discipline all verified under load.

## FLPR status shell output (after stress, fresh flash)

```
--- FLPR handshake ---
  Ready        : yes
  ACKed        : yes
  Healthy      : yes
  Epoch        : 1287686180 (ready=1 reboot=1)
  Errors       : len=0 ver=0 unk=0 send=0
  TX seq       : 88 (acked=87)
  RX seq       : 87 (last=87039 ms)
  RX lost      : 0
  RX dup       : 0
  RX ooo       : 0
  RX missed    : 0
```

## bt unpair

```
uart:~$ bt unpair
All bonds cleared.
```

BlueZ side: `bluetoothctl remove "DB:A6:0C:05:A2:AA"` → device removed, bond cleared.

## Stage 0 gate close — 60 s Mode A stream (2026-07-27 — PASS)

### Test procedure

1. Receiver bonds cleared: `bt unpair` → `All bonds cleared.`
2. BlueZ bonds cleared: `bluetoothctl remove DB:A6:0C:05:A2:AA` + bluetoothd restart
3. Dongle reattached: `btattach -B /dev/ttyACM2 -S 1000000`, verified
   `addr C0:AA:BB:CC:DD:EE` public, `powered le secure-conn cis-central`
4. Scan-based discovery: `python3 scripts/bap_central.py --duration 60`
   (no `--peer-addr` — scan works with public dongle identity)
5. Post-stream: `flpr status` and `audio perf` over serial shell

### Stream results

| Metric | Value |
|--------|-------|
| Duration | 60.00 s |
| Frames | 6000 |
| Rate | 100.0 fps |
| Mode | stereo_a (2 ASEs, chan_alloc 0x01 + 0x02) |
| Disconnects | 0 |
| Slab full | 0 |
| I2S underrun | 0 |
| Warnings | 0 |
| Assertions | 0 |
| Faults | 0 |

### Receiver serial (key events)

```
Connected: C0:AA:BB:CC:DD:EE (public)
Pairing accepted
Pairing complete, bonded: 1
ASE[0] configured: chan alloc 0x00000001
ASE[1] configured: chan alloc 0x00000002
I2S DMA started
Timing anchor: ts=1130747234 pd=40000
PCLK timer diag[1..55]: 1505–1663 ppm range
Stream[0] disabled, Stream[1] disabled  (reason 0x13)
Disconnected: C0:AA:BB:CC:DD:EE (public) reason 0x13
```

### FLPR status (post-stream)

```
Ready: yes  ACKed: yes  Healthy: yes
Epoch: 557137997 (ready=1 reboot=1)
Errors: len=0 ver=0 unk=0 send=0
TX seq: 648 (acked=647)
RX seq: 647  lost=0  dup=0  ooo=0  missed=0
```

### Audio perf (post-stream)

| Path | Count | Avg cycles | Max cycles | Deadline% |
|------|-------|-----------|-----------|-----------|
| iso_recv | 12072 | 1776 | 2441 | 24.4% |
| lc3_decode | 12055 | 1385 | 1609 | 16.0% |
| volume | 6026 | 99 | 159 | 1.5% |
| sink_push | 6025 | 632 | 767 | 7.6% |
| asrc | 6026 | 445 | 558 | 5.5% |

Queue: slab_free 5/7 (min/max), output_frames 476/478, output_blocks 6025
**Push failures: 0, Repeat fb: 0, ASRC cap fail: 0**

### Pairing root cause and fix

Two commits resolve the previously BLOCKED pairing:

- **87b8d36** — dongle compile-time BD_ADDR via `bt_ctlr_set_public_addr()`,
  defined in `dongle/hci_identity.h`. Replaces the broken `btmgmt static-addr`
  workaround. Restores LE scanning.
- **52abde1** — `--peer-addr` path uses `--addr-type public` (own_address_type=0x00)
  in the raw-HCI LE Extended Create Connection command. Previously used Random
  (0x01), causing BlueZ SMP SC DHKey Check mismatch (peer reason 0x0C —
  Numeric Comparison Failed). Also fixes the raw-HCI ACL lifetime: kept open for
  `duration+120 s` instead of brief connect+kill, so `Pair()` succeeds over the
  existing ACL.

With both fixes, scan-based discovery (preferred) and `--peer-addr` bypass
both work. BAP end-to-end stream (pair → bond → ASE config → CIS → 6000 frames)
confirmed on clean state (no prior bonds).

## Unit tests

| Suite | Tests | Pass | Fail |
|-------|-------|------|------|
| flpr_protocol | **40** | **40** | **0** |
| lifecycle | 13 | 13 | 0 |
| decode | 6 | 6 | 0 |
| rate_convert | 10 | 10 | 0 |
| timing | 19 | 19 | 0 |
| asrc | 20 | 20 | 0 |
| perf | 19 | 19 | 0 |
| actuator | 7 | 7 | 0 |
| drift | 18 | 18 | 0 |
| **Total** | **152** | **152** | **0** |

FLPR protocol suite (40 tests) covers: message struct, validation, sequence
arithmetic, rx_seq tracking (first/monotonic/gap/dup/ooo/wrap/complex),
OOO-then-inorder (no fake gap), health (recent/stale/single-transition-count/
repeated-stale-no-inflation/heartbeat-restores), READY (new-epoch-resets-state/
same-epoch-no-reset/new-epoch-increments-reboot), READY_ACK (sets-acked),
heartbeat ACK (advances/stale-rejected), stress PONG classifier (match/stale/
future/inactive), peer reset (clears-all), error accumulation, constant verification.
All tests call PRODUCTION helpers — zero tests assign peer fields directly.

## Concurrency fixes applied

- All `flpr` state (ready/acked/healthy/epoch/ready_count/reboot_count/err_*)
  protected by `flpr_lock`
- `hb_started` protected by `flpr_lock`
- All `stress_*` state (active/count/sent/recv/timeouts/cookie/stale/mismatch/err_send)
  protected by `flpr_lock`
- IPC callback `ep_received` uses `flpr_msg_validate` + switch + production helpers
- `hb_work_fn` (workqueue) snapshots state under lock, sends outside lock
- `ipc_service_send` never called under spinlock
- Stress semaphore drained before EVERY iteration, cookie validated via pure classifier
- Stress timeout invalidates cookie → late PONG is stale
- Stress rejects stale/future PONG; counts mismatches

## FLPR firmware fixes

- READY send: bounded retry loop with exponential backoff (1→64 ms), 5 s timeout
- Epoch: `k_cycle_get_32()` on FLPR (GRTC cycles, non-zero, distinct across resets)
- Heartbeat increment: only on `send_msg()` success; `err_send` counted on failure
- 5-safety backoff: no busy spin

## State machine helpers (new)

| Helper | Purpose |
|--------|---------|
| `flpr_peer_handle_ready(p, epoch)` | READY transition — new epoch resets state, duplicate preserved |
| `flpr_peer_handle_ready_ack(p, data)` | FLPR READY_ACK — set acked+healthy |
| `flpr_peer_handle_heartbeat_ack(p, seq)` | HEARTBEAT_ACK — advance tx_acked_seq (stale rejected) |
| `flpr_peer_reset(p)` | Zero all state (IPC unbound) |
| `flpr_peer_rx_seq(p, seq, ms)` | Rx tracking — OOO counted, baseline preserved |
| `flpr_peer_check_health(p, now)` | Health — transition edge counting only |
| `flpr_msg_validate(msg, len, p)` | Wire validation — length + version |
| `flpr_classify_stress_pong(c, exp, active)` | Pure cookie classifier |

## Probe

`nrf-probes --find nrf54l` → serial `8EE9B3FF` (Seeed Studio XIAO nrf54 CMSIS-DAP)
DPIDR 0x6ba02477, PART 0x00054b15, variant AAC0.

`nrf-probes --find nrf53` → J-Link OB-nRF5340-NordicSemi (nRF5340DK dongle).

## Known limitations

- **nRF54L15 SMP pairing**: RESOLVED (52abde1). Scan-based discovery (preferred)
  and `--peer-addr` bypass both work with the compile-time public dongle BD_ADDR.
  See "Pairing root cause and fix" above.
- nRF54L15 recovery: no valid recovery exists in OpenOCD tooling (different CTRL-AP
  from nRF53). Settings erase works via RRAM write-enable + `mww` fill.
- nRF5340 (E83) hardware regression pending — no E83 probe available this session.
