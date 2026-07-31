# Behavior contract — pre-refactor baseline

Version: T0, 2026-07-31.  Each contract carries a stable ID.  Breaking a contract
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
  Counted as `plc_frames`.
- VALID flag set but LC3 payload malformed → `lc3_decode()` returns a negative
  error code.  Counted as `decode_errors`.

Neither outcome corrupts decoder state for subsequent valid frames.

### CODEC-007 — Configuration rejection

Unsupported frequency, frame duration, channel count, or frame-block shape must
be rejected through ASCS response codes rather than silently guessed or
accepted.  The receiver must never accept a configuration it cannot decode.

**Known gap (T4):** current production code does not independently validate
remote codec configuration; it accepts whatever the remote sends.  T4 must add
explicit rejection tests and, if needed, a config-validation path in
`bt_bap.c`.

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

### I2S-002 — Startup pre-fill

On first push, the sink queues six distinct silence blocks (zero-filled), then
the first audio data block, then issues `i2s_trigger(START)`.  No audio output
before START.

### I2S-003 — Distinct slab ownership

Every `i2s_write` call owns its own distinct slab block.  The same `void *block`
pointer is never passed to `i2s_write` more than once.  Double-write of the
same block to I2S causes DMA corruption on the free slab block.

### I2S-004 — Drift controller once per block

Once the DMA stream is started, `audio_drift_controller_update(slab_free)` runs
exactly once per rendered stereo block, in `audio_sink_push`, before slab
allocation.  The controller runs in work/thread context, never ISR.

### I2S-005 — Emergency repeat fallback

After a successful normal `i2s_write` of audio data, if
`k_mem_slab_num_free_get(&i2s_slab) >= DRIFT_THRESHOLD`, a separate slab block
is allocated and `saved_frame` (the most recent successfully written PCM) is
copied into it and queued to I2S.  This repeat fallback provides a safety margin
against DMA starvation; it is counted via `audio_perf_repeat_fallback()`.

Slab allocation failure in the main push path is a different event: it logs
`"I2S slab full"`, increments `i2s_underruns`, and returns the allocation error
(`audio_sink_push` returns < 0).

### I2S-006 — `-EIO` recovery

An `i2s_write` returning `-EIO` records a stream reset counter, calls
`i2s_trigger(PREPARE)` to return the peripheral to READY state, and marks
the stream as not-started so the next push re-pre-fills and re-triggers START.

### I2S-007 — Stop order

Stop runs: reset timing/drift/actuator/rate-converter/ASRC state, then
`i2s_trigger(PREPARE)` before `i2s_trigger(DROP)`.  The configured flag
remains true so reconnect works without re-calling `audio_sink_init`.

### I2S-008 — Observable failure counters

Slab exhaustion, underrun count, push failure, repeat-fallback count, and ASRC
capacity-failure count remain observable through log output and stats
structures.

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
