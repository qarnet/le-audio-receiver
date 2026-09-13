# RH3 selected CIS-layout telemetry handoff

Status: host and receiver-firmware diagnostic implementation only. No physical
HIL belongs in this phase.

## Goal

Extend existing read-only `bt iso quality` snapshot so each active receiver
CIS retains controller-selected C-to-P layout values beside link-quality
counters. Preserve current command, lifecycle, validation, no-threshold policy,

Next physical diagnostic must be separately reviewed after new receiver image
hashes and this implementation review exist.

## Evidence and decision basis

Same current source/receiver image produced three direct controls:

| Row | CIS layout | Result | Receiver evidence |
| --- | --- | --- | --- |
| RH3-04 Mode A | two 120-byte, 10 ms CISes | failed log scan | `rx_unreceived=14308/14233`, `rx_lost=14310/14249` |
| RH3-05 mono | one 120-byte, 10 ms CIS | passed control | `rx_unreceived=9`, `rx_lost=12` |
| RH3-06 Mode B | one 240-byte, 10 ms CIS | passed control | `rx_unreceived=13267`, `rx_lost=13591` |

Mode B severe loss shows symptom is not exclusive to two concurrent CISes.
Mode B and Mode A both carry 240 codec bytes per 10 ms, while mono carries
120. This narrows symptom shape but cannot distinguish selected controller
layout, scheduling pressure, RF, or implementation behavior.

Verified NCS v3.3.0 facts:

- `bt_iso_chan_get_info()` is a snapshot copy and is safe from receiver
  `stream_started`/shell thread context after CIS establishment.
- On receiver, stable event-backed selected C-to-P fields are ISO interval,
  max subevents, CIG/CIS sync delays, central max PDU, PHY, burst number, and
  central flush timeout.
- Do not expose central latency, which host v1 mapping truncates, or v1-only
  fallback `max_sdu`, `sdu_interval`, or `subinterval` fields.
- Current nRF5340 SW Split source has no usable public ISO link-quality
  telemetry. Do not enable or alter it.
- Current 240-byte Mode B payload fits HCI and SW Split buffer sizing. Do not
  change SDU length, buffers, RTN, latency, PHY, controller policy, or RF.

## Exact output contract

Keep header exactly:

```text
--- ISO link quality ---
```

Extend each existing stream line exactly, appending these selected values in
this order:

```text
  Stream[<slot>] handle=0x<4-uppercase-hex> tx_unacked=<n> tx_flushed=<n> tx_last_subevent=<n> retransmitted=<n> crc_error=<n> rx_unreceived=<n> duplicate=<n> iso_interval_1250us=<n> nse=<n> cig_sync_us=<n> cis_sync_us=<n> c_max_pdu=<n> c_phy=<n> c_bn=<n> c_flush_1250us=<n>
```

Units:

- `iso_interval_1250us` and `c_flush_1250us` are counts of 1.25 ms units;
- `cig_sync_us` and `cis_sync_us` are microseconds;
- `nse`, `c_phy`, and `c_bn` are selected controller fields;
- `c_max_pdu` is selected central-to-peripheral maximum PDU octets.

No unknown/sentinel output is allowed for these base event fields. Absence,
zero, truncation, malformed grammar, duplicate slots, or wrong record count
must fail receiver-tail validation. Nonzero link-quality counters remain
retained diagnostics only, never pass/fail thresholds.

## Exact implementation

### `src/bt_bap.h`

Extend `struct bt_bap_iso_link_quality` with fields matching exact output:

```c
uint16_t iso_interval_1250us;
uint8_t nse;
uint32_t cig_sync_us;
uint32_t cis_sync_us;
uint16_t c_max_pdu;
uint8_t c_phy;
uint8_t c_bn;
uint32_t c_flush_1250us;
```

Update API documentation: snapshots now include selected C-to-P CIS-layout
fields from `bt_iso_chan_get_info()` plus link-quality counters. Preserve all
existing input, ordering, error, and thread-context contract.

### `src/bt_bap.c`

In the nRF54L15 active sink enumeration inside
`bt_bap_iso_link_quality_get_active()`:

1. After existing endpoint/ISO-object checks and before snapshot success,
   call `bt_iso_chan_get_info(ep_info.iso_chan, &iso_info)`.
