# RH3 controller-clock source fix-validation result

Status: **two direct fix-validation rows passed on 2026-09-08**. The current
nRF5340 SDC source fixture now passes the same 10 ms Mode B shape that failed
with the application core at 64 MHz, and it also passes the mandatory 10 ms
Mode A shape. These are direct rows, not the twice-run RH3 matrix, so
`TRANSPORT_RUNTIME_ACCEPTED` is not claimed.

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

Two pristine `fw-build-hil-source` builds were byte-identical:

| Image | SHA-256 |
| --- | --- |
| Source CPUAPP | `466fd7284574ee531c081ad857e0b5b9c70f662cf1d9795fa8cf6d0844723563` |
| Source CPUNET | `2c3af526538cacf56312a1b5649c7e036c92196ef0ced0efbfb2e6f583ec635b` |
| Source merged | `f33f28aa62308e8da861b7cefa445906f59b1489d187a0e03e0c1215699ab263` |
| Source merged CPUNET | `85fcb7674956e654f299e322d0417a572d27cddcf7787b8278d8ec9244bdd53b` |

Both builds completed without compiler, linker, Kconfig, or devicetree
warnings. Retained console notices were the dirty Git tree and Zephyr's
documented global `__ASSERT()` message.

Focused verification after the clock change:

- `tests/unit/hil_source_app`: 77/77 passed, no warnings.
- `tests/unit/hil_source_control`: 45/45 passed, no warnings.
- `scripts/test_hil_runner.py` plus `tests/hil`: 383 passed, 1 skipped.
- `scripts/check-test-matrix.py --repo-root .`: 0 errors, 0 notes.
- `git diff --check`: passed.
- Resolved source CPUAPP config contains `CONFIG_NRFX_CLOCK=y` and
  `CONFIG_CLOCK_CONTROL_NRF=y`.

## Physical identity and images

Both runs resolved identities before flashing:

| Role | Probe evidence | Console |
| --- | --- | --- |
| Receiver | `8EE9B3FF`, nRF54L15, DPIDR `0x6ba02477`, PART `0x00054b15`, variant `AAC0` | `/dev/ttyACM2`, USB interface 02 |
| Source | J-Link `001050023938`, nRF5340, DPIDR `0x6ba02477`, PART `0x00005340`, variant `0x514b4141` | `/dev/ttyACM1`, USB interface 02 |

Both passing rows used the source hashes above and these unchanged receiver
images:

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

## Warning classification

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

No analog capture or audibility observation occurred because
`capture_capability=none`. The runs prove neither physical stereo output nor
full RH3 acceptance. Per the plan of record, the next acceptance action is one
fixed `run-rh3-matrix` execution from independently established clean state:
two passes, each with the four healthy 10 ms rows followed by reconnect, FLPR
hang, and FLPR stall, for 14 child runs total. The 7.5 ms profile remains
outside the mandatory matrix.
