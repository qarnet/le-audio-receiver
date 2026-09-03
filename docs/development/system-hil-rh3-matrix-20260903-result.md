# RH3 matrix result (2026-09-03)

## Result

One runner-owned RH3 matrix attempt used run ID
`rh3-matrix-20260903-rh3a`. The outer invocation printed
`matrix_status=1`; this status is immutable and no child or matrix retry was
run.

Aggregate evidence records `outcome=failed`, `scheduled_child_count=14`,
`attempted_child_count=2`, `passed_child_count=1`,
`failed_child_count=1`, `cancelled_child_count=0`, and
`cleanup_failures=[]`. Both attempted children completed. The aggregate JUnit
records 14 tests, one failure, 12 skipped tests, and zero errors. Its first
failed boundary is exactly:

```text
pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: session end
```

Immutable aggregate evidence:

```text
/tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a/
```

External aggregate JUnit:

```text
/tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.junit.xml
```

Child evidence is retained under the runner-derived sibling root:

```text
/tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.children.636a795064db/
```

## Sequential preflight

All preconditions passed before the one matrix invocation:

- Disk gate: `free_gib=123.9`, above the required 80 GiB.
- `git status --short` contained only the documentation-only untracked
  execution handoff, which this result commit includes. `git diff --check`
  passed.
- Run-ID validation passed: `run_id=rh3-matrix-20260903-rh3a`, `length=24`,
  `valid=yes`.
- Both aggregate output paths were absent and not symlinks. The output-root
  parent was read, then those ownership checks were repeated immediately before
  invocation.
- Fixture validation returned
  `{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.
- All four required image hashes matched:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source app | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

The matrix command used the required outer timeout of 10,800,000 ms. No
diagnostic fragment build, production source change, manual target operation,
or RH3-7p5 work occurred.

## Per-row outcomes

`NOT ATTEMPTED` means the aggregate JUnit's exact skipped message:

```text
not attempted: pass1 row2 rh3.fresh_mode_a_48_4_1 stopped matrix: session end
```

| Pass | Row | Outcome | Boundary, detail, or PASS counters |
| ---: | --- | --- | --- |
| 1 | `rh3.fresh_mono_48_4_1` | PASS | Source final `sub=12644`, `cb=12644`, `sc=12000`, `sf=0`; receiver slot 0 `rx_valid=12644`, `decoded=12656`, `plc=12`, `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`, `empty_sdu=0`, `rx_error=0`, `rx_unknown=0`. |
| 1 | `rh3.fresh_mode_a_48_4_1` | FAIL | `session end`; exact detail: `missing receiver stream summary slot(s): [0, 1]`. |
| 1 | `rh3.fresh_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 1 | `rh3.preserved_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 1 | `rh3.reconnect_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 1 | `rh3.flpr_hang_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 1 | `rh3.flpr_stall_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.fresh_mono_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.fresh_mode_a_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.fresh_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.preserved_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.reconnect_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.flpr_hang_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |
| 2 | `rh3.flpr_stall_mode_b_48_4_1` | NOT ATTEMPTED | Matrix stopped after pass 1, row 2. |

## Frozen transport-limit violations

The failed child stopped before the live session-end collector accepted either
summary. Its retained receiver transcript nevertheless contains both complete
summary lines. A read-only reparse with the runner's frozen validator found
these violations for the fixed expected count of 12,644 submitted SDUs per
Mode A stream:

| Slot | Observed value | Frozen threshold | Violation |
| ---: | --- | --- | --- |
| 0 | `rx_valid=135` | exact validator detail: `need >= 11379 (90% of 12644 submitted)` | Below delivery floor. |
| 0 | `plc=28458`, `decoded=28726` | exact validator detail: `need <= 1436 (5% of decoded=28726)` | Above concealment ceiling. |
| 1 | `rx_valid=133` | exact validator detail: `need >= 11379 (90% of 12644 submitted)` | Below delivery floor. |

