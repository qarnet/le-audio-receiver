/* SPDX-License-Identifier: MIT
 * Continuous DMA H4 bridge for the XIAO's two-wire stock CDC UART.
 * UART callbacks only copy bytes and transfer DMA-buffer ownership. Packet
 * allocation/controller submission run in a thread, never the UART ISR.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/bluetooth/controller.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/buf.h>

#include "hci_identity.h"
#include "h4_rx.h"

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_bt_c2h_uart), timer),
	     "No-flow RX requires hardware byte counting");

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_c2h_uart));
static uint8_t dma_buffers[2][512] __aligned(4);
static atomic_t dma_owned;
/* SPSC: the UART ISR produces, the host-to-controller thread consumes. */
RING_BUF_DECLARE(rx_ring, 8192);
K_SEM_DEFINE(rx_ready, 0, 1);
K_SEM_DEFINE(tx_done, 0, 1);
K_FIFO_DEFINE(controller_rx);
K_THREAD_STACK_DEFINE(h2c_stack, CONFIG_BT_HCI_TX_STACK_SIZE);
static struct k_thread h2c_thread;
static struct h4_rx parser;
static atomic_t bridge_fault;
/* Retain fatal provenance for SWD without corrupting the binary H4 UART. */
const char *volatile bridge_assert_file;
volatile unsigned int bridge_assert_line;

void assert_post_action(const char *file, unsigned int line)
{
	bridge_assert_file = file;
	bridge_assert_line = line;
	k_panic();
}

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	assert_post_action(file, line);
}

static void fail(unsigned int code)
{
	(void)atomic_cas(&bridge_fault, 0, code);
	k_sem_give(&rx_ready);
}

static void uart_event(const struct device *dev, struct uart_event *event, void *user)
{
	ARG_UNUSED(user);
	switch (event->type) {
	case UART_RX_RDY: {
		const uint8_t *data = event->data.rx.buf + event->data.rx.offset;
		size_t len = event->data.rx.len;
		if (ring_buf_put(&rx_ring, data, len) != len) {
			fail(1);
		}
		k_sem_give(&rx_ready);
		break;
	}
	case UART_RX_BUF_REQUEST:
		for (unsigned int i = 0; i < ARRAY_SIZE(dma_buffers); i++) {
			if (!atomic_test_and_set_bit(&dma_owned, i)) {
				if (uart_rx_buf_rsp(dev, dma_buffers[i], sizeof(dma_buffers[i]))) {
					atomic_clear_bit(&dma_owned, i);
					fail(2);
				}
				return;
			}
		}
		fail(3);
		break;
	case UART_RX_BUF_RELEASED:
		for (unsigned int i = 0; i < ARRAY_SIZE(dma_buffers); i++) {
			if (event->data.rx_buf.buf == dma_buffers[i]) {
				atomic_clear_bit(&dma_owned, i);
				return;
			}
		}
		fail(4);
		break;
	case UART_RX_STOPPED:
	case UART_RX_DISABLED:
		fail(5);
		break;
	case UART_TX_ABORTED:
		fail(6);
		k_sem_give(&tx_done);
		break;
	case UART_TX_DONE:
		k_sem_give(&tx_done);
		break;
	default:
		break;
	}
}

static int submit(uint8_t type, const uint8_t *data, size_t len, void *user)
{
	ARG_UNUSED(user);
	/* HCI packet credits bound queued traffic. Wait in thread context for
	 * storage without ever losing the payload or changing parser position. */
	struct net_buf *buf =
		bt_buf_get_tx(bt_buf_type_from_h4(type, BT_BUF_OUT), K_FOREVER, NULL, 0);
	if (buf == NULL) {
		return -ENOMEM;
	}
	if (len > net_buf_tailroom(buf)) {
		net_buf_unref(buf);
		return -EMSGSIZE;
	}
	net_buf_add_mem(buf, data, len);
	int err = bt_send(buf);
	if (err) {
		net_buf_unref(buf);
	}
	return err;
}

static void receive(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	uint8_t chunk[128];
	while (!atomic_get(&bridge_fault)) {
		k_sem_take(&rx_ready, K_FOREVER);
		uint32_t count;
		while (!atomic_get(&bridge_fault) &&
		       (count = ring_buf_get(&rx_ring, chunk, sizeof(chunk))) != 0) {
			if (h4_rx_feed(&parser, chunk, count, submit, NULL)) {
				fail(7);
			}
			k_yield();
		}
	}
}

static int transmit(const uint8_t *data, size_t len)
{
	k_sem_reset(&tx_done);
	int err = uart_tx(uart, data, len, SYS_FOREVER_US);
	return err ? err : k_sem_take(&tx_done, K_SECONDS(1));
}

int main(void)
{
	if (!device_is_ready(uart) || uart_callback_set(uart, uart_event, NULL)) {
		k_panic();
	}
	/* NCS v3.3.0 statically converts the CBWT baud rate to bytes twice,
	 * then overwrites the correctly computed runtime swap threshold during
	 * initialization. Reapply the exact current configuration while RX is
	 * inactive: baudrate_set() restores the intended latency headroom.
	 * On this 1 Mbaud / 512-byte-half / 4000 us setup it is 112, not 502.
	 * No SDK source patch or assertion suppression is required. The exact
	 * SDK static-initializer warning is documented/scoped in CMakeLists. */
	struct uart_config uart_config;
	if (uart_config_get(uart, &uart_config) || uart_configure(uart, &uart_config)) {
		k_panic();
	}
	bt_ctlr_set_public_addr(dongle_bd_addr);
	if (bt_enable_raw(&controller_rx)) {
		k_panic();
	}
	h4_rx_init(&parser);
	atomic_set_bit(&dma_owned, 0);
	if (uart_rx_enable(uart, dma_buffers[0], sizeof(dma_buffers[0]), 1000)) {
		k_panic();
	}
	k_thread_create(&h2c_thread, h2c_stack, K_THREAD_STACK_SIZEOF(h2c_stack), receive, NULL,
			NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	for (;;) {
		unsigned int fault = atomic_get(&bridge_fault);
		if (fault) {
			/* Fail visibly. Do not silently resynchronize through audio
			 * bytes, drop packets, or restart and claim an intact run. */
			uint8_t event[] = {BT_HCI_H4_EVT, BT_HCI_EVT_HARDWARE_ERROR, 1, fault};
			(void)transmit(event, sizeof(event));
			(void)uart_rx_disable(uart);
			k_panic();
		}
		struct net_buf *buf = k_fifo_get(&controller_rx, K_MSEC(20));
		if (buf) {
			int err = transmit(buf->data, buf->len);
			net_buf_unref(buf);
			if (err) {
				fail(8);
			}
		}
	}
}
