/* SPDX-License-Identifier: Apache-2.0 */
/* A native I2S API device, not a physical DMA timing model. Word transfer is
 * driven independently by elapsed reference ticks and declared PCLK ratio. */
#define DT_DRV_COMPAT vnd_audio_i2s_fake
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <string.h>
#include "clock_i2s.h"

#define CAPACITY 15
static struct i2s_config config;
static bool configured, running, error;
static struct k_spinlock lock;
static struct {
	void *ptr;
	size_t frames, remaining;
	int16_t expected[962];
} queue[CAPACITY];
static unsigned head, count;
static struct clock_capture captures[16];
static size_t capture_count;
static uint64_t rate_num, fractional, epoch_us, last_us, transferred, missing, submitted, cancelled,
	bad_words;
static clock_oracle_fn oracle;
static void *oracle_context;
static uint32_t duplicates;
static K_SEM_DEFINE(space, 0, 1);
static K_SEM_DEFINE(blocked, 0, 1);

uint64_t reference_time_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

static unsigned drain_at(uint64_t now, void **released)
{
	unsigned released_count = 0;
	if (!running) {
		return 0;
	}
	uint64_t budget = (now - last_us) * rate_num + fractional;
	uint64_t due = budget / 1000000000000ULL;
	fractional = budget % 1000000000000ULL;
	last_us = now;
	while (due && count) {
		size_t n = MIN(due, queue[head].remaining);
		size_t offset = queue[head].frames - queue[head].remaining;
		const int16_t *live = queue[head].ptr;
		for (size_t i = offset * 2; i < (offset + n) * 2; i++) {
			if (live[i] != queue[head].expected[i]) {
				bad_words++;
			}
		}
		queue[head].remaining -= n;
		due -= n;
		transferred += n;
		if (!queue[head].remaining) {
			released[released_count++] = queue[head].ptr;
			head = (head + 1) % CAPACITY;
			count--;
		}
	}
	if (due) {
		missing += due;
		error = true;
	}
	return released_count;
}
static void release_completed(void **released, unsigned n)
{
	for (unsigned i = 0; i < n; i++) {
		k_mem_slab_free(config.mem_slab, released[i]);
	}
	if (n) {
		k_sem_give(&space);
	}
}
static void tick(struct k_timer *timer)
{
	(void)timer;
	void *released[CAPACITY];
	k_spinlock_key_t key = k_spin_lock(&lock);
	unsigned n = drain_at(reference_time_us(), released);
	k_spin_unlock(&lock, key);
	release_completed(released, n);
}
K_TIMER_DEFINE(word_clock, tick, NULL);

static void purge(void)
{
	while (count) {
		cancelled += queue[head].remaining;
		k_mem_slab_free(config.mem_slab, queue[head].ptr);
		head = (head + 1) % CAPACITY;
		count--;
	}
}

void clock_i2s_prepare(int32_t pclk_ppm)
{
	k_timer_stop(&word_clock);
	k_spinlock_key_t key = k_spin_lock(&lock);
	purge();
	running = error = false;
	rate_num = 47619ULL * (1000000 + pclk_ppm);
	fractional = transferred = missing = submitted = cancelled = bad_words = 0;
	duplicates = 0;
	capture_count = 0;
	k_sem_reset(&blocked);
	k_sem_reset(&space);
	k_spin_unlock(&lock, key);
}
uint64_t clock_i2s_set_skew(int32_t pclk_ppm)
{
	void *released[CAPACITY];
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint64_t effective = reference_time_us();
	unsigned n = drain_at(effective, released);
	rate_num = 47619ULL * (1000000 + pclk_ppm);
	k_spin_unlock(&lock, key);
	release_completed(released, n);
	return effective;
}
void clock_i2s_set_oracle(clock_oracle_fn callback, void *context)
{
	oracle = callback;
	oracle_context = context;
}
int16_t *clock_i2s_next_owned_word(void)
{
	__ASSERT(count, "No owned descriptor");
	return (int16_t *)queue[head].ptr + (queue[head].frames - queue[head].remaining) * 2;
}
int clock_i2s_wait_blocked(k_timeout_t timeout)
{
	return k_sem_take(&blocked, timeout);
}
void clock_i2s_clear_capture(void)
{
	capture_count = 0;
}
size_t clock_i2s_capture_count(void)
{
	return capture_count;
}
const struct clock_capture *clock_i2s_capture_get(size_t index)
{
	__ASSERT(index < capture_count, "capture index");
	return &captures[index];
}
void clock_i2s_snapshot(struct clock_snapshot *result)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	*result = (struct clock_snapshot){.epoch_us = epoch_us,
					  .time_us = last_us,
					  .transferred = transferred,
					  .missing = missing,
					  .submitted = submitted,
					  .cancelled = cancelled,
					  .bad_words = bad_words,
					  .queued = count,
					  .duplicates = duplicates};
	for (unsigned i = 0; i < count; i++) {
		result->remaining += queue[(head + i) % CAPACITY].remaining;
	}
	k_spin_unlock(&lock, key);
}

