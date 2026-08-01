/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake I2S driver for the I2S sink state-machine suites.
 *
 * A real driver with the exact public API (struct i2s_driver_api from
 * NCS v3.3.0 include/zephyr/drivers/i2s.h), instantiated as a normal
 * devicetree device so production DT_ALIAS(i2s_audio) / DEVICE_DT_GET()
 * / i2s_configure() / i2s_write() / i2s_trigger() calls remain real.
 *
 * Ownership semantics deliberately match nrfx I2S (see fake_i2s.h).
 */

#define DT_DRV_COMPAT vnd_audio_i2s_fake

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/util.h>

#include "fake_i2s.h"

static struct k_mem_slab *fake_slab; /* captured at configure */

static struct fake_i2s_cfg_rec cfg_rec;
static struct fake_i2s_write_rec writes[FAKE_I2S_MAX_WRITES];
static struct fake_i2s_trigger_rec triggers[FAKE_I2S_MAX_TRIGGERS];
static void *queue[FAKE_I2S_MAX_QUEUED];

static int write_count;
static int trigger_count;
static int queued_count;
static int configure_calls;
static int duplicates;

static int configure_ret;
static int write_fail_at = -1;
static int write_fail_errno = -EIO;
static int trigger_ret[4]; /* indexed by i2s_trigger_cmd value */

static void purge_queue(void)
{
	while (queued_count > 0) {
		void *ptr = queue[--queued_count];

		k_mem_slab_free(fake_slab, ptr);
	}
}

static int fake_i2s_configure(const struct device *dev, enum i2s_dir dir,
			      const struct i2s_config *cfg)
{
	(void)dev;
	configure_calls++;

	if (configure_ret < 0) {
		return configure_ret;
	}

	memset(&cfg_rec, 0, sizeof(cfg_rec));
	cfg_rec.captured = true;
	cfg_rec.dir = dir;
	cfg_rec.cfg = *cfg; /* deep copy; mem_slab pointer preserved */
	fake_slab = cfg->mem_slab;
	return 0;
}

static const struct i2s_config *fake_i2s_config_get(const struct device *dev, enum i2s_dir dir)
{
	(void)dev;
	(void)dir;
	return cfg_rec.captured ? &cfg_rec.cfg : NULL;
}

static int fake_i2s_write(const struct device *dev, void *mem_block, size_t size)
{
	(void)dev;

	if (write_count >= FAKE_I2S_MAX_WRITES) {
		__ASSERT(false, "fake i2s write record overflow");
		return -ENOMEM;
	}

	struct fake_i2s_write_rec *rec = &writes[write_count];

	rec->ptr = mem_block;
	rec->size = size;
	memset(rec->snapshot, 0, sizeof(rec->snapshot));
	memcpy(rec->snapshot, mem_block, MIN(size, FAKE_I2S_SNAPSHOT_BYTES));

	if (write_count == write_fail_at) {
		write_count++;
		return write_fail_errno; /* never takes ownership */
	}

	/* Double-submit detection: a pointer still queued (driver-owned)
	 * must never be written again in the same ownership lifetime.
	 */
	if (fake_i2s_ptr_queued(mem_block)) {
		duplicates++;
		write_count++;
		return -EBUSY;
	}

	if (queued_count >= FAKE_I2S_MAX_QUEUED) {
		__ASSERT(false, "fake i2s queue overflow");
		write_count++;
		return -ENOMEM;
	}

	queue[queued_count++] = mem_block; /* ownership transferred */
	write_count++;
	return 0;
}

static int fake_i2s_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	(void)dev;
	(void)dir;

	if (trigger_count >= FAKE_I2S_MAX_TRIGGERS) {
		__ASSERT(false, "fake i2s trigger record overflow");
		return -ENOMEM;
	}

	triggers[trigger_count++].cmd = cmd;

	int ret = trigger_ret[cmd];

	if (ret < 0) {
		return ret; /* failed trigger: no state change */
	}

	/* Successful PREPARE/DROP purge queued blocks (nrfx ownership).
	 * START must not touch the queue.
	 */
	if (cmd == I2S_TRIGGER_PREPARE || cmd == I2S_TRIGGER_DROP) {
		purge_queue();
	}
	return 0;
}

