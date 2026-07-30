/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-neutral audio timing math helpers.
 *
 * These are pure functions with no hardware dependency and are
 * compiled into both firmware and unit tests for exact coverage.
 */

#ifndef AUDIO_TIMING_MATH_H
#define AUDIO_TIMING_MATH_H

#include <stdint.h>

/**
 * @brief Expand a 32-bit ISO timestamp to a full 64-bit GRTC time.
 *
 * Follows Nordic iso_time_sync wrap logic: the upper 32 bits come
 * from the current GRTC time and are incremented when a wrap is
 * detected.
 *
 * @param ts_us  32-bit ISO timestamp in microseconds.
 * @param now_us Current 64-bit GRTC system counter value.
 * @return       Expanded 64-bit future GRTC time.
 */
uint64_t audio_timing_iso_ts_to_grtc64(uint32_t ts_us, uint64_t now_us);

/**
 * @brief Compute a future 64-bit GRTC anchor from an SDU timestamp and
 *        presentation delay.
 *
 * Computes the 32-bit target (= ts_us + pd_us) first, then expands that
 * target to a full 64-bit GRTC time.  Adding the presentation delay before
 * expansion ensures correct wrap behaviour when the raw SDU timestamp has
 * already rolled behind the current GRTC while the presentation-time target
 * is still ahead — exactly the pattern used by Nordic's iso_time_sync
 * sample (iso_rx.c followed by timed_led_toggle.c).
 *
 * The caller is responsible for verifying the result is safely in the
 * future and falling back to a safe future time if needed.
 *
 * @param sdu_ts_us  32-bit ISO timestamp in microseconds (info->ts).
 * @param pd_us       Presentation delay in microseconds (qos->pd).
 * @param now_us     Current 64-bit GRTC system counter value.
 * @return           Expanded 64-bit future GRTC presentation anchor.
 */
uint64_t audio_timing_anchor_to_grtc64(uint32_t sdu_ts_us, uint32_t pd_us, uint64_t now_us);

/**
 * @brief Unsigned 32-bit counter delta (handles wrap).
 *
 * @param current  Current counter value.
 * @param previous Previous counter value.
 * @return         Unsigned delta (current - previous, modulo 2^32).
 */
uint32_t audio_timing_counter_delta_u32(uint32_t current, uint32_t previous);

/**
 * @brief Integer ppm calculation: (measured - nominal) * 1e6 / nominal.
 *
 * @param measured         Measured count in the interval.
 * @param nominal_expected Nominal count for the given elapsed time.
 * @return                 Integer ppm (positive = faster than nominal).
 */
int32_t audio_timing_compute_ppm(uint32_t measured, uint32_t nominal_expected);

#endif /* AUDIO_TIMING_MATH_H */
