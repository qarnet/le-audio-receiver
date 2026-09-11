# RH3-7p5 Stage 3 result: 7.5 ms Mode A passes; three-stage evidence complete

Status: completed one-run diagnostic with a decisive PASS, completing
the RH3-7p5 three-stage re-baseline. On the proven 128 MHz
controller-clock source fixture, the 7.5 ms Mode A row
(`rh3.fresh_mode_a_48_3_1`) passed the frozen transport limits with
both CISes delivering: receiver slot 0 `rx_valid=16860` of `16859`
submitted, slot 1 `rx_valid=16859` of `16859`, `plc=27` of
`decoded=33746` (0.08%, ceiling 5%); slot 1's zero decoded count is the
expected Mode A pair attribution (assembled output on slot 0, matching
the accepted 10 ms Mode A matrix shape). Source fully healthy on both
streams (`sub=16859, sc=16000, sf=0, out=0`), shared-grid `skip=0`,
both lead arrays healthy (`2879..2948 us` / `2664..2847 us`,
`under=[0,0]`), and both final sync polls returned the IDENTICAL shared
reference `146627584` - the shared-timestamp grid mechanism proven at
the tighter interval. Controller CIG held both CISes with an offset
pair (`cis_sync_us=2346/4692`, `cig_sync_us=4692`, ~62% duty, `nse=3`,
`c_max_pdu=90` each). FLPR `ACTIVE` with `submit=0 success=0 fallback=0`
(the documented 360-frame cpuapp ASRC fallback).

All three RH3-7p5 stages now pass on the fixed fixture:

| Stage | Row | rx_valid | PLC |
| --- | --- | ---: | ---: |
| 1 mono | `rh3.fresh_mono_48_3_1` | 16860/16859 (100.006%) | 14 (0.083%) |
| 2 Mode B | `rh3.fresh_mode_b_48_3_1` | 16858/16859 (99.994%) | 32 (0.095%) |
| 3 Mode A | `rh3.fresh_mode_a_48_3_1` | 16860+16859/2x16859 | 27 (0.08%) |

The plan-of-record RH3-7p5 exit question ("7.5 ms must work or must not
be supported") now has its evidence basis: 7.5 ms WORKS across mono,
Mode B, and Mode A on the current fixture, at every stage with the
source fully healthy (`skip=0, sf=0, lead under=0`) and the receiver
inside all frozen limits with wide margins. The old H40/H42 collapse
is fully attributed to the two settled fixture defects (64 MHz app
core, host-offset scheduling) plus the since-replaced SW-split central.

DECISION NOW REQUIRED (user, plan revision): whether to reinstate the
three `48_3_1` rows in the mandatory matrix (`RH3_PASS_ROWS` move from
`RH3_7P5_DIAGNOSTIC_ROWS` in `scripts/hil/rows.py` - a rows/limits
change requiring a plan-of-record revision per the standing decision)
or to keep 7.5 ms diagnostic-only. Reinstatement would also require
updating the receiver-side known-limitation language (the 360-frame
FLPR fallback is expected behavior at 7.5 ms, not a defect) and
re-running the mandatory matrix with the reinstated rows. Nothing in
this phase changed rows, limits, code, or the product PACS.

## Scope and immutable evidence

One runner-owned diagnostic execution used the current-HEAD source
images (byte-identical to the RH3-accepted tuple; hashes re-verified
unchanged across all three stages) and the normal current-HEAD
receiver build:

```text
run ID: rh3-7p5-modea-20260911
row:    rh3.fresh_mode_a_48_3_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-7p5-modea-20260911/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-7p5-modea-20260911.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation
with `command.status=0`. `result.json` records `outcome="passed"`,
`first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`.

## Prediction versus outcome

| Handoff prediction | Observed |
| --- | --- |
| both streams `sub=16859, sc=16000, sf=0, out=0` | exact match |
| shared-grid `skip=0` | `skip=0` |
| both lead arrays healthy, `under=0` | `2879..2948` / `2664..2847`, `under=[0,0]` |
| both sync polls return the shared reference | both `146627584` |
| slot 0 `rx_valid >= 90%`, `plc <= 5%` | `16860/16859`, `27/33746` (0.08%) |
| slot 1 valid, zero decoded (pair behavior) | `16859`, decoded `0` |
| FLPR ACTIVE, zero submit/success (ASRC fallback) | `ACTIVE`, `submit=0 success=0 fallback=0` |
| controller CIG holds both CISes | `nse=3` each, `cis_sync 2346/4692`, `cig_sync 4692` |

## Build proof

Same proven images as Stages 1-2 (verified unchanged): source CPUAPP
`43bdef15f6cbca0ab4df9bbeda2594b0d1ccf0e5d9a534d0507998e712b4cb3d`,
source CPUNET
`2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b`,
receiver CPUAPP
`767715b610f3b96576fbb67202b84b87040e84b2f2d2ef13d47a6d5460fcc917`,
receiver FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`.
`images.json` is authoritative for the flashed tuple.

## Receiver summaries and ISO tail

```text
Stream[0] summary: SDUs=16860 decoded=33746 plc=27 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=16860 rx_error=0 rx_lost=59 rx_unknown=0 rx_no_ts=55
Stream[1] summary: SDUs=16859 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=16859 rx_error=0 rx_lost=32 rx_unknown=0 rx_no_ts=10
```

All frozen limits met on both slots; all zero-gates met. `rx_lost=59/32`
and `rx_no_ts=55/10` are record-only fields, absorbed with delivery at
99.99%+.

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 ... crc_error=1 rx_unreceived=55 duplicate=79 iso_interval_1250us=6 nse=3 cig_sync_us=4692 cis_sync_us=2346 c_max_pdu=90 c_phy=2 c_bn=1 c_flush_1250us=12
  Stream[1] handle=0x0002 ... crc_error=0 rx_unreceived=10 duplicate=64 iso_interval_1250us=6 nse=3 cig_sync_us=4692 cis_sync_us=4692 c_max_pdu=90 c_phy=2 c_bn=1 c_flush_1250us=12
```

Bounded observation: the two CISes schedule offset within the CIG event
(`cis_sync 2346/4692` at `cig_sync 4692`, ~62% total duty - the
tightest of the three stages but with both CISes delivering at 99.99%
with `retransmitted=0`). No warnings or errors on either console.

## Classification and stop point

Stage 3 PASS, classified clean. The RH3-7p5 phased evidence is
complete; the reinstatement-vs-diagnostic-only decision moves to the
user as a plan-of-record revision. Nothing in this phase changed rows,
limits, code, or the product PACS; 7.5 ms remains diagnostic-only until
that decision. Preserve this evidence root; do not rerun this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
1375b83ec932e65b14c2d55d5f2bf852586af671407e700ad6dfb0b6d5e680ed
```

Runner-retained raw identity evidence: receiver `serial=8EE9B3FF
target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15`; source J-Link
`001050023938` (raw scan in `source-jlink.txt`).