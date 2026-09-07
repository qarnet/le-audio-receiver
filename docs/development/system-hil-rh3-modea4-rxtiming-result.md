# RH3 ModeA4 receiver RX delivery-death timing result

Status: completed one-run diagnostic. Frozen receiver transport limits failed.
The retained timing data supports the scored-onset correlation branch, bounded
to a three-SDU (30 ms) lead before the source's scored boundary. It establishes
no root cause or repair.

## Scope and immutable evidence

One runner-owned execution used the diagnostic receiver image:

```text
run ID: rh3-modeb-rxtiming-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-rxtiming-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-rxtiming-20260904.junit.xml
```

The one authorized runner invocation returned `status=1`. `result.json`
records `outcome="failed"`, `first_failed_boundary="session end"`, and
`cleanup_failures=[]`. No retry, manual target operation, runner/parser change,
or evidence mutation occurred. The runner owned identity resolution, flashing,
console capture, cleanup, and evidence finalization.

The authoritative frozen-limit failure is:

```text
stream summary slot 0 transport limits: rx_valid=113 below floor: need >= 11379 (90% of 12644 submitted); plc=27196 above ceiling: need <= 1371 (5% of decoded=27422)
```

## Preflight and diagnostic build proof

Run HEAD was:

```text
f24ee6d28c54bfb7e476d00edcf60866d7d45248
```

The recorded run-time dirty tree contained the two implementation files, the
new fragment, the requested ModeA4 handoff, and the pre-existing untracked
ModeA3 handoff. `git diff --check` passed. Initial free space was
`163818594304` bytes (`152.6 GiB`), above the required `80 GiB` gate.

Run-ID validation passed for `rh3-modeb-rxtiming-20260904` (length `27`). Its
run root and external JUnit path were absent and non-symlinks before the runner
started; `.locks` was empty immediately before invocation. Fixture validation
returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

The frozen source identities matched before the receiver build:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Timing-trace fragment | `f9c4d59121a84b173168a1a1fc96bd6bab6d504c1648e8ef4a503f2eb6f571ef` |

Both pristine diagnostic builds used:

```text
fw-build-54l15 -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-rx-timing-trace.conf"
```

The resolved diagnostic configuration proved:

```text
CONFIG_HIL_RX_TIMING_TRACE=y
CONFIG_AUDIO_OFFLOAD_ASRC=y
# CONFIG_TRACING is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

The diagnostic ELF retained both `HILRX` format strings. Both builds emitted
only the documented dirty-tree notice, empty `drivers__watchdog` library CMake
warning, and global `__ASSERT()` notice. No actionable compiler or Kconfig
warning occurred.

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Receiver CPUAPP | `8442e97190bc24a9d090270ba2375bf81f5326f69038c23f49b01f4d1b991153` | `8442e97190bc24a9d090270ba2375bf81f5326f69038c23f49b01f4d1b991153` | Byte-identical |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | Byte-identical |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `8442e97190bc24a9d090270ba2375bf81f5326f69038c23f49b01f4d1b991153` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

The warning-pattern check imported all 12 `WARNING_PATTERNS` from
`scripts/hil/receiver.py` and tested all 338 retained `HILRX` lines from
`receiver-console.bin`: `warning_matches=0`. The required summary token
`err=<n>` does not match either `LOG_ERR` or `<err>` pattern.

## RX timing extraction and analysis

The source's `streaming` state record has `monotonic_ms=20333`. Applying the
fixed 144-SDU, 10 ms preamble contract gives the source-monotonic scored
boundary at `21773 ms`. Receiver `info->ts` is a different clock domain, so it
is not numerically subtracted from the source timestamp.

The receiver logged `Stream[0] started` at `[01:08:46.215,821]`. Its first
nine callbacks, sequence numbers 0 through 8, were `flags=0x04` with `ts=0`.
Therefore no direct receiver-clock timestamp exists for sequence 0. The first
valid timestamp was sequence 9:

```text
HILRX cb=10 slot=0 flags=0x09 ts=4126313445 seq=9
```

The observed timestamp cadence advances by approximately 10,000 units per
10 ms sequence interval. Back-projecting by nine such intervals gives an
estimated receiver-clock sequence-0 timestamp of `4126223445`. The last valid
callback was:

```text
HILRX cb=141 slot=0 flags=0x09 ts=4127623403 seq=140
```

The last valid timestamp is `1399958` controller-clock timestamp units after
that receiver-clock estimate (`1309958` units after the first valid timestamp),
consistent with sequence 140 at the observed 10 ms cadence. The API documents
this only as an ISO timestamp valid under `BT_ISO_FLAGS_TS` and does not specify
a unit, so this is a clock-domain-local comparison only.

The 200 retained per-callback lines contain exactly these flag populations:

| Flags | Callbacks | Meaning in this extraction |
| --- | ---: | --- |
| `0x04` | 9 | LOST without timestamp, sequences 0 through 8 |
| `0x09` | 113 | VALID with timestamp |
| `0x0C` | 78 | LOST with timestamp |

The preamble is sequence numbers 0 through 143. Its callback extraction had
113 valid and 31 lost callbacks (`78.47%` valid, `21.53%` lost). The last valid
was sequence 140; sequence 141 began an uninterrupted LOST run. The boundary
records are:

```text
HILRX cb=141 slot=0 flags=0x09 ts=4127623403 seq=140
HILRX cb=142 slot=0 flags=0x0C ts=4127633403 seq=141
HILRX cb=143 slot=0 flags=0x0C ts=4127643403 seq=142
HILRX cb=144 slot=0 flags=0x0C ts=4127653403 seq=143
HILRX cb=145 slot=0 flags=0x0C ts=4127663403 seq=144
```

Sequence 144 is the first scored SDU by the source contract. Thus valid
delivery ended three preamble SDUs, or 30 ms, before the scored boundary. The
observed post-preamble callback slice, sequence 144 through 199, had 0 valid
and 56 lost callbacks. The final receiver summary retained `rx_valid=113`, so
the remaining 12,500 source SDUs after the 144-SDU preamble also contributed
zero valid delivery.

The 138 per-second summary lines span `t=9s` through `t=146s`. Exact grouped
extraction is:

| Summary interval | Valid | LOST | Error | No timestamp | Interpretation |
| --- | ---: | ---: | ---: | ---: | --- |
| `t=9s` | 0 | 1 | 0 | 1 | First partial interval |
| `t=10s` | 60 | 18 | 0 | 8 | 18 LOST/s, 18/78 (`23.08%`) callbacks LOST |
| `t=11s` | 53 | 45 | 0 | 0 | Transition interval, 45 LOST/s, 45/98 (`45.92%`) callbacks LOST |
| `t=12s` through `t=146s` | 0 | 13,500 | 0 | 0 | 135 lines, each 99, 100, or 101 LOST callbacks and no valid callback |

For `t=12s` through `t=146s`, the exact LOST-line distribution is 33 lines at
99, 69 lines at 100, and 33 lines at 101. Thus post-death callbacks remained
continuous at about 100 LOST callbacks per second, not a sparse valid trickle.
Every such line preserved `last_valid_ts=4127623403`.

**Hypothesis branch supported: scored-onset correlation.** The evidence is the
final valid sequence 140, consecutive LOST records from sequence 141, first
scored sequence 144, zero valid callbacks throughout the observed scored slice,
and zero later valid callbacks in the final summary. The lead is three SDUs,
not an exact boundary hit. This single run cannot exclude a fixed-time trigger
that happened within that 30 ms lead, and it does not establish payload content,
source pacing, controller behavior, RF behavior, or any root cause.

The source completed `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and final `idle`. Its terminal stop status retained
`seq=12644 sub=12644 sc=12000 sf=0 cb=12644 out=0 first_errno=0`. The receiver
summary retained `decode_err=0`, `i2s_underrun=0`, `stream_reset=0`,
`empty_sdu=0`, `rx_error=0`, `rx_unknown=0`, `rx_no_ts=9`, and
`rx_lost=13598`. The live ISO tail retained `crc_error=0` and
`rx_unreceived=13252`.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `26/26` retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
9336b1fda78e23223a01369c6e2aa4876fc033cd55ee1acdc92e0b8d078c5b6f
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

