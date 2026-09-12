# RH3 ModeA7 PHY isolation result

Status: completed one-run diagnostic. Changing the source Mode B PHY from 2M
to 1M changed the receiver-selected CIS layout, but the fresh Mode B 10 ms row
retained the same runner-validated delivery-collapse signature. Modulation rate
is exonerated at this 240-byte shape. This is not a root-cause, repair, or
acceptance result.

## Scope and immutable evidence

One runner-owned execution used the diagnostic source image and normal receiver
image:

```text
run ID: rh3-modeb-phy1m-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-phy1m-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-phy1m-20260904.junit.xml
```

`environment.json` records the sole authorized normal `run` invocation with
command `status=0`. Its arguments contain the fixed row and no diagnostic
runner flag. `result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`. The runner
alone resolved identities, flashed targets, captured consoles, cleaned up, and
finalized evidence. No retry, manual target operation, runner change, or
evidence mutation occurred.

## Source change and preflight

`HIL_SOURCE_QOS_PHY` is an `int` Kconfig option with default `2` and range
`1..2`. Its fragment contains exactly:

```text
CONFIG_HIL_SOURCE_QOS_PHY=1
```

`HIL_SOURCE_QOS_PHY_SELECTOR` maps Kconfig value `1` to
`BT_BAP_QOS_CFG_1M`, otherwise to `BT_BAP_QOS_CFG_2M`. The installed NCS
v3.3.0 `BT_BAP_QOS_CFG` accepts interval, framing, PHY, SDU, RTN, latency,
and presentation delay. Only the two distinct Mode B presets changed: both use
`BT_BAP_QOS_CFG_FRAMING_UNFRAMED` and the selector while retaining their
existing SDU, RTN, latency, and presentation-delay values. The mono presets are
distinct `BT_BAP_LC3_UNICAST_PRESET_*` definitions and remain unchanged.

The native source suites contain no hardcoded PHY assertion or source QoS
preset assertion to adjust. Default native suites passed:

| Suite | Passed cases |
| --- | ---: |
| `hil_source_app` | 68/68 |
| `hil_source_control` | 45/45 |
| `hil_source_signal` | 22/22 |

Preflight used HEAD:

```text
b71230f957426afe27aca4f1839b1036fbe77baa
```

Initial free space was `174290964480` bytes, above the 80 GiB gate.
`git diff --check` passed. The dirty tree contained only the two scoped source
edits, the PHY fragment, the requested PHY handoff, and the pre-existing
untracked ModeA3 handoff. Run-ID validation passed with length `24`; the run
directory and external JUnit were absent and non-symlinks, and `.locks` was
empty. Fixture validation returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## Build proof and flashed images

Both pristine diagnostic source builds used:

```text
nix develop --command fw-build-hil-source -DEXTRA_CONF_FILE="$PWD/tests/hil/source-phy1m.conf"
```

The resolved source application configuration proved:

```text
CONFIG_HIL_SOURCE_QOS_PHY=1
CONFIG_HIL_SOURCE_QOS_RTN=5
```

The source CPUNET resolved configuration has no `HIL_SOURCE_QOS_PHY` entry.
The two source macro calls contain the same PHY selector. Diagnostic build
determinism was byte-identical:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `f484d97fc89d826ccf9e71955e84532a80d28d8d44a0b3d2b0710780ff67bdb0` | `f484d97fc89d826ccf9e71955e84532a80d28d8d44a0b3d2b0710780ff67bdb0` | Byte-identical |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | Unchanged |
| Merged CPUAPP | `eafc2a2ca3f2554cb966f8a4251ac0b0e45537fbb855d137c37fa1726ee3a008` | `eafc2a2ca3f2554cb966f8a4251ac0b0e45537fbb855d137c37fa1726ee3a008` | Byte-identical |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | Unchanged |

One pristine normal receiver build at the same HEAD used no fragment. Its
resolved configuration included `CONFIG_AUDIO_OFFLOAD_ASRC=y` and
`CONFIG_BT_ISO_RX_BUF_COUNT=3`. Its hashes were:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `5aa54b14166e4e685319e0e77b3cf0175d8e86715df3251baec088c54e831706` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f484d97fc89d826ccf9e71955e84532a80d28d8d44a0b3d2b0710780ff67bdb0` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `5aa54b14166e4e685319e0e77b3cf0175d8e86715df3251baec088c54e831706` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Build output contained no actionable compiler or Kconfig diagnostic. It
retained the documented dirty-tree notice, global `__ASSERT()` notice, and
required SW Split controller informational notices; the receiver build also
retained its documented empty `drivers__watchdog` library warning.

## Runner outcome and frozen limits

The source reached `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and `idle`. The final idle status retained `seq=12644`.
The sole required receiver stream summary retained:

