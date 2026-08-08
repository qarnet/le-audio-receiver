/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake IPC service backend implementation.  See fake_ipc_backend.h.
 */
#include "fake_ipc_backend.h"

#include <errno.h>
#include <string.h>

#include <zephyr/ipc/ipc_service_backend.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

#define DT_DRV_COMPAT fake_ipc_backend

/* ── Test controls ───────────────────────────────────────────── */

static int fake_open_ret;
static int fake_register_ret;
static int fake_deregister_ret;
static int fake_send_ret;
static bool fake_auto_bound = true;
static bool fake_send_block;
static struct k_sem fake_send_block_sem;
static bool fake_sem_initialized;

static uint32_t fake_send_calls;
static struct flpr_msg fake_sent[FAKE_IPC_MAX_SENT];
static uint32_t fake_sent_count;

/* ── Backend data ─────────────────────────────────────────────── */

struct fake_ipc_backend_data {
	const struct ipc_ept_cfg *cfg; /* captured endpoint config */
};

static struct fake_ipc_backend_data fake_data;

static void fake_ensure_sem(void)
{
	if (!fake_sem_initialized) {
		k_sem_init(&fake_send_block_sem, 0, 1);
		fake_sem_initialized = true;
	}
}

static int fake_open_instance(const struct device *instance)
{
	(void)instance;
	return fake_open_ret;
}

static int fake_register_ept(const struct device *instance, void **token,
			     const struct ipc_ept_cfg *cfg)
{
	(void)instance;
	(void)token;
	if (fake_register_ret < 0) {
		return fake_register_ret;
	}
	fake_data.cfg = cfg;
	if (fake_auto_bound && cfg && cfg->cb.bound) {
		cfg->cb.bound(cfg->priv);
	}
	return 0;
}

static int fake_deregister_ept(const struct device *instance, void *token)
{
	(void)instance;
	(void)token;
	if (fake_deregister_ret < 0) {
		return fake_deregister_ret;
	}
	fake_data.cfg = NULL;
	return 0;
}

static int fake_send(const struct device *instance, void *token, const void *data, size_t len)
{
	(void)instance;
	(void)token;
	(void)len;

	fake_send_calls++;

	if (fake_send_block) {
		/* Park the caller until the test releases the worker. */
		k_sem_take(&fake_send_block_sem, K_FOREVER);
	}

	if (fake_send_ret < 0) {
		fake_sent_count++;
		return fake_send_ret;
	}

	if (fake_sent_count < FAKE_IPC_MAX_SENT && len >= sizeof(struct flpr_msg)) {
		memcpy(&fake_sent[fake_sent_count], data, sizeof(struct flpr_msg));
	}
	fake_sent_count++;
	return 0;
}

static const struct ipc_service_backend fake_backend_ops = {
	.open_instance = fake_open_instance,
	.register_endpoint = fake_register_ept,
	.deregister_endpoint = fake_deregister_ept,
	.send = fake_send,
};

#define DEFINE_FAKE_BACKEND(i)                                                                     \
	DEVICE_DT_INST_DEFINE(i, NULL, NULL, &fake_data, NULL, POST_KERNEL,                        \
			      CONFIG_IPC_SERVICE_REG_BACKEND_PRIORITY, &fake_backend_ops);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_FAKE_BACKEND)

/* ── Test controls ───────────────────────────────────────────── */

void fake_ipc_reset(void)
{
	fake_ensure_sem();
	fake_open_ret = 0;
	fake_register_ret = 0;
	fake_deregister_ret = 0;
	fake_send_ret = 0;
	fake_auto_bound = true;
	fake_send_block = false;
	fake_send_calls = 0;
	fake_sent_count = 0;
	memset(fake_sent, 0, sizeof(fake_sent));
	fake_data.cfg = NULL;
}

void fake_ipc_set_open_result(int ret)
{
	fake_open_ret = ret;
}

void fake_ipc_set_register_result(int ret)
{
	fake_register_ret = ret;
}

void fake_ipc_set_deregister_result(int ret)
{
	fake_deregister_ret = ret;
}

void fake_ipc_set_send_result(int ret)
{
	fake_send_ret = ret;
}

void fake_ipc_set_auto_bound(bool enable)
{
	fake_auto_bound = enable;
}

void fake_ipc_set_send_block(bool enable)
{
	fake_ensure_sem();
	fake_send_block = enable;
}

void fake_ipc_receive(const void *data, size_t len)
{
	if (fake_data.cfg && fake_data.cfg->cb.received) {
		fake_data.cfg->cb.received(data, len, fake_data.cfg->priv);
	}
}

void fake_ipc_unbind(void)
{
	if (fake_data.cfg && fake_data.cfg->cb.unbound) {
		fake_data.cfg->cb.unbound(fake_data.cfg->priv);
	}
}

void fake_ipc_error(const char *message)
{
	if (fake_data.cfg && fake_data.cfg->cb.error) {
		fake_data.cfg->cb.error(message, fake_data.cfg->priv);
	}
}

uint32_t fake_ipc_sent_count(void)
{
	return fake_sent_count;
}

const struct flpr_msg *fake_ipc_sent_at(uint32_t i)
{
	return (i < fake_sent_count) ? &fake_sent[i] : NULL;
}

uint32_t fake_ipc_sent_type_count(uint8_t type)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < fake_sent_count; i++) {
		if (fake_sent[i].type == type) {
			n++;
		}
	}
	return n;
}

uint32_t fake_ipc_send_calls(void)
{
	return fake_send_calls;
}

bool fake_ipc_endpoint_registered(void)
{
	return fake_data.cfg != NULL;
}
