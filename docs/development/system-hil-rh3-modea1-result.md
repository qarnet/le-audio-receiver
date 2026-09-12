# RH3 Mode A offload-isolation diagnostic result

## Scope and immutable evidence

One runner-owned diagnostic execution used the offload-disabled receiver image
for `rh3.fresh_mode_a_48_4_1`. Run ID:

```text
rh3-modea1-20260903-offload-disabled
```

Immutable evidence root:

```text
/tmp/opencode/hil-runs/rh3-modea1-20260903-offload-disabled/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modea1-20260903-offload-disabled.junit.xml
```

The outer invocation printed `status=1`; that status is immutable. No retry,
manual target operation, production source change, or production configuration
change occurred. The runner owned target identity, flashing, consoles, and
cleanup.

## Preflight and diagnostic build

Preflight passed before the diagnostic build and again confirmed output-path
ownership immediately before the one runner invocation:

- Disk gate: `free_gib=115.3`, above the required 80 GiB.
- `git status --short` contained only this untracked handoff and the diagnostic
  fragment. `git diff --check` passed.
- Run-ID validation passed: `length=36`, `valid=yes`.
- The run root and external JUnit path were absent and not symlinks.
- Fixture validation returned
  `{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}`.
- Source image hashes matched:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` |

The one diagnostic receiver build used:

```text
CONFIG_AUDIO_OFFLOAD_ASRC=n
CONFIG_WARN_EXPERIMENTAL=y
```

Resolved diagnostic configuration proved:

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

Diagnostic image and fragment hashes:

| Artifact | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `91223b359b49f92eb1e5b7b2a03b025939d74ec40d8dda4598053f6d45347a84` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| `tests/hil/receiver-offload-disabled.conf` | `9d803fa50488b2058480d5888b2d09b2c87c1a2bef570b768f2b689984605b81` |

The diagnostic build completed. Its only CMake warnings were the documented
nRF54L15 watchdog empty library and globally enabled `__ASSERT()` statements.

## Outcome and transport evidence

`result.json` records `outcome="failed"`,
`first_failed_boundary="receiver active"`, `cleanup_failures=[]`, and
`summary={}`. Exact failure detail:

```text
FLPR active offload deadline expired after 149 attempt(s): last={'state': 'ACTIVE', 'epoch': 697441563, 'gen': 2, 'submit': 0, 'success': 0, 'fallback': 0, 'busy': 0, 'recovery_attempts': 0, 'recovery_fail': 0, 'relapses': 0, 'exhaustion': 0, 'probation_active': 0, 'probation_success': 0, 'probation_cleared': 0, 'fault_timeout': 0, 'fault_full': 0, 'fault_stale': 0, 'fault_seq': 0, 'fault_frame': 0, 'fault_crc': 0, 'fault_payload': 0, 'runtime_restarts': -1, 'runtime_fails': -1, 'runtime_last_ms': -1, 'remote_epoch': -1, 'hb_dedup': -1}
```

The source reached `streaming` with two connected sink ASEs. The runner then
stopped it after the active-offload deadline; its stop status retained both
streams at `seq=4020 sub=4020 sc=3876 sf=0 cb=4020 out=0`, followed by terminal
`verdict="fail"`. This is an early controlled stop, not completion of the
12,644-submitted-SDU row.

No receiver stream-summary record was captured. Per-slot values for
`rx_valid`, `plc`, `rx_lost`, `rx_error`, `rx_unknown`, `empty_sdu`, and
`decoded` are therefore unavailable for slots 0 and 1. Frozen transport limits
were not evaluated and have no PASS or FAIL verdict for this run.

## FLPR observations

The boot transcript proves handshake progress:

```text
flpr_hs: FLPR handshake init OK (waiting for FLPR boot)
flpr_hs: FLPR READY (epoch=2834948532, count=1, new)
flpr_hs: FLPR READY_ACK sent
```

Both Mode A CIS streams started and opened the audio gate. The active offload
transcript then recorded `State : ACTIVE / epoch=697441563 gen=2` with
`submit=0 success=0 fallback=0 busy=0` and zero listed fault, recovery, and
probation counters. This is the intended zero data-plane observation for an
offload-disabled image, despite the control state remaining `ACTIVE`.

The receiver-active failure stopped collection before the post-stop commands.
No `flpr status` Ready/ACKed/Healthy snapshot, no post-stop `STOPPED` state, and
no post-stop zero-plane snapshot were captured. The retained active transcript
also contains this receiver warning, which is recorded without causal claim:

```text
[00:47:24.616,509] <wrn> bt_conn: conn 0x20004e98 failed to establish. RF noise?
```

## Classification and interpretation limits

**Classification: fixture defect. Binary FLPR-versus-transport classification
not reached.**

The deciding evidence line is the exact `receiver active` failure above. The
offload-disabled image correctly retained `submit=0`, but the runner's active
readiness predicate in `scripts/hil/runner.py` requires `submit >= 1` for the
`48_4_1` profile before it can continue. That predicate is incompatible with
this diagnostic's intended CPUAPP-ASRC zero plane. It prevented session-end
summary collection and frozen-limit evaluation.

This run does not establish either binary arm:

- It does not implicate the FLPR offload pipeline because no offload-disabled
  transport-limits PASS was reached.
- It does not implicate the transport/controller path because no
  offload-disabled transport-limits FAIL was reached.

The receiver warning and the partial source progress do not establish an
environment or product classification. This run is not RH3 acceptance, audio
acceptance, a transport verdict, or a root-cause diagnosis.

## Integrity and raw identity

Read-only `SHA256SUMS` verification passed all 23 retained entries. The root
JUnit and external JUnit both hash to:

```text
399307251eb585b75d83f3d5314c62f7f243125b0a6315a8495f63358578d7a1
```

Runner-retained raw identities:

```text
receiver: serial=8EE9B3FF target=nRF54L15 DPIDR=0x6ba02477 PART=0x00054b15 variant=AAC0
source:   serial=001050023938 target=nRF5340 DPIDR=0x6ba02477 PART=0x00005340 variant=0x514b4141
```

The raw source J-Link scan also retained `ap0=0x84770001`,
`ap1=0x84770001`, `ap2=0x12880000`, and `ap3=0x12880000`.

## Normal restoration and stop point

One local, non-flashing `fw-build-54l15` restoration build completed with only
the same documented watchdog and `__ASSERT()` diagnostics. Its resolved config
proves `CONFIG_AUDIO_OFFLOAD_ASRC=y`. FLPR and both source images returned to
their expected identities, but CPUAPP did not:

| Artifact | Expected SHA-256 | Observed SHA-256 | Result |
| --- | --- | --- | --- |
| Receiver CPUAPP | `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f` | `7dabfdb2571e1d6f1d9e1fe417d096d12485e32ec9dbe154b9d0f7384e2f20a9` | Mismatch |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` | Match |
| Source CPUAPP | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333` | Match |
| Source CPUNET | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48` | Match |

`git diff --check` passed and final disk free space was `115.3 GiB`. No second
build, no diagnostic rerun, and no reflash occurred after the byte-identity
mismatch. Hardware remains at the runner-flashed diagnostic image; local normal
configuration does not prove normal hardware state.

Stop for review. A follow-up must first classify and repair the runner's
offload-disabled active-readiness expectation, then establish the unexpected
normal CPUAPP hash divergence before authorizing another physical row.