```text
Stream[0] summary: SDUs=144 decoded=27508 plc=27220 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=144 rx_error=0 rx_lost=13610 rx_unknown=0 rx_no_ts=8
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 144 | 27508 | 27220 | 0 | 0 | 0 | 0 | 144 | 0 | 13610 | 0 | 8 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=144 below floor: need >= 11379 (90% of 12644 submitted); plc=27220 above ceiling: need <= 1375 (5% of decoded=27508)
```

`rx_error=0`, `rx_unknown=0`, and `empty_sdu=0` met their zero requirements.
`rx_lost` and `rx_no_ts` remain record-only fields under the frozen limits.

## FLPR, QoS, and ISO tail

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=1826870089 gen=2
Counters    : submit=75 success=75 fallback=0 busy=0
```

The active ASRC-offload snapshot retained `submit=79 success=79 fallback=0`.
All listed top-level fault and recovery counters were zero. Limits validation
failed first, so this run has no post-stop FLPR snapshot and none is inferred.

The receiver QoS log directly recorded the on-air selected setting:

```text
QoS: interval 10000 framing 0x00 phy 0x01 sdu 240 rtn 5 latency 20 pd 40000
```

The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=13626 retransmitted=0 crc_error=0 rx_unreceived=13609 duplicate=0 iso_interval_1250us=8 nse=3 cig_sync_us=7236 cis_sync_us=7236 c_max_pdu=240 c_phy=1 c_bn=1 c_flush_1250us=16
```

**Mandatory sanity check: PASS.** The receiver QoS line has `phy 0x01` and
the tail has `c_phy=1`, the installed receiver ISO-info encoding for 1M. The
source PHY change therefore took effect on air and permits classification.

## Classification and stop point

This is the handoff's `FAIL with same signature` branch. The deciding evidence
is this runner-owned `result.json`: the fresh Mode B row reached `session end`
and failed frozen delivery limits with `rx_valid=144` versus the `11379` floor
and `plc=27220` versus the `1375` ceiling. The 1M tail simultaneously retained
near-total `rx_unreceived=13609` with `crc_error=0`, at `c_max_pdu=240` and
verified `c_phy=1`.

The current runner-validated failure remains the prior Mode B collapse shape,
not another boundary. The RTN=5, 2M Mode B control retained `rx_valid=113`,
`plc=27196`, and `rx_lost=13598`; this 1M run retained `rx_valid=144`,
`plc=27220`, and `rx_lost=13610`. Changing modulation rate did not recover
delivery under frozen limits.

**Verdict: modulation rate is exonerated at the 240-byte fresh Mode B shape.**
The evidence narrows the locus to 240-byte PDU handling independent of 1M or
2M modulation. A new reviewed PDU-size ladder or GATT/unicast-variant handoff
is required before further physical work; do not design that phase here. This
run does not identify an air, controller, source, receiver, RF, or FLPR root
cause, and it does not authorize a production QoS change. Do not rerun or
reuse this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
e8d25d63e7c3583b093b8e408a34cb04830a30fd31bfcdca04da849d7fe8ee0d
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

`nrf-probes.txt` separately retained the attached `E6635C08CB1F502B`
CMSIS-DAP nRF5340 identity. The runner used source J-Link `001050023938`; its
raw OpenOCD scan is retained in `source-jlink.txt`. Receiver flash logs retained
verified CPUAPP and FLPR writes. Source flash logs retained two `** Verified
OK **` records, one for each core, plus only the documented page-tail erase
extensions.

## Normal-source restoration

One local, non-flashing `nix develop --command fw-build-hil-source` completed
after read-only evidence review. Its resolved source app configuration proved:

```text
CONFIG_HIL_SOURCE_QOS_PHY=2
CONFIG_HIL_SOURCE_QOS_RTN=5
```

| Image | Local normal-build SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Merged CPUAPP | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` |

The normal source CPUAPP and merged hashes match pre-existing normal identities.
Restoration did not flash either target; `images.json` remains the authoritative
last-flashed diagnostic tuple. Post-restoration free space was `174263447552`
bytes, above the 80 GiB gate.
