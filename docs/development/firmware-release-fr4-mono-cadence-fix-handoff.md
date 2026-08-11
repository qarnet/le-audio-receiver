# FR4 mono runtime fix handoff: conceal timestamp-detected ISO omissions

Date: 2026-08-10

## Goal

Fix deterministic one-CIS mono I2S starvation exposed by FR4 hardware. The
central transmitted 12000 10 ms SDUs, but the exact draft nRF5340 firmware
received only 8876 callbacks. Its current per-CIS sequence tracker reported no
gap, produced no PLC, and the I2S backend restarted 225 times.

Add bounded ISO timestamp-cadence detection for one-CIS mono and Mode B. When
consecutive delivered callbacks span one or more ISO events for which no
callback arrived, conceal each omitted event through the existing PLC decode
and sink-push path before processing the current SDU. Keep current sequence-gap
detection as an independent signal for host-side drops, and never double
conceal when sequence and timestamp evidence describe the same omission.

This handoff covers firmware logic, direct tests, and current explanatory
comments/docs only. It does not authorize hardware, flashing, release asset
replacement, VERSION changes, tag/release writes, or FR4 acceptance. Draft
`v0.1.0` remains failed and unpublished.

## Grounding

### Hardware evidence

Retained run: `/tmp/opencode/fr4-v0.1.0-OEp9Kh`.

Current blocker logs:

- `logs/nrf5340-mono2-central.log`: strict mono succeeded centrally, exactly
  one CIS/transport, 120-byte SDU, 12000 frames in 120 s at 100 fps;
- `logs/nrf5340-mono2-receiver.log`: `SDUs=8876 decoded=8876 plc=0
  decode_err=0 i2s_underrun=0 stream_reset=225 empty_sdu=0`, plus 225 each of
  `i2s_nrfx: Next buffers not supplied on time`, `Cannot write in state: 4`,
  and `audio_i2s: I2S underrun, restarting DMA`;
- same exact draft firmware completed the authorized 30 s Mode A diagnostic
  with zero decode errors, underruns, resets, or warnings.

QoS matched between mono and Mode A: unframed, 2M PHY, 10000 us interval,
120-byte SDU, RTN 2, 10 ms latency, 40000 us presentation delay. Do not hide
the receiver defect by changing central QoS or weakening FR4 criteria.

### NCS v3.3.0 source semantics

Load-bearing installed-source evidence:

- `zephyr/include/zephyr/bluetooth/iso.h`, `struct bt_iso_recv_info`: `ts` is
  valid only with `BT_ISO_FLAGS_TS`; `seq_num` is the HCI ISO packet sequence
  number of the first SDU fragment.
- `zephyr/subsys/bluetooth/host/iso.c`, `bt_iso_recv()`: host copies the HCI
  timestamp and packet sequence number verbatim; it does not synthesize a
  timestamp when the TS flag is absent.
- `zephyr/subsys/bluetooth/controller/ll_sw/isoal.c`,
  `isoal_rx_buffered_emit_sdu()` / `isoal_rx_try_emit_sdu()`: controller
  `session->sn` advances only when an SDU is emitted to the host. A radio event
  with no received PDU emits no HCI SDU and consumes no sequence number.
  Therefore controller-side omissions leave app-visible `seq_num` contiguous,
  exactly matching FR4 evidence.
- `zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_peripheral_iso.c`
  and `zephyr/subsys/bluetooth/controller/ll_sw/isoal.c`: emitted SDU
  timestamps derive from the CIS event anchor and SDU synchronization
  reference in microseconds. A callback after omitted radio events advances by
  the corresponding integer multiple of the SDU interval.
- `zephyr/subsys/bluetooth/controller/hci/hci_driver.c`,
  `sink_sdu_emit_hci()`: SW Split includes the timestamp on START/SINGLE HCI
  ISO packets, including emitted LOST SDUs.
- `nrf/samples/bluetooth/iso_time_sync/`: nRF54L15 SDC sample consumes RX
  timestamps as controller-clock microseconds; timestamp absence remains
  allowed by the public host contract.

nRF5340 SW Split timestamps wrap at the controller RTC-derived boundary about
every 512 s, not natural `uint32_t` microsecond wrap. nRF54L15 SDC timestamps
use a 32-bit GRTC microsecond view. This fix must treat any backward timestamp
as a safe baseline rebase with no synthesis. Missing one event across a wrap is
preferable to false or unbounded PLC.

