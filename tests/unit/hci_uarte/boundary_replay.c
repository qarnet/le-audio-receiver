/* SPDX-License-Identifier: Apache-2.0 */
/* Host MMIO model; functions.inc contains actual extracted SDK driver functions. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UARTE_MAGIC_BYTE                        0xAA
#define UARTE_ANY_CACHE                         0
#define UARTE_CFG_FLAG_CACHEABLE                1
#define UARTE_TIMER_USR_CNT_CH                  1
#define UARTE_TIMER_BUF_SWITCH_CH               2
#define UARTE_FLAG_LATE_CC                      64
#define UARTE_FLAG_TRIG_RXTO                    8
#define UARTE_FLAG_RX_BUF_REQ                   32
#define NRF_UARTE_EVENT_RXDRDY                  1
#define NRF_UARTE_EVENT_RXSTARTED               2
#define NRF_UARTE_INT_RXTO_MASK                 1
#define NRF_UARTE_INT_RXDRDY_MASK               2
#define NRF_UARTE_SHORT_ENDRX_STARTRX           1
#define NRF_UARTE_TASK_STARTRX                  1
#define NRF_TIMER_TASK_CLEAR                    1
#define NRF_TIMER_TASK_START                    2
#define UART_RX_BUF_REQUEST                     1
#define UART_RX_RDY                             2
#define IS_ENABLED(x)                           (x)
#define MIN(a, b)                               ((a) < (b) ? (a) : (b))
#define ARG_UNUSED(x)                           ((void)(x))
#define __ASSERT(condition, ...)                assert(condition)
#define sys_cache_data_invd_range(ptr, len)     ((void)0)
#define sys_cache_data_flush_range(ptr, len)    ((void)0)
#define nrf_timer_cc_set(timer, channel, value) ((void)0)
#define nrf_timer_event_clear(timer, event)     ((void)0)
#define nrf_timer_compare_event_get(channel)    (channel)
#define nrf_timer_int_enable(timer, mask)       ((void)0)
#define nrf_timer_compare_int_get(channel)      (channel)
#define nrf_timer_task_trigger(timer, task)     ((void)0)
#define nrf_uarte_shorts_enable(regs, mask)     ((void)0)
#define nrf_uarte_int_enable(regs, mask)        ((void)(regs), (void)(mask))
#define nrf_uarte_rx_buffer_set(regs, buf, len) ((void)0)
#define nrf_uarte_task_trigger(regs, task)      ((void)0)
#define NRFX_IRQ_PENDING_SET(irq)               ((void)0)
#define atomic_or(flags, bits)                  ((void)0)
#define atomic_and(flags, bits)                 ((void)0)
#ifdef V2
static void scripted_quiet(unsigned int microseconds);
#define k_busy_wait(us) scripted_quiet(us)
#endif

typedef struct {
	int unused;
} NRF_TIMER_Type;
typedef struct {
	struct {
		struct {
			uint32_t PTR;
		} RX;
	} DMA;
} NRF_UARTE_Type;
struct uarte_async_rx_cbwt {
	uint8_t *curr_bounce_buf, *anomaly_byte_addr, *anomaly_byte_dst;
	uint8_t anomaly_byte;
	uint32_t usr_rd_off, usr_wr_off, bounce_off, bounce_limit, last_cnt;
	uint32_t boundary_old_start, boundary_known_end;
	bool boundary_pending;
	uint32_t cc_usr, cc_swap;
	uint8_t bounce_idx;
	bool discard_fifo;
	size_t bounce_buf_swap_len;
};
struct uarte_async_rx {
	uint8_t *buf, *next_buf;
	size_t buf_len, next_buf_len;
	bool discard_fifo;
};
struct uarte_async {
	struct uarte_async_rx rx;
};
struct uarte_nrfx_data {
	struct uarte_async *async;
	int flags;
};
struct uarte_nrfx_config {
	struct uarte_async_rx_cbwt *cbwt_data;
	NRF_UARTE_Type *uarte_regs;
	NRF_TIMER_Type *timer_regs;
	uint8_t *bounce_buf[2];
	size_t bounce_buf_len, bounce_buf_swap_len;
	uint32_t flags;
	int timer_irqn;
};
struct device {
	const struct uarte_nrfx_config *config;
	struct uarte_nrfx_data *data;
};
struct uart_event {
	int type;
	struct {
		struct {
			uint8_t *buf;
			size_t len, offset;
		} rx;
	} data;
};

static uint32_t observed_cnt;
static bool unstable_count, collision;
static uint32_t collision_delta;
#ifdef V2
static unsigned int quiet_mode, quiet_calls;
static NRF_UARTE_Type *scripted_regs;
static uint8_t *scripted_old_spare;
static void scripted_quiet(unsigned int microseconds)
{
	assert(microseconds == 1);
	quiet_calls++;
	if (quiet_mode == 1 && quiet_calls == 1) {
		scripted_regs->DMA.RX.PTR++;
	}
	if (quiet_mode == 2 && quiet_calls == 1) {
		*scripted_old_spare = 0x78;
	}
}
#endif
static uint8_t emitted[2048];
static size_t emitted_len;

static uint32_t get_byte_cnt(NRF_TIMER_Type *timer)
{
	(void)timer;
	if (unstable_count) {
		observed_cnt++;
	}
	return observed_cnt;
}
static int irq_lock(void)
{
	return 0;
}
static void irq_unlock(int key)
{
	(void)key;
}
static void nrf_uarte_event_clear(NRF_UARTE_Type *regs, int event)
{
	(void)regs;
	(void)event;
}
static bool nrf_uarte_event_check(NRF_UARTE_Type *regs, int event)
{
	if (collision && event == NRF_UARTE_EVENT_RXDRDY) {
		regs->DMA.RX.PTR += collision_delta;
	}
	return collision;
}
static void rx_buf_release(const struct device *dev, uint8_t *buf)
{
	(void)dev;
	(void)buf;
}
static void user_callback(const struct device *dev, const struct uart_event *event)
{
	(void)dev;
	if (event->type != UART_RX_RDY) {
		return;
	}
	assert(emitted_len + event->data.rx.len <= sizeof(emitted));
	memcpy(emitted + emitted_len, event->data.rx.buf + event->data.rx.offset,
	       event->data.rx.len);
	emitted_len += event->data.rx.len;
}

#include "functions.inc"

struct context {
	uint8_t a[512], b[512], user[512];
	struct uarte_async_rx_cbwt cbwt;
	struct uarte_async async;
	struct uarte_nrfx_data data;
	NRF_TIMER_Type timer;
	NRF_UARTE_Type regs;
	struct uarte_nrfx_config cfg;
	struct device dev;
};
static unsigned int failures;

static void reset(struct context *c, uint32_t last, uint32_t off)
{
	memset(c, 0, sizeof(*c));
	memset(c->a, 0xaa, sizeof(c->a));
	memset(c->b, 0xaa, sizeof(c->b));
	c->async.rx.buf = c->user;
	c->async.rx.buf_len = sizeof(c->user);
	c->data.async = &c->async;
	c->cfg.cbwt_data = &c->cbwt;
	c->cfg.uarte_regs = &c->regs;
	c->cfg.timer_regs = &c->timer;
	c->cfg.bounce_buf[0] = c->a;
	c->cfg.bounce_buf[1] = c->b;
	c->cfg.bounce_buf_len = sizeof(c->a);
	c->dev.config = &c->cfg;
	c->dev.data = &c->data;
	c->cbwt.curr_bounce_buf = c->b;
	c->cbwt.bounce_idx = 0;
	c->cbwt.bounce_off = off;
	c->cbwt.bounce_limit = sizeof(c->a);
	c->cbwt.last_cnt = last;
	emitted_len = 0;
	unstable_count = false;
	collision = false;
	collision_delta = 0;
#ifdef V2
	quiet_mode = quiet_calls = 0;
	scripted_regs = &c->regs;
	scripted_old_spare = NULL;
#endif
}
static void check(const char *name, const uint8_t *expected, size_t length)
{
	if (emitted_len != length || memcmp(emitted, expected, length) != 0) {
		size_t at = 0;
		while (at < length && at < emitted_len && emitted[at] == expected[at]) {
			at++;
		}
		fprintf(stderr,
			"%s mismatch offset=%zu got=%02x expected=%02x emitted=%zu wanted=%zu\n",
			name, at, at < emitted_len ? emitted[at] : 0,
			at < length ? expected[at] : 0, emitted_len, length);
		failures++;
	}
}

static void captured_54118(uint8_t first, bool prefetched, uint32_t last)
{
	struct context c;
	uint8_t expected[107];
	static const uint8_t old[7] = {0xb7, 0xad, 0x59, 0x28, 0xb2, 0x08, 0xfe};
	reset(&c, last, 107);
	memcpy(c.a + 107, old, 7);
	if (prefetched) {
		c.a[114] = first;
	}
	for (unsigned int i = 0; i < 100; i++) {
		c.b[i] = (uint8_t)(i * 23 + 14);
	}
	if (!prefetched) {
		c.b[0] = first;
	}
	observed_cnt = last + 7;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(c.cbwt.bounce_limit == 115);
	assert(update_usr_buf(&c.dev, 7, true, true));
	assert(emitted_len == 7);
	observed_cnt = last + (prefetched ? 108U : 107U);
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 101;
	assert(update_usr_buf(&c.dev, 100, true, true));
	memcpy(expected, old, 7);
	if (prefetched) {
		expected[7] = first;
		memcpy(expected + 8, c.b, 99);
	} else {
		memcpy(expected + 7, c.b, 100);
	}
	check(prefetched ? "old-prefetched" : "54118", expected, sizeof(expected));
}
static void captured_54119(void)
{
	struct context c;
	uint8_t expected[106];
	static const uint8_t old[6] = {0x40, 0xfb, 0x40, 0x00, 0xef, 0x91};
	reset(&c, 6061440U, 104);
	memcpy(c.a + 104, old, 6);
	for (unsigned int i = 0; i < 100; i++) {
		c.b[i] = (uint8_t)(i * 17 + 0xaa);
	}
	observed_cnt = 6061444U;
	assert(bounce_buf_swap(&c.dev, c.a) == 4);
	observed_cnt = 6061546U;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 101;
	assert(update_usr_buf(&c.dev, 106, true, true));
	memcpy(expected, old, 6);
	memcpy(expected + 6, c.b, 100);
	check("54119-real91", expected, sizeof(expected));
}
static void complete_h4_frame_across_boundary(void)
{
	struct context c;
	uint8_t packet[129] = {0x05, 0x05, 0x20, 0x7c, 0x00, 0x3d, 0x0b, 0x78, 0x00};
	for (unsigned int i = 9; i < sizeof(packet); i++) {
		packet[i] = (uint8_t)(i * 37U);
	}
	packet[46] = 0xaa;
	reset(&c, 6061328U, 107);
	memcpy(c.a + 107, packet, 7);
	memcpy(c.b, packet + 7, 122);
	observed_cnt = 6061335U;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(update_usr_buf(&c.dev, 7, true, true));
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 123;
	observed_cnt += 122;
	assert(update_usr_buf(&c.dev, 122, true, true));
	check("complete-129-byte-H4-ISO", packet, sizeof(packet));
}
static void no_new_bytes_and_safe_prefix(void)
{
	struct context c;
	static const uint8_t prefix[7] = {0xb7, 0xad, 0x59, 0x28, 0xb2, 0x08, 0xfe};
	reset(&c, 6061328U, 107);
	memcpy(c.a + 107, prefix, 7);
	observed_cnt = 6061335U;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(update_usr_buf(&c.dev, 0, true, true));
	assert(emitted_len == 0);
	assert(update_usr_buf(&c.dev, 7, true, true));
	check("safe-prefix", prefix, 7);
}
static void collision_case(unsigned int new_count)
{
	struct context c;
	uint8_t expected[17];
	unsigned int old_count = new_count == 1 ? 7 : 6;
	reset(&c, 6061328U, 107);
	for (unsigned int i = 0; i < 7; i++) {
		c.a[107 + i] = (uint8_t)(0x80 + i);
	}
	for (unsigned int i = 0; i < 10; i++) {
		c.b[i] = (uint8_t)(0x30 + i);
	}
	observed_cnt = 6061335U;
	collision = true;
	collision_delta = new_count;
	assert(bounce_buf_swap(&c.dev, c.a) == (int)old_count);
	collision = false;
	/* Both hardware counters now reflect the bytes about to be reported. */
	observed_cnt = 6061328U + old_count + 10U;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 11U;
	memcpy(expected, c.a + 107, old_count);
	memcpy(expected + old_count, c.b, 10);
	assert(update_usr_buf(&c.dev, old_count + 10, true, true));
	check(new_count == 1 ? "collision1" : "collision2", expected, old_count + 10);
}
static void redirected_anomaly(uint8_t incoming)
{
	struct context c;
	uint8_t expected[9];
	reset(&c, 6061328U, 107);
	for (unsigned int i = 0; i < 7; i++) {
		c.a[107 + i] = (uint8_t)(0x80 + i);
	}
	observed_cnt = 6061335U;
	collision = true;
	collision_delta = 1;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	collision = false;
	c.a[114] = incoming;
	c.b[0] = UARTE_MAGIC_BYTE;
	c.b[1] = 0x1f;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 2;
	observed_cnt++;
	assert(update_usr_buf(&c.dev, 9, true, true));
	memcpy(expected, c.a + 107, 7);
	expected[7] = incoming;
	expected[8] = 0x1f;
	check("redirected-anomaly", expected, 9);
}
static void exactly_one_old_prefetched_byte(void)
{
	struct context c;
	uint8_t expected = 0x91;
	reset(&c, 6061328U, 107);
	observed_cnt = 6061335U;
	for (unsigned int i = 0; i < 7; i++) {
		c.a[107 + i] = (uint8_t)(0x80 + i);
	}
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(update_usr_buf(&c.dev, 7, true, true));
	emitted_len = 0;
	c.a[114] = expected;
	observed_cnt++;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 1;
	assert(update_usr_buf(&c.dev, 1, true, true));
	check("exactly-one-old-prefetch", &expected, 1);
}
static void legitimate_aa_at_both_sides(void)
{
	struct context c;
	uint8_t expected[8] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0xaa, 0xaa};
	reset(&c, 6061328U, 107);
	memcpy(c.a + 107, expected, 7);
	observed_cnt = 6061335U;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(update_usr_buf(&c.dev, 7, true, true));
	c.b[0] = 0xaa;
	observed_cnt++;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 2;
	assert(update_usr_buf(&c.dev, 1, true, true));
	check("legitimate-aa-old-and-new", expected, 8);
}
static void captured_38740(void)
{
	struct context c;
	uint8_t expected[17];
	static const uint8_t old[4] = {0x06, 0x8e, 0x49, 0x3c};
	static const uint8_t captured_new[7] = {0xd5, 0x4b, 0x41, 0x67, 0xde, 0x01, 0x63};
	reset(&c, 4338992U, 108);
	memcpy(c.a + 108, old, 4);
	memcpy(c.b, captured_new, sizeof(captured_new));
	for (unsigned int i = 7; i < 13; i++) {
		c.b[i] = (uint8_t)(0x18 + i * 19);
	}
	observed_cnt = 4339003U;
	collision = true;
	collision_delta = 7;
	assert(bounce_buf_swap(&c.dev, c.a) == 5);
	assert(c.cbwt.bounce_limit == 113);
	collision = false;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 7;
#ifdef V2
	quiet_mode = 1; /* DMA pointer catches up during first one-microsecond sample. */
#endif
	assert(update_usr_buf(&c.dev, 11, true, true));
	memcpy(expected, old, 4);
	memcpy(expected + 4, c.b, 7);
	check("captured-38740-first-cut", expected, 11);
	memcpy(expected + 11, c.b + 7, 6);
	observed_cnt += 6;
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 14;
	assert(update_usr_buf(&c.dev, 6, true, true));
	check("captured-38740-next-notify", expected, sizeof(expected));
}

