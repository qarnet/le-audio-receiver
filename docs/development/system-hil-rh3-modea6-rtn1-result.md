# RH3 ModeA6 RTN/duty isolation result

Status: completed one-run diagnostic. Reducing the source Mode B RTN from 5
to 1 changed the receiver-selected CIS subevent count from 6 to 2, but the
fresh Mode B 10 ms row retained the same runner-validated delivery-collapse
signature. RTN/duty is exonerated at this shape. This is not a root-cause,
repair, or acceptance result.

## Scope and immutable evidence

One runner-owned execution used the diagnostic source image and normal
receiver image:

```text
run ID: rh3-modeb-rtn1-20260904
row:    rh3.fresh_mode_b_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-rtn1-20260904/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modeb-rtn1-20260904.junit.xml
```

The sole authorized runner invocation returned outer `status=1`.
`result.json` records `outcome="failed"`,
`first_failed_boundary="session end"`, and `cleanup_failures=[]`.
`environment.json` retains only normal `run` arguments, including the fixed
row, with no diagnostic runner flag. The runner alone resolved identities,
flashed targets, captured consoles, cleaned up, and finalized evidence. No
retry, manual target operation, runner change, or evidence mutation occurred.

## Source change and preflight

`HIL_SOURCE_QOS_RTN` is an `int` Kconfig option with default `5` and range
`0..5`. Its fragment contains exactly:

```text
CONFIG_HIL_SOURCE_QOS_RTN=1
```

`BT_BAP_QOS_CFG_UNFRAMED` takes RTN as its third argument and forwards it to
the `uint8_t` `.rtn` field in the installed NCS v3.3.0
`zephyr/include/zephyr/bluetooth/audio/bap.h`. The two Mode B presets now use
the lossless `(uint8_t)CONFIG_HIL_SOURCE_QOS_RTN` expression. No other QoS
field changed. `fw-build-hil-source` already passes `"$@"` after its CMake
`--` separator, so the source-only fragment passed through as
`-DEXTRA_CONF_FILE=...`; the helper was unchanged.

The native source suites have no QoS preset or `.qos.rtn == 5` assertion, so
no test assertion changed. Default native suites passed:

| Suite | Passed cases |
| --- | ---: |
| `hil_source_app` | 68/68 |
| `hil_source_control` | 45/45 |
| `hil_source_signal` | 22/22 |

Preflight used HEAD:

```text
c02f74efcac61bd6ed5d40e6db4cb49c9df5d0bf
```

Initial free space was `163767087104` bytes, above the 80 GiB gate.
`git diff --check` passed. The dirty tree contained only the two scoped source
edits, the RTN fragment, the requested RTN handoff, and the pre-existing
untracked ModeA3 handoff. Run-ID validation passed with length `23`; the run
directory and external JUnit were absent and non-symlinks, and `.locks` was
empty. Fixture validation returned:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

## Build proof and flashed images

Both pristine diagnostic source builds used:

```text
nix develop --command fw-build-hil-source -DEXTRA_CONF_FILE="$PWD/tests/hil/source-rtn1.conf"
```

The resolved source application configuration proved:

```text
CONFIG_HIL_SOURCE_QOS_RTN=1
```

The source CPUNET resolved configuration has no `HIL_SOURCE_QOS_RTN` entry.
The two source macro calls contain the same Kconfig expression. Diagnostic
build determinism was byte-identical:

| Image | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Source CPUAPP | `c9eccc4e8074525d5d0d178b183c738f8563e60168a22e4fcf6fa6ab49ced72a` | `c9eccc4e8074525d5d0d178b183c738f8563e60168a22e4fcf6fa6ab49ced72a` | Byte-identical |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | Unchanged |
| Merged CPUAPP | `f04ff179abd35b5a79c1c148829032724e6bb0efa07a00c5d1bb15b9396cd32b` | `f04ff179abd35b5a79c1c148829032724e6bb0efa07a00c5d1bb15b9396cd32b` | Byte-identical |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` | Unchanged |

One pristine normal receiver build at the same HEAD used no fragment. Its
resolved configuration included `CONFIG_AUDIO_OFFLOAD_ASRC=y` and
`CONFIG_BT_ISO_RX_BUF_COUNT=3`. Its hashes were:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `46ad03840b1aa9724c3ce32fe3da9370a55a29f69f1e25ce4fc340be9b5fdd40` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

`images.json` is authoritative for the runner-flashed tuple:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `c9eccc4e8074525d5d0d178b183c738f8563e60168a22e4fcf6fa6ab49ced72a` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `46ad03840b1aa9724c3ce32fe3da9370a55a29f69f1e25ce4fc340be9b5fdd40` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

Build output contained no actionable compiler or Kconfig diagnostic. It
retained the documented dirty-tree notice, global `__ASSERT()` notice, and
required SW Split controller informational notices; the receiver build also
retained its documented empty `drivers__watchdog` library warning.

## Runner outcome and frozen limits

The source reached `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and `idle`. The final idle status retained `seq=12644`.
The runner parsed this sole required receiver stream summary:

