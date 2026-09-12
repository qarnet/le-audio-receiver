# RH3 receiver ISO-status diagnostic handoff

Status: proposed software-only diagnostic phase. This phase classifies ISO
callbacks already delivered to the receiver application. It does not authorize
hardware execution, build, flash, reset, pairing, radio changes, or another
RH3 matrix row.

## Goal

Make a receiver stream summary distinguish HCI-delivered `VALID`, `ERROR`,
`LOST`, and unclassified ISO callbacks, plus callbacks lacking an ISO timestamp.
Keep current PLC, decode, Mode A, timing, offload, warning, and HIL verdict
behavior unchanged. New data is diagnostic evidence only. It must not create
or relax an RH3 acceptance threshold.

## Grounding

RH3-11 Mode A evidence retained at:

```text
/tmp/opencode/hil-runs/rh3-20260820-03.children.a62d866f712c/
  rh3-20260820-03.p1.r2.rh3.fresh_mode_a_48_4_1.f92e84021d7c/
    receiver-status.txt
```

showed source submission of `12348` SDUs on each stream, receiver summary
`SDUs=116` for stream 0 and `SDUs=0` for stream 1, and receiver audio stats
`decoded=28006 plc=27890`. The existing `SDUs` counter increments only when
the BAP receive adapter sees `BT_ISO_FLAGS_VALID`, so it proves only 116 valid
callbacks reached the app. The single Mode A overflow cannot explain 27,890
PLC frames.

Exact NCS v3.3.0 sources establish status meaning:

- `zephyr/include/zephyr/bluetooth/iso.h` defines `BT_ISO_FLAGS_VALID`,
  `BT_ISO_FLAGS_ERROR`, `BT_ISO_FLAGS_LOST`, and `BT_ISO_FLAGS_TS`.
- `BT_ISO_FLAGS_ERROR` means failed CRC or partial SDU is possible.
- `BT_ISO_FLAGS_LOST` means lost ISO packet.
- `zephyr/subsys/bluetooth/host/iso.c` maps HCI `BT_ISO_DATA_INVALID` to
  `BT_ISO_FLAGS_ERROR` and HCI `BT_ISO_DATA_NOP` to `BT_ISO_FLAGS_LOST`
  before calling the BAP receive callback.
- `src/bt_bap.c:stream_recv()` currently extracts only `VALID` and `TS`, then
  calls `audio_stream_session_recv_valid_count()` before its gate-closed
  return.
- `src/audio_stream_session.c` owns that counter under `session_mutex` and
  deliberately counts in-range slots before admission/configuration checks.
  `stream_started()` resets it through `audio_stream_session_recv_reset()`.
- `Kconfig` documents that some SW Split losses emit no callback at all. This
  phase can classify callbacks that arrive; it cannot make absent callbacks
  observable.

The source and tests show `audio_stream_session_recv_valid_count()` has only
two consumers: `src/bt_bap.c` and
`tests/unit/audio_stream_session/src/test_audio_stream_session.c`. The old
internal API may therefore be replaced without a compatibility wrapper.

## In scope

1. Add bounded per-slot callback-status accounting to the audio stream session.
2. Replace the old valid-only recording API in the BAP adapter.
3. Append a stable optional receiver-summary diagnostic suffix.
4. Extend host parser/evidence handling for that suffix while accepting legacy
   summaries.
5. Add direct C and fake-lab regression tests.
6. Update `tests/test-matrix.json` for changed public session APIs.

## Out of scope

- HCI monitor UART, raw HCI capture, ISO link-quality commands, controller
  changes, radio tuning, retry policy, source-fixture changes, or hardware
  diagnosis execution.
- Any PLC algorithm, `audio_iso_seq`, timestamp-cadence, Mode A assembler,
  LC3, I2S, offload, timing, pairing, or lifecycle behavior change.
- HIL row validation, PLC/received-SDU limits, warning-scanner exceptions,
  test-matrix rows, source terminal validation, or acceptance claims.
- Kconfig, devicetree, build configuration, image hashes, existing evidence,
  run IDs, public documentation, `STATUS.md`, commits, pushes, PRs, tags, or
  releases.
- Manual serial, `serial-mcp`, flash, reset, OpenOCD, recovery, pairing, or
  any HIL command.

## Exact implementation

### 1. Session-owned diagnostic model

In `src/audio_stream_session.h`, add these public types after
`enum audio_stream_mode`:

