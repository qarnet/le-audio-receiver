# Behavior contract — pre-refactor baseline

Version: T8, 2026-08-04 (R7 ownership clarification 2026-08-05).  Each
contract carries a stable ID.  Breaking a contract without a handoff that
updates this document is a regression.

## Bluetooth and service contract (`BT-*`)

### BT-001 — Sink-only BAP Unicast Server

Sink direction only.  Source ASEs are rejected deterministically, not silently
ignored or accepted.  `available_sink_contexts` field in PACS is non-NONE when
an ACL connection exists; `available_source_contexts` is never populated.

**T4 closed:** BSim scenario `unsupported_source_direction` attempts a
source-direction Config against the BSim-only source endpoint and
verifies the exact `CONF_UNSUPPORTED / NONE` response; no slot, decoder,
gate, or push results.  Production remains sink-only with zero source
ASEs; the BSim source PAC exists only to make the rejection reachable
through the real ASCS server (the server refuses source Configs it has
no PAC cap for before the application callback runs).

### BT-002 — PACS capability advertisement

Advertised capabilities: 48 kHz LC3 only, 7.5 ms and 10 ms frame durations,
one or two sink channels, one frame block per SDU.  No alternate codecs, sample
rates, or frame-duration shapes are offered.

### BT-003 — Context persistence

`AVAILABLE_SINK_CONTEXT` (current sink context bitmap) is set at init and does
not revert to `BT_AUDIO_CONTEXT_TYPE_NONE` during ACL connection or streaming.
Available contexts remain truthful through connect/disconnect cycles.

### BT-004 — Advertising data

Extended advertising contains: ASCS UUID, general announcement flag, supported
sink contexts bitmap, no source contexts, and full device name `"LE Audio
Receiver"`.

### BT-005 — Just Works pairing

Just Works Secure Connections pairing is accepted.  MITM is not required
(`CONFIG_BT_SMP_ENFORCE_MITM=n`).  Successful bonds persist through reboot via
ZMS settings storage.

### BT-006 — PACS registration order

PACS service registration runs after `settings_load()` (which restores bond
and GATT database state) and before advertising starts.  Skipping
`settings_load()` breaks PACS characteristic visibility.

### BT-007 — Disconnect cleanup

Disconnect releases the retained connection reference, resets stream lifecycle
state, stops audio/offload, and wakes the advertising restart loop.  No stale
connection reference survives disconnect.

**R7 clarified:** the DISCONNECT teardown is owned by the coordinator
(global close once → lifecycle reset → session reset → stats reset →
cleanup observer), which returns whether the advertising-restart
semaphore must fire; the connection callback handles conn unref /
`default_conn` only.

## Codec and routing contract (`CODEC-*`)

### CODEC-001 — 10 ms frame size

At 48 kHz 10 ms frame duration, `audio_decode_config()` calculates
`samples_per_ch = (frame_us * freq_hz) / USEC_PER_SEC`, yielding 480
samples/channel.  The `lc3_setup_decoder()` call validates the parameters.

### CODEC-002 — 7.5 ms frame size

At 48 kHz 7.5 ms frame duration, `samples_per_ch` = 360 (same calculation).

### CODEC-003 — Mono duplication

A single mono sink ASE is decoded to one PCM channel and duplicated to both
left and right output positions (stride 2, positions 0 and 1).

### CODEC-004 — Mode A stereo routing

Mode A uses two independent mono sink ASEs.  Each ASE provides one channel.
Audio output is emitted only after both channel halves of the current block are
available.  Channels map consistently: left ASE to left output, right ASE to
right output.

### CODEC-005 — Mode B stereo routing

Mode B uses one two-channel sink ASE.  Each SDU contains consecutive `[L_frame]
[R_frame]` payload.  Two independent `lc3_decoder_t` instances decode left
(stride 2, position 0) and right (stride 2, position 1) separately.

### CODEC-006 — Invalid ISO and PLC

Two failure outcomes exist for bad ISO input:

- ISO `BT_ISO_FLAGS_VALID` flag clear → `audio_decode_sdu()` receives
  `valid=false` and passes NULL LC3 data → `lc3_decode()` returns 1 (PLC).
  Counted as `plc_frames` + `total_frames`.  PLC never dereferences frame
  data.  Length semantics: a zero length is accepted (BSim startup
  frames arrive with length 0); any nonzero length must form a valid
  divisible per-channel 20..400 shape, otherwise the call is rejected
  with `-EINVAL` before output or decoder state is touched.
- VALID flag set but LC3 payload malformed → NCS v3.3.0 liblc3 1.1.2
  returns **1 (PLC)** for a malformed bitstream of valid length (verified
  empirically); hard negatives occur only for parameter errors (NULL
  handle, frame size outside 20..400 bytes), which `audio_decode_sdu()`
  pre-validates.  A hard negative is counted exactly once per failed
  decoder invocation as `decode_errors`, never as success, and
  `audio_decode_sdu()` returns a negative errno after completing any
  second Mode B decoder call so independent decoder state stays aligned.

Neither outcome corrupts decoder state for subsequent valid frames.

### CODEC-007 — Configuration rejection

Unsupported frequency, frame duration, channel count, or frame-block shape must
be rejected through ASCS response codes rather than silently guessed or
accepted.  The receiver must never accept a configuration it cannot decode.

**T4 closed:** `bt_bap.c` validates the codec shape before slot
allocation or lifecycle mutation and returns `CONF_REJECTED / CODEC_DATA`
for missing/invalid fields (`CONF_INVALID` is excluded from the ASCS
application response codes; codec ID not LC3 → `CONF_UNSUPPORTED /
CODEC`); the validated shape is stored per sink and Enable re-validates
the retained config against it, failing safely on mismatch.  The
expected negative remote-request paths log at INFO level.  BSim
scenario `invalid_codec_fields` pins nine exact rejections plus two
successful configs (valid mono and the missing-frame-blocks fallback,
proving no slot was consumed).

