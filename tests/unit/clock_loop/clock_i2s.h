/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CLOCK_I2S_H
#define CLOCK_I2S_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

struct clock_capture {
	uint64_t time_us;
	size_t frames;
	int16_t pcm[962];
};
struct clock_snapshot {
	uint64_t epoch_us, time_us, transferred, missing, remaining, submitted, cancelled,
		bad_words;
	uint32_t queued, duplicates;
};
uint64_t reference_time_us(void);
void clock_i2s_prepare(int32_t pclk_ppm);
uint64_t clock_i2s_set_skew(int32_t pclk_ppm);
typedef bool (*clock_oracle_fn)(const int16_t *pcm, size_t frames, int16_t *expected,
				void *context);
void clock_i2s_set_oracle(clock_oracle_fn oracle, void *context);
int16_t *clock_i2s_next_owned_word(void);
int clock_i2s_wait_blocked(k_timeout_t timeout);
void clock_i2s_clear_capture(void);
size_t clock_i2s_capture_count(void);
const struct clock_capture *clock_i2s_capture_get(size_t index);
void clock_i2s_snapshot(struct clock_snapshot *result);
#endif
