/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic source signal generator and LC3 frame packing (RH1A).
 *
 * Waveform chain: Q0.32 phase accumulator -> 1024-index quarter-wave Q15
 * sine (checked-in 257-entry LUT, mirrored) -> selected amplitude via
 * (int64_t)wave * amplitude / 32767 (C99 truncation toward zero).  The
 * phase advances once per generated sample in every stage.  Preamble
 * segments, the scored envelope (120 ms xorshift32 blocks with 240-sample
 * ramps), and the zero tail follow the frozen source signal contract.
 *
 * Stage lifecycle: entering scored requires every configured semantic
 * channel to have rendered exactly the full preamble; at scored entry the
 * channel PRNG is derived from the seed and advanced by exactly one
 * xorshift32 step before the first block target is chosen.  Every later
 * 5760-sample boundary applies one further update.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "hil_source_signal.h"

#include "hil_source_sine_lut.inc"

/* ── sine table ───────────────────────────────────────────────────── */

int16_t hil_source_sine_q15(uint32_t phase_index)
{
	uint32_t idx = phase_index & 0x3FFU;

	if (idx <= 256U) {
		return hil_source_sine_lut[idx];
	}
	if (idx <= 511U) {
		return hil_source_sine_lut[512U - idx];
	}
	if (idx <= 768U) {
		return (int16_t)-hil_source_sine_lut[idx - 512U];
	}
	return (int16_t)-hil_source_sine_lut[1024U - idx];
}

/* ── profile helpers ──────────────────────────────────────────────── */

uint16_t hil_source_profile_frame_samples(enum hil_source_profile profile)
{
	switch (profile) {
	case HIL_SOURCE_PROFILE_48_3_1:
		return 360U;
	case HIL_SOURCE_PROFILE_48_4_1:
		return 480U;
	}
	return 0U;
}

uint16_t hil_source_profile_frame_duration_us(enum hil_source_profile profile)
{
	switch (profile) {
	case HIL_SOURCE_PROFILE_48_3_1:
		return 7500U;
	case HIL_SOURCE_PROFILE_48_4_1:
		return 10000U;
	}
	return 0U;
}

uint16_t hil_source_profile_octets_per_channel(enum hil_source_profile profile)
{
	switch (profile) {
	case HIL_SOURCE_PROFILE_48_3_1:
		return 90U;
	case HIL_SOURCE_PROFILE_48_4_1:
		return 120U;
	}
	return 0U;
}

uint16_t hil_source_profile_sdu_size(enum hil_source_profile profile, enum hil_source_mode mode)
{
	uint16_t octets = hil_source_profile_octets_per_channel(profile);

	if (octets == 0U || mode < HIL_SOURCE_MODE_MONO || mode > HIL_SOURCE_MODE_B) {
		return 0U;
	}
	if (mode == HIL_SOURCE_MODE_B) {
		return (uint16_t)(octets * 2U);
	}
	return octets;
}

/* ── channel helpers ──────────────────────────────────────────────── */

static uint32_t hil_channel_phase_step(enum hil_source_semantic_channel ch)
{
	switch (ch) {
	case HIL_SOURCE_CHANNEL_LEFT:
		return HIL_SOURCE_LEFT_PHASE_STEP;
	case HIL_SOURCE_CHANNEL_RIGHT:
		return HIL_SOURCE_RIGHT_PHASE_STEP;
	}
	return 0U;
}

static uint32_t hil_channel_prng_mask(enum hil_source_semantic_channel ch)
{
	switch (ch) {
	case HIL_SOURCE_CHANNEL_LEFT:
		return HIL_SOURCE_LEFT_PRNG_MASK;
	case HIL_SOURCE_CHANNEL_RIGHT:
		return HIL_SOURCE_RIGHT_PRNG_MASK;
	}
	return 0U;
}

static uint32_t hil_derive_prng(uint32_t seed, uint32_t mask)
{
	uint32_t x = seed ^ mask;

	return x == 0U ? HIL_SOURCE_PRNG_ZERO_REPLACEMENT : x;
}

