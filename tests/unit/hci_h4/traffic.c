/* SPDX-License-Identifier: MIT */
#include "h4_rx.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const uint8_t traffic[] = {
	1, 3, 12, 0, 5, 1, 0, 5, 0, 0x11, 0x22, 1, 2, 5, 2, 0, 0, 3, 0, 7, 8, 9,
};
static const size_t offsets[] = {0, 4, 14, sizeof(traffic)};

struct capture {
	unsigned int packets;
	int failure;
};

static int capture(uint8_t type, const uint8_t *data, size_t len, void *user)
{
	struct capture *c = user;
	assert(c->packets < 3);
	size_t first = offsets[c->packets];
	assert(type == traffic[first]);
	assert(len == offsets[c->packets + 1] - first - 1);
	assert(memcmp(data, traffic + first + 1, len) == 0);
	c->packets++;
	return c->failure;
}

static int large(uint8_t type, const uint8_t *data, size_t len, void *user)
{
	unsigned int *count = user;
	assert(type == 5 && len == H4_RX_FRAME_MAX);
	for (size_t i = 4; i < len; i++) {
		assert(data[i] == (uint8_t)i);
	}
	(*count)++;
	return 0;
}

int main(void)
{
	struct h4_rx rx;
	/* Every possible chunk size, and every two-piece split, must retain
	 * complete payloads even when payload bytes resemble H4 headers. */
	for (size_t chunk = 1; chunk <= sizeof(traffic); chunk++) {
		struct capture c = {0};
		h4_rx_init(&rx);
		for (size_t pos = 0; pos < sizeof(traffic); pos += chunk) {
			size_t n = sizeof(traffic) - pos;
			if (n > chunk) {
				n = chunk;
			}
			assert(h4_rx_feed(&rx, traffic + pos, n, capture, &c) == 0);
		}
		assert(c.packets == 3);
	}
	for (size_t split = 0; split <= sizeof(traffic); split++) {
		struct capture c = {0};
		h4_rx_init(&rx);
		assert(h4_rx_feed(&rx, traffic, split, capture, &c) == 0);
		assert(h4_rx_feed(&rx, traffic + split, sizeof(traffic) - split, capture, &c) == 0);
		assert(c.packets == 3);
	}
	/* No partial packet reaches the controller boundary. */
	struct capture c = {0};
	h4_rx_init(&rx);
	assert(h4_rx_feed(&rx, traffic, 3, capture, &c) == 0);
	assert(c.packets == 0);
	assert(h4_rx_feed(&rx, traffic + 3, 1, capture, &c) == 0);
	assert(c.packets == 1);

	/* Delivery failure is terminal until explicit reset, not a silent skip. */
	c = (struct capture){.failure = -ENOMEM};
	h4_rx_init(&rx);
	assert(h4_rx_feed(&rx, traffic, sizeof(traffic), capture, &c) == -ENOMEM);
	assert(c.packets == 1);
	assert(h4_rx_feed(&rx, traffic, 4, capture, &c) == -ENOMEM);
	c = (struct capture){0};
	h4_rx_init(&rx);
	assert(h4_rx_feed(&rx, traffic, sizeof(traffic), capture, &c) == 0);
	assert(c.packets == 3);

	uint8_t unknown[] = {4, 1, 3, 12, 0};
	h4_rx_init(&rx);
	assert(h4_rx_feed(&rx, unknown, sizeof(unknown), capture, &c) == -EPROTO);
	uint8_t oversized[] = {5, 1, 0, 0xff, 0x3f, 1, 3, 12, 0};
	h4_rx_init(&rx);
	assert(h4_rx_feed(&rx, oversized, sizeof(oversized), capture, &c) == -EMSGSIZE);
	assert(h4_rx_feed(&rx, traffic, 4, capture, &c) == -EMSGSIZE);
	assert(h4_rx_feed(NULL, traffic, 4, capture, &c) == -EINVAL);

	uint8_t maximum[H4_RX_FRAME_MAX + 1] = {5, 1, 0, 0, 4};
	unsigned int count = 0;
	for (size_t i = 5; i < sizeof(maximum); i++) {
		maximum[i] = (uint8_t)(i - 1);
	}
	h4_rx_init(&rx);
	for (size_t i = 0; i < sizeof(maximum); i++) {
		assert(h4_rx_feed(&rx, maximum + i, 1, large, &count) == 0);
	}
	assert(count == 1);
	puts("H4 traffic PASS");
	return 0;
}