static void late_anomaly(void)
{
	struct context c;
	uint8_t expected[8];
	reset(&c, 6061328U, 107);
	for (unsigned int i = 0; i < 7; i++) {
		c.a[107 + i] = (uint8_t)(0x80 + i);
	}
	observed_cnt = 6061335U;
	collision = true;
	collision_delta = 1;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	collision = false;
	c.a[114] = 0xaa;
	c.b[0] = 0xaa;
	observed_cnt = 6061336U; /* Seven old and one new byte; no future byte. */
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 2;
#ifdef V2
	quiet_mode = 2;
	scripted_old_spare = &c.a[114]; /* Already received byte becomes visible. */
#endif
	assert(update_usr_buf(&c.dev, 8, true, true));
	memcpy(expected, c.a + 107, 7);
	expected[7] = 0x78;
	check("late-anomaly-78", expected, sizeof(expected));
}

static void fail_closed(void)
{
#ifdef REPAIR
	struct context c;
	reset(&c, 6061328U, 107);
	observed_cnt = 6061335U;
	assert(bounce_buf_swap(&c.dev, c.a) == 7);
	assert(!update_usr_buf(&c.dev, 8, true, true));
	assert(emitted_len == 0);
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 513;
	assert(!update_usr_buf(&c.dev, 8, true, true));
	assert(emitted_len == 0);
	c.regs.DMA.RX.PTR = (uint32_t)(uintptr_t)c.b + 101;
	unstable_count = true;
	assert(!update_usr_buf(&c.dev, 8, true, true));
	assert(emitted_len == 0);
	unstable_count = false;
#endif
}
static void restart_after_pending_boundary(void)
{
	struct context c;
	uint8_t expected[] = {0x11, 0x22};
	reset(&c, 6061328U, 107);
	c.cbwt.boundary_pending = true;
	c.cbwt.discard_fifo = true;
	c.cfg.bounce_buf_swap_len = 112;
	c.cbwt.bounce_buf_swap_len = 112;
	cbwt_rx_enable(&c.dev, false);
	c.a[0] = expected[0];
	c.a[1] = expected[1];
	observed_cnt = 2;
	assert(update_usr_buf(&c.dev, 2, true, true));
	check("restart-emitted-two-bytes", expected, sizeof(expected));
}
int main(void)
{
	no_new_bytes_and_safe_prefix();
	captured_54118(0xde, false, 6061328U);
	captured_54119();
	complete_h4_frame_across_boundary();
	for (unsigned int byte = 0; byte <= 255; byte++) {
		captured_54118((uint8_t)byte, false, 6061328U);
		captured_54118((uint8_t)byte, true, 6061328U);
	}
	captured_54118(0xde, false, UINT32_MAX - 3U);
	collision_case(1);
	collision_case(2);
	exactly_one_old_prefetched_byte();
	legitimate_aa_at_both_sides();
	for (unsigned int byte = 0; byte <= 255; byte++) {
		redirected_anomaly((uint8_t)byte);
	}
	captured_38740();
	late_anomaly();
	fail_closed();
	restart_after_pending_boundary();
	printf("actual extracted UART_RX_RDY outputs: %u mismatches\n", failures);
	return failures ? 1 : 0;
}
