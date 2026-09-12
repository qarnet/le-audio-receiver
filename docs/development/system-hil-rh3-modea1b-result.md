# RH3 Mode A offload-disabled rerun result

Status: failed one-run diagnostic. Binary FLPR-versus-transport classification
was not reached.

## Scope and immutable evidence

One runner-owned execution used the offload-disabled receiver image for
`rh3.fresh_mode_a_48_4_1`:

```text
run ID: rh3-modea1b-20260903-offload-disabled
row:    rh3.fresh_mode_a_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modea1b-20260903-offload-disabled/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modea1b-20260903-offload-disabled.junit.xml
```

The one authorized invocation printed `status=1`. `result.json` records
`outcome="failed"`, `first_failed_boundary="session end"`, and
`cleanup_failures=[]`. No retry, manual target operation, production-source
change, or evidence mutation occurred. The runner owned identity resolution,
flashing, serial capture, cleanup, and evidence finalization.

`images.json` is authoritative for flashed identity. Later local restoration
did not flash either target and does not change that record.

## Preflight and diagnostic identity

Diagnostic-build and runner HEAD:

```text
eac880d516d4828ac2169f9589c3808dc1810d4c
```

The tracked tree had no diff and `git diff --check` passed. `git status
--porcelain` contained exactly the two requested, initially untracked handoffs:

```text
?? docs/development/system-hil-rh3-modea1b-execution-handoff-amended.md
?? docs/development/system-hil-rh3-modea1b-execution-handoff.md
```

This was the documented documentation-only dirty state to be included in this
result commit; no unrelated or production changes were present.

- Initial disk free space was `176178753536` bytes (`164.1 GiB`), above the
  required `80 GiB`.
- Output root `/tmp/opencode/hil-runs` passed runner validation. The run root
  and external JUnit path were absent, not symlinks, and the fixture lock
  directory was empty before execution.
