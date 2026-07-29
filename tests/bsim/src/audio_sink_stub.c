/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM audio sink stub — validates decoded LC3 PCM without real I2S.
 *
 * Requires exactly 960 samples per push (48 kHz × 10 ms × 2 ch).
 * Checks at least one nonzero sample and nonzero bounded energy over
 * stream, tracks rolling CRC, fails on malformed count or pushes
 * after stop.  Passes after 100 valid decoded pushes.
 *
 * Does NOT claim exact PCM/audio quality.
 */

#include "audio_sink.h"
#include "bsim_test_helpers.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#define REQUIRED_SAMPLES 960 /* 48 kHz × 10 ms × 2 channels */
#define PASS_FRAME_COUNT 100
#define CRC_POLY         0xEDB88320UL

static atomic_int push_count;
static atomic_int decode_errors;
static atomic_int malformed_count;
static atomic_int pushes_after_stop;
static atomic_int nonzero_seen;
static atomic_bool stopped;
static atomic_uint rolling_crc;
static uint32_t running_crc;

int audio_sink_init(void)
{
	return 0;
}

void audio_sink_stop(void)
{
	atomic_store(&stopped, true);
}

static uint32_t crc32_update(uint32_t crc, uint32_t val)
{
	for (int b = 0; b < 4; b++) {
		crc ^= (val & 0xFF);
		val >>= 8;
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 1) {
				crc = (crc >> 1) ^ CRC_POLY;
			} else {
				crc >>= 1;
			}
		}
	}
	return crc;
}

int audio_sink_push(const int16_t *data, size_t sample_count)
{
	int cnt;

	/* Reject pushes after stop */
	if (atomic_load(&stopped)) {
		atomic_fetch_add(&pushes_after_stop, 1);
		FAIL("le_audio_receiver: push after stop — sample_count=%zu push#%d\n",
		     sample_count, atomic_load(&push_count));
		return -EIO;
	}

	/* Validate sample count */
	if (sample_count != REQUIRED_SAMPLES) {
		atomic_fetch_add(&malformed_count, 1);
		FAIL("le_audio_receiver: malformed sample count — expected %d got %zu push#%d\n",
		     REQUIRED_SAMPLES, sample_count, atomic_load(&push_count));
		return -EINVAL;
	}

	/* Check for at least one nonzero sample */
	bool has_nonzero = false;
	uint32_t frame_crc = 0xFFFFFFFFUL;
	int32_t energy = 0;

	for (size_t i = 0; i < sample_count; i++) {
		int32_t val = data[i];

		if (val != 0) {
			has_nonzero = true;
		}

		/* CRC32 accumulation */
		frame_crc = crc32_update(frame_crc, (uint32_t)val);

		/* Bounded energy (absolute value sum, prevents overflow vs int32) */
		int32_t abs_val = val < 0 ? -val : val;
		energy += abs_val;
	}

	frame_crc ^= 0xFFFFFFFFUL;

	/* Zero-energy frame: count as error, skip (server's source ASE
	 * may send empty frames during initial group setup). */
	if (energy == 0) {
		atomic_fetch_add(&decode_errors, 1);
		return -EINVAL;
	}

	if (has_nonzero) {
		atomic_store(&nonzero_seen, 1);
	}

	/* Update rolling combined CRC (XOR each frame CRC for compact hash) */
	running_crc ^= frame_crc;

	cnt = atomic_fetch_add(&push_count, 1) + 1;

	if (cnt == PASS_FRAME_COUNT) {
		atomic_store(&rolling_crc, running_crc);
		PASS("le_audio_receiver: %d decoded pushes — "
		     "nonzero=%d errors=%d malformed=%d after_stop=%d CRC=0x%08X\n",
		     cnt, atomic_load(&nonzero_seen), atomic_load(&decode_errors),
		     atomic_load(&malformed_count), atomic_load(&pushes_after_stop), running_crc);
	}

	return 0;
}
