/* SPDX-License-Identifier: MIT */
#include "h4_rx.h"

#include <errno.h>
#include <string.h>

void h4_rx_init(struct h4_rx *rx)
{
	memset(rx, 0, sizeof(*rx));
}

int h4_rx_feed(struct h4_rx *rx, const uint8_t *data, size_t len, h4_packet_fn deliver, void *user)
{
	if (rx == NULL || deliver == NULL || (data == NULL && len != 0)) {
		return -EINVAL;
	}
	if (rx->error) {
		return rx->error;
	}
	for (size_t i = 0; i < len; i++) {
		if (rx->type == 0) {
			switch (data[i]) {
			case 1:
				rx->header_size = 3;
				break; /* Command */
			case 2:        /* ACL */
			case 5:
				rx->header_size = 4;
				break; /* ISO */
			default:
				rx->error = -EPROTO;
				return rx->error;
			}
			rx->type = data[i];
			rx->used = 0;
			rx->expected = rx->header_size;
			continue;
		}
		rx->frame[rx->used++] = data[i];
		if (rx->used == rx->header_size) {
			size_t payload = rx->frame[2];
			if (rx->type != 1) {
				payload |= (size_t)rx->frame[3] << 8;
				if (rx->type == 5) {
					payload &= 0x3fffU;
				}
			}
			if (payload > sizeof(rx->frame) - rx->header_size) {
				rx->error = -EMSGSIZE;
				return rx->error;
			}
			rx->expected += payload;
		}
		if (rx->used == rx->expected) {
			int err = deliver(rx->type, rx->frame, rx->used, user);
			if (err) {
				rx->error = err;
				return err;
			}
			rx->type = 0;
		}
	}
	return 0;
}
