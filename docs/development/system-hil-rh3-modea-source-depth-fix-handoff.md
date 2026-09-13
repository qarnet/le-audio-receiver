# RH3 Mode A source TX-depth diagnostic fix handoff

Status: focused HIL source-fixture software phase. RH3 remains open. No
physical HIL command belongs in this phase.

## Goal

Change the dedicated two-CIS HIL source from two to three outstanding SDUs per
active stream. This permits the existing six-buffer source pool to be fully
used for Mode A, while preserving lockstep pairs, per-stream sequence numbers,
bounded progress timeout, and every acceptance rule.

This is a narrowly scoped source-pacing diagnostic correction. It does not
claim that buffer depth is root cause or that a later hardware row will pass.

## Evidence and decision

The current source has six host ISO TX buffers and six nRF5340 SW Split
controller ISO TX buffers:

- `hil/source/app/prj.conf:14` sets `CONFIG_BT_ISO_TX_BUF_COUNT=6`.
- `hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf:16` sets
  `CONFIG_BT_CTLR_ISO_TX_BUFFERS=6`.
- The resolved CPUNET configuration confirms both values are six, with two
  ISOAL sources and two streams in one CIG.

The NCS v3.3.0 HCI IPC central ISO base configuration uses this same
six-buffer setup and documents its completed-packet pipeline requirement:

```text
zephyr/samples/bluetooth/hci_ipc/nrf5340_cpunet_iso_central-bt_ll_sw_split.conf
34-38: Number of Completed Packets is returned one ISO interval later;
       host and controller ISO TX buffer counts must be equal or greater.
```

NCS's BAP unicast client producer also uses a six-buffer pool and keeps
submitting round-robin frames until pool exhaustion:

```text
zephyr/samples/bluetooth/bap_unicast_client/src/stream_tx.c:69-123
```

For two active transmitting streams, that normal pool-fill behavior permits
three outstanding SDUs on each stream. The HIL source instead freezes
`HIL_SOURCE_TX_OUTSTANDING_TARGET` at two per stream, so Mode A leaves two of
the six available source buffers unused.

Recent physical evidence warrants one controlled fixture change:

- `rh3-20260821-02-iso-link-quality-fix` completed all source submissions and
  callbacks (`12644` per stream, zero send failures), but took `139.556 s`
  from source `streaming` to `scored_complete`; the 48_4_1 preamble, scored,
  and tail target is `126.440 s`.
- Receiver SDC link-quality reports `rx_unreceived=14041` and `13968` for the
  two CISes, with only one CRC error total. Receiver callbacks recorded only
  119 and 115 valid SDUs and about 99 percent PLC.
- The older `rh3-20260815-08` Mode A row had no failed-CIS warning, yet showed
  the same approximately `139.434 s` source duration and 99 percent PLC.
  Therefore the one transient CIS establishment warning is not sufficient to
  explain the persistent loss.

Source sent callbacks remain controller completion credit only. NCS
`zephyr/include/zephyr/bluetooth/iso.h:765-775` says completion may mean
enqueue, on-air transmission, or flush. This change does not treat callbacks
as receiver-delivery proof.

## In scope

1. Set the fixed source outstanding target to three per active stream.
2. Update source-app native test names/comments that incorrectly describe the
   target as "two".
3. Add a public-boundary regression proving a two-stream Mode A run fills
   exactly three outstanding sends per stream, then blocks until sent callbacks
   arrive. The test must prove lockstep and no fourth send before a completion.
4. Update current internal HIL resume/status documentation after software
   verification. Record this as an unproven source-fixture diagnostic change,
   not an RH3 result.

## Out of scope

- No receiver, product firmware, BAP QoS, packing, timestamping, LC3 signal,
  PLC, I2S, FLPR, pairing, RF power, controller Kconfig, or buffer-count
  change.
- No HIL runner, row, parser, acceptance threshold, warning scanner, or
  evidence-policy change.
- No physical HIL run, flash, reset, serial session, pairing, OpenOCD, raw HCI,
  radio action, or manual hardware action.