### CODEC-008 — Safe SDU rejection

`audio_decode_sdu()` rejects with `-EINVAL` before touching output or
decoder state for: null context/output, unconfigured or reset context,
stored unsupported shape, Mode B without a right decoder, `valid=true`
with NULL data, zero valid length, any length above `INT_MAX` (valid and
PLC), valid per-channel frame length outside the liblc3 basic 20..400
byte range, Mode B length not divisible by the channel count (valid and
PLC), and nonzero PLC lengths that do not form a valid divisible
per-channel 20..400 shape.  All length arithmetic happens in `size_t`
before any narrowing cast.  Rejected input never mutates output, decoder
state, or statistics.

### CODEC-009 — Overlap-safe mono expansion

Mono duplication is exact for separate buffers and for in-place expansion
(input and output share the same base).  In-place expansion runs backward
so no unread source sample is overwritten; the production mono decode path
uses the in-place form.

### CODEC-010 — Hard decode failure accounting

Every LC3 decoder invocation is accounted identically: success →
`audio_stats_frame_decoded()`, PLC (1) → `audio_stats_frame_plc()`, hard
negative → `audio_stats_decode_error()`.  Mode B counts both channel
decoder invocations.  A hard negative makes `audio_decode_sdu()` return a
negative errno (`-EBADMSG`); a hard decode failure is never reported as
success.

### CODEC-011 — Exact SDU payload validation

Before any decode/pull/copy, a valid-flag packet whose length does not
match the configured shape (`octets_per_frame × frame_blocks_per_sdu`,
times the channel count for Mode B) increments decode-error evidence
exactly once, never calls liblc3, never mutates left/right pairing
state, and never applies volume or pushes stale PCM.  Mono/Mode B
negative `audio_decode_sdu()` returns skip volume and sink push; Mode A
hard decoder errors skip that half and cannot pair it.  PLC
(`valid=false`) remains supported with the configured byte shape and
may produce concealment output.

### CODEC-012 — Mode A pair identity

Each decoded half tracks its ISO `seq_num` and the ISO SDU reference
time (`BT_ISO_FLAGS_TS`).  The SW Split LL numbers each CIS from a
CIG-global counter, so the two CIS seq spaces carry a constant offset
(their activation delay) that no TX hold can remove — exact seq or
per-half-index pairing cannot match.  Both CISes of one CIG share the
SDU reference time at each event, so equal `half_ts` values pair the
two halves of the same audio frame; a wrap-safe 32-bit comparison
discards only the older unmatched half.  A VALID-flag SDU missing the
TS flag is a fault (skipped half, receive/decode counter increment,
test observer event, real warning); non-valid SDUs (`BT_ISO_FLAGS_LOST`
sync replacements) carry no TS by definition and keep the
concealment/startup-transient path.  Each half's original ISO-valid
flag is stored separately from the decoder result, and the receive path
reports per-push source validity to the test oracle before every sink
push.  Half state clears on configure, start-set completion, gate
close, release, stop, and disconnect.

### CODEC-013 — Release slot semantics

Release without prior Disable closes the audio-path gate before any
later receive callback can decode/push, stops the audio sink
immediately (before returning to ASCS) so the sink oracle finalizes the
segment with a statistics snapshot, stops offload exactly once through
the idempotent APIs, clears pending Mode A halves, clears the released
slot's lifecycle configuration, resets the decoder and app-owned slot
state so the slot is reusable, and preserves truthful PACS contexts.
The `bt_bap_stream` struct itself is left to the ASCS server (it owns
conn/ep/codec_cfg/iso and clears them at the ASE idle transition;
wiping them crashes the streaming-exit transition).  Later
disabled/disconnect paths stay idempotent and must not create a second
segment or hide pushes.

**R7 clarified:** the RELEASE coordinator event checks the session
configured flag first — a duplicate release of an already-cleaned slot
is an observable no-op (no observer, no close, no stats, no stream
touch).  Each slot releases once independently; the second Mode A slot
cleans while the gate is already closed.  The release-driven first edge
fires `release_sink_stop` once, at the same relative order (before the
disconnect cleanup, `rel_ss_seq < disc_seq`).

## Statistics contract (`STAT-*`)

### STAT-001 — Counter coupling

`audio_stats_frame_decoded()` increments `total_frames` only.
`audio_stats_frame_plc()` increments `plc_frames` and `total_frames`
exactly once each.  `audio_stats_decode_error()`, `audio_stats_i2s_underrun()`,
and `audio_stats_stream_reset()` increment only their own counter — a
decode error never counts as a total frame.  Snapshots are returned by
value; reading them never mutates state.  All counters are atomic and
exact under concurrent access, including `total = decoded + PLC`.

## Stream lifecycle contract (`LIFE-*`)

### LIFE-001 — Mono/Mode B stream open

A mono or Mode B stream opens as soon as its single configured ASE enters the
streaming state (QoS configured → enabling → streaming).

### LIFE-002 — Mode A stream open

A Mode A stream opens only after both configured ASEs have entered the
streaming state, independent of the order in which they start.  A partial
open (one ASE streaming, second not) does not pass audio.

### LIFE-003 — Closed-to-open edge

Stream open is a closed-to-open edge event.  It must fire exactly once per
stream lifecycle, not re-triggered by every subsequent ASE start notification.
The closed→open edge also opens sink push admission exactly once
(`audio_sink_stream_open()`), under the fixed lifecycle→sink lock order; a
sink open failure rolls the lifecycle gate back to closed and skips the
one-time open work.

