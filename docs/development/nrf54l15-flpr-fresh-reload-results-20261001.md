# PB-038: NCS v3.4.1 FLPR fresh-reload and IPC lifecycle repair

## Failed integrated boundary

Clean source-batch repair `b21c7a7` passed canonical software 80/80, builds,
six normal HCI cases and the first eight exact-local-artifact matrix rows.
The matrix then failed `rh3.flpr_hang_mode_b_48_4_1`: **8 passed / 1 failed /
11 unrun**, no cleanup failures. Immutable roots:

- `/tmp/opencode/nrf54-b21c7a7-matrix-20261001-r1/`
- `/tmp/opencode/hil-runs/nrf54-b21c7a7-matrix-20261001-r1/`
- Failed child identified by `children.jsonl`, pass 1 row 9, ending
  `rh3.flpr_hang_mode_b_48_.37d6187425d7`.

Failure was `FLPR hang recovery did not complete before deadline`.
FAULT_HANG_ACK was genuine; short ring reset timed out as expected, but all
five full runtime attempts failed WAIT_BOUND with `-EAGAIN`. No source fault,
row, deadline, PLC/transport limit, or receiver capability was substituted.
Old matrix remains failed. This was ordinary engineering work, not an
authorization or missing-board blocker.

## Two independently observed v3.4.1 boundaries

All SDK sources below refer to `/home/thomas-workstation/ncs/v3.4.1`.

### 1. Fresh reload was being treated as saved-context resume

SDK change `0f7c3590bdae4aff059357b3f46378166a8a4659` added launcher
hibernate support after the historical v3.3.0 checkpoint. The nRF54L15 VPR
node now has `hibernation-ram-block = <32>` and launcher code enables this
logical feature regardless of FLPR `CONFIG_NORDIC_VPR_HIBERNATE`:

- `zephyr/dts/vendor/nordic/nrf54l_05_10_15.dtsi:173-181`.
- `zephyr/drivers/misc/nordic_vpr_launcher/nordic_vpr_launcher.c:81-91`.

Hardware feature 32 is **MEMCONF POWER1.RET bit 0**, selecting VPR context
restore at reset. It is not physical SRAM block 0. VPR context occupies
`0x2003fe00..0x20040000`, inside this project's full 64 KiB execution reload.
The saved area cannot be resumed after replacement with fresh image bytes.
Verified API: `nrf_memconf_ramblock_ret_enable_set()`; current runtime derives
logical block from devicetree and asserts it is 32 before using the API.
Only this feature bit is cleared. POWER0 CONTROL/RET/RET2 and ICACHE/crypto
bits remain unchanged. No RAM power-down or broad retention mask write.

Original warm failure, controlled postfailure halt, showed entry PC
`0x20030000`, GP/SP/RA zero and `NORDIC.VPRNORDICCTRL=0`. Cold healthy
control showed executing kernel PC `0x20032d64`, GP `0x20038670`, SP
`0x20039194` and NordicCtrl 1. Actual FLPR ELF entry is `0x20030000`, with
RRAM load address `0x165000`; no entry-offset fix was needed.

**Clearing only restore feature** produced an executing FLPR with healthy
GP/SP and NordicCtrl 1, but binding still failed. This separates first
startup boundary from second notification boundary:
`/tmp/opencode/nrf54-flpr-context-off-execution-csr-20261001-r1/`.
Controlled halt is a diagnostic observation, not acceptance or an autonomous
repair action. Positive fault rows below used no halt/resume intervention.

### 2. CPUAPP notification hardware needed rearm after remote reset

SDK change `4445ed64f4e` moved Errata 16 VEVIF interrupt enable to one-time
device initialization. Current channel enable does not rewrite INTENSET:
`zephyr/drivers/mbox/mbox_nrf_vevif_event_rx.c:124-130,158-165`.

Once fresh FLPR startup was selected, CPUAPP still needed its prior VEVIF
mask restored after boot and endpoint registration against the live remote.
Current runtime caches the configured mask, retains it through failed reset-
held attempts, restores **only those bits**, verifies readback, then
re-registers the endpoint after the existing 200 ms settle window. This also
avoids joining stale reset-held FIFO magic. Errata 16 remains enabled.

Combination passes same frozen hang row. Independent one-boundary attempts
did not; a zero INTEN read alone was initially misleading because disabled
RTP forces APB INTEN read to zero. IRQ-only repair and its initial native
assumption were withdrawn rather than accepted from a green mock result.

## Final production behavior and lifecycle safety

- Fresh-context selection precedes reset/copy, with readback failure stopping
  the core before any unsafe reload.
- Original DMCONTROL assertion/release masks **3/1** remain: DMACTIVE stays
  enabled, NDMRESET held through copy, CRC, INITPC and CPURUN preparation.
  No hart-reset alternative, debug-module reset pulse or resume command adopted.
- Same source/execution CRC, execution address, QoS and timeouts retained.
- Registration moves after remote boot; existing settle time is not extended.
- Failed postlaunch attempts explicitly assert reset with CPURUN false.
  CPURUN false alone configures the next reset state, not an immediate halt.
- Local ICMsg deregistration does not call application `unbound`. CPUAPP
  heartbeat is drained before endpoint teardown, marked restartable, and
  starts again on new READY. Session admission precedes registration so an
  early callback cannot acknowledge READY without starting heartbeat.
  Failed registration rolls back admission, drains work and semaphores.