```c
enum audio_stream_rx_status {
	AUDIO_STREAM_RX_STATUS_VALID,
	AUDIO_STREAM_RX_STATUS_ERROR,
	AUDIO_STREAM_RX_STATUS_LOST,
	AUDIO_STREAM_RX_STATUS_UNKNOWN,
};

struct audio_stream_rx_stats {
	size_t valid;
	size_t error;
	size_t lost;
	size_t unknown;
	size_t no_ts;
};
```

Replace `audio_stream_session_recv_valid_count()` with exactly:

```c
size_t audio_stream_session_rx_status_record(size_t idx,
				      enum audio_stream_rx_status status, bool has_ts);
struct audio_stream_rx_stats audio_stream_session_rx_stats_get(size_t idx);
```

Contract:

- `rx_status_record()` records one app-delivered ISO callback for any in-range
  slot, even before configuration or receive admission. This preserves old
  gate-independent `SDUs` behavior.
- It returns post-record `valid` count. Out-of-range returns `0` and mutates
  nothing.
- `VALID` increments `valid`; `ERROR` increments `error`; `LOST` increments
  `lost`; `UNKNOWN` and any invalid enum value increment `unknown`.
- `no_ts` increments for every recorded callback with `has_ts == false`,
  independently of status.
- `rx_stats_get()` returns a zero-initialized snapshot for an out-of-range slot.
  It never returns a pointer to session state.
- `valid` is raw ISO status evidence. A `VALID` zero-length SDU remains counted
  valid here even though existing receive-path code later normalizes it to PLC.
  Do not redefine it as decoded audio.
- `audio_stream_session_recv_count()` must return this same `valid` count, so
  legacy `SDUs=` retains exact existing meaning.

In `src/audio_stream_session.c`:

1. Replace `recv_cnt` with `struct audio_stream_rx_stats rx_stats` in each
   slot. Keep all mutation and snapshot access under existing `session_mutex`.
   Do not add locks across decode, sink, timing, logging, or Bluetooth calls.
2. Implement both APIs above. `rx_status_record()` is the only per-callback
   status mutation point.
3. Make `audio_stream_session_recv_reset()` zero all five fields.
4. Zero all five fields on config, release, reset-all, and init, matching old
   `recv_cnt` lifecycle semantics.
5. Do not reset status counters in `audio_stream_session_start_clear()`: that
   function only resets Mode A/sequence/cadence state, and old receive count
   intentionally survived it.
6. Update header and source ownership comments to say receive-status counters,
   not only receive count.

### 2. BAP adapter classification

In `src/bt_bap.c:stream_recv()`:

1. Keep existing `valid` and `has_ts` booleans unchanged for timing and decode.
2. Derive one neutral session status with this precedence:
   - `VALID` when `BT_ISO_FLAGS_VALID` is set;
   - otherwise `ERROR` when `BT_ISO_FLAGS_ERROR` is set;
   - otherwise `LOST` when `BT_ISO_FLAGS_LOST` is set;
   - otherwise `UNKNOWN`.
3. Call `audio_stream_session_rx_status_record(idx, status, has_ts)` exactly
   once after lifecycle gate snapshot and before the gate-closed early return.
   Use returned count only for existing periodic valid-SDU log.
4. Keep current `audio_timing_sdu_ref_update()` condition, `valid` decode
   input, non-valid debug line, gate behavior, and performance accounting
   unchanged.
5. Update adapter comments to describe gate-independent callback-status
   accounting. Add no per-packet INFO/WARN/ERR logging.

This keeps existing valid-bit precedence for malformed future flag combinations
and prevents a diagnostic counter from changing receive-path behavior.

### 3. Stable summary suffix

In disabled teardown summary in `src/bt_bap.c`, take one
`audio_stream_session_rx_stats_get(slot)` snapshot beside existing
`audio_stats_get()` snapshot. Preserve all existing summary field names and
order, then append exactly:

```text
 rx_valid=<n> rx_error=<n> rx_lost=<n> rx_unknown=<n> rx_no_ts=<n>
```

Target full grammar:

```text
Stream[<slot>] summary: SDUs=<n> decoded=<n> plc=<n> decode_err=<n> i2s_underrun=<n> stream_reset=<n> empty_sdu=<n> rx_valid=<n> rx_error=<n> rx_lost=<n> rx_unknown=<n> rx_no_ts=<n>
```

