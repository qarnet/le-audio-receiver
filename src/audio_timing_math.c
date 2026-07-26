/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-neutral audio timing math helpers — see audio_timing_math.h.
 */

#include "audio_timing_math.h"

uint64_t audio_timing_iso_ts_to_grtc64(uint32_t ts_us, uint64_t now_us)
{
	uint64_t upper = now_us & 0xFFFFFFFF00000000ULL;
	uint64_t full = upper | ts_us;

	if (ts_us < (now_us & UINT32_MAX)) {
		/* Timestamp is after UINT32 wrap */
		full += 0x100000000ULL;
	}

	return full;
}

uint32_t audio_timing_counter_delta_u32(uint32_t current, uint32_t previous)
{
	return current - previous; /* unsigned arithmetic handles wrap */
}

int32_t audio_timing_compute_ppm(uint32_t measured, uint32_t nominal_expected)
{
	if (nominal_expected == 0) {
		return 0;
	}

	/* Use int64 to avoid overflow in intermediate */
	int64_t diff = (int64_t)measured - (int64_t)nominal_expected;

	return (int32_t)((diff * 1000000LL) / (int64_t)nominal_expected);
}
