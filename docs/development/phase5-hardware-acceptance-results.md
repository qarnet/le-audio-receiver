# Phase 5 — hardware acceptance results

Date: 2026-07-27
Firmware base: commit 4ee37ee (HEAD)

## Probe identity

```
SERIAL    PROBE                              TARGET    DPIDR       PART        VARIANT
8EE9B3FF  Seeed Studio XIAO nrf54 CMSIS-DAP  nRF54L15  0x6ba02477  0x00054b15  AAC0
```

Single probe — nRF54L15 only. No nRF5340 E83 probe detected → nRF5340 regression
deferred per handoff rule 5.

## Mode A — 600 s (10 min) on nRF54L15

**Build**: production config (2 ASEs, ASRC linear, ACTUATOR_NONE).

**Central**: `python scripts/bap_central.py --duration 600`
Two mono ASEs (FL=0x01, FR=0x02), Mode A stereo.

**Result**: **PASS**
- 60000 frames in 600.00 s (100.0 fps)
- No disconnect during active run
- No errors, warnings, underruns, slab-full, repeat fallback, push failure,
  ASRC capacity failure, or assertion
- Clean teardown

### Performance (Mode A, 128 MHz cpuapp)

| Path | Count | Avg cyc | Avg us | Max cyc | Max us | %deadline |
|------|-------|---------|--------|---------|--------|-----------|
| iso_recv | 120,058 | 1,819 | 14.6 | 2,451 | 19.6 | 25% |
| lc3_decode | 120,048 | 1,425 | 11.4 | 1,636 | 13.1 | 17% |
| volume | 60,023 | 99 | 0.8 | 159 | 1.3 | 2% |
| sink_push | 60,022 | 631 | 5.0 | 780 | 6.2 | 8% |
| **asrc** | **60,023** | **446** | **3.6** | **560** | **4.5** | **6%** |

Queue: slab free 5/7 (min/max), output frames 476/477, blocks 60,022.
PCLK diagnostics active: ~1,091–1,542 ppm across run.

## Mode B — 600 s (10 min) on nRF54L15

**Build**: `-DCONFIG_BT_ASCS_MAX_ASE_SNK_COUNT=1` (1 ASE, stereo chan_count=2).

**Central fix** (defect found and fixed):
- `bap_central.py` registered source PAC with mono-only capability (chan_count=1)
  even when `--stereo`. BlueZ could not match to stereo-capable sink.
- Added `LC3_CAPS_STEREO` (chan_count=2) and wired into endpoint registration
  when `--stereo`.
- SDU made dynamic: 120 (mono) / 240 (stereo Mode B).

**Central**: `python scripts/bap_central.py --stereo --duration 600`
Single ASE with ChannelAllocation=0x0003 (FL|FR).

**Receiver negotiation verified in logs**:
```
chan alloc 0x00000003 count=2
ASE[0] configured: num_sink_ase=1
LC3 decoder[0]: 48000 Hz 10000 us ch=2
Audio path gate OPEN (stream[0] completed the set)
```

**Result**: **PASS**
- 60000 frames in 600.00 s (100.0 fps)
- No errors, warnings, underruns, slab-full, repeat fallback, push failure,
  ASRC capacity failure, or assertion
- Clean teardown

### Performance (Mode B, 128 MHz cpuapp)

| Path | Count | Avg cyc | Avg us | Max cyc | Max us | %deadline |
|------|-------|---------|--------|---------|--------|-----------|
| iso_recv | 60,024 | 3,498 | 28.0 | 3,786 | 30.3 | 38% |
| lc3_decode | 120,048 | 1,375 | 11.0 | 1,680 | 13.4 | 17% |
| volume | 60,024 | 99 | 0.8 | 128 | 1.0 | 2% |
| sink_push | 60,023 | 634 | 5.1 | 848 | 6.8 | 9% |
| **asrc** | **60,024** | **447** | **3.6** | **661** | **5.3** | **7%** |

Queue: slab free 5/7 (min/max), output frames 476/478, blocks 60,023.
PCLK diagnostics active: ~1,510–1,837 ppm across run.

### ASRC comparison (Mode A vs Mode B)

| Metric | Mode A | Mode B |
|--------|--------|--------|
| ASRC avg cycles | 446 | 447 |
| ASRC avg us | 3.6 | 3.6 |
| ASRC max cycles | 560 | 661 |
| ASRC max us | 4.5 | 5.3 |
| Deadline % | 6% | 7% |
| iso_recv count | 120,058 (2 ASE) | 60,024 (1 ASE) |
| lc3_decode count | 120,048 | 120,048 |

Negligible difference. Both modes ~7% of 10 ms deadline.

## nRF5340 regression

Build passes (merged.hex produced). No E83 hardware probe detected → engine
run deferred. `nrf-probes` evidence logged above.

## Unit tests

All **20 ASRC unit tests PASS** (native_sim):
- Capacity overflow, chunking invariance, cross-block phase continuity,
  deterministic 60,000-block run, global continuous reference comparison,
  identity passthrough, long-run totals, monotonic ramp, ppm sign chain,
  null/rejection/bounds, signed extreme interpolation, worst-case fits 481.

## Defects found and fixed

1. **bap_central.py Mode B PAC capability**: registered source with mono-only
   (chan_count=1) regardless of `--stereo` flag. Fix: `LC3_CAPS_STEREO` with
   chan_count=2, selected dynamically. See `scripts/bap_central.py` lines 47–78.
2. **bap_central.py Mode B SDU size**: SelectProperties returned SDU=120
   hardcoded. Stereo Mode B needs 240 (2×120 bytes). Fix: dynamic SDU=240 for
   stereo. Same file lines 402–447.

## Verdict

- nRF54L15 Mode A 600 s: **PASS**
- nRF54L15 Mode B 600 s: **PASS** (with 2 central defects fixed)
- ASRC unit tests: **20/20 PASS**
- nRF5340 build: **PASS** (hardware regression deferred)
- Production mode: **Mode A restored** as default build
