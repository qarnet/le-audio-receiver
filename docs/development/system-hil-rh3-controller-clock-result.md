# RH3 controller-clock fix-validation and transport acceptance result

Status: **`TRANSPORT_RUNTIME_ACCEPTED` on 2026-09-09**. A direct post-fix Mode A
row passed, then the fixed RH3 matrix passed all 14 children. No child failed,
was cancelled, or was skipped, and no cleanup failed. This verdict covers the
required 10 ms transport and runtime boundary only. It does not cover 7.5 ms,
exact release artifacts, DAC output, analog audio, audibility, or stereo channel
mapping.

This result supersedes the SDC-defect and host-exhaustion conclusions in the
ModeA17/18 result and withdraws the proposed DevZone defect question. It does
not erase those immutable run records. It changes their interpretation because
the corrected source fixture now delivers timestamp-pinned traffic through the
same SDC central.

## Scope

The source fixture now uses one controller-clock scheduling path:

1. Send one untimestamped stream-0 bootstrap SDU.
2. Read one controller-assigned CIG timestamp after its completion.
3. Use the CPUNET MPSL RTC-start event to clear an application-core RTC0 mirror
   through IPC channel 4 and GPPI.
4. Encode each semantic frame before waiting for its event.
5. Submit timestamped SDUs at a target lead of 3000 us, with a 2000 us minimum
   and whole-interval stale-pin catch-up.
6. Give both Mode A streams the same event timestamp.
7. Read HCI LE Read ISO TX Sync once per stream after final drain as schedule
   telemetry. Its count is a successful-poll count, not an aired-SDU count.

The final throughput correction in `hil/source/app/src/main.c` changes the
nRF5340 application-core HFCLK divider to `NRF_CLOCK_HFCLK_DIV_1` before
Bluetooth initialization:

```c
nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1)
```

NCS v3.3.0 uses the same call to select 128 MHz in its Bluetooth ISO test
(`nrf/tests/bluetooth/iso/src/main.c`), nRF5340 Audio application
(`nrf/applications/nrf5340_audio/src/modules/audio_clock.c`), and Zephyr BAP
USB implementation (`zephyr/subsys/bluetooth/audio/shell/bap_usb.c`). The nrfx
implementation updates `SystemCoreClock` after changing the divider
(`modules/hal/nordic/nrfx/drivers/src/nrfx_clock_hfclk.c`).

## Software verification

Commit `8123b948fc82b33217a955c40021eda57d315f12` prevents a STATUS request from
splitting one source transmit batch. The batch mutex covers both Mode A sends,
and STATUS takes that mutex before reading application state. Two pristine
`fw-build-hil-source` builds at that clean commit were byte-identical:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `43bdef15f6cbca0ab4df9bbeda2594b0d1ccf0e5d9a534d0507998e712b4cb3d` |
| Source CPUNET | `2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b` |
| Source merged | `4132883138cb199cca1c1f74a6bfc5a511470bb6a56a9ff704f0696cc7147b2f` |
| Source merged CPUNET | `85fcb7674956e654f299e322d0417a572d27cddcf7787b8278d8ec9244bdd53b` |

Both builds completed without compiler, linker, Kconfig, or devicetree
warnings. The only matched diagnostic was Zephyr's documented global
`__ASSERT()` message.

Focused verification after the STATUS serialization fix:

- The detached pre-fix proof at `a1941bc6536d705c62096ec6cb7451e615f74bf7`
  passed 77 existing tests and failed only the new STATUS-interleave regression.
- `tests/unit/hil_source_app`: 78/78 passed, no warnings.
- `tests/unit/hil_source_control`: 45/45 passed, no warnings.
- `tests/unit/hil_source_signal`: 22/22 passed, no warnings.
- `tests/hil/rh2_test.py`, `scripts/test_hil_runner.py`,
  `tests/hil/rh2_hardware_test.py`, and Python byte-compilation passed.
- `git diff --check`: passed.
- Resolved source CPUAPP config contains `CONFIG_NRFX_CLOCK=y` and
  `CONFIG_CLOCK_CONTROL_NRF=y`.

## Physical identity and images

Retained hardware-run identity evidence:

| Role | Probe evidence | Console |
| --- | --- | --- |
| Receiver | `8EE9B3FF`, nRF54L15, DPIDR `0x6ba02477`, PART `0x00054b15`, variant `AAC0` | `/dev/ttyACM2`, USB interface 02 |
| Source | J-Link `001050023938`, nRF5340, DPIDR `0x6ba02477`, PART `0x00005340`, variant `0x514b4141` | `/dev/ttyACM1`, USB interface 02 |