- Run-ID validation passed with length `37`.
- Fixture validation returned
  `{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.
- Source image hashes matched the frozen inputs:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Diagnostic fragment | `9d803fa50488b2058480d5888b2d09b2c87c1a2bef570b768f2b689984605b81` |

The diagnostic build used
`tests/hil/receiver-offload-disabled.conf`. Resolved configuration proved:

```text
# CONFIG_AUDIO_OFFLOAD_ASRC is not set
CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=y
CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y
# CONFIG_TRACING is not set
# CONFIG_HIL_BAP_ENABLE_TRACE is not set
# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set
CONFIG_BT_ISO_RX_BUF_COUNT=3
CONFIG_WARN_EXPERIMENTAL=y
```

The only build diagnostics were the documented nRF54L15 empty watchdog library
and globally enabled `__ASSERT()` notice. No actionable build warning occurred.

### Required double-build proof

`fw-build-54l15` ran twice with the same diagnostic fragment, each time
pristine, with no repository change between builds.

| Artifact | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Receiver CPUAPP | `4f2c5e5a37f59ed1faa562ec89e0a77bef6ced10ad6d661f2b93160e45900204` | `4f2c5e5a37f59ed1faa562ec89e0a77bef6ced10ad6d661f2b93160e45900204` | Byte-identical |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | Stable |

The runner then recorded the same four flashed images in `images.json`:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `4f2c5e5a37f59ed1faa562ec89e0a77bef6ced10ad6d661f2b93160e45900204` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

CPUAPP hashes are HEAD-dependent because `cmake/version.cmake` embeds
`APP_COMMIT`. Future handoffs must derive CPUAPP identities under their own
execution HEAD and, for a flashed diagnostic image, repeat this two-build
proof. They must not reuse an absolute CPUAPP hash from this record.

## Outcome, raw per-slot data, and limits

Exact failed boundary and detail:

```text
session end
missing receiver stream summary slot(s): [0, 1]; raw evidence scan also missing them after 333 bytes
```

The source completed `streaming`, `scored_complete`, `teardown`, and terminal
`verdict="pass"`. Its later idle record retained `seq=12644`. The runner's
authoritative `summary` is `{}`, so no receiver summary was accepted into the
result and frozen limits have **no PASS or FAIL verdict** for this execution.

Raw receiver-console evidence does contain following unaccepted teardown
telemetry. It is recorded for diagnosis only, not substituted for runner-owned
limit evaluation.

| Field | Slot 0 | Slot 1 |
| --- | ---: | ---: |
| `SDUs` | 130 | 0 |
| `decoded` | 28750 | 0 |
| `plc` | 28620 | 0 |
| `decode_err` | 0 | 0 |
| `i2s_underrun` | 0 | 0 |
| `stream_reset` | 0 | 0 |
| `empty_sdu` | 0 | 0 |
| `rx_valid` | 130 | 0 |
| `rx_error` | 0 | 0 |
| `rx_lost` | 14289 | 14389 |
| `rx_unknown` | 0 | 0 |
| `rx_no_ts` | 50 | 14389 |

Each stream required `12644` submitted SDUs. Raw slot 0 delivery is
`130 / 12644` (`1.03%`) and raw slot 1 delivery is `0 / 12644`; raw slot 0 PLC
is `28620 / 28750` (`99.55%`). Those observations would be outside frozen
limits, but the runner did not reach `validate_stream_transport`; they do not
turn this failure into the required FAIL-on-limits binary arm.

The live ISO tail retained both CIS records:

```text
Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14301 retransmitted=0 crc_error=0 rx_unreceived=14292 duplicate=0 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=5304 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
Stream[1] handle=0x0002 tx_unacked=0 tx_flushed=0 tx_last_subevent=14380 retransmitted=0 crc_error=40 rx_unreceived=14386 duplicate=8 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=2652 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
```

## FLPR observations

The active receiver snapshot proves intended offload-disabled data plane:

```text
State       : ACTIVE / epoch=1017727062 gen=2
Counters    : submit=0 success=0 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
Probation   : active=0 success=0 cleared=0
```

No post-stop FLPR offload snapshot, `STOPPED` state, or `flpr status`
Ready/ACKed/Healthy snapshot was captured. The session-end failure occurred
before runner post-stop collection, so `receiver-post-stop-status.txt` was not
created. No manual query was made to fill that evidence gap. Post-stop
`submit=0 success=0` is therefore unproven for this run.

## Classification and stop point

**Classification: fixture/runner evidence-order failure. Binary
FLPR-versus-transport classification is inconclusive.**

Deciding evidence is the authoritative `result.json` boundary above: required
receiver summaries were not accepted, and the runner stopped before frozen
transport-limit validation and terminal FLPR collection. Raw console lines
showing summaries and severe loss cannot replace that fail-closed result.

This execution does not establish either intended binary arm:

- It does not implicate FLPR offload because the offload-disabled image did not
  receive a runner-validated PASS under frozen limits.
- It does not clear FLPR as a primary suspect or implicate transport/controller
  because the run did not receive a runner-validated FAIL on transport limits.

Stop here. Do not rerun this ID or use raw telemetry as acceptance evidence.
Next work needs a reviewed runner fix for receiver-summary collection ordering
and host proof that the summaries are captured before a later authorized row.
Root cause subsequently identified and fixed in RH3c; see `docs/development/system-hil-rh3c-full-raw-scan-handoff.md`.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
Root and external JUnit files were byte-identical and both hash to:

```text
a83bfce992f4c2388ee243daaab22ebd78861b4f4c10121553969d4e5dca6f5f
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
```

The source J-Link fingerprint additionally retained:

```text
ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

## Normal-build restoration

One local, non-flashing normal `fw-build-54l15` restoration build passed under
the same run HEAD. Its only diagnostics were the same documented watchdog and
global-`__ASSERT()` CMake notices. Resolved normal configuration proved:

```text
CONFIG_AUDIO_OFFLOAD_ASRC=y
```

| Artifact | Local restoration SHA-256 |
| --- | --- |
| Normal receiver CPUAPP | `dc983843332d5438a015a716a802fc6b7e7f37edd2634ce430bce2488f03337e` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

Final `git diff --check` passed. Final free space was `177610645504` bytes
(`165.4 GiB`). The local normal build did not flash either board, so hardware
must still be treated as running the diagnostic identity from `images.json`.
