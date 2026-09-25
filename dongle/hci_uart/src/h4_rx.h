/* SPDX-License-Identifier: MIT */
#ifndef DONGLE_H4_RX_H
#define DONGLE_H4_RX_H

#include <stddef.h>
#include <stdint.h>

/* Bound above the configured 255-byte HCI data payloads, including headers. */
#define H4_RX_FRAME_MAX 1028U

struct h4_rx {
	uint8_t type;
	uint8_t header_size;
	size_t used;
	size_t expected;
	int error;
	uint8_t frame[H4_RX_FRAME_MAX];
};

typedef int (*h4_packet_fn)(uint8_t type, const uint8_t *packet, size_t len, void *user);

void h4_rx_init(struct h4_rx *rx);
/* Feed any fragmentation of an H4 byte stream. Packet excludes the H4 type.
 * Invalid framing or delivery failure latches an error until explicit reset;
 * never scan arbitrary payload bytes as possible new packet boundaries. */
int h4_rx_feed(struct h4_rx *rx, const uint8_t *data, size_t len, h4_packet_fn deliver, void *user);

#endif
