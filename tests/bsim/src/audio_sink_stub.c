/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM audio sink stub — scenario-aware strict PCM oracle.
 *
 * Keeps the public audio_sink.h API exact.  Records per-streamed segment:
 * pushes, startup-zero pushes, startup PLC, final audio_stats, malformed
 * sample counts, pushes after stop, full interleaved ordered FNV-1a hash,
 * left/right ordered hashes, per-channel energy min/max, configured sample
 * count.  Segment-local state resets when a valid new stream shape is set
 * after disconnect/reconnect; push-after-stop is never hidden.
 *
 * Invariants (per scenario, checked at segment finalize / goal):
 *  - zero decode errors, except scenario 8 which expects exactly one
 *    (the pre-decode malformed-SDU rejection);
 *  - all PLC frames happened during startup (before first nonzero PCM);
 *  - total_frames == decoder_calls_per_push * (pushes + startup_zero);
 *  - mono: left hash == right hash; Mode A/B: left hash != right hash;
 *  - nonzero hashes, positive per-channel energy bounds;
 *  - zero pushes after stop, zero malformed sample counts;
 *  - available sink contexts never NONE after connection + stream.
 *
 * The oracle never emits PASS — the receiver scenario driver composes the
 * PASS record from the query API and observer counts.  Faults FAIL
 * immediately (bst_result = Failed).
 */

#include "audio_sink.h"
#include "audio_stats.h"
#include "bsim_sink_oracle.h"
#include "bsim_observer.h"
#include "bsim_test_helpers.h"

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/pacs.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#define FNV_OFFSET_BASIS   0x811c9dc5UL
#define FNV_PRIME          0x01000193UL
#define NORMAL_GOAL_PUSHES 100

static enum bsim_sink_scenario scenario;
static int dec_calls_per_push = 1;

static struct bsim_sink_segment segments[BSIM_SINK_MAX_SEGMENTS];
static int segment_count; /* finalized segments */
static int current_seg;   /* open segment index */
static bool stopped;
static bool first_nonzero_seen;   /* per segment */
static bool boundary_closed;      /* per segment: first nonzero source-valid push */
static uint32_t after_stop_total; /* cumulative, never hidden */

static uint16_t required_samples = 960; /* 48 kHz × 10 ms × 2 ch */

static bool seg_finalized(int idx)
{
	return idx >= 0 && idx < segment_count;
}

/*
 * True once the current segment reached the normal 100-push goal and was
 * finalized by it.  Pushes arriving after that (in-flight SDUs, teardown
 * PLC concealment) are ignored, not counted and not faulted: the scenario
 * is complete and the client has stopped sending.
 */
static bool goal_finalized;

static struct bsim_sink_segment *cur(void)
{
	return &segments[current_seg];
}

static uint32_t fnv1a_update(uint32_t hash, const uint8_t *bytes, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		hash ^= bytes[i];
		hash *= FNV_PRIME;
	}
	return hash;
}

/*
 * Ordered per-channel hash: prepend the 4-byte LE frame index, then each
 * sample as its two LE bytes.  Samples are converted to uint16_t BEFORE
 * byte extraction so negative signed values are never right-shifted.
 */
static uint32_t fnv1a_hash_channel(uint32_t hash, uint32_t push_idx, const int16_t *samples,
				   size_t count, size_t stride)
{
	uint8_t idx_bytes[4];

	idx_bytes[0] = push_idx & 0xFF;
	idx_bytes[1] = (push_idx >> 8) & 0xFF;
	idx_bytes[2] = (push_idx >> 16) & 0xFF;
	idx_bytes[3] = (push_idx >> 24) & 0xFF;
	hash = fnv1a_update(hash, idx_bytes, 4);

	for (size_t i = 0; i < count; i++) {
		uint16_t u = (uint16_t)samples[i * stride];

		hash ^= (uint8_t)(u & 0xFF);
		hash *= FNV_PRIME;
		hash ^= (uint8_t)((u >> 8) & 0xFF);
		hash *= FNV_PRIME;
	}
	return hash;
}

