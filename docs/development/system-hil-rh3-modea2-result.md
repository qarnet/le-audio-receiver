# RH3 Mode A offload-disabled isolation result

Status: completed one-run diagnostic. Binary classification reached: FAIL on
frozen transport limits.

## Scope and immutable evidence

One runner-owned execution used the offload-disabled receiver image for
`rh3.fresh_mode_a_48_4_1`:

```text
run ID: rh3-modea2-20260904-offload-disabled
row:    rh3.fresh_mode_a_48_4_1
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modea2-20260904-offload-disabled/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modea2-20260904-offload-disabled.junit.xml
```

The one authorized direct invocation returned `status=1`. `result.json`
records `outcome="failed"`, `first_failed_boundary="session end"`, and
`cleanup_failures=[]`. No retry, manual target operation, production-source
change, or evidence mutation occurred. Runner owned target identity resolution,
flashing, console capture, cleanup, and evidence finalization.

`images.json` is authoritative for flashed identity. The later normal build was
local only and did not alter either target.

## Preflight and diagnostic identity

Diagnostic builds and runner used this HEAD:

```text
d8a7ed28d8172852306b6c7d173467ccecfe11e1
```

`git diff --check` passed. Before either diagnostic build, `git status
--porcelain` contained exactly the requested untracked handoff:

```text
?? docs/development/system-hil-rh3-modea2-execution-handoff.md
```

This was the documented documentation-only dirty state. No production or
unrelated tracked change was present.

- Initial free disk space was `177240670208` bytes (`165.1 GiB`), above the
  required `80 GiB`.
- Output root `/tmp/opencode/hil-runs` passed lifecycle validation. The run
  root and external JUnit path were absent and not symlinks, and `.locks` was
  empty immediately before the one runner invocation.