```text
Stream[0] summary: SDUs=130 decoded=27496 plc=27236 decode_err=0 i2s_underrun=0 stream_reset=0 empty_sdu=0 rx_valid=130 rx_error=0 rx_lost=13618 rx_unknown=0 rx_no_ts=10
```

| Slot | SDUs | Decoded | PLC | Decode errors | I2S underruns | Stream resets | Empty SDUs | RX valid | RX error | RX lost | RX unknown | RX no timestamp |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 130 | 27496 | 27236 | 0 | 0 | 0 | 0 | 130 | 0 | 13618 | 0 | 10 |

The runner-validated frozen-limit failure was:

```text
stream summary slot 0 transport limits: rx_valid=130 below floor: need >= 11379 (90% of 12644 submitted); plc=27236 above ceiling: need <= 1374 (5% of decoded=27496)
```

`rx_error=0`, `rx_unknown=0`, and `empty_sdu=0` met their zero requirements.
`rx_lost` and `rx_no_ts` remain record-only fields under the frozen limits.

## FLPR, QoS, and ISO tail

The active 10 ms FLPR snapshot met the active predicate:

```text
State       : ACTIVE / epoch=737172189 gen=2
Counters    : submit=75 success=75 fallback=0 busy=0
```

The active ASRC-offload snapshot retained `submit=80 success=79 fallback=0`.
All listed top-level fault and recovery counters were zero. Limits validation
failed first, so this run has no post-stop FLPR snapshot and none is inferred.

The receiver QoS log directly recorded `sdu 240 rtn 1 latency 20 pd 40000`.
The ISO tail was:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=13625 retransmitted=0 crc_error=0 rx_unreceived=13623 duplicate=0 iso_interval_1250us=8 nse=2 cig_sync_us=2728 cis_sync_us=2728 c_max_pdu=240 c_phy=2 c_bn=1 c_flush_1250us=8
```

**Mandatory sanity check: PASS.** The tail has `nse=2`, not the baseline
`nse=6`. Together with the receiver's logged `rtn 1`, this proves the source
QoS change took effect on air and permits classification.

## Classification and stop point

This is the handoff's `FAIL with same signature` branch. The deciding evidence
is this run's runner-owned `result.json`: the fresh Mode B row reached
`session end` and failed frozen delivery limits with `rx_valid=130` versus the
`11379` floor and `plc=27236` versus the `1374` ceiling. The tail simultaneously
retained near-total `rx_unreceived=13623` with `crc_error=0`, despite the
verified `nse=2` layout.

The current runner-validated failure remains the prior Mode B collapse shape,
not another boundary: the RTN=5 Mode B control retained `rx_valid=113`,
`plc=27196`, `rx_lost=13598`, and `nse=6`; this RTN=1 run retains `rx_valid=130`,
`plc=27236`, `rx_lost=13618`, and `nse=2`. Halving subevent count did not
recover delivery.

**Verdict: RTN/duty is exonerated at the 240-byte, 2M, fresh Mode B shape.**
The next phase requires a new reviewed PDU-size or PHY ladder handoff. This
run does not identify an air, controller, source, receiver, RF, or FLPR root
cause, and it does not authorize a production QoS change. Do not rerun or
reuse this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root and external JUnit files were byte-identical, both hashing to:

```text
468ebb41a291e14458d0b57ad4b1cb62543b1097f9dd101fd4a56fdca7e18038
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
source APs: ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

`nrf-probes.txt` separately retained the attached `E6635C08CB1F502B`
CMSIS-DAP nRF5340 identity. The runner used source J-Link `001050023938`; its
raw OpenOCD scan above is retained in `source-jlink.txt`. Receiver flash logs
retained verified CPUAPP and FLPR writes. Source flash logs retained two
`** Verified OK **` records, one for each core, plus only the documented
page-tail erase extensions.

## Normal-source restoration

One local, non-flashing `nix develop --command fw-build-hil-source` completed
after read-only evidence review. Its resolved source app configuration proved:

```text
CONFIG_HIL_SOURCE_QOS_RTN=5
```

| Image | Local normal-build SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Merged CPUAPP | `97d095130a1196085d09263e10000aabe64852ec4d335e89eb1729343bcf8472` |
| Merged CPUNET | `b4ee66969efd97a150589af3d91ba7e7df2582e938687c87470e7eb6208096e0` |

The normal source CPUAPP and merged hashes match the pre-existing normal
identities. Restoration did not flash either target; `images.json` remains the
authoritative last-flashed diagnostic tuple. Post-restoration free space was
`163741626368` bytes, above the 80 GiB gate.