**T5 closed:** `stream_lifecycle_sink_started()` returns true only for a
closed-to-open transition of the audio-path gate; a duplicate start while
the gate is already open returns false, so the caller's one-time open work
(perf reset, offload start, observer event, session receive admission via
`audio_stream_session_rx_open()`) runs exactly once.  The expanded
`tests/unit/lifecycle/` matrix (28 tests after the R6 occupancy narrowing)
pins duplicate starts (single-ASE and Mode A), close-then-start edges,
configure/start/close/reconfigure/start permutations, release-then-slot-
reuse, reset from closed/partial/open states, repeated open/close cycles,
inert unconfigured starts, and the R1 forced-close latch (first-close
observer return, later starts blocked for the configured slot set, one-slot
release does not unblock while another remains, final release + reconfigure
permits open, reset permits fresh open).

### LIFE-004 — First close wins

The first stop, disable, release, disconnect, or shell stop closes the audio
path — and, in the same transition, closes sink push admission
(`audio_sink_stream_close()`, nonblocking) — before any teardown.  Subsequent
redundant close events are no-ops for the audio path.  A shell (forced) close
latches the gate closed for the current configured slot set so later
`stream_started()` callbacks cannot reopen it; releasing the last configured
slot or a full reset clears the latch.

**R7 closed:** one private teardown transition owner in `bt_bap.c`
(`teardown_transition` + `teardown_close_path`) owns every
stop/disable/disabled/release/disconnect/shell-stop composition; the
close primitive's first-edge return (from `stream_lifecycle_audio_path_close()`
or `force_close()`) gates the generation bump, `audio_sink_stop()`,
`audio_offload_stream_stop()`, and the gate-close observer exactly once —
duplicate events have no global side effect.  No callback composes
low-level stop/reset calls.

### LIFE-005 — Idempotent close

Close, sink stop, and offload stop are idempotent — calling them when already
stopped/closed returns success without side effects.  Sequential repeated
`audio_sink_stop()` calls rerun the software (drift/timing/rate/ASRC) resets
but issue no extra I2S triggers after the first PREPARE/DROP.  Overlapping
stop callers share exactly one finalization: one software reset set and one
PREPARE/DROP pair.

**R7 clarified:** the sink is stopped BEFORE the offload on every close path
(the approved R7 order); the R7 coordinator calls `audio_sink_stop()` then
`audio_offload_stream_stop()` once on the first close edge only.

### LIFE-006 — Late receive after closure

Receive callbacks after the audio path is closed cannot decode, push, update
timing, or restart DMA.  Late packets are safely discarded; a push that
reaches the sink after admission closes is rejected with `-EBUSY`.  An RX
callback that passed the lifecycle query before a forced close may finish
decode; when it reaches the sink it is either already admitted (the stop
drains it) or receives `-EBUSY`.  R6 adds a second admission layer in the
audio stream session: every close path calls `audio_stream_session_rx_close()`
(admission off, generation bump, admitted receive leases drained) before any
decoder/assembler/sequence reset, and only `rx_open()` at a successful
gate-open edge re-enables receive admission — config/release/reset never
reopen it.  Serialized BT callbacks retain ownership of Mode A/decoder/
sequence cleanup — the shell thread never clears that state; a forced close
preserves it (R1 policy).

### LIFE-007 — Disconnect reinitializes decoder + lifecycle

Disconnect clears configured/started stream lifecycle and decoder state so
reconnect starts cleanly without reinitializing the I2S peripheral
configuration.  `audio_sink_stop` drops DMA but retains `configured = true`.

**R7 clarified:** the DISCONNECT coordinator event owns the whole teardown
(normal global close once, lifecycle reset under `lifecycle_lock`, session
reset after the drain, stats reset once, disconnect-cleanup observer once)
and returns whether the advertising-restart semaphore must fire; the
connection callback keeps only conn unref / `default_conn` handling.

## Audio sink and I2S contract (`I2S-*`)

### I2S-001 — Input validation

`audio_sink_push` input is non-null, non-empty, stereo-paired, and exactly
`input_frames` × 2 samples in size.  `input_frames` is a runtime variable set
by `audio_sink_set_input_frames()` (called from the audio stream session at
Enable time).
Malformed input is rejected with observable error.

A configured-but-closed sink rejects a valid push with `-EBUSY` and zero
allocation/write/state/counter mutation.  `-EIO` remains the result for a
push before initialization (unconfigured).  Admission is closed by default
after successful init; only a valid BAP gate closed→open transition
(`audio_sink_stream_open()`) restores it, and `audio_sink_stop()` /
`audio_sink_stream_close()` close it.

`audio_sink_set_input_frames()` accepts only the supported frame counts 360
(7.5 ms) and 480 (10 ms).  Any other value — including 0 — safely resets to
480, so the identity path can never copy more than 480 × 2 × 2 = 1920 bytes
into the fixed 481-frame (1924-byte) slab block.  No buffer write may exceed
`BLOCK_SIZE`.

### I2S-002 — Transactional startup pre-fill

On first push, the sink queues six distinct silence blocks (zero-filled,
rate-converter-selected sizes), then the first audio data block, then issues
`i2s_trigger(START)`.  No audio output before START.

Startup is transactional.  For any startup allocation/write/START failure the
sink returns the exact primary failure (`-ENOMEM` for slab exhaustion, the
driver errno for write/trigger failures, `-ENOSPC` when the rate converter
reports an impossible silence count outside [1, 481]) and:

- frees every caller-owned block (failed write or never submitted);
- issues `i2s_trigger(DROP)` to purge previously queued driver-owned blocks
  (driver-owned blocks are never freed directly);
- leaves `started = false`, `configured = true`;
- leaves the slab fully reclaimable after the DROP.

### I2S-003 — Distinct slab ownership

Every `i2s_write` call owns its own distinct slab block.  The same `void *block`
pointer is never passed to `i2s_write` more than once.  Double-write of the
same block to I2S causes DMA corruption on the free slab block.  A failed
`i2s_write` never transfers ownership: the caller keeps (and frees) the block.
A successful write transfers ownership to the driver; the driver releases the
block back to the slab only on DMA completion or DROP/PREPARE purge.