## Exact design

### Keep module/file boundary

Do not add or rename a production source file. Extend `src/audio_iso_seq.h`
and `src/audio_iso_seq.c` with a second pure tracker named
`audio_iso_cadence`. The existing sequence tracker and API remain intact for
Mode A and host-side sequence gaps. Keeping both pure trackers in the current
module avoids coverage-population churn and makes their different evidence
sources explicit.

### Timestamp-cadence state and API

Add:

```c
#define ISO_TS_DELTA_TOLERANCE_US 10U

enum audio_iso_cadence_result {
	AUDIO_ISO_CADENCE_RES_FIRST = 0,
	AUDIO_ISO_CADENCE_RES_NO_TS,
	AUDIO_ISO_CADENCE_RES_CONTIG,
	AUDIO_ISO_CADENCE_RES_GAP,
	AUDIO_ISO_CADENCE_RES_WRAP,
	AUDIO_ISO_CADENCE_RES_RESYNC,
};

struct audio_iso_cadence {
	bool initialized;
	uint32_t last_ts;
	uint32_t callbacks_since_ts;
	uint32_t concealed;
	uint32_t resyncs;
};

void audio_iso_cadence_reset(struct audio_iso_cadence *st);

enum audio_iso_cadence_result audio_iso_cadence_update(
	struct audio_iso_cadence *st, bool has_ts, uint32_t ts,
	uint32_t interval_us, uint32_t *omitted);

uint32_t audio_iso_cadence_get_concealed(
	const struct audio_iso_cadence *st);
uint32_t audio_iso_cadence_get_resyncs(
	const struct audio_iso_cadence *st);
```

Names may be line-wrapped to project format, but do not change semantics or
add Zephyr dependencies.

`ISO_TS_DELTA_TOLERANCE_US` is a fixed 10 us. This matches installed
`nrf5340_audio`'s `SDU_REF_CH_DELTA_MAX_US` for 10 ms frames and safely covers
the 7.5 ms receiver shape. Use 64-bit intermediate arithmetic for
`event_count * interval_us` and absolute error; no floating point.

### Cadence state transitions

Every delivered callback in one-CIS mode feeds the cadence tracker, whether
VALID, LOST, empty, or malformed. Set `*omitted = 0` first when non-NULL.

1. `st == NULL`: return `FIRST`, no output mutation beyond zeroing `omitted`.
2. `has_ts == false`:
   - if initialized, increment `callbacks_since_ts` with saturation at
     `UINT32_MAX`;
   - return `NO_TS`; never warn and never synthesize because TS is optional.
3. `has_ts == true && interval_us == 0`: increment `resyncs`, return
   `RESYNC`, synthesize nothing, and leave timestamp initialization state
   unchanged.
4. First timestamp: store `last_ts`, set initialized, clear
   `callbacks_since_ts`, return `FIRST`.
5. `ts < last_ts`: classify controller timestamp wrap/rebase. Store current
   timestamp, clear `callbacks_since_ts`, return `WRAP`, no synthesis and no
   resync increment.
6. Otherwise capture `callbacks_since_ts`, then compute forward
   `delta_us = ts - last_ts`. Rebase stored timestamp and clear
   `callbacks_since_ts` before returning from every classified result.
7. Number of delivered callback positions represented since the prior valid
   timestamp is captured `callbacks_since_ts + 1` (all intervening no-TS
   callbacks plus current callback). Compute nearest integer timestamp event count as
   `(delta_us + interval_us / 2) / interval_us` using 64-bit arithmetic.
8. Return `RESYNC`, increment `resyncs`, and synthesize nothing when any is
   true:
   - `ts == last_ts` / zero event advance;
   - nearest event count is less than delivered callback positions;
   - absolute difference between `delta_us` and nearest-event-count times
     `interval_us` exceeds 10 us;
   - inferred omitted count exceeds `ISO_SEQ_MAX_CONCEAL`;
   - counter/arithmetic state cannot be represented safely.
9. `omitted = event_count - delivered_callback_positions`:
   - zero: return `CONTIG`;
   - 1 through `ISO_SEQ_MAX_CONCEAL`: write `omitted`, add to `concealed`,
     return `GAP`.

