# PB-046: independent timed sink feedback

This exec-only native suite is discovered automatically by unit and coverage
gates. It links real production sink, drift, ASRC, rate converter, NONE actuator,
offload manager and FLPR processor. Board drift tuning is imported at configure
time. Transport and platform timing are explicitly modeled; no physical FLPR,
IPC, RF, codec, I2S electrical, analog or presentation acceptance is claimed.

`clock_i2s.c` is a real native device behind the public I2S API. A timer consumes
live descriptor RAM according to independent elapsed-reference-time/PCLK frame
credit. FIFO capacity is 15; completion returns actual slab ownership even while
producer waits. No one-block-per-input completion shortcut is used. Rate steps
account old-rate credit and install the new rate atomically, returning the exact
model transition timestamp. Partial frame credit survives the step.

`test_loop.c` generates distinct authored stereo input and independently evaluates
global Q32 coordinates. It never reads DUT phase/history. The write-side oracle
generates each descriptor's expected words independently, checks the complete
submitted PCM/count, and stores expectations for later timed comparison against
live RAM. A post-submission mutation negative proves the timer reads consumed
words instead of trusting enqueue snapshots. Transferred, remaining and cancelled
words account for every submitted frame, including final stop.

Each required stability plant runs 1000 virtual seconds, with whole-trajectory
settled checks after 900 seconds. Separate cases cover source/PCLK skew, clock
step, jitter, omitted timed input, processing delay and remote faults; a mixed
360-frame case exercises current CPU fallback. Omission models a missed source
push, not ISO loss or PLC. Queue bounds and window-rate allowance derive from
production target/geometry, not frozen hardware-limit changes.

Enabled/disabled causal pairs share exact plant, input and schedules; only applied
correction changes. Disabled plants must fail intended repeat/deadline/queue/rate
criterion, not an unrelated waveform/setup error. Each remote fault must show
the exact public fault/fallback category, bounded ACTIVE return and subsequent
remote success. Permanent notify failure must fail recovery even though CPU PCM
remains correct. A late-success boundary test exercises the deadline decision
used by real integration. Modeled timeout returns immediately and does not prove
physical IPC timeout latency.

Lifecycle tests check stop-idle clock quiescence before any helper reset, timed
producer backpressure overlapping bounded stop, closed admission and fresh
startup/data after reopen without plant/private module reset. Both producer and
stop are supervised; stuck completion terminates the owned native test process
with failure rather than hanging the gate. Short reopen runs are lifecycle checks,
not replacement of required long stability horizons.

Six test methods currently cover the matrix and controls. Raw output starts with
`CLOCK_BEGIN` inputs, imported `CLOCK_TUNING`, explicit `CLOCK_EVENT` boundaries,
lossless compact `R` source-event records and final `CLOCK_RESULT` verdicts.
`CLOCK_ROW_FIELDS` declares the record columns: source sequence, actual arrival,
enqueue and clock-observation timestamps, produced frames, applied ppm, queued
descriptors and total transferred frames. Input pattern, control/clock equations
and source timing recipe are source-pinned. All per-event checks remain active;
compact printing does not sample away history or violations.

The native reference tick is 500 us; actual timestamps are retained. These clocks
are software-model clocks, not calibrated hardware timestamps. Long suites can
wrap Zephyr's 32-bit cycle-based displayed summary duration; use 64-bit reference
timestamps and per-scenario horizons for model time, not summary duration.
Wall-clock runtime in focused execution was about 209 seconds. Do not shorten
horizons, omit scenarios or relax bounds to fit CI; investigate ordinary failures.

## Envelope and horizon rationale

Declared PCLK cases are 0 and +/-1000 ppm, including a step; source cases are
0 and +/-100 ppm. Separate jitter is +/-1000 us and processing delay 2000 us.
At worst selected source skew, the 150 ppm integral rail leaves 50 ppm authority,
about 2.38 frames/s. Removing four 480-frame startup blocks takes about 807 s,
which motivates 900 s settling plus a 1000 s horizon. These are modeled stimuli,
not physical full-clamp guarantees. After settling, post-push depth 10..12 comes
from 16 slabs minus the 6-free target, plus one submitted descriptor and one
descriptor of quantization allowance. Window-rate bounds use queued-word spread,
including partial current descriptor, not a blanket ppm tolerance.
