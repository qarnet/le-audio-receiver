# Phase 6 Stage 4A — FLPR runtime restart infrastructure

## Goal

Implement CPUAPP-owned runtime restart of SRAM-executing FLPR using public nrfx
VPR HAL + public IPC service lifecycle. Prove idle restart/rebind/reload three
times. Do not connect restart to live audio recovery yet.

## Grounded constraints

- NCS v3.3.0 launcher is init-only; never call `device_init()`/private launcher.
- Use `nrf_vpr_cpurun_set`, `nrf_vpr_debugif_dmcontrol_mask_set`, and
  `nrf_vpr_initpc_set`, matching nrfxlib sQSPI/MCUboot reset sequence. No raw
  register writes.
- DT-derived nodes: `cpuflpr_vpr`, its `source-memory` phandle at 0x165000,
  `execution-memory` at 0x20030000. Copy execution-region size 64 KiB; source
  may be larger. INITPC must be 128-byte aligned.
- Both ICMsg endpoint nodes set `unbound = "enable"`.

## Handshake lifecycle API

Refactor `flpr_handshake.c` without changing wire messages:

- Store IPC device pointer after init.
- Add bound and new-READY semaphores, drained before restart.
- `flpr_handshake_disconnect()`: deregister endpoint through
  `ipc_service_deregister_endpoint`; mark session unavailable.
- `flpr_handshake_reconnect()`: register same endpoint/config again.
- `flpr_handshake_wait_bound(timeout_ms)` and
  `flpr_handshake_wait_new_ready(previous_epoch, timeout_ms)`.
- `ep_bound` gives bound semaphore. New distinct READY gives READY semaphore
  only after READY_ACK send succeeds.
- On unbound preserve lifetime ready/reboot/error/missed counters and last remote
  epoch, but clear bound/ready/acked/healthy and session sequence/timestamps.
  Do not call full `flpr_peer_reset()` except first init.
- Heartbeat work remains single instance; unavailable session sends nothing;
  new READY re-arms it without duplicate work items.

## Restart manager

Add `flpr_runtime.c/.h`, nRF54-only, with mutex-serialized sync API and status:

```c
int flpr_runtime_init(void);
int flpr_runtime_restart(uint32_t timeout_ms);
void flpr_runtime_get_status(struct flpr_runtime_status *out);
```

Status: state, requests/success/fail/busy, previous/new epoch, reload bytes,
source CRC, execution CRC, last errno, total/max duration.

Exact restart sequence:

1. Snapshot previous handshake epoch; mark manager busy.
2. Deregister CPUAPP IPC endpoint.
3. `nrf_vpr_cpurun_set(vpr, false)`.
4. Pulse NDMRESET with same DMCONTROL mask sequence as
   `nrfxlib/softperipheral/sQSPI/src/nrf_sqspi.c`.
5. Copy exactly DT execution-memory size from DT source-memory to execution SRAM.
6. Flush copied range with `sys_cache_data_flush_range()`; full barrier.
7. Compute CRC32 source/execution over copied bytes and require equality.
8. Set INITPC to execution base.
9. Re-register CPUAPP IPC endpoint before releasing core.
10. `nrf_vpr_cpurun_set(vpr, true)`.
11. Wait bound, then READY+ACK with epoch different from previous.
12. Return success; on failure leave FLPR unavailable/stopped and report exact
    stage/error. Never reboot CPUAPP.

Build assertions: execution size <= source size, base/size valid, execution base
128-byte aligned, execution range exactly current reserved 0x20030000..0x20040000.

Use dedicated normal thread/shell context; never run restart from ISR/spinlock/BT
callback. No heap.

## Shell and tests

- `flpr runtime` prints status.
- `flpr restart` performs synchronous restart only when no stream/offload active;
  reject `-EBUSY` otherwise in Stage 4A. Add `audio_offload_is_stopped()` helper.
- Pure/mock tests assert exact operation ordering, HAL abstraction calls, CRC
  mismatch, bind timeout, READY timeout, duplicate/same epoch rejection,
  concurrent busy rejection, and lifetime-counter preservation.
- Protocol/handshake/ring/offload existing tests pass.

## Hardware gate

Normal production build. Before BLE connection, run `flpr restart` three times.
Each run must show CPUAPP uptime continues, 64 KiB reload CRC match, endpoint
unbound→bound, distinct FLPR epoch, ready/reboot counters increment, heartbeat
healthy. Reinitialize rings and run 1,000 ping/pong + 1,000 ring blocks after
each restart. Then normal Mode A 60 s identity/ASRC stream at 100 fps zero faults.

Both targets build; nRF5340 excludes manager. Commit code/tests/docs/results. No
live-fault integration, WDT expiry, direct registers, security changes, HPF,
BabbleSim, mass erase, push, install, or analog claim.