Missing-TS callbacks must not become false omissions. Example: timestamps at
10000 and 30000 with one delivered no-TS callback between them represent two
delivered event positions and zero omissions. Timestamps at 10000 and 40000
with that same one no-TS callback represent one omitted event.

### Session ownership and reset

In `src/audio_stream_session.c`, add one `struct audio_iso_cadence` beside each
slot's existing sequence tracker. Reset it at every location that resets the
sequence tracker: config, start-clear, release, and reset-all. Update matching
comments and `src/audio_stream_session.h` contract text.

Do not move cadence state into `bt_bap.c`, `audio_timing`, global shared state,
or ISR context. Receive lease/lifecycle ownership stays unchanged.

### One-CIS merge rule

In `session_recv_path()`:

1. Preserve existing sequence update on every callback.
2. For `MONO` and `MODEB` only, update timestamp cadence with `has_ts`, `ts`,
   and validated `shape.frame_dur_us`. Frame blocks per SDU are already
   constrained to exactly one by `validate_codec_cfg()`, so frame duration is
   the SDU interval.
3. For Mode A, do not run timestamp-cadence synthesis. Keep existing per-CIS
   sequence sentinel path and shared timestamp assembler unchanged. Timestamp-
   only Mode A synthesis needs event-position/sentinel design beyond this
   blocker and is out of scope.
4. For one-CIS modes choose:

   ```c
   omitted = MAX(seq_omitted, cadence_omitted);
   ```

   Never add them. A host-side dropped HCI SDU produces both a sequence jump
   and timestamp jump for the same missing output event; `MAX` conceals it
   once. A controller-side radio omission produces only a timestamp jump.
5. Preserve exact current bounded PLC loop and process-current-after-PLC order.
   `first_seq` remains relevant only to Mode A sequence sentinels.
6. Preserve current sequence `RESYNC` warning. Cadence `WRAP` is expected and
   must not warn. Cadence `RESYNC` is unexpected and must emit one clear
   `LOG_WRN` with slot, current timestamp, interval, and cumulative cadence
   resync count, with no synthesis.
7. Do not emit INFO logs per timestamp gap. FR4 observed thousands of omitted
   events; per-gap UART logging could perturb real-time behavior. A `LOG_DBG`
   line is allowed. Existing aggregate PLC and stream-reset summary remains
   public evidence.

All callback positions advance before empty/malformed validation, preserving
the current no-double-conceal contract for delivered rejected packets.

## Tests

### Pure tracker

Expand `tests/unit/iso_seq/src/test_iso_seq.c`, using production
`audio_iso_seq.c`, with direct cadence tests for:

1. NULL/reset/first timestamp;
2. exact contiguous 10 ms and 7.5 ms timestamps;
3. one and multiple timestamp-only omissions with contiguous sequence
   implied externally;
4. +10 us and -10 us accepted tolerance boundaries, ±11 us rejected;
5. one and multiple missing-TS callbacks followed by a timestamp with no false
   concealment;
6. missing-TS callback plus a real omitted event;
7. LOST-style delivered callbacks at contiguous timestamps causing no extra
   omission;
8. backward nRF5340-style timestamp wrap returning `WRAP`, rebasing, then
   normal contiguous operation;
9. duplicate timestamp, non-integral forward delta, zero interval, and
   event-count-less-than-delivered callbacks returning bounded `RESYNC`;
10. exact concealment bound accepted; over-bound gap resyncs with no synthesis;
11. cumulative concealed/resync counters and reset clearing all state.

Do not delete or weaken existing sequence and Mode A tests. Update top-level
test comments so they no longer claim controller omissions necessarily jump
HCI packet sequence numbers.

### Production session boundary

Expand
`tests/unit/audio_stream_session/src/test_audio_stream_session.c` with tests
through `audio_stream_session_recv()` and fake sink/stat public boundaries:

1. mono, contiguous `seq_num` plus a 20 ms timestamp delta at 10 ms: exactly
   one PLC push before current valid push, one PLC frame, no decode error;
2. Mode B same case: one omitted SDU produces one stereo sink push and two PLC
   frames before current valid output;
3. sequence and timestamp both report the same two omitted events: exactly two
   PLC pushes, never four;