- Idle heartbeat faults enqueue repair on the existing dedicated offload
  workqueue rather than restarting inline from system heartbeat work. This
  removes self-cancellation/runtime-mutex wait cycles. Submit ownership,
  stream-open redirection, dedup and scheduling-failure rollback are tested.
  Idle runtime/ring errors remain explicit, including warning/error logs;
  no general warning suppression was added.

Temporary RAM trace and debugger helpers are absent from production source.
SDK source files were never edited.

## Verification already completed

Native public-boundary evidence:

| Suite | Result | Raw log |
| --- | --- | --- |
| Runtime | 25/25 | `/tmp/opencode/nrf54-flpr-runtime-final-20261001-r1.log` |
| Handshake | 37/37 | `/tmp/opencode/nrf54-flpr-handshake-final-20261001-r1.log` |
| Offload and ASRC | 36/36 plus 30/30 | `/tmp/opencode/nrf54-offload-idle-worker-20261001-r3.log` |

Native HAL/transport models represent observed reset conditions, not physical
IPC acceptance. Actual kernel work and encoded heartbeat messages prove early
READY admission, two generations, blocked in-flight send drain and retry.
Actual SYS work plus held backend proves idle callback returns while restart
waits elsewhere. Queue plug/unplug, failure rollback, dedup and newly opened
stream redirection exercise public outcomes rather than helper-call counts.

Same selected regressions fail against original behavior:
`/tmp/opencode/nrf54-flpr-fresh-reload-red-run.log`,
`nrf54-flpr-heartbeat-red-run-20261001-r3.log`, and
`nrf54-idle-restart-red-run.log`. Initial test-harness errors (wrong delayable
flush API and interpreting queue drain's successful return 1 as failure) are
retained separately; corrected public-boundary runs do not waive them.

Trace-free physical repair validation:

- `/tmp/opencode/hil-runs/nrf54-flpr-reviewed-final-hang-20261001-r1/`:
  **PASS**, one 224 ms runtime restart, 44 CPU fallbacks, probation cleared,
  zero runtime failures and clean cleanup.
- `/tmp/opencode/hil-runs/nrf54-flpr-reviewed-final-stall-20261001-r1/`:
  **PASS**, 12 CPU fallbacks, probation cleared, zero runtime restarts and
  clean cleanup. Stall remained stall, not replaced by another fault.
- Both use source HEX
  `57c836e3a70e6a1d3bbf6a15839fa879ef3548899322a05df4277e12b32a69c2`,
  receiver CPUAPP HEX
  `052ba9efe78f6d7234b28519548540f70491746d36ef4f000622c8a52e94af97`,
  and FLPR HEX
  `c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`.

These are local-build diagnostic passes, not final clean archives. Later
logging and registration rollback changes receive native and final exact-
candidate validation. New clean canonical/build/HCI and full two-pass
20-row exact-local-artifact matrix remain required after this commit.

## Private diagnosis retained, scope excluded

No-reset failed-receiver RAM:
`/tmp/opencode/nrf54-flpr-fault-postmortem-20261001-r1/`.
Controlled debug, autonomous variation rows and trace snapshots have unique
external roots prefixed `nrf54-flpr-` for 2026-10-01. Invalid-context selection,
IRQ-only rearm, reconnect/start timing, awake policy, resume/reset-halt and
unsupported hart-reset experiments are diagnostics, not accepted fixes.
Deprecated `mem2array` warnings in initial debugger scripts remain raw; later
controls use verified `read_memory`, not a warning filter or waiver.

Raw identity in every row includes DP `0x6ba02477`, AP0/1 `0x84770001`,
AP2 `0x32880000`, AP3 `0x00000000`, PART `0x00054b15`, VARIANT
`0x41414330`, plus live USB/probe role matching and six checkpoints. No static
probe-to-role mapping is declared. Private RAM/identity/bond material must not
be staged or uploaded. No nRF5340, analog, release publication or merge action
belongs to this repair.

## Clean coverage follow-up

Clean integrated `e478e59` canonical run retained **79 PASS / 1 FAIL / 80
TOTAL** solely from per-file branch enforcement:
`src/flpr_handshake.c branches below baseline: current 94/124 vs baseline 92/120`.
Raw log: `/tmp/opencode/nrf54-e478e59-canonical-20261001-r1.log`.
No baseline, denominator or threshold was lowered. New encoded partial-register
failure test delivers bound and READY before registration returns an error,
then proves rollback discards both semaphore signals and a fresh retry works.
This covers the real new rollback boundary, not copied constants. Focused
handshake suite now passes 38/38 at
`/tmp/opencode/nrf54-flpr-partial-register-20261001-r1.log`; full clean baseline
enforcement follows. Production firmware code is unchanged by this test slice.

## Clean integrated follow-up

Exact `104e67a` canonical rerun passed **80 PASS / 0 FAIL / 80 TOTAL**,
including unchanged per-file baseline enforcement and strict BSim 17 scenarios /
26 runs. Three physical build shapes and 73/73 resolved build contract passed.
Normal-image Linux HCI six-case repeat passed 72,000 writer frames, PLC 428,
zero case/kernel HCI/SMP alerts. Raw canonical log is
`/tmp/opencode/nrf54-104e67a-canonical-20261001-r1.log`; HCI evidence is
`/tmp/opencode/nrf54-104e67a-hci-clean-20261001-r1/`.
The [integrated verification record](nrf54l15-migration-verification-results-20261001.md)
owns the latest exact-local-artifact matrix verdict and scope boundaries.