static int fake_i2s_read(const struct device *dev, void **mem_block, size_t *size)
{
	(void)dev;
	(void)mem_block;
	(void)size;
	return -ENOTSUP;
}

static DEVICE_API(i2s, fake_i2s_api) = {
	.configure = fake_i2s_configure,
	.config_get = fake_i2s_config_get,
	.trigger = fake_i2s_trigger,
	.read = fake_i2s_read,
	.write = fake_i2s_write,
};

static int fake_i2s_init(const struct device *dev)
{
	(void)dev;
	return 0;
}

DEVICE_DT_INST_DEFINE(0, &fake_i2s_init, NULL, NULL, NULL, POST_KERNEL, CONFIG_I2S_INIT_PRIORITY,
		      &fake_i2s_api);

/* ── test-facing API ─────────────────────────────────────────────── */

void fake_i2s_reset(void)
{
	if (fake_slab != NULL) {
		purge_queue();
	}
	memset(writes, 0, sizeof(writes));
	memset(triggers, 0, sizeof(triggers));
	memset(&cfg_rec, 0, sizeof(cfg_rec));
	write_count = 0;
	trigger_count = 0;
	queued_count = 0;
	configure_calls = 0;
	duplicates = 0;
	configure_ret = 0;
	write_fail_at = -1;
	write_fail_errno = -EIO;
	memset(trigger_ret, 0, sizeof(trigger_ret));
	/* The captured slab is deliberately RETAINED across reset: a real
	 * driver keeps its configured mem_slab even when the stream stops
	 * and restarts without re-configuration (audio_sink_stop keeps
	 * configured=true).  Only a fresh configure re-captures it.
	 */
}

void fake_i2s_set_configure_ret(int ret)
{
	configure_ret = ret;
}

void fake_i2s_fail_write_at(int call_index)
{
	write_fail_at = call_index;
}

void fake_i2s_set_write_fail_errno(int err)
{
	write_fail_errno = err;
}

void fake_i2s_set_trigger_ret(enum i2s_trigger_cmd cmd, int ret)
{
	trigger_ret[cmd] = ret;
}

int fake_i2s_configure_calls(void)
{
	return configure_calls;
}

int fake_i2s_write_calls(void)
{
	return write_count;
}

int fake_i2s_trigger_calls(void)
{
	return trigger_count;
}

int fake_i2s_queued_count(void)
{
	return queued_count;
}

const struct fake_i2s_write_rec *fake_i2s_write_rec(int idx)
{
	return &writes[idx];
}

const struct fake_i2s_trigger_rec *fake_i2s_trigger_rec(int idx)
{
	return &triggers[idx];
}

const struct fake_i2s_cfg_rec *fake_i2s_cfg_rec(void)
{
	return cfg_rec.captured ? &cfg_rec : NULL;
}

void *fake_i2s_queued_ptr(int idx)
{
	return queue[idx];
}

bool fake_i2s_ptr_queued(void *ptr)
{
	for (int i = 0; i < queued_count; i++) {
		if (queue[i] == ptr) {
			return true;
		}
	}
	return false;
}

int fake_i2s_duplicate_write_violations(void)
{
	return duplicates;
}

void fake_i2s_release(void *ptr)
{
	for (int i = 0; i < queued_count; i++) {
		if (queue[i] == ptr) {
			/* DMA completion: free back to the captured slab. */
			k_mem_slab_free(fake_slab, ptr);
			memmove(&queue[i], &queue[i + 1],
				(queued_count - i - 1) * sizeof(queue[0]));
			queued_count--;
			return;
		}
	}
	__ASSERT(false, "fake i2s release of unknown pointer");
}

void fake_i2s_release_all(void)
{
	purge_queue();
}
