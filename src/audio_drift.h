/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AUDIO_DRIFT_H
#define AUDIO_DRIFT_H

#include <stdint.h>

/*
 * APLL register constants for nRF5340 HFCLKAUDIO (12.288 MHz band).
 * One step ≈ 3.3 ppm. Range spans ≈ ±600 ppm around center.
 * Used by the APLL actuator; kept here for shared definition.
 */
#define AUDIO_DRIFT_APLL_CENTER 0x9BA6U
#define AUDIO_DRIFT_APLL_MIN    0x8FD8U
#define AUDIO_DRIFT_APLL_MAX    0xA774U

/**
 * @brief Feed a measured local audio-clock frequency error.
 *
 * Called from platform timing measurement (nRF54L15: PCLK TIMER20
 * vs GRTC; nRF5340: no-op, stays zero).  Positive local_clock_error_ppm
 * means the local audio clock / PCLK runs faster than the Bluetooth
 * controller / GRTC clock.
 *
 * Every measurement must reach the controller, not only diagnostic
 * samples.  Must be called from thread / work context, never ISR.
 *
 * @param local_clock_error_ppm  Measured ppm (positive = local fast).
 */
void audio_drift_frequency_error_update(int32_t local_clock_error_ppm);

/**
 * @brief Run the phase-PI controller once per rendered audio block.
 *
 * Reads the I2S DMA-slab free count and computes a ppm correction.
 * Combines the filtered PCLK frequency feedforward with the
 * buffer-phase PI term.
 *
 * Phase error = PHASE_SETPOINT - slab_free_count:
 *   - High slab_free (queue draining, many free slots)
 *       → negative correction (slow consumption down).
 *   - Low slab_free (queue filling, few free slots)
 *       → positive correction (speed consumption up).
 *
 * Positive ppm = consume source faster / drop one frame eventually.
 * Negative ppm = consume source slower / insert one frame eventually.
 *
 * The phase integrator uses directional anti-windup: at a saturation
 * rail, same-direction increments are blocked but opposite-direction
 * increments are allowed to unwind toward range.
 *
 * Thread-safe: serialises with audio_drift_frequency_error_update()
 * and audio_drift_reset() via internal spinlock.
 *
 * @param slab_free_count  Number of free blocks in the I2S DMA slab.
 * @return                 PPM correction to apply, 0 before first SDU.
 */
int32_t audio_drift_controller_update(int slab_free_count);

/** Reset controller, filter, and PI state to initial conditions.
 *  Thread-safe against concurrent update/reset. */
void audio_drift_reset(void);

/** Current state string for shell diagnostics. */
const char *audio_drift_state_str(void);

/** Current ppm output for shell diagnostics. */
int32_t audio_drift_get_ppm(void);

#endif /* AUDIO_DRIFT_H */