The two 2026-09-08 direct rows used source CPUAPP
`466fd7284574ee531c081ad857e0b5b9c70f662cf1d9795fa8cf6d0844723563` and
the same CPUNET hash shown above. The 2026-09-09 direct row and matrix used the
post-fix source hashes in the software-verification table. Every run used these
unchanged receiver images:

| Image | SHA-256 |
| --- | --- |
| Receiver CPUAPP | `e8f1bd19b7c8821504e1e7fc88cef5293d1adaeba84e56c759f469afc552b07d` |
| Receiver FLPR | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

The unrelated CMSIS-DAP target `E6635C08CB1F502B` was not selected or used.

## 64 MHz failure baseline

Run `rh3-modeb-sdc-controller-clock-20260908`, row
`rh3.fresh_mode_b_48_4_1`, used the current controller-clock scheduler before
the 128 MHz change. Its source CPUAPP hash was
`950d61bd68231ea14d83d42982fd4c594f82d1d317e9287b9c9d9ab5cb98b314`.
The CPUNET and both receiver hashes were identical to the passing rerun.

The runner failed at `run row` with `run state deadline exceeded`. At the
runner stop request the source had reached only `sub=10907`, `sc=10763`, and
`cb=10907`, with `sf=0`, `out=0`, and `skip=10185`. The source needed roughly
two 10 ms intervals for each Mode B SDU and caught up by skipping almost one
event per submission. The runner then requested stop; the source emitted a
terminal failure because it had not reached the scored target. The retained
status had `first_errno=0`; this was not the later frozen-clock `-ETIMEDOUT`
unit-test path.