2. Propagate its exact nonzero errno. Do not log a warning or touch lifecycle
   locks.
3. Populate the eight fields only from:

```c
iso_info.iso_interval
iso_info.max_subevent
iso_info.unicast.cig_sync_delay
iso_info.unicast.cis_sync_delay
iso_info.unicast.central.max_pdu
iso_info.unicast.central.phy
iso_info.unicast.central.bn
iso_info.unicast.central.flush_timeout
```

4. Keep current public HCI handle lookup and `LE Read ISO Link Quality`
   request/response validation unchanged.
5. Do not derive FT, access `bt_conn` internals, cache handles, add polling,
   add HCI commands, or access RADIO.

### `src/bt_shell.c`

Append exact fields and values to existing one-line `bt iso quality` output.
Keep header, command path, negative-errno text, and nRF54L15-only command
registration unchanged. Update command help to mention selected C-to-P CIS
parameters.

### `scripts/hil/receiver.py`

Extend strict `RE_ISO_LINK_QUALITY_STREAM`, parsed record, and validator for
eight exact appended fields. Require every selected field to be a positive
integer. Preserve present counter parsing and preserve no link-quality
threshold. Structured `summary.receiver_tail[*].receiver.iso_link_quality`
records must carry all selected fields.

### Tests

Modify existing test methods rather than adding independent test methods, so
current test counts stay stable:

- `tests/unit/audio_shell_nrf54/src/test_audio_shell_nrf54.c`: extend existing
  exact multi-stream shell test with nonzero selected fields and exact complete
  lines. Increase local output-line buffer safely if required. Keep existing
  errno test.
- `tests/hil/hil_fakes.py`: make default and supplied quality transcripts emit
  valid selected values in exact field order.
- `tests/hil/rh2_test.py`: extend existing exact parser, malformed-candidate,
  Mode A retained-evidence, and lifecycle tests. Prove selected data reaches
  `summary.json`; prove a missing/truncated selected field fails receiver tail;
  prove nonzero `crc_error`/`rx_unreceived` still do not fail a row.
- `tests/unit/audio_shell/src/fake_audio_shell_deps.[ch]`: keep fake API shape;
  it copies the expanded struct. Change only if compiler requires it.

Do not alter `scripts/hil/runner.py` command ordering: `bt iso quality` remains
the only tail command. Do not add another shell command or source HIL protocol
field.

## Scope limits

Only these files may change beyond the new/updated internal status docs:

```text
src/bt_bap.h
src/bt_bap.c
src/bt_shell.c
scripts/hil/receiver.py
tests/hil/hil_fakes.py
tests/hil/rh2_test.py
tests/unit/audio_shell_nrf54/src/test_audio_shell_nrf54.c
```

No hardware/HIL execution, flash, reset, serial, Bluetooth/RF action,
firmware-source (`hil/source/`) change, source build, Kconfig/devicetree,
controller, QoS/RF parameter, threshold, `STATUS.md`, commit, push, merge, PR,
tag, or release.

## Verification

Run sequentially in one shell session from repository root so
`shell54_build` remains defined. `rh2_test.py` needs outer terminal-tool timeout
at least `1800000` ms. Do not use shell `timeout`. Build directory must be
outside repository:

```bash
test -d /tmp
shell54_build="$(mktemp -d /tmp/le-audio-receiver-iso-selected-shell54.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$shell54_build" tests/unit/audio_shell_nrf54 -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
nix develop --command fw-build-5340
nix develop --command fw-build-54l15
nix develop --command python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
nix develop --command python3 -m py_compile \
  scripts/hil/receiver.py \
  tests/hil/hil_fakes.py \
  tests/hil/rh2_test.py \
  tests/hil/capture_runner_test.py
```

Treat compiler, Kconfig assigned-value, and unlisted CMake/DTS diagnostics as
failures. Only documented NCS baseline diagnostics may appear. Do not delete
the temporary build directory without explicit user approval.

## Return format

Return changed files; exact output contract; all test/build/check results and
counts; four hashes; updated factual docs; final status; no-hardware/no-source-
build/no-commit confirmation; deviations and blockers.