`SDUs` and `rx_valid` must match. No new warning, error, threshold, or summary
line is permitted. Existing `decoded` and `plc` meanings stay unchanged.

### 4. Host parser and evidence compatibility

In `scripts/hil/receiver.py`:

1. Extend `RE_STREAM_SUMMARY` with one all-or-none optional suffix containing
   exactly five nonnegative decimal fields in exact order shown above.
2. Preserve strict rejection of missing old fields, malformed numbers, partial
   suffixes, reordered suffixes, and extra trailing text.
3. `parse_stream_summary()` must always return keys `rx_valid`, `rx_error`,
   `rx_lost`, `rx_unknown`, and `rx_no_ts`. Return `None` for every new key
   when a legacy summary lacks suffix; return integers when suffix exists.
4. Do not change `_step_session_end()` or row validation. Existing runner
   already serializes parser-returned stream dicts in `summary.json`, so fields
   become retained diagnostic evidence without becoming pass/fail criteria.

In `tests/hil/hil_fakes.py`, extend `receiver_stream_summary_line()` with five
optional keyword arguments. Require all five or none; reject negative values.
With none, emit exact legacy grammar. With all five, emit exact suffix grammar.

### 5. Regression tests and matrix ledger

In `tests/unit/audio_stream_session/src/test_audio_stream_session.c`:

1. Replace every old valid-count API use with
   `audio_stream_session_rx_status_record(..., AUDIO_STREAM_RX_STATUS_VALID,
   ...)` while preserving tested lifecycle behavior.
2. Rename `test_recv_valid_count_gate_independent` to
   `test_rx_status_record_gate_independent`.
3. In that test, before admission and without enabled decoder, record three
   valid callbacks with timestamps, one error without timestamp, one lost
   without timestamp, and one unknown without timestamp. Assert:

   ```text
   valid=3 error=1 lost=1 unknown=1 no_ts=3
   recv_count=3
   ```

4. Prove `recv_reset()` clears all five counters, an in-range unconfigured
   slot still records status, and out-of-range record/get stays zero.
5. Add assertion in existing start-clear test that `start_clear()` retains the
   status snapshot. Existing release, reset-all, and reconnect tests must
   continue proving fresh zeroed counters after their lifecycle boundaries.
6. Test observable snapshots and return values only. Do not inspect private
   session structures or count mutex/helper calls.

In `tests/test-matrix.json` session entry:

1. Replace `audio_stream_session_recv_valid_count` outcome with
   `audio_stream_session_rx_status_record`, outcome `3`, witness
   `test_rx_status_record_gate_independent`.
2. Add `audio_stream_session_rx_stats_get`, outcome
   `"valid=3,error=1,lost=1,unknown=1,no_ts=3"`, same witness.
3. Update `recv_reset` and `recv_count` witness names to renamed test.
4. Remove every ledger reference to deleted valid-count API.

In `tests/hil/rh2_test.py`:

1. Preserve legacy parser tests and assert all five new parsed fields are
   `None` for legacy text.
2. Add extended-summary parser coverage with nonzero values for every field.
3. Add malformed partial-suffix coverage that fails to parse.
4. Add one fake full-run test using extended fake summary. Read resulting
   `summary.json` and assert `receiver_streams[0].last` retains all five exact
   values. Use zero errors/losses in this passing fake row: phase adds evidence,
   not a healthy-row exemption.

## Verification

Run from repository root in existing NCS v3.3.0 dev shell. Use a fresh build
directory outside repository; leave it retained for diagnosis.

```bash
build_dir="$(mktemp -d /tmp/le-audio-receiver-rx-status.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$build_dir" tests/unit/audio_stream_session -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
python3 -m py_compile scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
git diff --check
```

Do not run full canonical gate: current worktree is intentionally dirty and
coverage baseline enforcement requires clean tree. Do not run firmware builds,
flash, HIL, or hardware commands in this phase.

## Executor rules

Implement only this handoff. Preserve unrelated dirty files, including prior
uncommitted RH3 host-tail work. Do not commit because user did not request one.
Stop and report if any requirement needs a PLC threshold, warning exemption,
controller/API invention, build configuration change, hardware access, or a
change to decode/timing/Mode A behavior.

Return changed files, exact verification results, build directory path, final
`git diff --check`, final `git status --short`, deviations, blockers, and
suggested next step.
