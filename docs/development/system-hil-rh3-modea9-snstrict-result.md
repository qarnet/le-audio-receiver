# RH3 ModeA9 SN_STRICT validation result

Status: completed one-run diagnostic with a partial-recovery outcome that
selects neither of the approved handoff's predicted arms. Disabling strict
ISO-AL TX sequencing (`CONFIG_BT_CTLR_ISOAL_SN_STRICT=n`) on the fixture
SW-split central recovered fresh Mode B 10 ms delivery from `113` to `10141`
valid SDUs (0.9% to 80.2% of 12644 submitted), confirming strict-sequence
payload expiry as the dominant loss mechanism, but delivery stayed below the
frozen 90% floor and PLC stayed far above the 5% ceiling. This is not
acceptance, not a full validation of the phase-slip diagnosis, and not a
fixture default change. The overlay line remains uncommitted pending the
recorded user decision.

## Scope and immutable evidence

One runner-owned execution used the diagnostic source CPUNET image (the
staged overlay line) and the normal current-HEAD receiver build:

```text
run ID: rh3-modeb-snstrict-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-snstrict-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-snstrict-20260904.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation with
`command.status=0`, the fixed row, and no diagnostic runner flag.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The runner
alone resolved identities, flashed targets, captured consoles, cleaned up,
and finalized evidence. No retry, manual target operation, receiver or runner
change, or evidence mutation occurred.

## Prediction versus outcome

The approved handoff predicted a binary outcome:

- PASS (delivery recovers to `rx_valid >= 11379`, `plc <= 5%` of decoded)
  validates the phase-slip diagnosis fully.
- FAIL with the same collapse signature (delivery stays near zero) falsifies
  it and points at `CONFIG_BT_CTLR_ISOAL_PSN_IGNORE=y` as the next discussion.

The actual outcome is a third state: massive but incomplete recovery.
`rx_valid=10141` (80.2%) with `plc=14800` (42.2% of `decoded=35082`). The
dominant-mechanism claim is supported (a 90x delivery improvement is not
noise), but the diagnosis is incomplete: something still removes roughly one
in five submitted SDUs and every stretch interval. Per the handoff this is the
"other boundary" arm: record, classify, stop.

## Preflight

Run HEAD was:

```text
87b141300782127f0691684688ff7b01df7fbd25
```

The run-time dirty tree contained exactly the staged overlay modification and
the three untracked handoff/session-state documents; `git status --porcelain`
matched the handoff's expectation. `git diff --check` passed. Initial free
space was `164215214080` bytes, above the `80 GiB` gate.

Run-ID validation passed for `rh3-modeb-snstrict-20260904` (length 27, matches
the checked-in safe pattern). Its run directory and external JUnit path were
absent and non-symlinks; `.locks` was empty. Fixture validation returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## Build proof and flashed images

Both pristine diagnostic source builds used no fragment; the staged overlay is
wired into `hci_ipc` through `add_overlay_config` in
`hil/source/app/sysbuild.cmake`:

```text
nix develop --command fw-build-hil-source
```

The resolved source CPUNET configuration proved the diagnostic change and the
unchanged controller shape:

```text
# CONFIG_BT_CTLR_ISOAL_SN_STRICT is not set
CONFIG_BT_LL_SW_SPLIT=y
CONFIG_BT_CTLR_CONN_ISO=y
```

The resolved source application configuration proved the frozen QoS values:

```text
CONFIG_HIL_SOURCE_QOS_RTN=5
CONFIG_HIL_SOURCE_QOS_PHY=2
CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3
```

Diagnostic build determinism was byte-identical across two pristine builds:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | Unchanged from normal build |
| Source CPUNET | `696d4c4f320c56e9ddde1d040f9a8cf3ee3251e8c673432f043e0866448a8aec` | `696d4c4f320c56e9ddde1d040f9a8cf3251e8c673432f043e0866448a8aec` | Byte-identical; changed from `4e4b82f5...` exactly as predicted (controller config change) |
| Merged CPUAPP | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` | Unchanged |
| Merged CPUNET | `c161377f28d4bc9f07dc3d417741ac4bcca369db3ffafef511dbaa147703bec3` | `c161377f28d4bc9f07dc3d417741ac4bcca369db3ffafef511dbaa147703bec3` | Byte-identical |