### I2S-004 — Drift controller once per block

Once the DMA stream is started, `audio_drift_controller_update(slab_free)` runs
exactly once per rendered stereo block, in `audio_sink_push`, before slab
allocation.  The controller runs in work/thread context, never ISR.  Nonzero
ppm output is passed exactly once to the clock actuator per block.

### I2S-005 — Emergency repeat fallback

After a successful normal `i2s_write` of audio data, if
`k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD` and a frame was saved
in the current stream (never a zero-length write), a separate slab block is
allocated and `saved_frame` (the most recent successfully written PCM) is
copied into it and queued to I2S.  This repeat fallback provides a safety
margin against DMA starvation; it is counted via
`audio_perf_repeat_fallback()` exactly once per attempted fallback, whether
or not the separate block could be allocated/written.  The repeat block is
always a separate allocation — never the just-written data block.

Slab allocation failure in the main push path is a different event: it logs
`"I2S slab full"`, increments `i2s_underruns`, and returns the allocation error
(`audio_sink_push` returns < 0).

### I2S-006 — `-EIO` recovery

An `i2s_write` returning `-EIO` frees the caller block, records a stream reset
counter, calls `i2s_trigger(PREPARE)` to return the peripheral to READY state,
and marks the stream as not-started so the next push performs a fresh
six-silence pre-fill and re-triggers START.  A non-`-EIO` write error frees the
caller block and keeps the stream started.

### I2S-007 — Stop order

Stop closes push admission first and waits until every admitted push fully
exits (including the emergency repeat fallback), then runs the software
reset: timing/drift/actuator/rate-converter/ASRC state and clears the
saved-frame/offload-sequence state, then `i2s_trigger(PREPARE)` before
`i2s_trigger(DROP)`.  The configured flag remains true so reconnect works
without re-calling `audio_sink_init`.  Repeated stop issues no extra triggers
after the first stop; stop trigger errors never flip `configured` and never
cause double frees.  Overlapping stop callers share one finalization: the
owner drains and resets, every joiner waits, and every caller decrements the
caller count exactly once (the last broadcast wakes a waiting open).
`audio_sink_stop()` never times out and proceeds to DROP against a
still-running push; a drain that cannot complete stays blocked and becomes
visible through the watchdog/test timeout.

### I2S-011 — Admission and drain concurrency