The runner verified both receiver CPUAPP and FLPR writes. Source flash retained
only the documented page-tail erase extensions at `0x01023afc .. 0x01023fff`
and `0x0005741c .. 0x00057fff`.

## Normal-build restoration

One local, non-flashing `fw-build-54l15` normal build completed after the
read-only review. It had only the same documented diagnostics. Resolved normal
configuration proved:

```text
# CONFIG_HIL_RX_TIMING_TRACE is not set
CONFIG_AUDIO_OFFLOAD_ASRC=y
# CONFIG_TRACING is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
```

`strings` found no `HILRX` text in the normal CPUAPP ELF. Every trace statement
and trace-only static object is inside the matching preprocessor gate, so the
normal image compiles the instrument out completely.

| Image | Local normal-build SHA-256 |
| --- | --- |
| Receiver CPUAPP | `e1241bd06e416c2b1d1abd4a0ec92f793838032834c07a8435c5a144781ab459` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

Post-restoration free space was `163680931840` bytes (`152.4 GiB`), above the
`80 GiB` gate. The normal local build did not flash either board. The runner's
`images.json` remains authoritative for the last flashed diagnostic image.

Do not reuse or rerun this ID. Any next physical diagnostic needs a new
reviewed handoff and must preserve this evidence unchanged.

## Correction note (appended 2026-09-07, after the ModeA9 SN_STRICT validation)

The "scored-onset correlation" conclusion above is WRONG and is superseded by
the corrected analysis recorded in
`docs/development/system-hil-session-state-20260904.md` and validated by
`docs/development/system-hil-rh3-modea9-snstrict-result.md`. The per-second
HILRX lines log `t=<k_uptime/1000>` (uptime since boot), not time since
streaming start; streaming began at uptime ~9 s. Corrected profile: delivery
was never healthy (first nine events LOST, 23% LOST in the best second),
degrades monotonically to zero over ~2.5 s of streaming, and stays zero. The
"last valid seq 140 vs scored onset seq 144" was a coincidence artifact (the
7.5 ms runs die at seq 24, far before scored onset; scored onset is fixed in
samples = 1.44 s wall in all profiles, so it cannot align with a fixed wall
time across profiles). The supported mechanism is progressive ISO-AL
strict-sequencing payload expiry under a completion-paced host, not scored
content. All original observations, counters, hashes, and tables above are
retained unchanged; only this conclusion is corrected.