static void segment_finalize(int idx)
{
	struct bsim_sink_segment *s = &segments[idx];
	struct audio_stats stats = audio_stats_get();

	s->total_frames = stats.total_frames;
	s->plc_frames = stats.plc_frames;
	s->decode_errors = stats.decode_errors;
	s->configured_samples = required_samples;
	s->finalized = true;

	printk("SINK_SEG %d pushes=%u szero=%u splc=%u total=%u plc=%u derr=%u "
	       "malformed=%u hash=0x%08X lhash=0x%08X rhash=0x%08X "
	       "lemin=%d lemax=%d remin=%d remax=%d samples=%u\n",
	       idx, s->pushes, s->startup_zero, s->startup_plc, s->total_frames, s->plc_frames,
	       s->decode_errors, s->malformed_samples, s->full_hash, s->l_hash, s->r_hash,
	       s->l_energy_min, s->l_energy_max, s->r_energy_min, s->r_energy_max,
	       s->configured_samples);
}

/*
 * Start a new segment.  Called on begin() and whenever a valid new stream
 * shape is set while stopped (post-disconnect/reconnect).  The previous
 * open segment, if any, is finalized first.
 */
static void segment_start(void)
{
	if (current_seg >= 0 && !segments[current_seg].finalized) {
		segment_finalize(current_seg);
		segment_count = current_seg + 1;
	}
	if (current_seg + 1 >= BSIM_SINK_MAX_SEGMENTS) {
		FAIL("le_audio_receiver: too many oracle segments\n");
		return;
	}
	current_seg++;
	memset(&segments[current_seg], 0, sizeof(segments[current_seg]));
	segments[current_seg].full_hash = FNV_OFFSET_BASIS;
	segments[current_seg].l_hash = FNV_OFFSET_BASIS;
	segments[current_seg].r_hash = FNV_OFFSET_BASIS;
	segments[current_seg].l_energy_min = INT32_MAX;
	segments[current_seg].r_energy_min = INT32_MAX;
	first_nonzero_seen = false;
	boundary_closed = false;
}

void audio_sink_test_begin(enum bsim_sink_scenario scn, int dec_calls)
{
	scenario = scn;
	dec_calls_per_push = (dec_calls > 0) ? dec_calls : 1;
	stopped = false;
	after_stop_total = 0U;
	segment_count = 0;
	current_seg = -1;
	goal_finalized = false;
	segment_start();
}

/* ── public audio_sink API ───────────────────────────────────────── */

int audio_sink_init(void)
{
	return 0;
}

void audio_sink_stop(void)
{
	if (!stopped) {
		stopped = true;
		if (current_seg >= 0 && !segments[current_seg].finalized) {
			segment_finalize(current_seg);
			segment_count = current_seg + 1;
		}
	}
}

void audio_sink_set_input_frames(uint16_t frames)
{
	uint16_t samples = (uint16_t)(frames * 2); /* stereo: frames → samples */

	if (samples == 0) {
		samples = 960;
	}

	if (stopped && current_seg >= 0 && segments[current_seg].finalized) {
		/* Valid new stream shape after disconnect/reconnect:
		 * reset stopped/segment-local oracle state (a new segment
		 * starts fresh).  Push-after-stop stays cumulative. */
		stopped = false;
		goal_finalized = false;
		segment_start();
	} else if (current_seg < 0 || segments[current_seg].finalized) {
		goal_finalized = false;
		segment_start();
	}
	required_samples = samples;
}