`audio_sink_stream_open()` waits for any overlapping stop cohort (including
the owner's DROP/reset) before enabling admission, so a reopen can never land
between drain completion and DROP/reset.  `audio_sink_stream_close()` rejects
new pushes atomically and returns without waiting.  A push admitted before a
close runs to completion and retains its current return result; every
admitted push decrements the active-push count exactly once through one
common exit (startup and steady failure paths included).  Stop drains all
admitted pushes before finalizing; the fake-driver write gate and the shared
`tests/unit/audio_i2s_common/test_sink_concurrent.c` suite pin the ordering
deterministically (stop drains, two stops finalize once, closed rejection and
reconnect, failure exits release admission, open waits for the full stop
cohort, close is nonblocking).

### I2S-008 — Observable failure counters

Slab exhaustion, underrun count, push failure, repeat-fallback count, and ASRC
capacity-failure count remain observable through log output and stats
structures.

### I2S-009 — ASRC/offload output validation

CPU ASRC output is usable only when the produced frame count is in [1, 481]:
a nominal success with 0 or >481 frames frees the slab block and returns
`-ENOSPC`.  An offload round trip is usable only when it returns success with
`output_frames` in [1, 481]; a zero-frame result (valid FLPR error response)
or an oversized result falls back to CPU ASRC from the unchanged exported
pre-state, and the CPU run overwrites any untrusted offload output.  The
offload post-state import commits ASRC continuity state only after full
validation (valid frame range + accepted import).  The offload sequence
increments exactly once per successfully rendered block — offload success or
CPU fallback — and never on a failed block.

### I2S-010 — Idempotent initialization

`audio_sink_init()` returns 0 immediately when the sink is already
configured — even while streaming — without changing `started`, the saved
frame, input frame selection, ASRC/offload state, slab ownership, or the I2S
queue, without calling device-ready/configure/ASRC-init/actuator-init/
timing-init, and without issuing any DROP/PREPARE (stream control remains
`audio_sink_stop()`'s responsibility).  Initialization from unconfigured
state performs the full normal init exactly once; any first-attempt failure
leaves `configured` false and permits a later retry that performs the full
init.  Re-initialization never resets the negotiated input frame selection.

## Clock and rate contract (`CLOCK-*`)

### CLOCK-001 — nRF5340 clock path

nRF5340 uses identity rate conversion (`audio_rate_convert.c`, conversion ratio
1.0) and APLL actuator (`audio_clock_actuator_apll.c`) that steers HFCLKAUDIO
via register trim.  PCLK frequency measurement is not called on nRF5340.

### CLOCK-002 — nRF54L15 clock path

nRF54L15 uses fixed-point linear ASRC (`audio_asrc.c`) and NONE actuator
(`audio_clock_actuator_none.c`).  The ASRC consumes the drift controller ppm
output directly; no physical clock actuator exists.

### CLOCK-003 — Drift sign convention

Positive local PCLK error (local clock runs faster than controller) produces
negative feedforward correction (`-measured_ppm`).  Phase-error sign follows
the documented convention: `phase_error = PHASE_SETPOINT - slab_free`.

### CLOCK-004 — Clamping and anti-windup

Drift output and integral are clamped to configurable limits.  Directional
anti-windup: at a saturation rail, same-direction phase increments are blocked;
opposite-direction increments are always allowed so the integrator can unwind
toward range.

### CLOCK-005 — Controller reset

`audio_drift_reset()` returns the controller to INIT state and clears frequency
estimate, integral accumulator, and output.

### CLOCK-006 — nRF54L15 timing measurement

nRF54L15 PCLK frequency measurement uses GRTC compare via GPPI to TIMER20
CAPTURE.  SDC/MPSL owns RADIO; timing code never accesses RADIO registers,
events, IRQ, or DPPI publication separately.

### CLOCK-007 — Stale generation guard

Timing work items carry a generation counter.  Stale timing work from a prior
stream generation cannot feed drift with outdated frequency measurements.

### CLOCK-008 — nRF54 timing measurement contract (T5)

The production nRF54 timing path (`tests/unit/timing_nrf54`, 18 tests
compiling `audio_timing_nrf54.c` + `audio_timing_math.c` against mocked
GRTC/GPPI/TIMER HALs) pins: GRTC allocation failure returns the exact error
with no later setup; GPPI allocation failure disables the GRTC compare/
interrupt state via `nrfx_grtc_syscounter_cc_disable()` before freeing the
channel (no GPPI free — the allocation never succeeded); successful init
configures TIMER mode/32-bit/prescaler 0, CLEAR then START, allocates and
enables the GRTC→CAPTURE GPPI connection with the wired event/task addresses,
and is idempotent; updates before init and zero SDU timestamps are no-ops;
the first valid timestamp creates exactly one anchor/compare per session;
past first compares fall back to `now + 1 s` (incl. the 64-bit wrap case);
future anchors schedule `anchor + 1 s`; 32-bit timestamp wrap expands one
epoch ahead; the first compare callback schedules the next compare and
records the baseline without feedforward; the second delivers the exact ppm
incl. TIMER32 wrap; late callbacks reschedule at `now + 1 s`; reschedule
failure clears active, defers exactly one error payload, and delivers no
later measurement; reset clears session state, disables the compare,
increments the generation, and permits one new anchor; work captured before
reset is rejected as stale; and every non-stale measurement reaches
`audio_drift_frequency_error_update()` while diagnostic log pacing does not
suppress feedforward.

**T5 review-fix (FIFO/backlog/overflow):** measurements are published into a
fixed, allocation-free 16-entry FIFO (`diag_fifo`) instead of a single
mailbox, so Zephyr's `k_work_submit()` coalescing while the work item is
pending/running cannot lose one-second feedforward measurements.  The ISR
producer appends measurement and schedule-error payloads in order under
`diag_lock` (bounded critical section, no logging, no dynamic allocation);
the work handler drains every queued payload in FIFO order in one invocation
(a single submission may represent many payloads).  Each payload retains its
generation: stale generations are discarded independently, reset does not
rewrite queued payloads, and fresh-session payloads may follow old payloads
and still deliver.  Logging cadence stays per-payload sequence.  Queue
overflow is an explicit fault, never silent evidence loss: a full FIFO sets
an observable overflow fault, clears `active`, and submits work; the handler
emits one `LOG_ERR` and clears the report; measurement resumes only via a
normal session reset and a new anchor; later callbacks while inactive do
nothing (ISR entry guard).  Generation — not the inactive flag — is the
staleness authority, so accepted payloads queued before a schedule failure
or overflow still deliver.  Tests pin 10-payload backlog FIFO order from one
dispatch, stale-then-fresh mixed generations in one FIFO, and full-FIFO
overflow (16 accepted drained in order, fault observable and consumed,
later callbacks inactive).

### CLOCK-009 — Defined drift arithmetic (T5)

All public drift inputs are defined across the full `int32_t`/`int` range:
the EMA delta/update, the feedforward negation (INT32_MIN valid), phase
subtraction/scaling, proportional term, integral increment/candidate, phase
sum, and final total are computed in `int64_t`; the integral and the final
output are clamped to their configured rails before narrowing.  The slab
count is never silently clamped to a guessed size — arithmetic stays safe
for the full `int` range while preserving sign and final rails.  `tests/unit/
drift/` (29 tests) pins int32/int extremes, exact rail boundaries,
100 000-update long runs at setpoint and both phase extremes, symmetric
feedforward-rail phase unwind, and real-thread concurrent update/frequency/
reset loops with a deterministic final reset; the focused run is clean under
UBSan.  Tuning, signs, first-update behavior, filter ratio, anti-windup, and
clamps are unchanged for normal production inputs.

### CLOCK-010 — APLL actuator conversion (T5)

`audio_clock_actuator_apll.c` converts ppm to APLL register steps with
`offset = (ppm * 10) / 33` (C truncation toward zero), adds the center, and
clamps to `[APLL_MIN, APLL_MAX]` — all in `int64_t` — before narrowing to the
`uint16_t` register value, so `INT32_MIN..INT32_MAX` inputs are defined.
`tests/unit/actuator_apll` (8 tests) pins init/reset center writes, exact
positive/negative conversions, ±1..±3 near-zero truncation, exact MIN/MAX
values, beyond-rail and int32-extreme clamps, and repeated calls; the
no-HFCLKAUDIO variant compiles the
same production file with `NRF_CLOCK_HAS_HFCLKAUDIO=0` and proves all no-op
returns with zero register writes.  `tests/unit/actuator_none` compiles the
production NONE actuator and pins all-zero returns.  The retired
sample-adjust actuator remains under
`tests/unit/actuator_sample_adjust_historical` (historical/retired label)
and is not selectable in production.

## ASRC and FLPR contract (`OFFLOAD-*`)

### OFFLOAD-001 — Shared ASRC contract

cpuapp and FLPR use the same fixed-point linear stereo ASRC state-machine
contract (`audio_asrc.h`).  Both paths produce identical output for identical
input and state.

### OFFLOAD-002 — FLPR transactional commit

FLPR result commits output buffer and post-state transactionally only after
complete validation (input state match, CRC match, payload range check).  No
partial update is visible to cpuapp.

### OFFLOAD-003 — Fallback from pre-state

Any offload failure falls back to cpuapp ASRC from unchanged pre-state.  The
PRESUBMIT state buffer is not modified by a failed or timed-out offload.

### OFFLOAD-004 — Nonblocking start/stop

Stream start enters PREPARING state nonblocking.  Stream stop enters STOPPED
state and invalidates the generation counter so in-flight work items are
recognized as stale.

### OFFLOAD-005 — Distinguished faults

Timeout, full, stale, sequence, frame, CRC, payload, and state faults are
distinguishable and counted exactly once per occurrence.

### OFFLOAD-006 — Bounded recovery

FLPR recovery is bounded to the current five-attempt policy with exponential
backoff.  It does not retry indefinitely.

### OFFLOAD-007 — Probation

Successful recovery enters probation.  100 consecutive successes clear
probation and mark FLPR healthy.  Relapse during probation escalates backoff.

### OFFLOAD-008 — Runtime restart sequence

FLPR runtime restart: hold DMACTIVE enabled, assert reset, copy/flush code +
memory segments, validate CRC, set INITPC, reconnect IPC, set CPURUN, then
release reset as final launch edge.

### OFFLOAD-009 — Ring epoch protection

Ring epochs reject stale slots and stale notifications.  A slot from an
expired epoch is never consumed.

### OFFLOAD-010 — Backpressure isolation

Output-ring backpressure may stall consumption but cannot drop pending input
slots.  FLPR checks output capacity before consuming an input slot; if the
output ring is full or stalled, the input consumer index remains unchanged for
retry on the next poll cycle.  Input and output ring throughput are
independent.

## Initialization and diagnostics contract (`APP-*`)

### APP-001 — Init order

Initialization order is: watchdog, Bluetooth (`bt_enable`), settings
(`settings_load`), volume, BAP (`bt_bap_init` including PACS registration),
I2S (`audio_sink_init`), nRF54L15 FLPR services, advertising.  Deviations from
this order may cause silent failures (e.g., PACS not registered).

**T6 closed:** the order is owned by the production boot coordinator
`src/app_lifecycle.c` (`tests/unit/app_lifecycle/`, 13 tests): exact
all-success order including the optional platform step between sink and
advertising, platform-absent wiring, and every fatal step failing
independently with no later callback and exactly one cold reboot.
`main.c` adapts `bt_enable(NULL)`, `sys_reboot(SYS_REBOOT_COLD)`, and the
subsystem calls to the coordinator's operations structure and retains the
watchdog device/thread and the disconnect/advertising-restart loop.
`settings_load()` stays after Bluetooth enable and before BAP/PACS
registration.

### APP-002 — Fatal init/advertise reboot

Fatal initialization or advertising failure requests a cold reboot via
`sys_reboot(SYS_REBOOT_COLD)`.  The watchdog ensures progress if the reboot
path itself hangs.

**T6 closed:** `app_lifecycle_boot()` and
`app_lifecycle_restart_advertising()` log the step and error, call
`cold_reboot()` exactly once, stop immediately, and return the original
error only if the reboot callback returns.  `main.c` never continues into
normal operation after a nonzero boot/restart result.

### APP-003 — Advertising restart on disconnect

Disconnect restarts advertising.  The advertise-restart loop is woken by
disconnect completion and restarts from the same configuration.

**T6 closed:** restart success invokes only `advertising_start`;
restart failure invokes it once, cold-reboots exactly once, and returns
the original error (unit-tested).

### APP-004 — Stable parseable status fields

Status-log fields used by automation (`status`, `stream_summary`, FLPR health,
fault/error counters) remain stable in format and naming.  Changing these
fields breaks gate scripts.

### APP-005 — Warning and error policy

Warnings, assertions, boot errors, and flashing warnings are not normalized as
success.  Recorded SDK diagnostics (Kconfig, CMake, upstream gaps) remain
separately listed with root-cause analysis.  Project-level warnings are fixed
or suppressed with a recorded reason.

### APP-006 — Boot coordinator operations validation (T6)

`app_lifecycle_boot()` validates the operations structure and every required
callback (all except the optional `platform_init`) before any call: NULL ops
or a missing required callback returns `-EINVAL` without invoking anything
and without rebooting.  `app_lifecycle_restart_advertising()` validates
`advertising_start` and `cold_reboot` the same way.  `platform_init` is
nonfatal and void.

### APP-007 — Shell diagnostic formatting (T6)

The production shell command bodies in `src/audio_shell.c` are executed
directly by `tests/unit/audio_shell/` (13 tests, perf enabled),
`tests/unit/audio_shell_noperf/` (10 tests, perf disabled), and
`tests/unit/audio_shell_nrf54/` (16 tests, FLPR fields) through the real
Zephyr dummy backend and `shell_execute_cmd()` against mocked subsystem
APIs:

- `audio status` prints the exact field order/labels with a zero-safe PLC
  percentage; the percentage numerator is computed in `uint64_t`
  (large `plc_frames`/`total_frames` cannot overflow) preserving integer
  truncation, and zero total frames prints `(0%)` without division.
- `audio perf` prints the exact path labels, zero-count averages, integer
  one-decimal deadline percentage, and the queue fields consumed by
  diagnostics.  With `CONFIG_AUDIO_PERF_MEASUREMENT` disabled the deadline
  is unavailable and the percentage prints a truthful `0.0%` — never a
  value computed against a fake deadline.
- The commands are reachable under their documented names:
  `audio reset-stats`, `audio perf-reset`, `audio stop` (the previous
  `reset - stats` / `perf - reset` spaced syntax strings could not be
  matched by any input word — a T6-found defect, fixed).
- `bt unpair` propagates the exact negative errno and prints
  `bt_unpair failed: <errno>` on failure, `All bonds cleared.` on success.
- nRF54 FLPR status commands print the exact parseable labels consumed by
  `scripts/flpr_hang_gate.py` (handshake health/epoch/error/TX/RX/loss
  fields, ring counters/diagnostics/test/latency/stall, offload
  state/epoch/generation/counters/faults/recovery/probation/runtime
  restart/heartbeat dedup/RTT/last error, ASRC counters/faults/RTT/cycles,
  runtime state/stage/requests/epochs/reload/CRC/errno/duration and
  DMCONTROL/INITPC/CPURUN readbacks).
- `flpr runtime` state/stage conversion is bounded: out-of-range enum
  values print `UNKNOWN` / `unknown` instead of indexing past string
  arrays (a T6-found defect, fixed with switch-based helpers).
- The `flpr restart` success line (`FLPR restart OK: epoch <old>→<new>
  crc=0x… duration=total … ms`) is locked by the production test and by
  the gate-parser suite `tests/unit/flpr_hang_gate/` (10 tests): the
  gate's `RE_RUNTIME_RESTART_OK` matches the exact production line and
  extracts old/new epoch, CRC, and duration, while partial and stale
  forms produce no false recovery observation (a T6 review-fix defect,
  fixed).
- Narrow `AUDIO_SHELL_TEST`-guarded wrappers expose otherwise-static
  command handlers to the test suites only; they never enter production
  firmware.

## Board and build contract (`BUILD-*`)

### BUILD-001 — NCS version

Both production targets (nRF5340 + nRF54L15) and the central dongle build
against NCS v3.3.0.  Mixing versions is not supported.

### BUILD-002 — nRF5340 SW Split overlays

nRF5340 cpunet applies both the SW Split Kconfig overlay and the SW Split
devicetree overlay through `sysbuild.cmake`.  Without both, the net core stays
on SoftDevice and `bt_enable()` fails.

**T6 closed:** `scripts/check-build-contract.py` proves both halves on the
resolved netcore image: `CONFIG_BT_LL_SW_SPLIT=y` with peripheral/connection
ISO enabled in `hci_ipc/zephyr/.config`, and chosen `zephyr,bt-hci`
resolving to an okay `bt_hci_controller` node compatible with
`zephyr,bt-hci-ll-sw-split` while `bt_hci_sdc` is disabled in
`hci_ipc/zephyr/zephyr.dts`.

### BUILD-003 — ACL/ISO buffer agreement

Host (`CONFIG_BT_BUF_ACL_TX_COUNT`, `CONFIG_BT_ISO_TX_BUF_COUNT`) and
controller ACL/ISO buffer counts match per target.  nRF5340: 7 ACL / 6 ISO.
nRF54L15: 3 ACL / 1 ISO.  Mismatch causes `bt_hci_core` warnings and
connection throttling.

**T6 closed:** the checker asserts the exact counts on both resolved app
configs, the netcore controller counts equal to the nRF5340 app host values,
and (nRF54L15) host ISO TX equal to
`CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT`.

### BUILD-004 — nRF54L15 pin and crystal

nRF54L15 I2S pins: D0/P1.4 (BCK), D1/P1.5 (LRCK), D2/P1.6 (SDOUT).  UART20
to SAMD11 USB CDC bridge: P1.9 TX / P1.8 RX.  RF switch pins via fixed
regulator nodes: `rfsw_ctl` on P2.05 `GPIO_ACTIVE_LOW`, `rfsw_pwr` on P2.03
`GPIO_ACTIVE_HIGH`, both `regulator-boot-on`.  PDM20 disabled to prevent
pinctrl conflict.  16 pF crystal configuration via DTS `load-capacitance`
properties.

**T6 closed:** the checker asserts on the resolved app DTS: `i2s20` okay
with `clock-source = "PCLK32M"` and exact default pin cells
SCK P1.4 / LRCK P1.5 / SDOUT P1.6 / MCK P1.7 (function, port, pin decoded
from the NRF_PSEL cells); PDM20, SPI00, and MX25R64 disabled; TIMER20
reserved; `rfsw_ctl` = `<&gpio2 5 1>` and `rfsw_pwr` = `<&gpio2 3 0>` with
`regulator-boot-on`; LFXO and HFXO `load-capacitors = "internal"` with
exactly 16000 fF.

### BUILD-005 — FLPR memory regions

FLPR source, execution, and shared-ring memory regions are exact and non-
overlapping in the resolved devicetree.  RRAM write-enable is applied before
image loading.

**T6 closed:** the checker asserts the exact app-side ranges on the resolved
app DTS (cpuapp SRAM `0x20000000`+`0x28000`, RX `0x20028000`+`0x2000`,
TX `0x2002A000`+`0x2000`, PCM ring `0x2002C000`+`0x4000`, FLPR execution
SRAM `0x20030000`+`0x10000`, FLPR code partition `0x165000`+`0x18000`),
that the SRAM intervals are contiguous in the designed order, non-
overlapping, and within physical `0x20000000..0x20040000`, and cross-checks
the FLPR image's resolved memory/chosen/code-partition values
(`cpuflpr_sram`, chosen `zephyr,sram`/`zephyr,code-partition`,
`CONFIG_FLASH_BASE_ADDRESS`/`CONFIG_FLASH_LOAD_SIZE`) against the
app-side launcher ranges.

### BUILD-006 — Probe runtime resolution

Probe identity is resolved at runtime via `nrf-probes`.  No static serial↔board
mapping enters source files or documentation.  Doc hygiene: all hardware-
identity claims include the raw evidence they rest on.

### BUILD-007 — Resolved build contract checker (T6)

`scripts/check-build-contract.py` (stdlib only, deterministic PASS/FAIL
report, all failures listed in one run, exit 0 only when every contract
passes) parses the resolved `.config` and `zephyr.dts` beneath each sysbuild
root — app image, nRF5340 `hci_ipc` controller image, and nRF54L15 `flpr`
image — and asserts the contracts above plus the nRF5340 app path
(identity resampler + APLL, no ASRC/NONE, 48000 Hz output, LIBLC3, two sink
ASEs, `I2S_NRFX_ALLOW_MCK_BYPASS`, 7/6/6 host counts, `i2s0` okay with
12.288 MHz HFCLKAUDIO and exact BCK P1.15 / LRCK P1.12 / SDOUT P1.13 pins,
QSPI disabled, WDT0 okay) and the nRF54L15 app path (ASRC linear + NONE
actuator, no APLL/identity, offload ASRC, 47619 Hz output, LIBLC3, two sink
ASEs, 3/1/1/3 host/controller counts).  The sysbuild app image directory is
named after the application source directory basename, so the checker
resolves it from each root's `domains.yaml` (`default:` image; missing file
or missing default is a hard error); `hci_ipc`/`flpr` domain names are
fixed.  Missing, duplicate, unreadable, or
malformed required inputs are hard failures; comments can never satisfy a
DTS assertion.  **R8:** the checker additionally asserts the acceptance
diagnostics parity — nRF5340 app `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`
not enabled (`5340-029`), nRF54L15 app `CONFIG_AUDIO_ACCEPTANCE_
DIAGNOSTICS=y` (`54l15-035`) and FLPR image `CONFIG_FLPR_ACCEPTANCE_
DIAGNOSTICS=y` (`54l15-036`) — 76 → **79 assertions**.  `tests/unit/
build_contract/` (33 tests) covers a complete
valid dual-target fixture, every hard-input class, explicit unset vs set
symbols, comment-only satisfaction attempts, wrong node status/compatible/
chosen/pins/counts/polarity/capacitance, missing/overlapping/out-of-range
memory intervals, SW Split Kconfig-only and DTS-only half failures, an
alternate sysbuild default-domain name, a missing `domains.yaml`, and the
deterministic multi-error report with nonzero exit.

### BUILD-008 — 48 kHz capability proof split (T6)

The resolved half of the 48 kHz contract (output sample rate 48000,
`CONFIG_LIBLC3=y`, two sink ASEs) is proven from resolved build data by the
checker; the checker explicitly does NOT claim the PACS LC3 frequency LTV
is a `.config`/DTS property (it is a C object in `src/bt_bap.c`).  The
exact advertised capability (48 kHz, 7.5+10 ms, 1+2 channels) remains
pinned by the direct production-source T2/T4 tests (BT-002).  A small
source-contract check (SRC-001/002, clearly labeled source, not resolved)
pins `BT_AUDIO_CODEC_CAP_FREQ_48KHZ` and the exact 48000 Hz acceptance gate
in `src/bt_bap.c`.

## Explicitly unsupported

These configurations are unsupported and must be rejected rather than silently
accepted:

- Sampling rates other than 48 kHz.
- More than two sink channels.
- More than one frame block per SDU.
- Source (transmit) ASEs.
- CAP/CAS, TMAS, CSIP, extra codecs, A2DP, or phone interoperability.
- Analog audio-fidelity guarantees.
- Arbitrary malformed LC3 recovery beyond safe rejection or PLC.
- nRF54L15 recovery from APPROTECT lock (no recovery path exists in current
  tooling; APPROTECT is not the nRF5340 soft-branch design).

## Coverage and test-matrix gate contract (`CV-*`)

Version: T8, 2026-08-04.  Enforced by `scripts/test-coverage.sh` (default
mode) and `scripts/check-test-matrix.py --coverage-json`, both ordered
children of `scripts/test-all.sh`.

### CV-001 — Numeric coverage never decreases

The committed `tests/coverage-baseline.json` (schema v1, generated on the
clean commit `971e6a4`, committed in `1a5842d`; lines 3281/3722, branches
1433/2041, functions 205/205 in the 26-file numeric population) is enforced
with integer cross multiplication:
`current_covered/current_total >= baseline_covered/baseline_total` for the
overall lines and branches totals and for every per-file lines, branches,
and functions record.  The recorded gcovr/gcov versions are also enforced:
in baseline mode the current tool version first lines must equal the
baseline's `gcovr_version`/`gcov_version` when those fields are present
(old baselines that omit either field remain accepted); a mismatch is a
hard error directing an intentional `--write-baseline` refresh.
`--report-only` never enforces versions.  Lowering the baseline is a
regression.

### CV-002 — Population drift is a hard failure

Every file in the baseline population must still be in the manifest
numeric population and vice versa.  Adding a direct source requires an
intentional manifest + baseline update; removing one requires the same.
No automatic exclusion exists.

### CV-003 — Every compiled production function executes

`check-test-matrix.py --coverage-json` reports a zero-hit function error
for any compiled production function (test-only blocks excluded via
`GCOVR_EXCL_START`/`GCOVR_EXCL_STOP` markers are not production metrics)
that does not execute at least once across the native suites.  Precise
`function_exclusions` require a reason and hardware/structural evidence.

### CV-004 — Public API outcome ledger

Every top-level non-static function definition in a direct source must
appear in `public_outcomes` with an exact observable outcome — `0`/success,
an exact negative errno, an exact enum/status constant, `true`/`false`,
`void`, or an exact hex/string literal.  `error-class` and vague labels are
forbidden, as are duplicate `(api,outcome)` records and empty placeholders.

### CV-005 — State transitions

Stateful entries (`stateful: true`) must carry a nonempty, duplicate-free
`from->to` transition list with witnesses; stateless entries must not carry
transitions.

### CV-006 — Witnesses are real

Every witness string must exist in the referenced test source (or name an
existing hardware script/evidence doc for hardware-dependent outcomes).
Invented witnesses fail the gate.

### CV-007 — Worktree hygiene

Baseline write and enforcement require a clean worktree; the run manifest
records the exact `HEAD` and `dirty` state.  `--report-only` may run dirty
but can never create or update a committed baseline, and the output
directory is only ever cleaned with `--clean-output` inside an allowed
`/tmp` or `$HOME` tree.
