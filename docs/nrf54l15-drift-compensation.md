# nRF54L15 Clock Drift Compensation — Options Analysis

> **SUPERSEDED (2026-07-05)** by [`design.md`](design.md). The analysis below
> is absorbed there (Part II §Clock recovery and Appendix A), and the option
> dispositions were decided: E → Phase 4, A → Phase 5, B → Phase 6 (gated),
> C/D rejected, F/G backlog. Kept for reference only — do not update.

## Problem statement

I2S sample clock (derived from HFPLL/HFXO) and BLE ISO clock are independent
oscillators. Difference of 10–200 ppm causes I2S DMA queue to drain or fill over
time, eventually triggering underrun/overrun.

On nRF5340 we trimmed HFCLKAUDIO APLL in software. **nRF54L15 has no APLL**:

- `NRF_CLOCK_HAS_HFCLKAUDIO == 0` (confirmed by MDK header).
- HFXO has `TASKS_XOTUNE` but it is a one-shot calibration to nominal, not a
  runtime-adjustable trim.
- HFPLL frequency is fixed at boot (locked to HFXO).

So we cannot trim the clock driving I2S. We must either:

1. **Bridge the rate mismatch in software** (resample, repeat/drop).
2. **Use an externally-clocked I2S** (slave mode + adjustable external clock).
3. **Tolerate the artifact** (packet repeat fallback, current behaviour).

---

## Available silicon primitives

| Block | Capability | Why it matters |
|-------|-----------|---------------|
| **GRTC** | 12 capture/compare channels, PCLK-clocked (~7.8 ns resolution) | Hardware timestamping of I2S + radio events |
| **I2S20** | `FRAMESTART` event publishable to DPPI; supports master and slave mode | Can be triggered/measured by hardware |
| **DPPIC + PPIB** | Cross-domain peripheral interconnect | Routes I2S/RADIO events to GRTC capture without CPU |
| **PWM (PWM20–22)** | HW frequency generator with DPPI integration | Could drive I2S BCLK in slave mode |
| **FLPR (cpuflpr)** | RISC-V VPR @ 128 MHz, VIO real-time GPIO (1-cycle = 7.8 ns), independent of cpuapp | Co-processor for ASRC, bit-banged clocks, or measurement loop |
| **External pin** | Any GPIO can wire physically to any I2S input pin | Loopback to feed external clock into I2S20 |

Important constraints:

- **FLPR cannot access I2S20 directly** — I2S is in cpuapp domain. FLPR has GPIO (VIO), GRTC, GPIOTE, TWIM, UARTE, DPPIC.
- **No internal signal routing exists from PWM/GPIO output to I2S clock input** — physical pin-to-pin wire required if I2S is slave.
- HFXO baseline accuracy on the DK is typically ~10–20 ppm (good crystal). Drift dominantly comes from BLE peer device, not from us.

---

## Drift signal: GRTC + DPPI hardware measurement

This is the foundation for any of the options below. Replaces the noisy
queue-depth heuristic we use today.

```
RADIO event (BLE ISO RX END)  ──┐
                                ├─→ DPPI ch X ──→ GRTC.SUBSCRIBE_CAPTURE[0]
I2S event (FRAMESTART)        ──┘                  GRTC.SUBSCRIBE_CAPTURE[1]
```

After N seconds:
- `cc[0]` — last BLE event timestamp
- `cc[1]` — last I2S frame timestamp
- Their *delta-of-deltas* over N → exact ppm offset

CPU cost: zero during measurement. Periodic ISR computes drift from captured
values. Far better SNR than queue-depth (no aliasing with jitter).

---

## Options

### A. Software ASRC on cpuapp

- Polyphase resampler or simpler linear/cubic interpolation between samples
- Drift-driven ratio adjustment (from GRTC measurement)
- Output to I2S DMA at locally-correct rate

**Pros**: no hardware change, transparent, works across all drift ranges.
**Cons**: 5–15% extra cpuapp load at 48 kHz stereo (linear interp cheap;
polyphase higher quality but more CPU). Adds 1–2 ms latency.

### B. Software ASRC on FLPR (offload)

Same algorithm as A, runs on cpuflpr instead.

```
cpuapp:  BLE → LC3 decode → shared buffer (input)
cpuflpr: shared input → ASRC → shared output
cpuapp:  shared output → I2S DMA
```

- FLPR computes drift from GRTC captures (FLPR can access GRTC)
- IPC via shared SRAM region, signal via VEVIF or icmsg
- cpuapp untouched; FLPR ~30% utilized