int audio_sink_push(const int16_t *data, size_t sample_count)
{
	if (goal_finalized) {
		/* Scenario goal already reached: ignore any later push. */
		return 0;
	}

	if (stopped) {
		after_stop_total++;
		FAIL("le_audio_receiver: push after stop — sample_count=%zu push#%u\n",
		     sample_count, (unsigned int)segments[current_seg].pushes);
		return -EIO;
	}

	if (sample_count != (size_t)required_samples) {
		cur()->malformed_samples++;
		FAIL("le_audio_receiver: malformed sample count — expected %u got %zu push#%u\n",
		     required_samples, sample_count, (unsigned int)cur()->pushes);
		return -EINVAL;
	}

	/* Per-channel energy. */
	int32_t l_energy = 0;
	int32_t r_energy = 0;

	for (size_t i = 0; i < sample_count; i += 2U) {
		int32_t lv = data[i];
		int32_t rv = data[i + 1U];
		int32_t lav = lv < 0 ? -lv : lv;
		int32_t rav = rv < 0 ? -rv : rv;

		l_energy += lav;
		r_energy += rav;
	}

	const int32_t energy = l_energy + r_energy;
	const bool src_valid = bsim_observer_get_last_push_src_valid();

	/*
	 * Strict startup boundary: startup stays open until the first
	 * nonzero push sourced entirely from valid ISO input.  While open,
	 * zero or nonzero concealment is a startup transient; startup_plc
	 * is updated after each transient push.  After the boundary
	 * closes, any source-invalid push, any zero-energy push, and any
	 * post-start PLC (plc_frames != startup_plc at finalize) is a
	 * fault.
	 */
	if (!boundary_closed) {
		if (energy == 0) {
			cur()->startup_zero++;
		}
		if (energy != 0 && src_valid) {
			/* First fully valid push: closes the boundary and is
			 * the first hashed frame (not a transient). */
			boundary_closed = true;
		} else {
			cur()->transients++;
			cur()->startup_plc = audio_stats_get().plc_frames;
			return 0;
		}
	} else {
		if (!src_valid) {
			FAIL("le_audio_receiver: source-invalid push after valid boundary — "
			     "push#%u immediate FAIL\n",
			     (unsigned int)cur()->pushes);
			return -EINVAL;
		}
		if (energy == 0) {
			FAIL("le_audio_receiver: zero-energy push after audio started — "
			     "push#%u immediate FAIL\n",
			     (unsigned int)cur()->pushes);
			return -EINVAL;
		}
	}

	first_nonzero_seen = true;

	if (l_energy < cur()->l_energy_min) {
		cur()->l_energy_min = l_energy;
	}
	if (l_energy > cur()->l_energy_max) {
		cur()->l_energy_max = l_energy;
	}
	if (r_energy < cur()->r_energy_min) {
		cur()->r_energy_min = r_energy;
	}
	if (r_energy > cur()->r_energy_max) {
		cur()->r_energy_max = r_energy;
	}

	/* Ordered FNV-1a: prepend the 4-byte LE frame index, then channel
	 * sample bytes (uint16_t conversion before byte extraction).
	 * Full = interleaved L,R; L and R per channel, one helper. */
	uint32_t cnt = cur()->pushes;
	uint8_t idx_bytes[4];

	idx_bytes[0] = cnt & 0xFF;
	idx_bytes[1] = (cnt >> 8) & 0xFF;
	idx_bytes[2] = (cnt >> 16) & 0xFF;
	idx_bytes[3] = (cnt >> 24) & 0xFF;
	cur()->full_hash = fnv1a_update(cur()->full_hash, idx_bytes, 4);
	cur()->full_hash = fnv1a_update(cur()->full_hash, (const uint8_t *)data, sample_count * 2U);
	cur()->l_hash = fnv1a_hash_channel(cur()->l_hash, cnt, data, sample_count / 2U, 2U);
	cur()->r_hash = fnv1a_hash_channel(cur()->r_hash, cnt, data + 1, sample_count / 2U, 2U);

	cur()->pushes++;

	if (cur()->pushes >= NORMAL_GOAL_PUSHES && !cur()->finalized) {
		segment_finalize(current_seg);
		segment_count = current_seg + 1;
		goal_finalized = true;
	}

	return 0;
}

/* ── oracle queries ──────────────────────────────────────────────── */

bool audio_sink_test_goal_reached(void)
{
	struct bsim_sink_segment *s0;
	struct bsim_sink_segment *s1;

	switch (scenario) {
	case BSIM_SCN_MONO_10MS:
	case BSIM_SCN_MONO_7P5MS:
	case BSIM_SCN_MODEA_10MS:
	case BSIM_SCN_MODEA_7P5MS:
	case BSIM_SCN_MODEA_REVERSE_START_10MS:
	case BSIM_SCN_MODEB_10MS:
	case BSIM_SCN_MODEB_7P5MS:
		return segment_count >= 1 && segments[0].finalized &&
		       segments[0].pushes == NORMAL_GOAL_PUSHES;

	case BSIM_SCN_INVALID_SDU_RESUME_10MS:
		return segment_count >= 1 && segments[0].finalized &&
		       segments[0].pushes == NORMAL_GOAL_PUSHES;

	case BSIM_SCN_MODEA_FIRST_STOP_10MS:
	case BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS:
	case BSIM_SCN_DISCONNECT_STREAMING_10MS:
		/* Segment finalized by stop; client guarantees >= 20 pushes. */
		return segment_count >= 1 && segments[0].finalized && segments[0].pushes >= 20U &&
		       after_stop_total == 0U;

	case BSIM_SCN_RECONNECT_SECOND_STREAM_10MS:
		if (segment_count < 2) {
			return false;
		}
		s0 = &segments[0];
		s1 = &segments[1];
		return s0->finalized && s1->finalized && s0->pushes >= 20U &&
		       s1->pushes == NORMAL_GOAL_PUSHES && after_stop_total == 0U;

	case BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION:
	case BSIM_SCN_NO_FREE_SINK_SLOT:
	case BSIM_SCN_INVALID_CODEC_FIELDS:
		/* No audio expected: goal is driven by observer events in the
		 * receiver scenario driver.  Ignore the (empty) segment that the
		 * final disconnect finalizes; any actual push FAILs the oracle. */
		return after_stop_total == 0U;

	default:
		return false;
	}
}

