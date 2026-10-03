/* Host model of DMA register pointer and async receive state. Real driver
 * functions are injected by test_hci_uarte.py, not rewritten here.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UARTE_MAGIC_BYTE         0xAA
#define UARTE_ANY_CACHE          0
#define UARTE_CFG_FLAG_CACHEABLE 1
#define IS_ENABLED(x)            (x)
#define ARG_UNUSED(x)            (void)(x)
#define MIN(a, b)                ((a) < (b) ? (a) : (b))
#define __ASSERT(condition, ...) assert(condition)
static void sys_cache_data_invd_range(void *buf, size_t len)
{
	(void)buf;
	(void)len;
}
static void sys_cache_data_flush_range(void *buf, size_t len)
{
	(void)buf;
	(void)len;
}

struct uarte_async_rx_cbwt {
	uint8_t *curr_bounce_buf;
	uint8_t *anomaly_byte_addr;
	uint8_t *anomaly_byte_dst;
	uint8_t anomaly_byte;
	uint32_t bounce_idx, bounce_off, bounce_limit, last_cnt, usr_wr_off;
};
struct uarte_async_rx {
	uint8_t *buf;
	uint32_t buf_len;
};
struct uarte_async {
	struct uarte_async_rx rx;
};
struct uarte_nrfx_data {
	struct uarte_async *async;
};
struct uarte_regs {
	struct {
		struct {
			uint32_t PTR;
		} RX;
	} DMA;
};
struct uarte_nrfx_config {
	struct uarte_async_rx_cbwt *cbwt_data;
	struct uarte_regs *uarte_regs;
	uint8_t *bounce_buf[2];
	uint32_t bounce_buf_len, flags;
};
struct device {
	const struct uarte_nrfx_config *config;
	struct uarte_nrfx_data *data;
};

#include "functions.inc"

static int replay(unsigned int offset, uint8_t stale, uint8_t incoming, bool old_tail)
{
	uint8_t old[512], next[512], user[5] = {0};
	struct uarte_async_rx_cbwt cbwt = {0};
	struct uarte_async async = {.rx = {.buf = user, .buf_len = sizeof(user)}};
	struct uarte_nrfx_data data = {.async = &async};
	struct uarte_regs regs = {0};
	struct uarte_nrfx_config cfg = {
		.cbwt_data = &cbwt,
		.uarte_regs = &regs,
		.bounce_buf = {old, next},
		.bounce_buf_len = sizeof(old),
	};
	struct device dev = {.config = &cfg, .data = &data};
	uint8_t expected[5] = {0x8e, 0x11, 0x21, incoming, 0x1f};

	memset(old, 0x67, sizeof(old));
	memset(next, 0x49, sizeof(next));
	old[offset + 3] = stale; /* byte retained from old buffer's prior turn */
	prepare_bounce_buf(&dev, old, 112, sizeof(old));
	old[offset] = expected[0];
	old[offset + 1] = expected[1];
	old[offset + 2] = expected[2];
	cbwt.curr_bounce_buf = next;
	cbwt.anomaly_byte_addr = &old[offset + 3];
	cbwt.bounce_idx = 0;
	cbwt.bounce_off = offset;
	cbwt.bounce_limit = offset + 3;
	prepare_bounce_buf(&dev, next, 112, sizeof(next));
	if (old_tail) {
		old[offset + 3] = incoming;
	} else {
		next[0] = incoming;
	}
	next[1] = 0x1f;
	/* nRF DMA PTR is 32 bits. Truncation models register address on 64-bit host. */
	regs.DMA.RX.PTR = (uint32_t)(uintptr_t)next + 2;
	anomaly_byte_handle(&dev);
	if (fill_usr_buf(&dev, 3) != 3 || fill_usr_buf(&dev, 2) != 2 ||
	    cbwt.usr_wr_off != sizeof(user) || memcmp(user, expected, sizeof(user))) {
		fprintf(stderr, "offset=%u stale=%02x incoming=%02x old_tail=%u got=%02x\n", offset,
			stale, incoming, old_tail, user[3]);
		return 1;
	}
	return 0;
}

int main(void)
{
	unsigned int failures = 0, total = 0;
	const unsigned int offsets[] = {107, 109, 112, 125};
	const uint8_t stale[] = {0x49, 0x03, 0x00, 0xaa};
	for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		for (unsigned int j = 0; j < sizeof(stale); ++j) {
			for (unsigned int byte = 0; byte < 256; ++byte) {
				failures += replay(offsets[i], stale[j], byte, false);
				total++;
				failures += replay(offsets[i], stale[j], byte, true);
				total++;
			}
		}
	}
	printf("UARTE emitted-user-buffer replay: %u/%u mismatches\n", failures, total);
	return failures ? 1 : 0;
}
