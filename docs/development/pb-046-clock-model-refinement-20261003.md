# PB-046: clock-driven sink refinement

## Scope

Implement additive native software-model acceptance through real production
drift, ASRC, sink, rate converter, NONE actuator, offload manager and FLPR
processor. A suite-local I2S driver owns a finite descriptor FIFO and consumes
words from an independently configured virtual PCLK-derived output clock.
It releases descriptors only when clock budget covers their actual frame count,
including while a producer waits on a full queue. No one-release-per-push pacing.

No hardware timing, RF, analog, presentation or physical FLPR acceptance follows
from this model. No 360-frame FLPR feature, codec references or LC3plus is needed.
Existing canonical 17/26, coverage baseline and physical limits stay unchanged.

## Load-bearing implementation facts

- `src/audio_drift.c` has public frequency-error and per-block slab-free inputs,
  a 6-free-block phase target, gains 0.300/0.050, 50 ppm/block scaling and 1 Hz
  frequency-feedforward filtering through an eight-sample EMA.
- Production board configuration selects output clamp 2000 ppm and phase-integral
  clamp 150 ppm. Import these assignments from the board conf into the native
  test build rather than silently using existing drift unit defaults 500/500.
- `src/audio_i2s.c` has 16 slab blocks, fourteen startup silence writes plus data,
  and a 15-descriptor physical TX queue contract. First startup uses 0 ppm; the
  first subsequent controller call transitions INIT to ACTIVE with 0 ppm.
- The real sink owns exported continuity, remote result commit and CPU fallback.
  The PB-045 native processor peer can be adapted without mocking ASRC/drift.
  Ring transport remains an explicitly modeled boundary, not physical IPC proof.
- Independent waveform coordinates must not read production phase/history.
  Use authored distinct stereo input and a separate global-coordinate model;
  actual applied public ppm is a model input, not expected PCM from the DUT.

## Clock and event contract

Reference clock is virtual microseconds. Source cadence is 480 frames per nominal
10000 us or 360 per 7500 us, with separately declared source-clock error. Output
word rate is `47619 * (1000000 + pclk_error_ppm) / 1000000` stereo frames/second.
Independent integer frame credit derives transferred words from elapsed reference
time, not source block completion or produced-count target.

Run the FIFO clock from a Zephyr timer independent of source calls. Read elapsed
reference time through public ticks-to-microseconds conversion. Record actual
timer granularity, credit, transfer count and event times. Bound and label sampling
uncertainty; do not claim exact physical wire phase from timer callbacks.
Frequency measurements arrive at 1 Hz from the declared clock relationship in
thread context, never by calling drift from the timer ISR.

Source arrivals use absolute scheduled times. Separate stimuli: constant PCLK
skew, source skew, PCLK step, deterministic bounded arrival jitter, one omitted
timed input and declared processing delay. Omitted input is a missed source push,
not a claim to test ISO loss/LC3 PLC. Its input/event ledger must show omission.

## Declared initial envelope and derivation

PCLK cases: 0 and +/-1000 ppm, including a 0-to-1000 step. Source cases: 0 and
+/-100 ppm. These leave authority inside production output clamp and the 150 ppm
phase-integral limit; no full-clamp physical operating guarantee is asserted.
Jitter is separately bounded to +/-1000 us; processing-delay stimuli to 2000 us,
below either input interval. Do not merge jitter, omitted input and skew labels.

Initial horizon is 1000 virtual seconds; settling boundary 900 seconds. At the
worst selected source offset, phase-integral headroom is 150-100=50 ppm, about
2.38 frames/second. Removing roughly four 480-frame startup blocks at that lower
authority takes about 807 seconds, plus filter/ramp/quantization margin. A short
run cannot validate this loop merely because its reservoir has not drained.

After settling, post-push descriptor depth must stay in 10..12: pre-push target
is 16-6=10 descriptors, one newly submitted descriptor gives target 11, with one
descriptor of phase quantization allowance. Check the whole settled trajectory,
not final depth only. Throughout positive cases, enforce finite 15-descriptor
ownership, no underflow, duplicate ownership or emergency-repeat acceptance.

For each frame geometry, derive min/max output block lengths from quantized
nominal rate and configured ppm rails. Final-window production-versus-independent
clock-transfer difference is bounded by the allowed queued-word spread, including
partial current descriptor, not an invented blanket ppm tolerance. Check exact
frame conservation and every emitted startup/data sample separately.

The same +/-1000 ppm plant with correction disabled must fail intended queue,
rate/deadline or repeat-continuity criterion. No weaker substituted disturbance.
Stop, reopen, reset and supported CPU/remote/fallback transitions must restart
public output correctly, with timer/descriptor cleanup and no retained ownership.

## Preliminary research, not acceptance

An external Zephyr prototype links real public drift and ASRC, uses production
2000/150 tuning and an independent exact integer word-clock budget. Its exploratory
FIFO capacity is slab count 16, not the final 15-descriptor driver. It has no
production sink, no full waveform oracle and no asynchronous DMA; results cannot
satisfy PB-046 acceptance. It checks feasibility and settling duration only.

At 1000 seconds, selected positive 480/360 cases with 0/+/-1000 PCLK and
0/+/-100 source ppm stayed finite, with settled post-push depths 10..12, including
one missed input. Correction-disabled +/-1000 cases failed queue constraints
before the settled window. The final driver must handle the stricter 15-entry
queue, including short capacity waits while the independent clock continues.

Successful diagnostic build/run logs:
`/tmp/opencode/pb046-clock-research-build-r3.log`, SHA-256
`48dd895db8eb90e8f120e3a5a0a5d658f77fb07d1af430e233eb1b4f034d9740`;
`/tmp/opencode/pb046-clock-research-run-r3.log`, SHA-256
`2533b5476176e9309c22b08d854a984429807392ae7bdcdfdb9906c6a2e6150e`.
The first attempt retained fortify-at-O0 diagnostics and did not exit its idle
native application; it is not acceptance. Later builds remove only incompatible
native fortify modes using existing repository policy and explicitly exit.
All prior logs remain retained. Availability must be rechecked on restart.

## Smallest public-boundary regression

Authored timed input -> real `audio_sink_push()` -> real correction/conversion
and offload/fallback -> exact I2S API words -> independent clock consumption ->
finite queue feedback for the next real controller update. Disabling correction
on the same plant must change that public result from accepted to rejected.