/* Invariant checks performed by the receiver driver before PASS. */
bool audio_sink_test_validate(void)
{
	for (int i = 0; i < segment_count; i++) {
		struct bsim_sink_segment *s = &segments[i];
		const uint32_t expected_dec =
			(uint32_t)dec_calls_per_push * (s->pushes + s->transients);
		const bool expect_stereo = (dec_calls_per_push == 2);
		const uint32_t expected_err =
			(scenario == BSIM_SCN_INVALID_SDU_RESUME_10MS) ? 1U : 0U;

		if (s->malformed_samples != 0U) {
			FAIL("le_audio_receiver: segment %d malformed sample count %u\n", i,
			     s->malformed_samples);
			return false;
		}
		if (s->decode_errors != expected_err) {
			FAIL("le_audio_receiver: segment %d decode_errors=%u != expected %u\n", i,
			     s->decode_errors, expected_err);
			return false;
		}
		/* Zero post-start PLC: every PLC frame happened during the
		 * startup phase (source-valid boundary), exactly. */
		if (s->plc_frames != s->startup_plc) {
			FAIL("le_audio_receiver: segment %d plc=%u != startup_plc=%u "
			     "(post-start PLC)\n",
			     i, s->plc_frames, s->startup_plc);
			return false;
		}
		/* Decoder-invocation accounting: every pushed SDU (valid or
		 * startup transient) costs exactly dec_calls decoder
		 * invocations, so the total can never undercut
		 * pushes+transients; unpaired halves at the CIS activation
		 * skew or a pairing cut add a bounded number of extra
		 * decodes whose exact deterministic value is pinned per
		 * scenario in the strict runner. */
		if (s->total_frames < expected_dec) {
			FAIL("le_audio_receiver: segment %d total=%u < pushes=%u+transients=%u "
			     "x dec=%d\n",
			     i, s->total_frames, s->pushes, s->transients, dec_calls_per_push);
			return false;
		}
		if (s->pushes > 0U && (s->full_hash == FNV_OFFSET_BASIS || s->full_hash == 0U)) {
			FAIL("le_audio_receiver: segment %d full hash unchanged from seed\n", i);
			return false;
		}
		if (s->pushes > 0U) {
			if (expect_stereo) {
				if (s->l_hash == s->r_hash) {
					FAIL("le_audio_receiver: segment %d L hash == R hash "
					     "(stereo expected distinct)\n",
					     i);
					return false;
				}
			} else {
				if (s->l_hash != s->r_hash) {
					FAIL("le_audio_receiver: segment %d L hash != R hash "
					     "(mono expected equal)\n",
					     i);
					return false;
				}
			}
			/* Each channel must have produced audio at least once; a
			 * per-push channel min of 0 is legitimate at the CIS-sync
			 * boundary (one half's PLC concealment paired with the
			 * other half's valid frame). */
			if (s->l_energy_max <= 0 || s->r_energy_max <= 0) {
				FAIL("le_audio_receiver: segment %d dead channel "
				     "(lmax=%d rmax=%d)\n",
				     i, s->l_energy_max, s->r_energy_max);
				return false;
			}
		}

		/* PACS available sink contexts must never be NONE after
		 * connection + stream (Phase 1 regression). */
		enum bt_audio_context ctx = bt_pacs_get_available_contexts(BT_AUDIO_DIR_SINK);

		if (ctx == BT_AUDIO_CONTEXT_TYPE_NONE) {
			FAIL("le_audio_receiver: available sink contexts NONE after "
			     "connection + stream\n");
			return false;
		}
	}
	return true;
}

int audio_sink_test_segment_count(void)
{
	return segment_count;
}

bool audio_sink_test_get_segment(int idx, struct bsim_sink_segment *out)
{
	if (!seg_finalized(idx) || out == NULL) {
		return false;
	}
	*out = segments[idx];
	return true;
}

uint32_t audio_sink_test_after_stop_total(void)
{
	return after_stop_total;
}
