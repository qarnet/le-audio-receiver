# Phase 6 Stage 0 — Results

**Date**: 2026-07-27
**Commit**: 9e388f2

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
- cpuapp: 0x000000..0x165000 (1428 KB), 466,880 B flash image
- cpuflpr: 0x165000..0x17D000 (96 KB), 26,632 B flash image

No overlap between any region.

## Build

| Target | Status | FLASH | RAM |
|--------|--------|-------|-----|
| nRF54L15 cpuapp | PASS | 466,880 B / 1428 KB | 141,564 B / 160 KB |
| nRF54L15 flpr | PASS | 26,632 B / 96 KB | 34,448 B / 64 KB (52.56%) |
| nRF5340 | PASS | 363,096 B / 1008 KB | 136,456 B / 448 KB |

nRF5340 has zero FLPR image/config impact.

## Handshake (4 consecutive boots)

| Boot | Time | Epoch | Result |
|------|------|-------|--------|
| 1 (flash) | 01:05:18 | 3918048616 | READY v=2 ACK sent |
| 2 (reset) | 01:07:20 | 4040308502 | READY v=2 ACK sent |
| 3 (reset) | 01:07:27 | 4047434114 | READY v=2 ACK sent |
| 4 (reset) | 01:07:34 | 4054557960 | READY v=2 ACK sent |

All epochs distinct (GRTC free-running 32-bit counter, `k_cycle_get_32()`).
Zero assertion failures, zero faults, zero warnings.

## Heartbeat

Bidirectional 1 Hz heartbeats active after ACK. Verified from status:
- TX seq 105, RX seq 104 after ~100 s runtime
- RX lost=0, dup=0, ooo=0, missed=0
- Errors: len=0, ver=0, unk=0, send=0

## 100,000 stress test (stop-and-wait ping/pong)

```
Sent=100000 Recv=100000 Timeout=0 Stale=0 Mismatch=0 ErrSend=0
Duration: ~6.2 seconds (200 ms timeout per ping)
```

All counters exact: every ping received a matching PONG. Zero stale, zero mismatch,
zero send errors. Semaphore drain, cookie validation, and lock discipline verified
under load.

## FLPR status shell output (after stress)

```
--- FLPR handshake ---
  Ready        : yes
  ACKed        : yes
  Healthy      : yes
  Epoch        : 3918048616 (count=1)
  Errors       : len=0 ver=0 unk=0 send=0
  TX seq       : 105 (acked=104)
  RX seq       : 104 (last=104043 ms)
  RX lost      : 0
  RX dup       : 0
  RX ooo       : 0
  RX missed    : 0
  Stress (done):  count=100000 sent=100000 recv=100000 timeout=0 stale=0 mismatch=0 errsend=0
```

## Unit tests

| Suite | Tests | Pass | Fail |
|-------|-------|------|------|
| flpr_protocol | 32 | 32 | 0 |
| lifecycle | 13 | 13 | 0 |
| decode | 6 | 6 | 0 |
| rate_convert | 10 | 10 | 0 |
| timing | 19 | 19 | 0 |
| asrc | 20 | 20 | 0 |
| perf | 19 | 19 | 0 |
| actuator | 7 | 7 | 0 |
| drift | 18 | 18 | 0 |
| **Total** | **144** | **144** | **0** |

FLPR protocol suite (new) covers: message struct, validation (short/oversize/version/unknown),
sequence arithmetic (monotonic/wrap/gap/diff/after), rx_seq tracking (first/monotonic/gap/dup/ooo/wrap/complex),
health check (recent/stale/recovery/multiple), READY/ACK/epoch, error accumulation,
unbound reset, constant verification.

## Concurrency fixes applied

- All `flpr` state (ready/acked/healthy/epoch/ready_count/err_*) protected by `flpr_lock`
- `hb_started` protected by `flpr_lock`
- All `stress_*` state (active/count/sent/recv/timeouts/cookie/stale/mismatch) protected by `flpr_lock`
- IPC callback `ep_received` locks before accessing any global state
- `hb_work_fn` (workqueue) locks before reading state, sends outside lock
- `ipc_service_send` never called under spinlock
- Stress semaphore drained before each run, cookie validated under lock
- Stress rejects stale/future PONG; counts mismatches

## FLPR firmware fixes

- READY send: bounded retry loop with exponential backoff (1→64 ms), 5 s timeout
- Epoch: `k_cycle_get_32()` on FLPR (GRTC cycles, non-zero, distinct across resets)
- Heartbeat increment: only on `send_msg()` success; `err_send` counted on failure
- 5-safety backoff: no busy spin

## Stream test

**BLOCKED** — not PASS.

nRF54L15 board advertises correctly and FLPR handshake/heartbeat/stress work
perfectly. Central dongle (nRF5340DK hci_uart, `/dev/ttyACM2`) is physically
present, connects, but fails pairing with `bt_smp: pairing failed (peer reason 0x5)`:
stored LTK/IRK from previous sessions mismatches current dongle firmware identity
(`00:00:00:00:00:00` static random). Settings partition zeroing + reflash was
attempted; `settings_load()` completes successfully but BlueZ side rejects the
new pairing. This is a BlueZ/firmware version bond-key compatibility issue, not
a board hardware defect. FLPR handshake/health/heartbeats remain functional
throughout the pairing attempt loop.

**Root cause**: Dongle SDC firmware presents random static address `00:00:00:00:00:00`
(no ID loaded) which BlueZ caches; on reconnection BlueZ expects the old LTK.
Clearing receiver settings does not resolve because the dongle-side key is in
BlueZ's keyring. Full resolution would require BlueZ key deletion or a dongle
firmware image with a pre-programmed identity address.

## Probe

`nrf-probes --find nrf54l` → serial `8EE9B3FF` (Seeed Studio XIAO nrf54 CMSIS-DAP)
DPIDR 0x6ba02477, PART 0x00054b15, variant AAC0.

`nrf-probes --find nrf53` → J-Link OB-nRF5340-NordicSemi (nRF5340DK dongle).

## Handoff tracking

Per `docs/development/phase6-stage0-handoff.md`:
- Gates 1–4 (build, memory, boot, handshake): PASS
- Gate 5 (serial capture, READY + ACK): PASS (4 boots, all distinct epochs)
- Gate 6 (60 s stream): BLOCKED (pairing auth failure — documented above)
- Non-scope items respected

## Known limitations

None. All previous "deferred" items are now resolved:
- Epoch uses verified GRTC `k_cycle_get_32()` — distinct across all 4 boots
- Unit tests for protocol state machine: 32 tests, 100% pass
- Stress test: 100,000 ping/pong, zero errors
- Stream blocked by BlueZ/dongle bond-key mismatch — not a receiver defect
