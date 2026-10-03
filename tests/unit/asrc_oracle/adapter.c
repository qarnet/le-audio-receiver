/* SPDX-License-Identifier: Apache-2.0 */
/* Host ABI plumbing only. No expected samples or private-state assertions. */
#include "../../../src/audio_asrc.h"
#include "../../../src/flpr_audio_process.h"
#include "../../../src/flpr_ring.h"
#include <stdlib.h>
#include <stdatomic.h>

struct stream {
	struct audio_asrc ctx;
	int16_t left, right;
	bool valid;
};

void *oracle_stream_new(uint32_t input_rate, uint32_t output_rate)
{
	struct stream *s = calloc(1, sizeof(*s));
	if (s && audio_asrc_init(&s->ctx, input_rate, output_rate)) {
		free(s);
		return NULL;
	}
	return s;
}

void oracle_stream_free(void *stream)
{
	free(stream);
}

void oracle_stream_reset(void *stream)
{
	struct stream *s = stream;
	audio_asrc_reset(&s->ctx);
	s->left = s->right = 0;
	s->valid = false;
}

int oracle_stream_process(void *stream, const int16_t *input, size_t frames, int16_t *output,
			  size_t capacity, int32_t ppm, int backend, size_t *consumed,
			  size_t *produced)
{
	struct stream *s = stream;
	int16_t left = 0, right = 0;
	int ret;
	*consumed = *produced = 0;
	if (backend) {
		struct flpr_ring_slot_meta in = {
			.valid_frames = (uint16_t)frames,
			.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR,
			.correction_ppm = ppm,
		};
		struct flpr_ring_slot_meta out;
		audio_asrc_state_export(&s->ctx, s->left, s->right, s->valid, &in.asrc_state);
		ret = flpr_audio_process(&in, (const uint8_t *)input, frames * 4, &out,
					 (uint8_t *)output, capacity * 4);
		if (ret) {
			return ret;
		}
		/* Public continuity token is opaque to the Python oracle. */
		ret = audio_asrc_state_import(&s->ctx, &out.asrc_state, &s->left, &s->right,
					      &s->valid);
		*consumed = frames;
		*produced = out.valid_frames;
		return ret;
	}
	ret = audio_asrc_process(&s->ctx, input, frames, output, capacity, ppm, s->left, s->right,
				 s->valid, consumed, produced, &left, &right);
	if (!ret && frames) {
		s->left = left;
		s->right = right;
		s->valid = true;
	}
	return ret;
}

/* Host platform fences for unused ring transport functions. Arithmetic lane
 * exercises the real CRC implementation, not shared-memory transport. */
void flpr_cache_write_barrier(void)
{
	atomic_thread_fence(memory_order_release);
}
void flpr_cache_read_barrier(void)
{
	atomic_thread_fence(memory_order_acquire);
}
void flpr_cache_full_barrier(void)
{
	atomic_thread_fence(memory_order_seq_cst);
}
