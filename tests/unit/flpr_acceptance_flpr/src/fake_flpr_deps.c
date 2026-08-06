/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake transport for the FLPR-image acceptance native suite: a send
 * recorder + wake counter injected as the flpr_acceptance_deps table.
 */
#include "fake_flpr_deps.h"

#include <errno.h>
#include <string.h>

static struct flpr_msg fake_sent[FAKE_DEPS_MAX_SENT];
static uint32_t fake_sent_count;
static uint32_t fake_wake_count;

int fake_deps_send(const struct flpr_msg *msg)
{
	if (!msg) {
		return -EINVAL;
	}
	if (fake_sent_count < FAKE_DEPS_MAX_SENT) {
		fake_sent[fake_sent_count] = *msg;
	}
	fake_sent_count++;
	return 0;
}

void fake_deps_wake(void)
{
	fake_wake_count++;
}

void fake_deps_reset(void)
{
	fake_sent_count = 0;
	fake_wake_count = 0;
	memset(fake_sent, 0, sizeof(fake_sent));
}

uint32_t fake_deps_sent_count(void)
{
	return fake_sent_count;
}

const struct flpr_msg *fake_deps_sent_at(uint32_t i)
{
	return (i < fake_sent_count) ? &fake_sent[i] : NULL;
}

uint32_t fake_deps_sent_type_count(uint8_t type)
{
	uint32_t n = 0;

	for (uint32_t i = 0; i < fake_sent_count; i++) {
		if (fake_sent[i].type == type) {
			n++;
		}
	}
	return n;
}

uint32_t fake_deps_wake_count(void)
{
	return fake_wake_count;
}