One pristine normal receiver build at the same HEAD used no fragment. Its
resolved configuration proved `CONFIG_AUDIO_OFFLOAD_ASRC=y` and
`CONFIG_BT_ISO_RX_BUF_COUNT=3`. CPUAPP embeds `APP_COMMIT`, so its hash is
HEAD-dependent and was derived at execution time:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `b1ed65df7ade58e2c7b79b463ab1fa57bf9a22e2c3f7332987cdc620ed4285c9` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `696d4c4f320c56e9ddde1d040f9a8cf3251e8c673432f043e0866448a8aec` |
| Receiver CPUAPP | `b1ed65df7ade58e2c7b79b463ab1fa57bf9a22e2c3f7332987cdc620ed4285c9` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Build output contained no actionable compiler or Kconfig diagnostic beyond the
documented dirty-tree notice, global `__ASSERT()` notice, required SW Split
controller informational notices, and the receiver's documented empty
`drivers__watchdog` library warning.

## Runner outcome and frozen limits

The source completed its full lifecycle with terminal `verdict="pass"`:
`streaming` at `monotonic_ms=20208`, active snapshot
`seq=16 sub=16 sc=0 sf=0 cb=15 out=1`, `scored_complete` at `189640`,
`teardown` at `195496`, terminal at `195978`, final idle retaining
`seq=12644` with reset counters. Source records show `sub=12644, sc` reached
`12000` scored, `sf=0`, `first_errno=0`; the source host remains exonerated.

The sole required receiver stream summary was:

```text
Stream[0] summary: SDUs=10141 decoded=35082 plc=14800 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=10141 rx_error=0 rx_lost=7400 rx_unknown=0 rx_no_ts=9
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 10141 | 35082 | 14800 | 0 | 0 | 0 | 0 | 10141 | 0 | 7400 | 0 | 9 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=10141 below floor: need >= 11379 (90% of 12644 submitted); plc=14800 above ceiling: need <= 1754 (5% of decoded=35082)
```

`rx_error=0`, `rx_unknown=0`, and `empty_sdu=0` met their zero requirements.
`decode_err=0`, `i2s_underrun=0`, and `stream_reset=0` met the existing gates.
`rx_lost` and `rx_no_ts` remain record-only under the frozen limits. No
receiver runtime warning, source warning, assertion, or shell error was
retained on either console.

