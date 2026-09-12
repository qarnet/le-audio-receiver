# RH3 ModeA15 handoff: raw readback diagnostic

> [!WARNING]
> Historical, completed diagnostic plan. Do not execute it against the current
> source. Its pre-committed controller-flush classifications were superseded by
> the passing controller-clock source fix-validation in
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).
> Current successful status records use compact `tx.anchor`, `tx.pin`,
> `tx.skip`, `tx.lead`, and `tx.sync` fields. Failed command responses use
> `"tx":null` to preserve the fixed 1024-byte protocol line.

Status: completed one-run bounded historical diagnostic (user decision at the
ModeA14 stop point). Records the RAW SDC HCI VS ISO Read TX Timestamp
values and the pinned timestamps actually sent, as additive tolerated
fields in the existing HIL1 status record (the `pin_adv` field already
proved the parser tolerates extra stream fields). This is instrument
calibration on the HIL source fixture: no receiver change, no rows or
limits changes. The run is EXPECTED to reproduce the collapse
(`rx_valid=167`); it was intended to collect raw evidence at and after the
collapse point and discriminate the competing readback/flush hypotheses.
Later evidence showed that the recorded fields did not uniquely identify a
controller flush mechanism.

## What it recorded (historical schema)

Per stream, six additive uint32 fields in the status record's stream
objects (`rb_first`, `rb_min`, `rb_max`, `rb_last`, `rb_cnt`,
`pin_last`):

- `rb_*`: envelope of every successful `hci_vs_sdc_iso_read_tx_timestamp`
  return this segment (first, min, max, last, count). If the readback
  returns CIG event starts on the controller's grid, consecutive values
  advance ~one SDU interval and `rb_max - rb_first` ~= `(rb_cnt - 1) x
  10000`.
- `pin_last`: the pinned timestamp of the most recent `tx_send_ts`.
  `pin_last - rb_last` ~= small multiple of the interval means the pins
  tracked the readback grid; a huge or negative gap means they diverged
  (clock-domain mismatch or semantics mismatch).

Interpretation table (pre-committed, so the outcome classifies itself):

1. `rb` values advance ~10000 per readback and `pin_last` stays within
   a few intervals of `rb_last`: the pins WERE on the controller's
   event grid, yet the controller flushed them - the flush evaluation
   is not what the documentation's past-timestamp rule suggests on this
   path; the next lever is the controller-side configuration
   (FT/nse/bn structure, `BT_CTLR_SDC_CIG_RESERVED_TIME_US`), not the
   host chain.
2. `rb` values are constant or oscillate in a small range: the readback
   does not return event-grid values on this Zephyr-host-over-IPC
   configuration (different semantic), and any base computed from it is
   wrong; the host must derive pins another way (e.g., pure host-side
   pacing without pins, or the mirrored-RTC controller-time path).
3. `rb` values jump by orders of magnitude or wrap early: the returned
   clock domain differs from the assumed microseconds-since-event-grid
   (for example an epoch-anchored controller clock); pins must be
   computed with the correct domain conversion.
4. `rb_cnt` stops growing early (<< completions/3): the readback
   command itself fails or stalls after the collapse point - the flush
   may be poisoning the readback; capture the failure errno as an extra
   field if this appears.

## Scope

In scope (source fixture only):

1. `hil/source/app/src/hil_source_app.c`: six per-stream diagnostic
   counters (reset per segment, updated in the readback and send
   blocks) and the status-record fields.
2. Status JSON grows by six fields per stream object; the
   `streams_json` scratch (512 bytes) and the 1024-byte line bound are
   re-checked (single stream ~250 bytes, two streams ~500: fits).
3. Software verification (Twister, regression, builds, determinism)
   and ONE diagnostic run.

Out of scope: any behavior change to the send/pin/readback chain (the
ModeA14 completion-driven no-gate behavior stays EXACTLY as-is so the
collapse signature is reproduced under observation); receiver; rows;
limits; matrix; evidence changes.

## Fixed identity

```text
run ID: rh3-modeb-sdc-rbdiag-20260907 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_4_1
```

## Sequence

1. Implement the diagnostic fields.
2. Native Twister `tests/unit/hil_source_app`: all suites pass (the
   fields are additive; no test asserts absence).
3. Source build twice (determinism), no fragment
   (`CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`); receiver normal
   current-HEAD build.
4. Preflight as usual (disk gate, run-ID validation, fixture validate).
5. ONE runner run (outer timeout 3600000 ms):

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-modeb-sdc-rbdiag-20260907 \
     --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-rbdiag-20260907.junit.xml \
     --row rh3.fresh_mode_b_48_4_1
   ```

6. Extract `rb_*`/`pin_last` from every status record in
   `source-records.jsonl` (active snapshot, stop status, idle status)
   plus the receiver summary; classify against the interpretation
   table; write the result doc; commit; present the classified next
   step to the user.

## Prediction

The run reproduces the collapse (`rx_valid` ~167, `plc` ~2 x
`rx_lost`); that is expected and is not the pass/fail signal of this
diagnostic. The diagnostic succeeds if the `rb_*` envelope +
`pin_last` discriminate rows 1-4 of the interpretation table.

## Result documentation

`docs/development/system-hil-rh3-modea15-rbdiag-result.md`:
prediction vs outcome, all raw field values from every status record,
classification per the table, the receiver summary, integrity, and the
classified recommended next step.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run. Status
0/1/130 immutable. Preserve all prior evidence roots.
