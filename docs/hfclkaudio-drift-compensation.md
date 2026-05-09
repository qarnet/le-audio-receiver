# HFCLKAUDIO Drift Compensation — Design Plan

## Problem

The nRF5340's HFCLKAUDIO PLL (nominally 12.288 MHz) and the BLE ISO clock are independent
oscillators. Their frequency offset — anywhere from a few ppm to ~200 ppm in the worst case —
causes the I2S DMA queue to drain or fill over time.

At 100 ppm offset:
- I2S consumes one 10 ms block every 9.999 ms instead of 10 ms
- Queue drains by 1 block every ~100 s
- Every ~100 s: `i2s_nrfx: Next buffers not supplied on time` → hard cutout

The current fallback (packet-repeat when free-slab count ≥ 5) prevents the hard cutout by
inserting a duplicate frame, but it does not fix the underlying clock mismatch. The drift
accumulates indefinitely, so the duplicate-frame insertion fires forever at a rate proportional
to the ppm error.

## Why HFCLKAUDIO trimming is the correct fix

The HFCLKAUDIO PLL frequency register (`HFCLKAUDIO.HCLK.FREQVALUE`) can be adjusted at runtime
in steps of 40.7 Hz (~3.3 ppm each) without stopping or restarting the clock. Nudging the
register by ±1 step per 100 ms trims the I2S sample rate by 3.3 ppm per step. Once the DMA
queue depth stabilises, no further corrections are needed and the duplicate-frame path goes
permanently quiet.

This is the approach used by Nordic's own nRF5340 LE Audio reference application
(`applications/nrf5340_audio/src/audio/audio_datapath.c` + `src/modules/audio_clock.c`).
Their full implementation uses SDU reference timestamps and a state machine
(INIT → CALIB → OFFSET → LOCKED) to measure the ppm error precisely. Our implementation uses
DMA queue depth as a simpler proxy signal — sufficient for a sink-only receiver that does not
need TWS synchronisation.

## Sources

- **Nordic nRF5340 Audio reference — audio_datapath.c** (local NCS tree):
  `~/ncs/v3.3.0/nrf/applications/nrf5340_audio/src/audio/audio_datapath.c`
  Functions: `audio_datapath_drift_compensation()`, `err_us_calculate()`, `APLL_FREQ_ADJ(t)`

- **Nordic audio_clock.c / audio_clock.h** (local NCS tree):
  `~/ncs/v3.3.0/nrf/applications/nrf5340_audio/src/modules/audio_clock.c`
  Shows `nrfx_clock_hfclkaudio_config_set()` → wraps `nrf_clock_hfclkaudio_config_set(NRF_CLOCK, v)`

- **nrfx_clock_hfclkaudio.h** (local NCS tree):
  `~/ncs/v3.3.0/modules/hal/nordic/nrfx/drivers/include/nrfx_clock_hfclkaudio.h`
  Documents the register formula and the valid frequency bands.

- **Nordic DevZone — "Clock drift with gateway, I2S slave"**:
  https://devzone.nordicsemi.com/f/nordic-q-a/96100/clock-drift-with-gateway-i2s-slave
  Confirms that adjusting HFCLKAUDIO is the canonical fix; packet-repeat / SRC are fallbacks.

- **Nordic firmware architecture docs**:
  https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/applications/nrf5340_audio/doc/firmware_architecture.html
  Describes the two-layer compensation: drift (APLL trim) + presentation (block insert/drop).

## API

```c
#include <hal/nrf_clock.h>

// Adjusts HFCLKAUDIO frequency without stopping the clock.
// freq_value formula: FREQ_VALUE = 2^16 * ((12 * f_out / 32 MHz) - 4)
// Valid bands: 12.165 MHz–12.411 MHz (the 48 kHz audio band)
nrf_clock_hfclkaudio_config_set(NRF_CLOCK, uint16_t freq_value);
```

Register constants (from `audio_clock.h`):

| Constant          | Value    | Frequency     |
|-------------------|----------|---------------|
| `APLL_FREQ_MIN`   | `0x8FD8` | 12.165 MHz    |
| `APLL_FREQ_CENTER`| `0x9BA6` | 12.288 MHz    |
| `APLL_FREQ_MAX`   | `0xA774` | 12.411 MHz    |

