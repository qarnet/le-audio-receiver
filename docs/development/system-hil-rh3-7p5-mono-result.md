# RH3-7p5 Stage 1 result: 7.5 ms mono passes on the fixed fixture

Status: completed one-run diagnostic with a decisive PASS. On the proven
128 MHz controller-clock source fixture, the 7.5 ms mono row
(`rh3.fresh_mono_48_3_1`) passed the frozen transport limits with
near-perfect delivery: receiver `rx_valid=16860` of `16859` submitted
(100.006%, one over-run duplicate absorbed), `plc=14` of
`decoded=16874` (0.083%, ceiling 5%), zero CRC errors, zero decode/I2S/
lifecycle faults. The pre-declared 7.5 ms receiver expectation held
exactly: FLPR stayed `ACTIVE` with `submit=0 success=0 fallback=0`
(the documented 360-frame cpuapp ASRC fallback path, validated as
correct by the runner's `48_3_1` branch), and the controller selected a
comfortable CIG layout at the tighter interval (`iso_interval_1250us=6`,
`nse=3`, `cig_sync_us=2346`, ~31% duty). This re-baselines 7.5 ms on
the current fixture and retroactively explains the H40/H42 collapse
evidence: those runs predate the two now-settled fixture defects (64
MHz app core starving the encode budget; host-offset scheduling), and
the SW-split central they used no longer exists in the fixture. It
does NOT by itself reinstate 7.5 ms in the mandatory matrix: Stage 2
(Mode B, the two-encode budget at 7.5 ms) and Stage 3 (Mode A, the
shared timestamp grid at 7.5 ms) remain before the reinstatement
question can open, and reinstatement itself is a rows/limits plan
revision. Not acceptance evidence beyond this row.

## Scope and immutable evidence

One runner-owned diagnostic execution used the current-HEAD source
images - byte-identical to the RH3-accepted tuple, because HEAD differs
from the acceptance commit only in documentation and the HIL source
embeds no commit stamp - and the normal current-HEAD receiver build:

```text
run ID: rh3-7p5-mono-20260911
row:    rh3.fresh_mono_48_3_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-7p5-mono-20260911/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-7p5-mono-20260911.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation
with `command.status=0`. `result.json` records `outcome="passed"`,
`first_failed_boundary=null`, `failure_detail=null`, and
`cleanup_failures=[]`. The runner alone resolved identities, flashed
targets, captured consoles, cleaned up, and finalized evidence.

## Prediction versus outcome

The Stage 1 handoff predicted: if the fixed fixture carries 7.5 ms,
mono passes the frozen limits (`rx_valid >= 90%` of 16859, `plc <= 5%`)
with source `skip` at or near zero, `sf=0`, and healthy lead telemetry.
The outcome met the prediction on every term:

| Handoff prediction | Observed |
| --- | --- |
| `rx_valid >= 90%` of 16859 | `rx_valid=16860` (100.006%) |
| `plc <= 5%` of decoded | `plc=14` of `decoded=16874` (0.083%) |
| source `skip` near zero | `skip=0` |
| `sf=0` | `sf=0` |
| lead healthy | `min=2878 us`, `max=2939 us`, `under=0` |
| FLPR ACTIVE, submit/success 0 (ASRC fallback) | `ACTIVE`, `submit=0 success=0 fallback=0` |

## Preflight and build proof

Run HEAD: `d4c77bc` (docs-only delta from the acceptance commit
`8123b94`). Tracked tree clean; untracked files were the ModeA3 handoff
(kept untracked, dormant) and `.cache/` directories only. Free space
`155545554944` bytes, above the `80 GiB` gate. Run-ID validation passed
(run directory and external JUnit absent, `.locks` empty). Fixture
validation returned `{"capture_capability": "none", "fixture_id":
"local-nrf54l15-receiver"}`.

Source build (one pristine build this session; the images are
byte-identical to the double-proven acceptance builds at `8123b94`
because the source embeds no commit stamp and only docs changed):

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `43bdef15f6cbca0ab4df9bbeda2594b0d1ccf0e5d9a534d0507998e712b4cb3d` |
| Source CPUNET | `2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b` |

Resolved source app config proved `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`
(no fragment); resolved CPUNET config proved the SDC block
(`CONFIG_BT_LL_SOFTDEVICE=y`, `CONFIG_BT_CTLR_CENTRAL_ISO=y`). Build
output contained only the documented dirty-tree and global `__ASSERT()`
notices.

Receiver: normal current-HEAD build, CPUAPP
`767715b610f3b96576fbb67202b84b87040e84b2f2d2ef13d47a6d5460fcc917`
(APP_COMMIT-derived), FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
(unchanged); resolved config proved `CONFIG_AUDIO_OFFLOAD_ASRC=y`,
`CONFIG_BT_ISO_RX_BUF_COUNT=3`.

`images.json` is authoritative for the runner-flashed tuple.

## Runner outcome and frozen limits

Source lifecycle completed with terminal `verdict="pass"`: `streaming`
at `monotonic_ms=19981`, `scored_complete` at `141424` (121.4 s
streaming window for 126.4 s of nominal SDU content - the pinned
lead-window head start, well within limits), final idle retaining
`seq=16860` with reset counters. Final telemetry:
`sub=16859, sc=16000, cb=16859, sf=0, out=0, skip=0`, lead
`min=2878 us, max=2939 us, under=0`, TX anchor `19523715`, sync
reference `145966353` with one successful final poll per stream.

The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=16860 decoded=16874 plc=14 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=16860 rx_error=0 rx_lost=14 rx_unknown=0 rx_no_ts=9
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no TS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 16860 | 16874 | 14 | 0 | 0 | 0 | 0 | 16860 | 0 | 14 | 0 | 9 |

All frozen limits met with wide margins; all zero-gates met
(`rx_error=0`, `rx_unknown=0`, `empty_sdu=0`, `decode_err=0`,
`i2s_underrun=0`, `stream_reset=0`).

## FLPR, QoS, and ISO tail

The pre-declared 7.5 ms receiver expectation held exactly. Active FLPR:

```text
State       : ACTIVE / epoch=820589926 gen=2
Counters    : submit=0 success=0 fallback=0 busy=0
```

FLPR submit/success at zero during streaming is the documented
360-frame payload-contract fallback (`FLPR_RING_PAYLOAD_MAX_INPUT ==
480U`, `src/flpr_ring.h`; AGENTS.md known-behavior question) - cpuapp
ASRC carried the stream, as the runner's `48_3_1` validation branch
requires. All fault and recovery counters zero.

Receiver QoS retained the 7.5 ms shape:

```text
QoS: interval 7500 framing 0x00 phy 0x02 sdu 90 rtn 5 latency 15 pd 40000
```

The ISO tail retained the controller-selected CIG layout at the 7.5 ms
interval:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=12 retransmitted=0 crc_error=0 rx_unreceived=9 duplicate=61 iso_interval_1250us=6 nse=3 cig_sync_us=2346 cis_sync_us=2346 c_max_pdu=90 c_phy=2 c_bn=1 c_flush_1250us=12
```

Notable bounded observations: `cig_sync_us=2346` (~31% duty at the
7.5 ms interval - substantially more margin than the 10 ms Mode B
shape's 41%), `rx_unreceived=9`, `duplicate=61` (absorbed retransmissions;
`rx_valid` still within 100.006%), `c_flush_1250us=12` (FT window 15 ms
per the 15 ms latency parameter). No warnings or errors on either
console.

## Classification and next step

Stage 1 PASS, classified clean: the fixed fixture (128 MHz,
controller-clock scheduling, SDC netcore) carries 7.5 ms mono with
near-perfect delivery. The pre-Stage-1 hypothesis - that the H40/H42
7.5 ms collapse was the source fixture and the SW-split central, both
since fixed/replaced - is now directly supported by a passing 7.5 ms
row on the current fixture with the receiver's counters unchanged in
their healthy 10 ms shape.

Per the staged sequence, Stage 2 (Mode B `48_3_1`: two LC3 encodes per
7.5 ms interval at 128 MHz against the pinned throughput lesson) is next
under its own handoff. Stage 3 (Mode A shared-grid at 7.5 ms) follows
only after Stage 2 passes. Reinstating the three `48_3_1` rows in the
mandatory matrix remains a plan revision, out of these diagnostics'
scope.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
eee5dec7a6b899a5ebdcd9388e7c4b823045918b05b47a3274edda497f142501
```

Runner-retained raw identity evidence: receiver `serial=8EE9B3FF
target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15`; source J-Link
`001050023938` (raw scan in `source-jlink.txt`). Preserve this evidence
root; do not rerun this ID.