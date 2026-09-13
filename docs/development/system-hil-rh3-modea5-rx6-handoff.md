# RH3-ModeA5 ISO RX buffer-depth isolation handoff

Status: approved one-run hardware diagnostic. Tests the single remaining
runner-controlled variable for the Mode A/Mode B delivery-collapse:
receiver host ISO RX buffer pool depth.

## Grounding

Evidence chain (runner-validated or from immutable evidence):

1. Failing shapes and their delivered fractions: Mode B 10 ms `113/12644`,
   Mode A 10 ms `130+0/12644`, Mode B 7.5 ms `23-24/16859`. Passing: mono
   10 ms `12644/12644`.
2. The HILRX timing run (`rh3-modeb-rxtiming-20260904`, commit `0c3c9f7`)
   proved death is progressive, not instant: LOST fraction climbs 23% ->
   46% -> 100% over ~2 s, then callbacks continue at ~100 LOST/s for the
   rest of the run with `crc_error=0`. That is a host-side receive-pipeline
   saturation signature, not an RF/content switch. Content hypotheses
   (scored-carrier onset) are weakened by the 30 ms lead and by Mode A slot
   1 dying from sequence 0 (before any phase change).
3. The receiver host pool is `CONFIG_BT_ISO_RX_BUF_COUNT=3` shared across
   CISes. Every failing shape multiplies per-callback host work versus
   mono: Mode B doubles decode input per callback (240-byte SDU), Mode A
   doubles callback count (2 CIS), 7.5 ms raises callback rate 33%. The
   pool is the only structural variable that all four shapes stress while
   mono stays within margin.
4. Live pool exhaustion is already instrument-proven in this repo: H39's
   ISO RX lifetime trace captured `buffer_available=False` with
   `outstanding=capacity` and the teardown circular wait; run
   `rh3-20260824-31-sdc-hci-iso-rx6-trace` (7.5 ms) also recorded
   `retained_iso_buffer_unavailable`. The RX=6 variable has never been
   tested at 10 ms with working summary validation (that run predates the
   RH3b/RH3c capture repairs).

## Hypothesis (no root-cause claim yet)

If the collapse is host ISO RX pool depth under the failing shapes'
callback load, doubling the pool to 6 restores delivery and the limits
verdict flips to PASS. If it still fails with identical signature, pool
depth is exonerated at this size and the locus moves back to
controller/window timing.

## Exact change

One fragment: `tests/hil/receiver-rx6.conf` (new, minimal, no trace
instrumentation):

```text
CONFIG_BT_ISO_RX_BUF_COUNT=6
CONFIG_WARN_EXPERIMENTAL=y
```

No other change. RAM impact: the ISO RX pool grows by 3 buffers
(~each 255B data + headers); current diagnostic CPUAPP margin must be
checked at build (record `_image_ram_end`; if the build does not link,
stop and report - do not shrink other pools).

## Build proof

- Diagnostic build via
  `fw-build-54l15 -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-rx6.conf"`.
- Prove resolved config: `CONFIG_BT_ISO_RX_BUF_COUNT=6`, `AUDIO_OFFLOAD_ASRC=y`,
  `TRACING` unset, `HIL_RX_TIMING_TRACE` unset, `HIL_BAP_ENABLE_TRACE`
  unset.
- Double-build determinism proof; record hashes (HEAD-dependent CPUAPP).

## One hardware run

```text
run ID: rh3-modeb-rx6-20260904 (validated unused)
row:    rh3.fresh_mode_b_48_4_1
```

Normal runner invocation (no special flags; HILRX stays off to keep the
console clean - the summary IS the measurement). Outer timeout 3600000 ms;
status 0/1/130 immutable; never rerun.

## Classification

Runner-validated verdict only:

- PASS under frozen limits: ISO RX pool depth implicated for the failing
  shapes; next phase is a production-decision handoff (raise
  `BT_ISO_RX_BUF_COUNT` in the board conf with RAM margin analysis, or
  reduce per-callback host work), followed by the full matrix attempt.
- FAIL with the same death signature: pool depth exonerated; locus moves
  to controller/window timing between SW-split central and SDC peripheral.
- Other boundary: record, classify, stop for review.

## Result documentation and commit

`docs/development/system-hil-rh3-modea5-rx6-result.md` (canonical),
resume-state update. Commit exactly:

```text
docs(hil): record ISO RX buffer-depth isolation verdict
```

Include fragment, handoff, result, resume-state. No attribution footers,
no push.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no other production/source changes, no
rows/limits changes, no evidence mutation, no retry.