## FLPR, QoS, and ISO tail

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=2068362617 gen=2
Counters    : submit=55 success=55 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
```

ASRC offload retained `submit=60 success=60 fallback=0` with all listed
faults zero. Limits validation failed before post-stop collection, so no
post-stop FLPR snapshot is retained or inferred, matching the established
ModeA5/ModeA8 pattern.

The receiver QoS log retained the frozen shape:

```text
QoS: interval 10000 framing 0x00 phy 0x02 sdu 240 rtn 5 latency 20 pd 40000
```

The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=4808 retransmitted=0 crc_error=2 rx_unreceived=7231 duplicate=0 iso_interval_1250us=8 nse=6 cig_sync_us=8184 cis_sync_us=8184 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

**Mandatory on-air sanity check: PASS.** Resolved CPUNET config proves the
single staged change (`SN_STRICT` unset) with `BT_LL_SW_SPLIT=y` retained;
receiver QoS and the ISO tail retain the frozen 2M, RTN5, 240-byte Mode B
shape (`c_phy=2`, `c_max_pdu=240`, `nse=6`). Classification is therefore
permitted.

## Analysis: what recovered, what did not

Bounded comparison against the fresh Mode B control at the same shape
(`rh3-modeb-control-20260904`, SN_STRICT=y):

| Observation | SN_STRICT=y control | SN_STRICT=n this run |
| --- | ---: | ---: |
| Expected submitted SDUs | 12644 | 12644 |
| `rx_valid` | 113 | 10141 |
| Delivered fraction | 0.9% | 80.2% |
| `decoded` | 27422 | 35082 |
| `plc` | 27196 | 14800 |
| `rx_lost` | 13598 | 7400 |
| Controller `rx_unreceived` | 13571 | 7231 |
| `crc_error` | 1 | 2 |

Three bounded observations follow from the counters:

1. **Expiry-drop removal is the dominant recovery.** With strict sequencing
   disabled, payloads that were previously expired and dropped are now
   transmitted and received. The 90x `rx_valid` improvement with an unchanged
   receiver image and unchanged QoS isolates the CPUNET `SN_STRICT` change as
   the cause.
2. **The stream now stretches in time.** The receiver saw `17541` CIS callback
   events (`rx_valid + rx_lost`; and `decoded=35082` is exactly
   `2 x 17541`, two Mode B frames per event) for `12644` submitted SDUs. The
   `4897`-event excess matches the arithmetic identity
   `rx_lost (7400) = undelivered SDUs (12644 - 10141 = 2503) + stretch events
   (17541 - 12644 = 4897)` exactly. The source timeline confirms it:
   `streaming` to `scored_complete` took `169.4 s` for a nominal
   `126.4 s` of SDU content, about 34% longer. Under non-strict sequencing
   late SDUs are transmitted shifted rather than dropped, so the CIS stays
   alive while the completion-paced host lags, and events fire with nothing
   on time.
3. **A residual ~20% SDU loss remains, and this run does not establish its
   exact mechanism.** `2503` of `12644` submitted SDUs never delivered, and
   the controller still counted `7231` events with nothing received
   (near-half of all events). Candidate mechanisms consistent with the
   session-state diagnosis are: late-shifted payloads that exceed the
   receiver's flush window, and controller ISO TX queue (6-buffer) saturation
   once the completion-paced host falls far enough behind. The Kconfig help
   text itself only promises payloads are "less likely to be dropped", not
   never dropped. No claim here identifies which candidate holds.

## Classification

Fixture defect, partially remediated. The failing component is the HIL source
fixture's SW-split central controller ISO-AL behavior under a completion-paced
host at ~82% CIG event duty; the staged overlay change recovered most of the
loss. The receiver is untouched by this run and its prior exoneration history
stands (SDC dongle passed Mode B 240 B with `SDUs=11659, plc=2`; Intel AX210
passed 7.5 ms Mode B). Not a product defect; not an environment defect; not
acceptance evidence.

## Consequences and stop point

Per the approved handoff's "other boundary" arm, this run records, classifies,
and stops. The staged overlay line stays **uncommitted**: it did not produce a
passing fixture, so it is not yet a fixture default. The pre-flagged fallback
lever `CONFIG_BT_CTLR_ISOAL_PSN_IGNORE=y` remains a **user decision** (standing
session-state rule: it trades stream alignment for delivery), and the
previously discussed larger alternative, switching the fixture net core from
SW-split to SDC (a production-grade controller that already passes this exact
Mode B shape via the SDC dongle history), remains a plan-revision option. No
further physical run is authorized before that decision. The full RH3 matrix
remains blocked behind a passing Mode B row.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
d757586411edb7b5ddb21527b7c4f8238e477fe07aae01e69acb6c228982818e
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 (J-Link per binding; raw scan in source-jlink.txt)
```

`nrf-probes.txt` separately retained the attached `E6635C08CB1F502B`
CMSIS-DAP nRF5340 identity; the runner used source J-Link `001050023938`.
Receiver flash logs retained verified CPUAPP and FLPR writes; source flash
logs retained the two `** Verified OK **` records, one per core.

## Restoration

No receiver change to restore; the receiver image is the normal current-HEAD
build. The source fixture is currently flashed with the diagnostic
SN_STRICT=n CPUNET image (`696d4c4f...`) from this run; the last local normal
source build (SN_STRICT=y, CPUNET `4e4b82f5...`) did not flash either target.
`images.json` in this run directory is authoritative for the currently flashed
tuple. Do not rerun or reuse this ID; any next physical diagnostic needs a new
reviewed handoff and must preserve this evidence unchanged.