- No `STATUS.md` edit, commit, push, merge, PR, tag, or release action.
- Do not modify historical execution handoffs or immutable evidence.

## Exact implementation

### 1. Source target

In `hil/source/app/src/hil_source_app.h` change only:

```c
#define HIL_SOURCE_TX_OUTSTANDING_TARGET    3U
```

Do not add a runtime configuration field or a new HIL1 protocol field. The
source retains one compile-time bounded target. With `HIL_SOURCE_MAX_STREAMS`
equal to two, Mode A can hold at most six app-owned ISO buffers, exactly the
existing pool capacity. Do not change `HIL_SOURCE_TX_POOL_COUNT`, either ISO
buffer Kconfig assignment, timeouts, or signal data.

`hil_app_tx_run()` already requires every Mode A stream to be below the target
before encoding a semantic pair. With target three, that existing condition
continues to enforce L/R lockstep and prevents a fourth submission on either
stream until both have completion capacity.

### 2. Native regression

In `tests/unit/hil_source_app/src/test_hil_source_app.c`, add one focused
Mode A behavior test using the existing fake backend and its externally visible
TX ledger:

1. Set up 48_4_1 Mode A and suppress automatic sent callbacks before START.
2. Pump until both streams reached `HIL_SOURCE_TX_OUTSTANDING_TARGET` sends.
3. Pump several more iterations without sent callbacks. Assert exactly three
   sends on stream 0 and exactly three sends on stream 1. Assert ledger order
   remains `0, 1, 0, 1, 0, 1`.
4. Re-enable callbacks, complete the run, and assert normal terminal PASS.

This proves caller-visible submission/backpressure behavior, not private
counter layout. Existing lockstep/cap, timeout, stale-callback, and completion
wake tests must stay passing.

Update only stale test helper names or comments that say "depth two" when they
refer to the shared compile-time target. Do not rename unrelated public HIL
fields (`sub`, `sc`, `sf`, `cb`, `out`).

### 3. Documentation

After tests and build pass, update these current internal state documents only:

- `docs/development/system-hil-resume-state.md`
- `docs/development/system-hil-rh3-software-status.md`

State:

- source outstanding target changed from two to three per Mode A stream;
- six host and six controller ISO TX buffers remain unchanged;
- NCS HCI IPC sample/pool evidence motivated the correction;
- software verification completed, but no physical execution occurred;
- existing RH3 evidence remains immutable and no acceptance claim follows.

Do not alter `docs/development/system-hil-rh1b-handoff.md` or older physical
handoffs. They are historical evidence of the former target.

## Verification

Run sequentially from repository root. Do not run `tests/hil` concurrently
with the source build because both use `build/hil-source`.

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west twister \
  -T tests/unit/hil_source_app \
  -p native_sim/native/64 \
  --inline-logs \
  --outdir /tmp/hil-source-app-depth-twister

nix develop --command fw-build-hil-source

git diff --check
git status --short
```

Inspect source-build output. Treat every actionable compiler, Kconfig, or CMake
warning as failure. Existing documented NCS ISO experimental notices may be
reported separately, never called clean warnings.

Record SHA-256 for all source artifacts after successful build:

```bash
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/hil-source/merged.hex \
  build/hil-source/merged_CPUNET.hex
```

Inspect `build/hil-source/app/zephyr/.config` and
`build/hil-source/hci_ipc/zephyr/.config`. Confirm source host and controller
ISO TX buffers remain six. No hardware action follows this handoff.

## Return format

Return after all scoped work completes. Include:

1. Files changed and exact source behavior change.
2. Regression test and source-build command results.
3. Resolved buffer values and four source artifact SHA-256 values.
4. Diagnostics, `git diff --check`, and `git status --short`.
5. Explicit no-hardware/no-commit status.
6. Any blocker or deviation.

Stop and escalate without guessing if target three exceeds pool capacity, the
NCS build changes an ISO buffer assignment, a warning appears, or test behavior
contradicts strict lockstep/backpressure semantics.