- Run-ID validation passed with length `36`.
- Fixture validation returned
  `{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.
- Source image and diagnostic-fragment hashes matched the frozen inputs:

| Artifact | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| `tests/hil/receiver-offload-disabled.conf` | `9d803fa50488b2058480d5888b2d09b2c87c1a2bef570b768f2b689984605b81` |

The diagnostic builds used `tests/hil/receiver-offload-disabled.conf`. Resolved
configuration proved:

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

Both diagnostic builds produced only the documented nRF54L15 empty watchdog
library and global `__ASSERT()` CMake notices. No actionable build warning
occurred.

### Required double-build proof

`fw-build-54l15` ran twice, each time pristine, with the same diagnostic
fragment and no repository change between builds.

| Artifact | Build 1 SHA-256 | Build 2 SHA-256 | Result |
| --- | --- | --- | --- |
| Receiver CPUAPP | `544df700815a4c9fa9ae931a8b1ccd201c7995021e534aad99f99a0d5c55ca0f` | `544df700815a4c9fa9ae931a8b1ccd201c7995021e534aad99f99a0d5c55ca0f` | Byte-identical |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | Stable |

The runner retained the same four flashed images in `images.json`:

| Logical image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |
| Receiver CPUAPP | `544df700815a4c9fa9ae931a8b1ccd201c7995021e534aad99f99a0d5c55ca0f` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

CPUAPP hashes are HEAD-dependent because `cmake/version.cmake` embeds
`APP_COMMIT`. This record derives its CPUAPP identities at its own execution
HEAD and must not be used as an absolute pin for a later execution.

## Outcome, per-slot data, and frozen limits

Authoritative runner result:

```text
outcome="failed"
first_failed_boundary="session end"
cleanup_failures=[]
failure_detail="stream summary slot 0 transport limits: rx_valid=130 below floor: need >= 11379 (90% of 12644 submitted); plc=28594 above ceiling: need <= 1436 (5% of decoded=28724)"
```

The source completed `streaming`, `scored_complete`, `teardown`, terminal
`verdict="pass"`, and final `idle`; the final idle record retained
`seq=12644`. The receiver summaries retained in `receiver-status.txt` were:

| Field | Slot 0 | Slot 1 |
| --- | ---: | ---: |
| `SDUs` | 130 | 0 |
| `decoded` | 28724 | 0 |
| `plc` | 28594 | 0 |
| `decode_err` | 0 | 0 |
| `i2s_underrun` | 0 | 0 |
| `stream_reset` | 0 | 0 |
| `empty_sdu` | 0 | 0 |
| `rx_valid` | 130 | 0 |
| `rx_error` | 0 | 0 |
| `rx_lost` | 14275 | 14376 |
| `rx_unknown` | 0 | 0 |
| `rx_no_ts` | 50 | 14376 |

Each Mode A stream required `12644` submitted SDUs. The frozen delivery floor
is `11379` valid SDUs per slot. Slot 0 delivered `130 / 12644` (`1.03%`) and
had PLC `28594 / 28724` (`99.55%`), causing the runner-validated delivery and
PLC violations quoted above. Slot 1 retained `rx_valid=0`; its raw value is
also below the same floor, but the runner stops after recording the first slot
violation, so it is not a second serialized `failure_detail`.

`result.json` has `summary={}` because session-end validation raised on that
first limits violation before a successful summary payload could be written.
The decisive verdict is the runner's `session end` transport-limits failure,
not a replacement verdict calculated from raw telemetry.

The live ISO tail retained both CIS records:

```text
Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14298 retransmitted=0 crc_error=1 rx_unreceived=14277 duplicate=0 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=5304 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
Stream[1] handle=0x0002 tx_unacked=0 tx_flushed=0 tx_last_subevent=14365 retransmitted=0 crc_error=38 rx_unreceived=14371 duplicate=9 iso_interval_1250us=8 nse=3 cig_sync_us=5304 cis_sync_us=2652 c_max_pdu=120 c_phy=2 c_bn=1 c_flush_1250us=16
```

## FLPR observations

The active receiver snapshot proves the intended offload-disabled data plane:

```text
State       : ACTIVE / epoch=1244230088 gen=2
Counters    : submit=0 success=0 fallback=0 busy=0
Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0
Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0
Probation   : active=0 success=0 cleared=0
RTT         : (none)
```

The limits failure occurred in session-end collection before the runner's
post-stop collector. No `receiver-post-stop-status.txt` or post-stop
`flpr status` snapshot exists in this immutable evidence root. Post-stop
`submit=0 success=0` is therefore unproven for this run; no manual query was
made to fill that gap.

## Classification and stop point

**Classification: transport/controller path implicated; FLPR offload cleared
as primary suspect for this Mode A starvation.**

Deciding evidence is the runner-owned `result.json` failure at `session end`:
the offload-disabled image reached the expected active zero data plane
(`submit=success=0`) and the repaired runner reached frozen transport-limit
validation, which failed on slot 0 delivery and PLC. This is the required
FAIL-on-limits binary arm.

This result does not identify a specific controller, buffer, RF, source, or
receiver mechanism. It is not RH3 acceptance, audio acceptance, or a claim
that every FLPR feature is irrelevant outside this isolated data plane. The
missing post-stop FLPR snapshot is an evidence limit only; it does not replace
or weaken the runner's limits verdict. Next work is controller/buffer
diagnostics. Do not rerun or reuse this ID.

## Integrity and raw identity

Read-only `sha256sum --check SHA256SUMS` passed all `24/24` retained entries.
The root JUnit and external JUnit were byte-identical and both hash to:

```text
b4121b4f81292c189be040e0350dc4da088fa401b6817f2259e6d56e07546f01
```

Runner-retained raw identity evidence:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
```

The source J-Link fingerprint retained:

```text
ap0=0x84770001 ap1=0x84770001 ap2=0x12880000 ap3=0x12880000
```

No receiver or source runtime `<wrn>`/assertion line was retained. Source flash
retained only the documented page-tail erase extensions
`0x01023afc .. 0x01023fff` and `0x0005741c .. 0x00057fff`.

## Normal-build restoration

One local, non-flashing normal `fw-build-54l15` restoration build passed under
the same HEAD. Its only diagnostics were the same documented nRF54L15 watchdog
and global-`__ASSERT()` CMake notices. Resolved normal configuration proved:

```text
CONFIG_AUDIO_OFFLOAD_ASRC=y
```

| Artifact | Local restoration SHA-256 |
| --- | --- |
| Normal receiver CPUAPP | `c11336dae509bf9458c16518a2c59bbbf7bdceb357e9ebb47b8dc118633e951b` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

Final `git diff --check` passed. Final free space was `177226158080` bytes
(`165.1 GiB`). The local restoration build did not flash either board. Hardware
must therefore still be treated as running the runner-flashed diagnostic
identity from `images.json`.