4. a delivered no-TS mono callback between valid timestamps does not synthesize
   PLC; add one true omitted event and prove exactly one PLC;
5. timestamp over-bound/resync produces no PLC and current SDU still decodes;
6. Mode A timestamps spanning a silent interval do not trigger new cadence
   synthesis or disturb current assembler/sequence behavior;
7. start-clear/release/reset/reconnect rebase cadence so no gap crosses session
   boundaries.

Preserve existing golden PCM/hash behavior and all current tests.

### Documentation truth

Update only current, load-bearing descriptions:

- `Kconfig` help for `AUDIO_ISO_SEQ_MAX_CONCEAL`: shared bound for sequence-
  and timestamp-detected omissions; explain HCI packet sequence can stay
  contiguous for controller-side omission.
- `src/audio_iso_seq.{c,h}` and `src/audio_stream_session.{c,h}` comments.
- `docs/testing/behavior-contract.md`: ordering says sequence/cadence work.
- `docs/testing/coverage-matrix.md`: current tracker/session coverage and test
  counts after implementation.
- `docs/bluetooth-adapter-evaluation.md` only where current analysis treats
  absence of sequence-gap logs as proof that controller-side omissions did not
  occur. Preserve historical measured facts; clarify that HCI sequence
  continuity cannot rule out silent controller omissions.

Do not rewrite historical accepted-result documents, `STATUS.md`, refactor
plan/history, FR4 procedure/results, README, VERSION, release plan, or AGENTS.md
in this code-fix commit. Public documentation must contain no U+2014 em dash.

## Scope

Expected touched files:

- `docs/development/firmware-release-fr4-mono-cadence-fix-handoff.md`
- `src/audio_iso_seq.c`
- `src/audio_iso_seq.h`
- `src/audio_stream_session.c`
- `src/audio_stream_session.h`
- `Kconfig`
- `tests/unit/iso_seq/src/test_iso_seq.c`
- `tests/unit/audio_stream_session/src/test_audio_stream_session.c`
- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `docs/bluetooth-adapter-evaluation.md`

No CMake/test-matrix/coverage-baseline population change should be needed: both
production implementations remain in `src/audio_iso_seq.c`, already compiled
by the existing `audio.iso_seq`, session, and BSim targets.

Out of scope:

- Mode A timestamp-derived synthetic sentinels;
- central QoS changes;
- I2S reservoir size or underrun recovery changes;
- audio timing/drift/ASRC changes;
- stats schema or shell output changes;
- hardware runs or flashing;
- draft asset replacement, release deletion/edit/publication, VERSION/tag/CI
  changes;
- FR4 acceptance/result claims.

## Verification and commit

Run focused suites first:

```bash
west twister -T tests/unit/iso_seq -p native_sim/native/64 --inline-logs
west twister -T tests/unit/audio_stream_session -p native_sim/native/64 --inline-logs
git diff --check
```

Run production builds because this changes firmware input:

```bash
fw-build-5340
fw-build-54l15
```

Treat every actionable compiler, linker, Kconfig, CMake, and build warning as a
failure. Compare only documented NCS diagnostics against `STATUS.md`; do not
normalize a new warning.

Before commit, inspect `git status`, full diff, and recent log. Stage only
handoff-scoped files. Commit:

```text
fix: conceal timestamp-detected ISO omissions
```

Do not amend. Require clean worktree, then run full canonical gate:

```bash
./scripts/test-all.sh
```

Expected total remains 65 children because existing suites gain cases without
adding a suite. Coverage population remains 36 and every committed baseline
ratio must pass. BSim oracle hashes must remain unchanged.

Do not push, merge, open a PR, run hardware, download/rebuild release assets,
flash, or touch GitHub release/tag state.

Return:

- files and behavior changed;
- focused test counts/results;
- both production build results and complete warning classification;
- canonical gate result, coverage totals/population, build contract result,
  and BSim pins;
- commit hash/message and final git status;
- deviations/blockers;
- exact proposed hardware validation step, clearly marked unexecuted.

Stop and escalate without committing incomplete work if source evidence
contradicts this design, timestamp cadence cannot be represented without
inventing another architecture, two materially different implementation/debug
attempts fail, any oracle changes unexpectedly, any warning cannot be
explained, or scope must expand.