Step size: 1 register unit = 40.7 Hz = ~3.3 ppm at 12.288 MHz.
Full range from center: ±123 MHz × step = ±~400 ppm.

## Error signal

Nordic's reference measures error from SDU reference timestamps vs. I2S frame timestamps.
We do not have I2S frame timestamps from the nrfx driver, so we use DMA queue depth instead:

```
k_mem_slab_num_free_get(&i2s_slab)
```

After the 4-block pre-fill + 1 audio block at stream start:
- 5 blocks held by DMA pipeline → 3 blocks free (normal operating point)
- Rising free count → DMA consuming blocks faster than BLE delivers them → HFCLKAUDIO too high
- Falling free count → DMA consuming slower → HFCLKAUDIO too low

## Control loop design

Check every `APLL_INTERVAL = 10` audio frames (~100 ms). Apply one step if outside deadband:

```
free ≥ APLL_DRAIN_FREE (5):  apll_freq--; set register   // slow down I2S
free ≤ APLL_FILL_FREE  (1):  apll_freq++; set register   // speed up I2S
2 ≤ free ≤ 4:                no adjustment               // deadband
```

This is a bang-bang controller with hysteresis. Worst-case convergence:
- 200 ppm offset = 60 register steps
- 1 step per 100 ms → ~6 s to converge
- During convergence, existing packet-repeat fallback prevents hard cutouts

Once converged the register stabilises at one value and the packet-repeat path goes silent.

## Implementation (changes to `src/audio_i2s.c` only)

No changes to `main.c` or `audio_i2s.h` needed.

### New includes

```c
#include <hal/nrf_clock.h>
```

### New defines

```c
#define APLL_FREQ_CENTER   0x9BA6U
#define APLL_FREQ_MIN      0x8FD8U
#define APLL_FREQ_MAX      0xA774U
#define APLL_INTERVAL      10      /* frames between APLL checks (~100 ms) */
#define APLL_DRAIN_FREE    5       /* free ≥ this → I2S too fast → trim down */
#define APLL_FILL_FREE     1       /* free ≤ this → I2S too slow → trim up  */
```

### New statics

```c
static uint16_t apll_freq = APLL_FREQ_CENTER;
static int      apll_ctr;
```

### After successful i2s_write (normal path, started == true)

```c
if (++apll_ctr >= APLL_INTERVAL) {
    apll_ctr = 0;
    uint32_t free = k_mem_slab_num_free_get(&i2s_slab);

    if (free >= APLL_DRAIN_FREE && apll_freq > APLL_FREQ_MIN) {
        nrf_clock_hfclkaudio_config_set(NRF_CLOCK, --apll_freq);
    } else if (free <= APLL_FILL_FREE && apll_freq < APLL_FREQ_MAX) {
        nrf_clock_hfclkaudio_config_set(NRF_CLOCK, ++apll_freq);
    }
}
```

### On I2S start (alongside `started = true`)

```c
apll_freq = APLL_FREQ_CENTER;
nrf_clock_hfclkaudio_config_set(NRF_CLOCK, apll_freq);
apll_ctr  = 0;
```

### In `audio_i2s_stop`

```c
apll_freq = APLL_FREQ_CENTER;
nrf_clock_hfclkaudio_config_set(NRF_CLOCK, apll_freq);
apll_ctr  = 0;
```

## Interaction with existing packet-repeat

The packet-repeat (inserted when `free >= DRIFT_THRESHOLD`) remains as a last-resort fallback
for large jitter spikes or slow convergence. Once the APLL loop locks, free count stays in the
2–4 deadband and the packet-repeat path never fires. The two mechanisms do not conflict.

## What this does NOT fix

- BLE packet loss / PLC gaps — handled by LC3 PLC (`lc3_decode` with NULL input)
- Presentation synchronisation between two headsets (TWS) — not applicable to our sink-only setup
- Jitter spikes > one block (10 ms) — handled by the packet-repeat fallback