static int clock_configure(const struct device *dev, enum i2s_dir dir, const struct i2s_config *cfg)
{
	(void)dev;
	if (dir != I2S_DIR_TX) {
		return -EINVAL;
	}
	config = *cfg;
	configured = true;
	return 0;
}
static const struct i2s_config *clock_config_get(const struct device *dev, enum i2s_dir dir)
{
	(void)dev;
	return configured && dir == I2S_DIR_TX ? &config : NULL;
}
static int clock_write(const struct device *dev, void *block, size_t bytes)
{
	(void)dev;
	if (!configured || !bytes || bytes > 1924 || bytes % 4) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	while (count == CAPACITY && !error) {
		k_sem_reset(&space);
		k_spin_unlock(&lock, key);
		k_sem_give(&blocked);
		if (k_sem_take(&space, K_MSEC(config.timeout))) {
			return -EAGAIN;
		}
		key = k_spin_lock(&lock);
	}
	if (error) {
		k_spin_unlock(&lock, key);
		return -EIO;
	}
	for (unsigned i = 0; i < count; i++) {
		if (queue[(head + i) % CAPACITY].ptr == block) {
			duplicates++;
			k_spin_unlock(&lock, key);
			return -EBUSY;
		}
	}
	__ASSERT(capture_count < ARRAY_SIZE(captures), "per-push capture overflow");
	struct clock_capture *capture = &captures[capture_count++];
	capture->time_us = reference_time_us();
	capture->frames = bytes / 4;
	memcpy(capture->pcm, block, bytes);
	__ASSERT(oracle, "Independent waveform oracle must be registered");
	if (!oracle(block, bytes / 4, queue[(head + count) % CAPACITY].expected, oracle_context)) {
		bad_words++;
	}
	queue[(head + count) % CAPACITY].ptr = block;
	queue[(head + count) % CAPACITY].frames = bytes / 4;
	queue[(head + count) % CAPACITY].remaining = bytes / 4;
	count++;
	submitted += bytes / 4;
	k_spin_unlock(&lock, key);
	return 0;
}
static int clock_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd command)
{
	(void)dev;
	if (dir != I2S_DIR_TX) {
		return -EINVAL;
	}
	if (command == I2S_TRIGGER_START) {
		k_spinlock_key_t key = k_spin_lock(&lock);
		running = true;
		epoch_us = last_us = reference_time_us();
		fractional = 0;
		transferred = missing = cancelled = 0;
		submitted = 0;
		for (unsigned i = 0; i < count; i++) {
			submitted += queue[(head + i) % CAPACITY].remaining;
		}
		k_spin_unlock(&lock, key);
		k_timer_start(&word_clock, K_USEC(500), K_USEC(500));
		return 0;
	}
	if (command != I2S_TRIGGER_PREPARE && command != I2S_TRIGGER_DROP) {
		return -EINVAL;
	}
	tick(NULL);
	k_timer_stop(&word_clock);
	k_spinlock_key_t key = k_spin_lock(&lock);
	running = error = false;
	purge();
	k_spin_unlock(&lock, key);
	return 0;
}
static int clock_read(const struct device *dev, void **block, size_t *bytes)
{
	(void)dev;
	(void)block;
	(void)bytes;
	return -ENOTSUP;
}
static DEVICE_API(i2s, api) = {.configure = clock_configure,
			       .config_get = clock_config_get,
			       .write = clock_write,
			       .read = clock_read,
			       .trigger = clock_trigger};
static int init(const struct device *dev)
{
	(void)dev;
	return 0;
}
#define DEFINE_DEVICE(inst)                                                                        \
	DEVICE_DT_INST_DEFINE(inst, init, NULL, NULL, NULL, POST_KERNEL, 50, &api);
DT_INST_FOREACH_STATUS_OKAY(DEFINE_DEVICE)
