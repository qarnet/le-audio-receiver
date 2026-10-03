/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "audio_sink.h"
#include "audio_offload.h"
#include "fake_i2s.h"
#include "oracle_vectors.h"

extern int peer_fault;
extern int32_t scripted_ppm;
void audio_i2s_test_reset_module_state(void);

static void wait_active(void)
{
	for (int i = 0; i < 2000 && !audio_offload_is_healthy(); i++) {
		k_sleep(K_MSEC(1));
	}
	zassert_true(audio_offload_is_healthy(), "Offload did not reach ACTIVE");
}

static void setup(void *unused)
{
	(void)unused;
	audio_offload_stream_stop();
	audio_sink_stop();
	fake_i2s_reset();
	audio_i2s_test_reset_module_state();
	peer_fault = 0;
	zassert_equal(audio_sink_init(), 0);
	zassert_equal(audio_offload_init(), 0);
	audio_offload_stream_start();
	wait_active();
	zassert_equal(audio_sink_stream_open(), 0);
}

static void run_waveform(const struct oracle_block *blocks, bool faults)
{
	audio_sink_set_input_frames(blocks[0].frames);
	for (unsigned i = 0; i < ORACLE_BLOCKS; i++) {
		const struct oracle_block *b = &blocks[i];
		if (i) {
			/* One deterministic DMA completion per push keeps the full
			 * queue below capacity without triggering emergency repeats. */
			fake_i2s_release(fake_i2s_queued_ptr(0));
		}
		struct audio_offload_status before, after;
		audio_offload_get_status(&before);
		scripted_ppm = b->ppm;
		if (faults && i == 2) {
			peer_fault = 1;
		}
		if (faults && i == 4) {
			peer_fault = 2;
		}
		int start = fake_i2s_write_calls();
		zassert_equal(audio_sink_push(b->input, b->frames * 2U), 0);
		int finish = fake_i2s_write_calls();
		zassert_equal(finish - start, i ? 1 : 15, "Unexpected submitted PCM blocks");
		if (!i) {
			uint64_t remainder = 0;
			for (int j = start; j < finish - 1; j++) {
				const struct fake_i2s_write_rec *sil = fake_i2s_write_rec(j);
				uint64_t scaled = (uint64_t)b->frames * 47619 + remainder;
				zassert_equal(sil->size, (scaled / 48000) * 4U);
				remainder = scaled % 48000;
				zassert_true(fake_i2s_ptr_queued(sil->ptr));
				const uint8_t *bytes = sil->ptr;
				for (size_t k = 0; k < sil->size; k++) {
					zassert_equal(bytes[k], 0);
				}
			}
		}
		const struct fake_i2s_write_rec *data = fake_i2s_write_rec(finish - 1);
		zassert_true(fake_i2s_ptr_queued(data->ptr));
		zassert_equal(data->size, b->produced * 4U, "Block %u frame count", i);
		const int16_t *samples = data->ptr;
		for (unsigned k = 0; k < b->produced * 2U; k++) {
			zassert_equal(samples[k], b->output[k], "Block %u sample %u", i, k);
		}
		audio_offload_get_status(&after);
		if (b->frames == 480) {
			if (faults && (i == 2 || i == 4)) {
				zassert_equal(after.success_count, before.success_count);
				zassert_equal(after.fallback_count, before.fallback_count + 1);
				wait_active();
			} else {
				zassert_equal(after.success_count, before.success_count + 1,
					      "Remote success required, not silent CPU fallback");
			}
		} else {
			zassert_equal(after.success_count, before.success_count);
		}
	}
	zassert_equal(fake_i2s_duplicate_write_violations(), 0);
	audio_sink_stop();
	audio_offload_stream_stop();
}

ZTEST(sink_oracle, test_480_remote_fault_cpu_recovery_full_waveform)
{
	run_waveform(v480, true);
}
ZTEST(sink_oracle, test_360_cpu_fallback_full_waveform)
{
	run_waveform(v360, false);
}
ZTEST_SUITE(sink_oracle, NULL, NULL, setup, NULL, NULL);
