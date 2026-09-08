# RH3 ModeA13 handoff: timestamp-mode without the host-side time gate

> [!WARNING]
> Historical, completed diagnostic plan. Do not execute it against the current
> source. Its classification arms are superseded by
> [system-hil-rh3-controller-clock-result.md](system-hil-rh3-controller-clock-result.md).

Status: historical completed plan. It originally approved one fix-validation
diagnostic that removed the
host-side time gate that the ModeA12 hardware run
(`docs/development/system-hil-rh3-modea12-tsmode-result.md`) convicted
with its own counter (`pin_adv=2040` guard fires correlating with
`rx_lost=2049` empty events and `plc=4098`), while keeping the
timestamp-mode pinning that the same run proved sound (delivery
`12643/12644`, stream aligned). The design simplification follows
directly from the SDC documentation: a pinned SDU submitted any time
before its ISO event is held by the controller for that event; only a
PAST timestamp is flushed. Therefore no host-clock-to-controller-clock
offset, lead/margin gate, or stale-pin guard is needed — the pins
themselves enforce one-SDU-per-event pacing, and the outstanding target
(3) bounds how far ahead the queue runs.

## What changed (already implemented and software-verified)

In `hil/source/app/src/hil_source_app.c`:

- Removed `tx_ts_offset_us` (the once-learned host-clock offset), the
  entire timestamp-mode send gate block (lead/margin arithmetic), the
  stale-pin advance guard, and `tx_ts_pin_advances` (with its
  `"pin_adv"` status field).
- Kept: the send-driven readback chain (every successful send marks a
  readback due; the readback resyncs `tx_ts_next` monotonically to
  `assigned + interval`, base learned at the first readback after the
  untimestamped first SDU), the pinned sends (`tx_send_ts` with
  `tx_ts_next`, spec-advance `+ interval` per successful send with
  rollback on failure), and the prefix rule (no further sends until a
  stream's base is learned; bounded wait on the readback wake).
- Bookkeeping-before-send (from the ModeA12 session) stays: the send
  applies seq/outstanding/submitted/pin/readback-due under the mutex
  BEFORE the backend call and rolls everything back on failure, closing
  the early-callback race the native suite exposed.

In `hil/source/app/src/hil_source_app.h`: removed
`HIL_SOURCE_TX_TS_MARGIN_US` and `HIL_SOURCE_TX_TS_LEAD_US`.

In tests: removed the pin_adv status test (field gone); the two
remaining ModeA12 tests (first-send-plain-then-pinned;
pins-advance-one-interval) are unchanged and pass. Fake readback keeps
mirroring the last pinned timestamp (SDC "most recently submitted"
semantics).

Software verification (this session, all green):

- Native source-app Twister: `70/70` passed.
- Two pristine source builds byte-identical: source CPUAPP
  `7f9de7fc367f13be694b3353bde06b75c163e3d2b35354a6e31ee00c39ae0c18`,
  CPUNET unchanged `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`.
- Resolved app config `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`, QoS
  unchanged.
- Only the documented dirty-tree and global `__ASSERT()` notices.

## Fixed identity

```text
run ID: rh3-modeb-sdc-tsnogate-20260907 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_4_1
```

## Sequence

1. Preflight: disk gate; `git rev-parse HEAD` + `git status --porcelain`
   (expected: the ModeA10 SDC rework, the ModeA12/13 timestamp-mode
   source changes and tests, the ModeA10-13 handoff/result documents,
   and the pre-existing untracked ModeA3 handoff); run-ID validation;
   `.locks` empty; fixture validate.
2. Receiver: normal current-HEAD `fw-build-54l15`; record hashes
   (APP_COMMIT-derived CPUAPP; FLPR `45ab8d15...` expected); resolved
   config proves `CONFIG_AUDIO_OFFLOAD_ASRC=y`,
   `CONFIG_BT_ISO_RX_BUF_COUNT=3`.
3. Source: builds from this session are already double-proven; rebuild
   once more to re-hash at run HEAD, or reuse the current
   `build/hil-source` artifacts if unchanged since the determinism proof
   (record which in the result doc).
4. ONE runner invocation (no special flags), outer timeout 3600000 ms:

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-modeb-sdc-tsnogate-20260907 \
     --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-tsnogate-20260907.junit.xml \
     --row rh3.fresh_mode_b_48_4_1
   ```

5. Integrity review; per-slot summary; limits verdict; FLPR active; ISO
   tail.

## Prediction (falsifiable)

Without the gate, pinned SDUs are submitted as soon as outstanding slots
free (completions), always ahead of their events, and the guard cannot
fire (removed). If the pin mechanism alone sustains the schedule, empty
events collapse to start-up transients: `rx_lost` in single digits
(healthy mono control showed `rx_lost=12`), `plc <= 5%` of decoded
(expected near the mono baseline's 12-13, far below the ~1470 ceiling),
and the streaming window returns to the nominal 126.4 s (no stretch).

## Classification arms

- PASS: timestamp-mode pinning validated as the fixture's production
  provisioning. Write the ModeA13 result doc, update resume-state, commit
  everything (SDC rework + timestamp-mode source + tests + docs) as:

    fix(hil): switch fixture to SDC timestamp-mode ISO provisioning

  Then report readiness for the full RH3 matrix attempt (separate phase,
  new run ID `rh3-matrix-<date>-2`, `run-rh3-matrix`, 14 child runs,
  outer timeout 10800000 ms).

- FAIL plc still `2 x rx_lost` with rx_lost large: the pins alone did not
  sustain the schedule. Record, leave uncommitted, STOP for redesign
  review (second same-shape failure of the SDC timestamp-mode line; the
  plan's two-consecutive-failure stop point applies; next candidates:
  VS readback semantics on hardware (assigned of submitted vs
  completed), bn/FT retransmission interleave, ACL-event skipping).

- Other boundary: record, classify, and either one bounded
  fix-validation with a named change or STOP for user decision.

## Result documentation

`docs/development/system-hil-rh3-modea13-tsnogate-result.md` (canonical):
scope, prediction vs outcome, preflight, build proof, per-slot summary,
limits verdict, FLPR active, QoS, ISO tail, integrity, raw identity,
restoration, stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. Single run; no retry.
Status 0/1/130 immutable. Preserve all prior evidence roots, including
`/tmp/opencode/hil-runs/rh3-modeb-sdc-tsmode-20260907/`.