static uint32_t hil_xorshift32(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

/* Preamble amplitude for one semantic channel at a given segment
 * (0-based): silence, left-only, silence, right-only, silence, both. */
static int16_t hil_preamble_amplitude(enum hil_source_semantic_channel ch, uint32_t segment)
{
	bool on = false;

	if (ch == HIL_SOURCE_CHANNEL_LEFT) {
		on = (segment == 1U || segment == 5U);
	} else {
		on = (segment == 3U || segment == 5U);
	}
	return on ? HIL_SOURCE_AMPLITUDE_FULL : 0;
}

/* Scored envelope amplitude for the current sample; mutates channel
 * envelope state (entry init, ramp, and block boundaries).  At scored
 * entry the derived PRNG is advanced by exactly one xorshift32 step
 * before the first block target is chosen; each later 5760-sample block
 * boundary applies one further update. */
static int16_t hil_scored_amplitude(struct hil_source_signal_encoder *enc,
				    struct hil_source_signal_channel *ch)
{
	int32_t amp;

	if (!ch->scored_entered) {
		ch->prng = hil_derive_prng(enc->signal_seed, hil_channel_prng_mask(ch->semantic));
		ch->prng = hil_xorshift32(ch->prng); /* first update */
		ch->block_index = 0U;
		ch->block_pos = 0U;
		ch->ramp_index = 0U;
		ch->ramp_old = HIL_SOURCE_AMPLITUDE_FULL;
		ch->ramp_next =
			(ch->prng & 1U) ? HIL_SOURCE_AMPLITUDE_FULL : HIL_SOURCE_AMPLITUDE_LOW;
		ch->scored_entered = true;
	}

	if (ch->ramp_index < HIL_SOURCE_TRANSITION_RAMP_SAMPLES) {
		int64_t diff = (int64_t)ch->ramp_next - (int64_t)ch->ramp_old;
		int64_t step = (diff * (int64_t)(ch->ramp_index + 1U)) /
			       (int64_t)HIL_SOURCE_TRANSITION_RAMP_SAMPLES;

		amp = (int32_t)ch->ramp_old + (int32_t)step;
		ch->ramp_index++;
	} else {
		amp = ch->ramp_next;
	}

	ch->block_pos++;
	if (ch->block_pos >= HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES) {
		ch->block_pos = 0U;
		ch->block_index++;
		ch->prng = hil_xorshift32(ch->prng);
		ch->ramp_old = ch->ramp_next;
		ch->ramp_next =
			(ch->prng & 1U) ? HIL_SOURCE_AMPLITUDE_FULL : HIL_SOURCE_AMPLITUDE_LOW;
		ch->ramp_index = 0U;
	}
	return (int16_t)amp;
}

/* ── encoder API ──────────────────────────────────────────────────── */

int hil_source_signal_encoder_init(struct hil_source_signal_encoder *enc, enum hil_source_mode mode,
				   enum hil_source_profile profile, uint32_t signal_seed,
				   enum hil_source_semantic_channel ch0, int ch1)
{
	uint16_t duration_us;
	uint16_t nch;
	uint8_t i;

	if (enc == NULL) {
		return -EINVAL;
	}
	if (profile < HIL_SOURCE_PROFILE_48_3_1 || profile > HIL_SOURCE_PROFILE_48_4_1 ||
	    mode < HIL_SOURCE_MODE_MONO || mode > HIL_SOURCE_MODE_B || signal_seed == 0U) {
		return -EINVAL;
	}
	if (mode == HIL_SOURCE_MODE_MONO) {
		if (ch0 != HIL_SOURCE_CHANNEL_LEFT || ch1 != -1) {
			return -EINVAL;
		}
		nch = 1U;
	} else if (mode == HIL_SOURCE_MODE_A) {
		if (ch1 != -1 ||
		    (ch0 != HIL_SOURCE_CHANNEL_LEFT && ch0 != HIL_SOURCE_CHANNEL_RIGHT)) {
			return -EINVAL;
		}
		nch = 1U;
	} else {
		if (ch0 != HIL_SOURCE_CHANNEL_LEFT || ch1 != HIL_SOURCE_CHANNEL_RIGHT) {
			return -EINVAL;
		}
		nch = 2U;
	}
	duration_us = hil_source_profile_frame_duration_us(profile);

	memset(enc, 0, sizeof(*enc));
	enc->mode = mode;
	enc->profile = profile;
	enc->signal_seed = signal_seed;
	enc->stage = HIL_SOURCE_SIGNAL_PREAMBLE;
	enc->channel_count = (uint8_t)nch;
	enc->channels[0].semantic = ch0;
	enc->channels[0].phase = 0U;
	enc->channels[1].semantic = (ch1 >= 0) ? (enum hil_source_semantic_channel)ch1 : ch0;
	enc->channels[1].phase = 0U;

	for (i = 0U; i < enc->channel_count; i++) {
		enc->encoder[i] = lc3_setup_encoder(duration_us, HIL_SOURCE_SAMPLE_RATE_HZ, 0,
						    &enc->encoder_mem[i]);
		if (enc->encoder[i] == NULL) {
			enc->unusable = true;
			return -EIO;
		}
	}
	return 0;
}

int hil_source_signal_set_stage(struct hil_source_signal_encoder *enc,
				enum hil_source_signal_stage stage)
{
	uint8_t i;

	if (enc == NULL || enc->unusable) {
		return -EINVAL;
	}
	if (stage < HIL_SOURCE_SIGNAL_PREAMBLE || stage > HIL_SOURCE_SIGNAL_TAIL) {
		return -EINVAL;
	}
	if (enc->stage == HIL_SOURCE_SIGNAL_PREAMBLE && stage == HIL_SOURCE_SIGNAL_SCORED) {
		/* Scored entry requires the exact full preamble on every
		 * configured semantic channel. */
		for (i = 0U; i < enc->channel_count; i++) {
			if (enc->channels[i].preamble_rendered !=
			    HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES) {
				return -EINVAL;
			}
		}
		enc->stage = HIL_SOURCE_SIGNAL_SCORED;
		return 0;
	}
	if (enc->stage == HIL_SOURCE_SIGNAL_SCORED && stage == HIL_SOURCE_SIGNAL_TAIL) {
		enc->stage = HIL_SOURCE_SIGNAL_TAIL;
		return 0;
	}
	/* Any other transition (same stage, preamble -> tail, tail -> *,
	 * scored -> preamble, or early preamble -> scored) is rejected
	 * without mutation. */
	return -EINVAL;
}

int hil_source_signal_render(struct hil_source_signal_encoder *enc, uint32_t channel_index,
			     int16_t *out, uint32_t samples, size_t cap)
{
	struct hil_source_signal_channel *ch;
	uint32_t step;
	uint32_t i;

	if (enc == NULL || out == NULL || enc->unusable) {
		return -EINVAL;
	}
	if (channel_index >= enc->channel_count) {
		return -EINVAL;
	}
	if (samples == 0U) {
		return 0;
	}
	/* 32-bit capacity guard: samples * sizeof(int16_t) must not wrap
	 * before the capacity comparison.  On a 32-bit target a huge
	 * sample count (e.g. UINT32_MAX or 2^31) would otherwise wrap the
	 * product and bypass capacity rejection in scored/tail rendering. */
	if ((size_t)samples > SIZE_MAX / sizeof(int16_t)) {
		return -ENOSPC;
	}
	if (cap < (size_t)samples * sizeof(int16_t)) {
		return -ENOSPC;
	}
	ch = &enc->channels[channel_index];
	if (enc->stage == HIL_SOURCE_SIGNAL_PREAMBLE &&
	    samples > HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES - ch->preamble_rendered) {
		/* Subtraction-based bounds: a huge sample count cannot
		 * wrap before rejection. */
		return -EOVERFLOW;
	}
	step = hil_channel_phase_step(ch->semantic);

	for (i = 0U; i < samples; i++) {
		int16_t wave = hil_source_sine_q15(ch->phase >> 22);
		int16_t amp;
		int16_t pcm;

		if (enc->stage == HIL_SOURCE_SIGNAL_PREAMBLE) {
			uint32_t segment =
				ch->preamble_rendered / HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES;

			amp = hil_preamble_amplitude(ch->semantic, segment);
		} else if (enc->stage == HIL_SOURCE_SIGNAL_SCORED) {
			amp = hil_scored_amplitude(enc, ch);
		} else {
			amp = 0; /* tail: exact PCM zero */
		}

		if (amp == 0) {
			pcm = 0;
		} else {
			pcm = (int16_t)(((int64_t)wave * amp) / 32767);
		}
		out[i] = pcm;
		ch->phase += step;
		if (enc->stage == HIL_SOURCE_SIGNAL_PREAMBLE) {
			ch->preamble_rendered++;
		}
	}
	return (int)samples;
}

int hil_source_signal_encode_next(struct hil_source_signal_encoder *enc, uint8_t *out, size_t cap)
{
	uint16_t samples;
	uint16_t octets;
	uint16_t sdu;
	int16_t pcm[2][480];
	uint8_t i;
	int err;

	if (enc == NULL || out == NULL) {
		return -EINVAL;
	}
	if (enc->unusable) {
		return -EIO;
	}
	samples = hil_source_profile_frame_samples(enc->profile);
	octets = hil_source_profile_octets_per_channel(enc->profile);
	sdu = hil_source_profile_sdu_size(enc->profile, enc->mode);
	if (samples == 0U || octets == 0U || sdu == 0U) {
		return -EINVAL;
	}
	if (cap < sdu) {
		return -ENOSPC;
	}
	if (enc->stage == HIL_SOURCE_SIGNAL_PREAMBLE) {
		for (i = 0U; i < enc->channel_count; i++) {
			if (samples > HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES -
					      enc->channels[i].preamble_rendered) {
				return -EOVERFLOW;
			}
		}
	}

	for (i = 0U; i < enc->channel_count; i++) {
		int n = hil_source_signal_render(enc, i, pcm[i], samples, sizeof(pcm[i]));

		if (n != (int)samples) {
			return (n < 0) ? n : -EIO;
		}
	}

	if (enc->mode == HIL_SOURCE_MODE_B) {
		err = lc3_encode(enc->encoder[0], LC3_PCM_FORMAT_S16, pcm[0], 1, octets, out);
		if (err < 0) {
			enc->unusable = true;
			return -EIO;
		}
		err = lc3_encode(enc->encoder[1], LC3_PCM_FORMAT_S16, pcm[1], 1, octets,
				 out + octets);
		if (err < 0) {
			enc->unusable = true;
			return -EIO;
		}
	} else {
		err = lc3_encode(enc->encoder[0], LC3_PCM_FORMAT_S16, pcm[0], 1, octets, out);
		if (err < 0) {
			enc->unusable = true;
			return -EIO;
		}
	}
	return (int)sdu;
}
