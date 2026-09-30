# PB-035: timestamped source peer-enqueue guard

## Boundary and repair

The clean `2a0e792` exact-artifact matrix failed fresh Mode A `48_4_1`
before scoring: stream 0 submitted 16 regular SDUs, stream 1 submitted 15,
then the second-stream minimum-lead guard aborted with `-ETIME`. The failed
run remains immutable. See `logic-analyzer-continuation-results-20260930.md`.

Installed NCS v3.4.1 makes `bt_bap_stream_send_ts()` a nonblocking host enqueue,
not a controller-completion wait. Enqueue wakes cooperative `bt_tx_processor`;
work submission can reschedule that thread before the preemptible source worker
returns. The old status-exclusion mutex did not exclude this scheduling boundary.
Source's 3000-us target and 2000-us minimum therefore left at most 1000 us,
and sometimes less after wake jitter, to finish the peer enqueue.

`hil_app_tx_submit_batch()` now acquires app ownership before a narrow scheduler
lock, rereads controller time inside that lock, and enqueues both already-encoded
peer SDUs before releasing it. App accounting cannot wait on another callback
inside the guarded window. Real buffer allocation remains `K_NO_WAIT`; no
encoding, output, controller-relative sleep, credit wait, synchronous HCI,
or interrupt masking was added inside the window. App mutex is released before
scheduler unlock, including every error and stale-pin retry path.

Both CISes still use the same pinned timestamp. The **3000/2000-us margins,
QoS, source recipes, receive limits and matrix rows are unchanged**. Genuine
late peers caused by elapsed time despite scheduling exclusion still fail closed.
This is not an atomic controller transaction or an RF-delivery guarantee.

SDK witnesses, root `/home/thomas-workstation/ncs/v3.4.1`:

- `zephyr/subsys/bluetooth/audio/bap_stream.c:388-452` and
  `zephyr/subsys/bluetooth/host/iso.c:892-906,984-1006`: enqueue path.
- `zephyr/subsys/bluetooth/host/conn.c:874-905` and
  `zephyr/subsys/bluetooth/host/hci_core.c:5209-5215`: wake TX processor.
- `zephyr/kernel/work.c:393-406`: reschedule opportunity.
- `zephyr/subsys/bluetooth/host/conn.c:291-331`: completion callback is
  workqueue/thread context, not an ISR that can block on the app mutex.
- `nrfxlib/softdevice_controller/include/sdc_hci.h:79-83`: 1000-us margin
  at **controller arrival**, not public API entry. Guard does not measure that
  arrival or turn the 2000-us software check into controller evidence.

## Native behavioral verification

New regression submits real cooperative Zephyr work from fake host enqueue.
It models 1500 us of virtual elapsed TX work anchored to the enqueue time.
It proves successful public terminal/status records, equal completed event
counts, same timestamps on both submitted CISes, and unchanged minimum lead.
The fake plant does not add that work interval twice after already advancing
to a later event. No DUT algorithm is copied as an independent timing oracle.

The **same final regression fails against untouched `2a0e792` source**:
`/tmp/opencode/nrf54-source-batch-red-final-app/provenance.json`,
`/tmp/opencode/nrf54-source-batch-red-final-build/`, and
`/tmp/opencode/nrf54-source-batch-red-final-run.log` retain source identity,
build and selected negative-control failure. The 83 unselected cases in that
one-case negative control are not a canonical gate or skipped acceptance rows.

Final production source and test suite passed **84/84, zero skips**:
`/tmp/opencode/nrf54-source-batch-final-20261001-r1.log`.
Cases include final/peer clock-read failure, first/peer timestamped-send
failure, first errno surviving distinct cleanup error, status/idle and valid
fresh run after each failure, aborted injection not leaking across reset,
status exclusion, cancellation/backpressure, wrap, and genuine late-peer abort.

Command:

```sh
env -u ZEPHYR_BASE nix develop -c env NIX_HARDENING_ENABLE= \
  west build --no-sysbuild -b native_sim/native/64 \
  -d /tmp/opencode/nrf54-source-batch-final-20261001-r1 \
  tests/unit/hil_source_app -p -t run
```

Initial direct Twister attempt failed host configuration on `_FORTIFY_SOURCE`
at `-O0`, plus unused `TC_NAME` and jobserver notices. It is retained at
`/tmp/opencode/nrf54-source-batch-red-20260930-r1/`; no waiver granted.
Subsequent runs use the repository canonical `west build` command and its
host-only `NIX_HARDENING_ENABLE=` setting. The native unsupported-SoC notice
and fake-entropy test banner retain their existing narrow dispositions.

## Physical timing diagnosis, not historical root-cause proof

Temporary compile-gated RAM timing fields captured controller-clock samples
around stream-0 entry/return and peer check. Raw reads used fresh session
revalidation and explicit-probe OpenOCD, with no halt/reset/write. Diagnostic
source/config/ELF/HEX snapshots reside beside each run. The trace code was
**removed** from final source and acceptance builds.

| Diagnostic supervisor/HIL run ID | Frozen row | Max send0 elapsed | Max gate-to-peer elapsed |
| --- | --- | --- | --- |
| `nrf54-source-batch-trace-20260930-r1` | Mode A 10 ms | 360 us | 421 us |
| `nrf54-source-batch-trace-20260930-r2` | Mode A 7.5 ms | 335 us | 349 us |
| `nrf54-source-batch-guard-trace-20260930-r1` | Mode A 10 ms | 32 us | 54 us |

Supervisor roots are `/tmp/opencode/<ID>/`; row roots
`/tmp/opencode/hil-runs/<ID>/`. Same diagnostic receiver CPUAPP HEX
`4a61b4531bc211884a5f211502236961ad5a660112f22205f8b796996c608725`
and FLPR HEX
`c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`
were used throughout. Source hashes and instrumentation flags are retained
per run; these are dirty-tree diagnostics, not clean archive provenance.

All three diagnostic rows passed. The historical physical `-ETIME` did not
reproduce under instrumentation; maxima are observations, not WCET or a
reconstructed historical execution trace. They support reduced enqueue-window
delay. Deterministic red/green test proves the exposed scheduling defect;
full uninstrumented physical matrix remains the integration acceptance gate.

Uninstrumented final source pristine build succeeded:
`/tmp/opencode/nrf54-source-batch-final-build-20261001-r1.log`.
Source HEX SHA-256
`57c836e3a70e6a1d3bbf6a15839fa879ef3548899322a05df4277e12b32a69c2`;
ELF SHA-256
`d4abe7b74edc40c25d107723a850c5cf5d2d9d8e7781fe9109ca09d293a84a32`.
One unchanged frozen Mode A 120-s row passed at
`/tmp/opencode/hil-runs/nrf54-source-batch-final-physical-20261001-r1/`:
12,000 scored and 12,644 submitted/completed per CIS; source send failures,
skips and under-lead zero; lead minima 2741/2706 us. No trace symbol exists
in this final ELF. This row remains local-build proof, not final clean matrix.

Raw six-checkpoint identity evidence lives in each row's `identity.json`,
CMSIS-DAP/udev captures and `session-revalidations.jsonl`. Both nRF54L15
candidates read DP `0x6ba02477`, AP0/1 `0x84770001`, AP2 `0x32880000`,
AP3 `0x00000000`, FICR PART `0x00054b15`, VARIANT `0x41414330`.
Role assignment also uses bound USB/probe identity; these shared silicon
values are not a static role table or authority for a later target action.

No nRF5340 action, SDK edit, threshold relaxation, analog claim or release
acceptance occurred. Clean exact-commit software/build and full physical
matrix follow this repair under PB-035/PB-036/PB-038.