The failed run has no accepted receiver summary, so it is throughput evidence,
not a receiver-delivery verdict. Its immutable evidence root is:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-controller-clock-20260908/
```

All 23 entries listed by its `SHA256SUMS` verify.

## 128 MHz Mode B fix-validation

Run ID: `rh3-modeb-sdc-controller-clock-128mhz-20260908`

Row: `rh3.fresh_mode_b_48_4_1`

Evidence root:

```text
/tmp/opencode/hil-runs/rh3-modeb-sdc-controller-clock-128mhz-20260908/
```

Runner result: `outcome=passed`, no failed boundary, no failure detail, and
`cleanup_failures=[]`.

Source final status:

- `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`, `out=0`;
- `first_errno=0`, `security_error=0`, `disconnect_reason=0`;
- `skip=0`;
- controller-relative lead `min=2878 us`, `max=2939 us`, `under=0`;
- final TX Sync telemetry `sync=[[146008579,1]]`, meaning one successful final
  schedule-reference poll.

Receiver summary:

- `rx_valid=12644`, `rx_lost=11`, `plc=22`, `decoded=25310`;
- `decode_err=0`, `rx_error=0`, `rx_unknown=0`, `empty_sdu=0`;
- `i2s_underrun=0`, `stream_reset=0`;
- FLPR offload `submit=12655`, `success=12655`, `fallback=0`, with every fault
  and recovery counter zero;
- post-stop handshake Ready/ACKed/Healthy, with all protocol error counters
  zero.

All 26 entries listed by this run's `SHA256SUMS` verify.

## 128 MHz Mode A fix-validation

Run ID: `rh3-modea-sdc-controller-clock-128mhz-20260908`

Row: `rh3.fresh_mode_a_48_4_1`

Evidence root:

```text
/tmp/opencode/hil-runs/rh3-modea-sdc-controller-clock-128mhz-20260908/
```

Runner result: `outcome=passed`, no failed boundary, no failure detail, and
`cleanup_failures=[]`.

Source final status:

- both streams: `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`, `out=0`;
- `first_errno=0`, `security_error=0`, `disconnect_reason=0`;
- shared-grid `skip=0`;
- stream-0 lead `2862..2939 us`, stream-1 lead `2642..2847 us`, both with
  `under=0`;
- both final schedule polls returned the shared reference `146563751`, with one
  successful poll per stream.

Receiver summaries:

| Slot | rx_valid | rx_lost | rx_no_ts | decoded | PLC | Errors/faults |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 12645 | 45 | 41 | 25312 | 23 | zero |
| 1 | 12644 | 25 | 8 | 0 | 0 | zero |

Mode A decoding is attributed to the assembled output on slot 0; slot 1's zero
decoded count is expected for the second half of the pair. The receiver logged
one startup `Mode A: half dropped (overflow=1 rejects=0)` informational record,
then completed the row within the frozen limits with no warning, decode error,
I2S underrun, stream reset, push failure, or offload fault. FLPR offload ended at
`submit=12656`, `success=12656`, and `fallback=0`.

All 26 entries listed by this run's `SHA256SUMS` verify.

## 2026-09-08 warning classification

Each source flash retained only two OpenOCD page-tail erase messages:

```text
Warn : Adding extra erase range, 0x01025708 .. 0x010257ff
Warn : Adding extra erase range, 0x000588c0 .. 0x00058fff
```

The first remains inside the nRF5340 network-core flash region and its 2 KiB
page; the second remains inside application-core flash and its 4 KiB page.
Both programming operations ended with `** Verified OK **`. This is the
page-granularity behavior already classified in `STATUS.md`, not an unexplained
runtime or programming failure. No other warning, error, assertion, or fatal
line was retained for either passing row.

## STATUS-interleave failure and repair

The first RF-controlled matrix attempt,
`rh3-matrix-controller-clock-rf-controlled-20260908`, stopped at pass 1 Mode A.
Its source STATUS record observed stream 0 at `seq=17 sub=16 cb=16 out=1` and
stream 1 at `seq=16 sub=16 cb=16 out=0`. STATUS had run between the two sends
that form one Mode A event. The runner then expired its active FLPR progress
deadline even though the receiver's last active FLPR sample was `submit=28
success=28` with no fault. This was a source-fixture consistency defect, not a
receiver offload failure.

Commit `8123b94` serializes STATUS with complete transmit batches. It preserves
the frozen timestamp lead, common Mode A timestamp, outstanding target, ISO TX
buffer count, and receiver limits.

## Direct post-fix Mode A validation

Run ID: `rh3-modea-status-batch-fix-20260909`

Evidence root:

```text
/tmp/opencode/hil-runs/rh3-modea-status-batch-fix-20260909/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-modea-status-batch-fix-20260909.junit.xml
```

The command returned `0`. `result.json` records `outcome=passed`, no failed
boundary, no failure detail, and `cleanup_failures=[]`. Source final status
recorded `sub=12644`, `sc=12000`, `cb=12644`, `sf=0`, and `out=0` on both
streams. Shared-grid `skip=0`; both lead-under counters were zero; both final TX
Sync polls returned timestamp `146454515`.

Receiver slot 0 recorded `rx_valid=12645`, `rx_lost=45`, `plc=23`, and
`decoded=25312`. Slot 1 recorded `rx_valid=12644`, `rx_lost=25`, and zero PLC,
as expected for the second half of Mode A assembly. Decode errors, RX errors,
I2S underruns, stream resets, empty SDUs, and push failures were zero. Post-stop
FLPR recorded `submit=12656`, `success=12656`, zero fallback, and zero faults.
Handshake health and protocol counters passed. All 26 entries in `SHA256SUMS`
verified.

## Fixed RH3 matrix acceptance

Run ID: `rh3-matrix-status-batch-fix-20260909`

Aggregate evidence:

```text
/tmp/opencode/hil-runs/rh3-matrix-status-batch-fix-20260909/
```

Child evidence:

```text
/tmp/opencode/hil-runs/rh3-matrix-status-batch-fix-20260909.children.82ccd373be50/
```

External JUnit:

```text
/tmp/opencode/hil-runs/rh3-matrix-status-batch-fix-20260909.junit.xml
```

The command returned `0`. Aggregate `result.json` records 14 scheduled, 14
attempted, 14 passed, zero failed, zero cancelled, and no cleanup failures.
JUnit records 14 tests with zero failures, errors, or skips. Child execution ran
from `2026-09-09T20:51:59Z` through `2026-09-09T21:42:11Z`; summed child
duration was 3011.358 seconds.

Host adapters `hci0` (`A0:AD:9F:7B:C7:95`) and `hci1`
(`B0:82:E2:1F:A2:80`) were powered off before the direct row and matrix. An exit
and signal trap restored both adapters afterward, and the operator confirmed
both powered on. This host-state observation was not written into the run
manifests, so treat it as session context rather than manifest-backed evidence.

| Pass | Row | Source submitted/scored | Receiver `rx_valid` | `rx_lost` | PLC |
| ---: | --- | --- | --- | --- | --- |
| 1 | fresh mono | 12644/12000 | 12645 | 10 | 10 |
| 1 | fresh Mode A | 12644/12000 each | 12645 / 12644 | 52 / 25 | 23 / 0 |
| 1 | fresh Mode B | 12644/12000 | 12643 | 12 | 24 |
| 1 | preserved Mode B | 12644/12000 | 12645 | 10 | 20 |
| 1 | reconnect Mode B | 25288/24000 | 12645 / 12645 | 10 / 10 | 20 / 20 |
| 1 | FLPR hang Mode B | 12644/12000 | 12645 | 10 | 20 |
| 1 | FLPR stall Mode B | 12644/12000 | 12645 | 10 | 20 |
| 2 | fresh mono | 12644/12000 | 12645 | 10 | 10 |
| 2 | fresh Mode A | 12644/12000 each | 12645 / 12644 | 45 / 25 | 23 / 0 |
| 2 | fresh Mode B | 12644/12000 | 12644 | 11 | 22 |
| 2 | preserved Mode B | 12644/12000 | 12645 | 10 | 20 |
| 2 | reconnect Mode B | 25288/24000 | 12645 / 12645 | 10 / 10 | 20 / 20 |
| 2 | FLPR hang Mode B | 12644/12000 | 12645 | 10 | 20 |
| 2 | FLPR stall Mode B | 12644/12000 | 12645 | 10 | 20 |

Every source segment ended with zero send failure, zero outstanding SDU, zero
skipped event, zero lead underrun, and zero lifecycle or security error. Every
receiver segment stayed within the frozen delivery and PLC limits. Decode
errors, RX errors, unknown RX status, empty SDUs, I2S underruns, stream resets,
and push failures were zero.

Both hang rows received `FAULT_HANG_ACK`, recorded one timeout and one recovery
attempt, used 45 CPUAPP fallback blocks, restarted FLPR runtime once, and ended
with zero recovery failure. Both stall rows received `TIMED_STALL_ACK`, recorded
one timeout and one recovery attempt, used 12 fallback blocks, required no
runtime restart, and ended with zero recovery failure.

All children resolved receiver `8EE9B3FF` as nRF54L15 with DPIDR `0x6ba02477`,
PART `0x00054b15`, variant `AAC0`, and source J-Link `001050023938` as nRF5340
with DPIDR `0x6ba02477`, PART `0x00005340`, variant `0x514b4141`. Every child
recorded clean Git HEAD `8123b948fc82b33217a955c40021eda57d315f12` and the
two source and two receiver image hashes listed above. The unrelated CMSIS-DAP
target `E6635C08CB1F502B` was never selected.

Aggregate `SHA256SUMS` verified all seven files. Healthy and reconnect children
verified 26 files each; hang and stall children verified 28 files each. Source
flash logs contained only these page-tail extensions, followed by successful
verification:

```text
Warn : Adding extra erase range, 0x01025708 .. 0x010257ff
Warn : Adding extra erase range, 0x00058984 .. 0x00058fff
```

The application-core start address changed with the post-fix image size. Its
end address remains the same 4 KiB flash-page boundary. All programming steps
ended with `** Verified OK **`.

Only hang rows emitted runtime warnings. Each retained the expected
`RING_RESET_ACK timeout (100 ms)` and escalation-to-runtime-restart warning
inside its named fault window. No other warning, error, assertion, fatal line,
or cleanup failure occurred.

## Conclusion and next gate

The 64/128 MHz A/B comparison proves that the corrected encode-before-wait
controller-clock scheduler needs the nRF5340 application core at 128 MHz for
the worst-case Mode B encoding workload. At 128 MHz it submits every SDU inside
the lead window with zero skipped events. The same image also passes Mode A on
the shared controller grid.

These passes show that SDC on this nRF5340 central can carry the tested
timestamp-pinned Mode A and Mode B streams. The prior evidence does not support
a SoftDevice Controller defect report. The old near-zero-delivery chain used a
different source scheduler, and the new A/B comparison does not assign each
historical failure to one sub-cause.

The fixed two-pass matrix satisfies RH3's source, receiver, recovery, lifecycle,
identity, and evidence-integrity requirements. Verdict:
`TRANSPORT_RUNTIME_ACCEPTED`.

No analog capture or audibility observation occurred because
`capture_capability=none`. This verdict reaches the I2S submission boundary; it
does not prove DAC pin activity, analog output, audibility, or stereo channel
mapping. It also does not cover 7.5 ms or immutable release assets. RH3-7p5
remains open. Next transport gate is RH4 against exact candidate archives when
such assets exist.
