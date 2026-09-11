# RH3 reinstated-matrix acceptance result (7.5 ms rows included)

Status: **`TRANSPORT_RUNTIME_ACCEPTED` under the 2026-09-11 plan revision,
with the three 7.5 ms rows included in the mandatory matrix.** The fixed
reinstated matrix passed all 20 children: 20 scheduled, 20 attempted, 20
passed, zero failed, zero cancelled, zero skipped, no cleanup failures.
Both passes completed the full 10-row schedule: the four 10 ms healthy
rows, the three reinstated 7.5 ms rows (mono, Mode A, Mode B
`48_3_1`), the preserved-bond Mode B row, the reconnect row, and the two
FLPR fault rows (hang, stall). Every 7.5 ms child matched its
three-stage re-baseline values exactly. This verdict covers the 10 ms
AND 7.5 ms transport and runtime boundary. It still does not cover exact
release artifacts, DAC output, analog audio, audibility, or stereo
channel mapping.

## Scope and immutable evidence

One runner-owned two-pass matrix execution at clean commit
`6ab8e3b77bab66e5de2cf0a1a5ae237be84afccc` (the plan-revision commit):

```text
run ID: rh3-matrix-48-3-1-reinstated-20260911
rows:   all 10 RH3_PASS_ROWS (7 healthy + reconnect + hang + stall), two passes
```

Aggregate evidence root:

```text
/tmp/opencode/hil-runs/rh3-matrix-48-3-1-reinstated-20260911/
```

Child evidence root:

```text
/tmp/opencode/hil-runs/rh3-matrix-48-3-1-reinstated-20260911.children.34b8eeedd7fc/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-matrix-48-3-1-reinstated-20260911.junit.xml
```

`result.json` records `outcome="passed"`,
`scheduled_child_count=20, attempted_child_count=20,
passed_child_count=20, failed_child_count=0,
cancelled_child_count=0`, `cleanup_failures=[]`. All 20 per-child
outcomes verified `passed` by direct inspection of every child
`result.json`.

## Images

Source images are byte-identical to the RH3-accepted tuple (the HIL
source embeds no commit stamp and the HEAD delta is documentation plus
runner/test code):

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `43bdef15f6cbca0ab4df9bbeda2594b0d1ccf0e5d9a534d0507998e712b4cb3d` |
| Source CPUNET | `2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b` |

Receiver rebuilt at the plan-revision HEAD (APP_COMMIT-derived):

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `f779c0d226d772570fa413d042f2aff210ba7e8a26264f3ae95d4353f57b8007` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Resolved receiver config proved `CONFIG_AUDIO_OFFLOAD_ASRC=y` and
`CONFIG_BT_ISO_RX_BUF_COUNT=3`; resolved source config proved target 3,
SDC netcore block unchanged. `images.json` in each child is
authoritative for the flashed tuples.

## 7.5 ms children match the re-baseline exactly

Pass-1 7.5 ms receiver summaries (pass 2 identical in shape):

```text
fresh_mono_48_3_1:   SDUs=16860 decoded=16874 plc=14 ... rx_valid=16860 rx_lost=14
fresh_mode_a_48_3_1: SDUs=16860 decoded=33746 plc=27 ... rx_valid=16860 rx_lost=59 (slot 1: 16859 valid, pair-attributed)
fresh_mode_b_48_3_1: SDUs=16858 decoded=33748 plc=32 ... rx_valid=16858 rx_lost=16
```

Each matches the corresponding single-run Stage 1/3/2 diagnostic exactly
(mono `plc=14`; Mode A `plc=27`; Mode B `plc=32`), demonstrating
run-to-run determinism of the receiver limits at 7.5 ms. The FLPR
state at 7.5 ms stayed `ACTIVE` with `submit=0 success=0 fallback=0`
in every child - the documented 360-frame cpuapp ASRC fallback path,
validated by the runner's `48_3_1` branch. The 10 ms children retained
their accepted 10 ms behavior including FLPR offload ACTIVE with
nonzero submit/success.

## Aggregate verification

- Every child: `outcome=passed`, no failed boundary, no failure detail,
  no cleanup failure.
- All child and aggregate SHA-256 manifests verified (aggregate check:
  0 mismatched entries).
- Aggregate and external JUnit byte-identical, both hashing to
  `a3f592bcd39e4523e2a60574c4db1a2050430a4e608007bae249e3f79a7216d1`.
- Identities freshly resolved for the run (receiver `8EE9B3FF`
  nRF54L15, source J-Link `001050023938`); raw identity evidence
  retained in each child.

## Verdict and scope

`TRANSPORT_RUNTIME_ACCEPTED` (2026-09-11 plan revision) proves real
two-device radio and firmware operation through the I2S submission
boundary for BOTH frame durations now in the mandatory matrix: 10 ms
(48_4_1) and 7.5 ms (48_3_1), across mono, Mode A, and Mode B, fresh
and preserved-bond states, reconnect, and FLPR fault recovery. It does
not prove exact release artifacts, DAC activity, analog output,
audibility, or stereo channel mapping.

This supersedes the 2026-09-09 10 ms-only acceptance as the current
RH3 verdict. The 2026-09-09 matrix evidence remains immutable and
valid for its scope; no child was retried in producing this
acceptance.

## Next steps

RH4 exact-artifact transport/runtime integration remains the next
transport gate, blocked on exact candidate archives (trusted-main
release lifecycle). The analog extensions (MA0/MA1, SA0/SA1) remain
separate per the plan of record.