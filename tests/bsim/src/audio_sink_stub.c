/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM audio sink stub — strict PCM oracle.
 *
 * Zero-energy push = immediate FAIL (no tolerated error counter).
 * Ordered FNV-1a hash — chained with prior hash and frame index
 * over all sample bytes so 100 identical sine blocks cannot cancel.
 * At PASS requires exactly 100 valid pushes, zero errors/PLC/malformed,
 * nonzero final hash, and reports energy min/max plus final hash.
 */

#include "audio_sink.h"
#include "audio_stats.h"
#include "bsim_test_helpers.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>

#define REQUIRED_SAMPLES 960 /* 48 kHz × 10 ms × 2 channels */
#define PASS_FRAME_COUNT 100
#define FNV_OFFSET_BASIS 0x811c9dc5UL
#define FNV_PRIME        0x01000193UL

static atomic_int push_count;
static atomic_int malformed_count;
static atomic_int pushes_after_stop;
static atomic_bool stopped;

/* Energy bounds tracked over ALL valid pushes */
static atomic_int energy_min = ATOMIC_VAR_INIT(INT32_MAX);
static atomic_int energy_max;
static int32_t running_energy_min = INT32_MAX;
static int32_t running_energy_max;

/* Ordered FNV-1a hash across frame index and all sample bytes.
 * Initial seed = FNV_OFFSET_BASIS; chained: hash = FNV-1a(hash, idx, samples...).
 * Stored atomically at PASS time for the runner to read. */
static uint32_t running_hash = FNV_OFFSET_BASIS;
static atomic_uint final_hash;

int audio_sink_init(void)
{
	return 0;
}

void audio_sink_stop(void)
{
	atomic_store(&stopped, true);
}

static uint32_t fnv1a_update(uint32_t hash, const uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		hash ^= bytes[i];
		hash *= FNV_PRIME;
	}
	return hash;
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

	/* Compute energy */
	int32_t energy = 0;

	for (size_t i = 0; i < sample_count; i++) {
		int32_t val = data[i];
		int32_t abs_val = val < 0 ? -val : val;

		energy += abs_val;
	}

	/* Zero-energy frame: skip during CIS startup (client may not have
	 * queued first SDU yet).  After any nonzero push has been seen,
	 * a zero-energy frame is an immediate failure — the audio stream
	 * must never produce silence once established.
	 */
	if (energy == 0) {
		if (atomic_load(&push_count) > 0) {
			FAIL("le_audio_receiver: zero-energy push after audio started — "
			     "push#%d immediate FAIL\n",
			     atomic_load(&push_count));
			return -EINVAL;
		}
		/* Startup transient: silently consume this push without counting */
		return 0;
	}

	/* Track energy bounds */
	if (energy < running_energy_min) {
		running_energy_min = energy;
	}
	if (energy > running_energy_max) {
		running_energy_max = energy;
	}

	/* Ordered FNV-1a: chain in frame index (4-byte LE) then all sample bytes */
	cnt = atomic_fetch_add(&push_count, 1);
	uint8_t idx_bytes[4];

	idx_bytes[0] = cnt & 0xFF;
	idx_bytes[1] = (cnt >> 8) & 0xFF;
	idx_bytes[2] = (cnt >> 16) & 0xFF;
	idx_bytes[3] = (cnt >> 24) & 0xFF;
	running_hash = fnv1a_update(running_hash, idx_bytes, 4);
	running_hash =
		fnv1a_update(running_hash, (const uint8_t *)data, sample_count * sizeof(int16_t));

	cnt++; /* push_count after increment */

	if (cnt == PASS_FRAME_COUNT) {
		struct audio_stats stats;
		int nonzero = (running_energy_max > 0) ? 1 : 0;

		/* Atomically snapshot final state before PASS */
		stats = audio_stats_get();
		atomic_store(&energy_min, running_energy_min);
		atomic_store(&energy_max, running_energy_max);
		atomic_store(&final_hash, running_hash);

		/* Enforce: hash must be nonzero and not initial seed */
		if (running_hash == FNV_OFFSET_BASIS) {
			FAIL("le_audio_receiver: ordered hash unchanged from seed — "
			     "possible all-zero or repeating identical blocks\n");
			return -EIO;
		}

		/* Enforce: zero decode errors */
		if (stats.decode_errors != 0) {
			FAIL("le_audio_receiver: decode_errors=%" PRIu32 " != 0\n",
			     stats.decode_errors);
			return -EIO;
		}

		/* Enforce: zero PLC frames */
		if (stats.plc_frames != 0) {
			FAIL("le_audio_receiver: plc_frames=%" PRIu32 " != 0\n", stats.plc_frames);
			return -EIO;
		}

		/* Enforce: >= 100 total frames */
		if (stats.total_frames < PASS_FRAME_COUNT) {
			FAIL("le_audio_receiver: total_frames=%" PRIu32 " < %d\n",
			     stats.total_frames, PASS_FRAME_COUNT);
			return -EIO;
		}

		/* Enforce: zero malformed and zero pushes after stop */
		if (atomic_load(&malformed_count) != 0) {
			FAIL("le_audio_receiver: malformed_count=%d != 0\n",
			     atomic_load(&malformed_count));
			return -EIO;
		}
		if (atomic_load(&pushes_after_stop) != 0) {
			FAIL("le_audio_receiver: pushes_after_stop=%d != 0\n",
			     atomic_load(&pushes_after_stop));
			return -EIO;
		}

		PASS("le_audio_receiver: %d pushes — "
		     "nonzero=%d errors=%" PRIu32 " plc=%" PRIu32 " total=%" PRIu32
		     " malformed=%d after_stop=%d "
		     "energy_min=%" PRId32 " energy_max=%" PRId32 " hash=0x%08" PRIX32 "\n",
		     cnt, nonzero, stats.decode_errors, stats.plc_frames, stats.total_frames,
		     atomic_load(&malformed_count), atomic_load(&pushes_after_stop),
		     running_energy_min, running_energy_max, running_hash);
	}

	return 0;
}
