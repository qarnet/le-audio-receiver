# Behavior contract — pre-refactor baseline

Version: T2, 2026-08-01.  Each contract carries a stable ID.  Breaking a contract
without a handoff that updates this document is a regression.

## Bluetooth and service contract (`BT-*`)

### BT-001 — Sink-only BAP Unicast Server

Sink direction only.  Source ASEs are rejected deterministically, not silently
ignored or accepted.  `available_sink_contexts` field in PACS is non-NONE when
an ACL connection exists; `available_source_contexts` is never populated.

**Known gap (T4):** source-direction rejection is asserted by design but lacks
an automated regression test.  T4 must add a BSim scenario that attempts source
ASE configuration and verifies the correct ASCS response.

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

**Known gap (T4):** the decode layer now rejects these shapes
independently (`audio_decode_config()` returns `-EINVAL` without calling
liblc3 for null context, channel count other than 1 or 2, frequency other
than 48000 Hz, frame duration other than 7500/10000 µs, `frames_per_sdu`
other than exactly 1; failed configurations leave the context fully
reset), but translating those rejections into ASCS response codes in
`bt_bap.c` remains T4.

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

## Statistics contract (`STAT-*`)

### STAT-001 — Counter coupling

`audio_stats_frame_decoded()` increments `total_frames` only.
`audio_stats_frame_plc()` increments `plc_frames` and `total_frames`
exactly once each.  `audio_stats_decode_error()`, `audio_stats_i2s_underrun()`,
and `audio_stats_stream_reset()` increment only their own counter — a
decode error never counts as a total frame.  Snapshots are returned by
value; reading them never mutates state.  All counters are atomic and
exact under concurrent access, including `total = decoded + PLC`.

## Stream lifecycle contract (`LIFE-*`)### LIFE-001 — Mono/Mode B stream open

A mono or Mode B stream opens as soon as its single configured ASE enters the
streaming state (QoS configured → enabling → streaming).

### LIFE-002 — Mode A stream open

A Mode A stream opens only after both configured ASEs have entered the
streaming state, independent of the order in which they start.  A partial
open (one ASE streaming, second not) does not pass audio.

### LIFE-003 — Closed-to-open edge

Stream open is a closed-to-open edge event.  It must fire exactly once per
stream lifecycle, not re-triggered by every subsequent ASE start notification.

**Known gap (T5):** current lifecycle unit tests exist but do not exhaustively
prove one-shot semantics across all re-configure/re-start permutations.  T5
must add coverage for duplicate-start protection and edge-counting.

### LIFE-004 — First close wins

The first stop, disable, release, or disconnect closes the audio path and stops
offload.  Subsequent redundant close events are no-ops for the audio path.

### LIFE-005 — Idempotent close

Close, sink stop, and offload stop are idempotent — calling them when already
stopped/closed returns success without side effects.

### LIFE-006 — Late receive after closure

Receive callbacks after the audio path is closed cannot decode, push, update
timing, or restart DMA.  Late packets are safely discarded.

### LIFE-007 — Disconnect reinitializes decoder + lifecycle

Disconnect clears configured/started stream lifecycle and decoder state so
reconnect starts cleanly without reinitializing the I2S peripheral
configuration.  `audio_sink_stop` drops DMA but retains `configured = true`.

## Audio sink and I2S contract (`I2S-*`)

### I2S-001 — Input validation

`audio_sink_push` input is non-null, non-empty, stereo-paired, and exactly
`input_frames` × 2 samples in size.  `input_frames` is a runtime variable set
by `audio_sink_set_input_frames()` (called from `bt_bap.c` at ASE config time).
Malformed input is rejected with observable error.

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

Stop runs: reset timing/drift/actuator/rate-converter/ASRC state and clear
the saved-frame/offload-sequence state, then `i2s_trigger(PREPARE)` before
`i2s_trigger(DROP)`.  The configured flag remains true so reconnect works
without re-calling `audio_sink_init`.  Repeated stop issues no extra triggers
after the first stop; stop trigger errors never flip `configured` and never
cause double frees.

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

### APP-002 — Fatal init/advertise reboot

Fatal initialization or advertising failure requests a cold reboot via
`sys_reboot(SYS_REBOOT_COLD)`.  The watchdog ensures progress if the reboot
path itself hangs.

### APP-003 — Advertising restart on disconnect

Disconnect restarts advertising.  The advertise-restart loop is woken by
disconnect completion and restarts from the same configuration.

### APP-004 — Stable parseable status fields

Status-log fields used by automation (`status`, `stream_summary`, FLPR health,
fault/error counters) remain stable in format and naming.  Changing these
fields breaks gate scripts.

### APP-005 — Warning and error policy

Warnings, assertions, boot errors, and flashing warnings are not normalized as
success.  Recorded SDK diagnostics (Kconfig, CMake, upstream gaps) remain
separately listed with root-cause analysis.  Project-level warnings are fixed
or suppressed with a recorded reason.

## Board and build contract (`BUILD-*`)

### BUILD-001 — NCS version

Both production targets (nRF5340 + nRF54L15) and the central dongle build
against NCS v3.3.0.  Mixing versions is not supported.

### BUILD-002 — nRF5340 SW Split overlays

nRF5340 cpunet applies both the SW Split Kconfig overlay and the SW Split
devicetree overlay through `sysbuild.cmake`.  Without both, the net core stays
on SoftDevice and `bt_enable()` fails.

### BUILD-003 — ACL/ISO buffer agreement

Host (`CONFIG_BT_BUF_ACL_TX_COUNT`, `CONFIG_BT_ISO_TX_BUF_COUNT`) and
controller ACL/ISO buffer counts match per target.  nRF5340: 7 ACL / 6 ISO.
nRF54L15: 3 ACL / 1 ISO.  Mismatch causes `bt_hci_core` warnings and
connection throttling.

### BUILD-004 — nRF54L15 pin and crystal

nRF54L15 I2S pins: D0/P1.4 (BCK), D1/P1.5 (LRCK), D2/P1.6 (SDOUT).  UART20
to SAMD11 USB CDC bridge: P1.9 TX / P1.8 RX.  RF switch pins via fixed
regulator nodes: `rfsw_ctl` on P2.05 `GPIO_ACTIVE_LOW`, `rfsw_pwr` on P2.03
`GPIO_ACTIVE_HIGH`, both `regulator-boot-on`.  PDM20 disabled to prevent
pinctrl conflict.  16 pF crystal configuration via DTS `load-capacitance`
properties.

### BUILD-005 — FLPR memory regions

FLPR source, execution, and shared-ring memory regions are exact and non-
overlapping in the resolved devicetree.  RRAM write-enable is applied before
image loading.

### BUILD-006 — Probe runtime resolution

Probe identity is resolved at runtime via `nrf-probes`.  No static serial↔board
mapping enters source files or documentation.  Doc hygiene: all hardware-
identity claims include the raw evidence they rest on.

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