**Pros**: zero cpuapp impact (cpuapp's BT RX thread already at limits during LC3).
Quality identical to A. FLPR is otherwise idle.
**Cons**: more code (RISC-V build target, IPC setup, shared memory layout).
FLPR has no FPU — fixed-point ASRC required. Adds one extra buffer copy
(input→ASRC→output) so latency +1 frame (~10 ms).

### C. FLPR generates I2S BCLK/LRCK via VIO bit-banging

- I2S20 in slave mode (`I2S_OPT_BIT_CLK_SLAVE | I2S_OPT_FRAME_CLK_SLAVE`)
- FLPR runs a tight loop toggling two GPIOs (BCLK + LRCK)
- 128 MHz / 3.072 MHz (BCLK) = 41.67 cycles per edge → fractional via dithered
  toggle counts (e.g., 5× 42 cycles + 1× 41 cycles per pattern)
- Frequency adjustment: change the dither pattern → fractional ppm precision
- **Requires physical wire** from FLPR GPIO pins to I2S20 BCLK + LRCK input pins

**Pros**: real clock control, no software resampling, low cpuapp load.
**Cons**:
- Any FLPR interrupt or memory stall = audible audio glitch (clock jitter)
- FLPR must run uninterrupted, which conflicts with running ASRC in B
- Loop-only programming model on FLPR — no `wait_for_event` style
- Physical jumper wires needed on the DK (P1.x → P1.y)
- Verdict: clever, fragile in practice

### D. PWM peripheral as adjustable clock source

- PWM20 generates BCLK + LRCK at exact frequency
- Output pinned to GPIO, **physically wired** to I2S20 BCLK/LRCK inputs (slave mode)
- Frequency adjusted from cpuapp (or FLPR) via `PRESCALER` and `COUNTERTOP`
- DPPI-triggered updates possible

**Pros**: PWM is hardware → essentially zero jitter. cpuapp barely involved
once configured.
**Cons**:
- Frequency resolution limited by PWM timer step (~PCLK / N)
- Still needs physical wire(s)
- LRCK must be precisely 1/64 of BCLK for I2S — synchronizing two PWM outputs
  requires DPPI choreography

### E. Adaptive single-sample insert/drop (driven by GRTC)

- Measure exact drift via GRTC every M frames
- When accumulated drift exceeds 1 sample period (~20.8 µs at 48 kHz):
  - Drift positive → insert one sample (zero-stuffing or repeat last)
  - Drift negative → drop one sample
- Glitch is one sample, not one frame. Inaudible at typical drift rates.

**Pros**: minimal CPU, no hardware change, no FLPR needed.
**Cons**: still has minor artifacts (one-sample discontinuity), but
~10000× less than current 10 ms packet repeat.

### F. External programmable oscillator (Si5351, MAX9485, CS2200)

- Add an I²C-controlled fractional-N oscillator chip on the PCB
- Generates 12.288 MHz MCLK directly to the I2S DAC
- I2S20 follows as slave
- Drift correction = single I²C register write

**Pros**: industrial-grade. CS2200 is purpose-built for audio clock recovery
(<1 ppb resolution, used in Tesla, Sonos, etc.). Zero CPU after setup.
**Cons**: hardware change. Adds ~$2 BOM cost. Useless for prototype unless
PCB respin happens.

### G. Buffer crossfade smoothing

- Today: when queue drains, repeat last frame (10 ms hard glitch)
- Better: crossfade two adjacent frames over a 5 ms window
- Reduces audibility of the artifact

**Pros**: trivial to implement, no measurement needed.
**Cons**: still glitches, just less audibly. Doesn't address root cause.

---

## Recommended approach

**Phase 1 — ship now (zero new hardware):**

Combine:
- **D-foundation: GRTC + DPPI drift measurement** — replaces queue-depth heuristic
- **Option E: single-sample insert/drop** — driven by GRTC drift signal

Effort: 1–2 days. No FLPR, no extra wires, no PCB change. Glitches reduced from
10 ms (one frame) to 20 µs (one sample), at the same correction rate.

**Phase 2 — better quality if Phase 1 is audible:**

Add:
- **Option B: FLPR-based linear-interp ASRC** with the same GRTC drift signal

Effort: ~1 week. Eliminates audible artifacts entirely. Justifies having the
FLPR onboard — otherwise it sits idle.

**Phase 3 — production audio (if pursued):**

- **Option F: external CS2200 PMA fractional-N oscillator** + I2S slave mode

PCB change. The CS2200 is the standard for this use case (network audio bridges
all use it). After it's installed, software complexity drops dramatically — just
write the drift value to the chip's register.

---

## Why FLPR ASRC over FLPR clock generation (B over C)

C looks elegant on paper but the FLPR has a single-issue VPR core. Any cache
miss, IPC interrupt, or VEVIF event during the BCLK-toggle loop becomes audio
jitter. ASRC, in contrast, runs on a buffered dataflow — late by 100 µs is
fine, the consumer just reads samples slower. ASRC is robust to scheduling
jitter, clock generation is not.

---

## What this doc replaces

The earlier `hfclkaudio-drift-compensation.md` described a working solution
specific to nRF5340. That solution is implemented in `audio_i2s.c` under
`#if NRF_CLOCK_HAS_HFCLKAUDIO`. On nRF54L15 the same code path compiles to
nothing, leaving only the packet-repeat fallback active.

---

## References

- nRF54L15 product specification §5.x (CLOCK, GRTC, DPPIC, I2S, PWM)
- `~/ncs/v3.3.0/zephyr/dts/vendor/nordic/nrf54l_05_10_15.dtsi` — peripheral map
- `~/ncs/v3.3.0/nrf/applications/hpf/gpio/src/hrt/` — example of FLPR VIO usage
- `~/ncs/v3.3.0/nrf/doc/nrf/app_dev/device_guides/nrf54l/vpr_flpr.rst` — FLPR
  programming guide
- Cirrus CS2200-CP datasheet — fractional-N oscillator for audio clock recovery