The plan's 90% value is 11,379.6 SDUs for 12,644 submitted SDUs, so an integer
delivery count must be at least 11,380. The table preserves the validator's
exact current diagnostic wording as well as the underlying ratio. No other
frozen-field violation appears in the two retained summary lines: `rx_error`,
`rx_unknown`, `empty_sdu`, `decode_err`, `i2s_underrun`, and `stream_reset` are
all zero.

Exact retained receiver lines from the failed child are:

```text
uart:~$ [00:07:33.019,747] <inf> bt_bap: Stream[0] summary: SDUs=135 decoded=28726 plc=28458 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=135 rx_error=0 rx_lost=14305 rx_unknown=0 rx_no_ts=85
uart:~$ [00:07:33.159,310] <inf> bt_bap: Stream[1] summary: SDUs=133 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=133 rx_error=0 rx_lost=14244 rx_unknown=0 rx_no_ts=8
```

These observations are retained evidence from this child, not a retry and not
a root-cause diagnosis.

## Integrity and identities

Read-only SHA-256 verification passed for all retained manifest entries:

| Evidence | Result |
| --- | --- |
| Aggregate `rh3-matrix-20260903-rh3a` | 7/7 entries OK |
| Passed child `rh3-matrix-20260903-rh3.p1.r1.rh3.fresh_mono_48_4_1.b29ada458bc9` | 26/26 entries OK |
| Failed child `rh3-matrix-20260903-r.p1.r2.rh3.fresh_mode_a_48_4_1.80dd38b48348` | 23/23 entries OK |

`children.jsonl` names the sibling child-evidence paths above. Its flat
`/tmp/opencode/hil-runs/<child_run_id>` siblings do not exist, so read-only
child verification used each retained `child_evidence_path`. No evidence file
was altered.

Raw identity evidence retained by both attempted children records:

```text
receiver probe: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15
source probe:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340
```

The receiver values are in `identity.json` and the retained `nrf-probes.txt`
line:

```text
8EE9B3FF          Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  0x6ba02477  0x00054b15  AAC0
```

The source J-Link serial is in `identity.json`; its retained raw
`source-jlink.txt` lines are:

```text
FWJ|dpidr|0x6ba02477
FWJ|part|0x00005340
```

## Failure classification

The failed pass 1 Mode A child has two classified evidence facts. Neither is a
causal diagnosis.

1. **Fixture defect, session-end summary capture:** `result.json` records the
   exact failure detail `missing receiver stream summary slot(s): [0, 1]`, while
   the two exact retained `Stream[0] summary` and `Stream[1] summary` lines
   above show both summaries were emitted. The same runner parser read those
   retained bytes as two summaries in read-only review. The failed boundary is
   therefore a harness live-summary-capture failure, not evidence that receiver
   firmware omitted both summaries.
2. **Product defect, frozen transport behavior:** those same raw summary lines
   record `rx_valid=135` and `rx_valid=133` against the 90% delivery floor, plus
   `plc=28458` of `decoded=28726` on slot 0 against the 5% ceiling. This is a
   product transport-limit failure under the exact production images. It does
   not identify whether receiver firmware, source firmware, controller behavior,
   or RF conditions caused the loss.

No environment classification is claimed from this one attempt. The preceding
fresh-mono row passed, but that contrast does not establish a cause for Mode A.

## Scope conclusion and stop point

This failed aggregate does not record `TRANSPORT_RUNTIME_ACCEPTED`. The one
passing mono child is transport/runtime evidence only for that child and these
exact images. It is not whole-matrix acceptance, audio acceptance, DAC-output
proof, channel-mapping proof, analog proof, or audibility proof.

Preserve the aggregate and child roots. Do not reuse the run ID or retry a
failed child in this phase. Per the plan, later work must address the classified
failure, rerun the same row after a reviewed fix, then run a new full matrix.
