/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO probe-verification pattern for logic-analyzer capture.
 *
 * Four non-overlapping states, each held for 100 ms:
 *
 *   State  D0/P1.4  D1/P1.5  D2/P1.6
 *   A      high      low      low
 *   B      low       high     low
 *   C      low       low      high
 *   D      low       low      low
 *
 * Nominal cycle: 400 ms.  Repeat indefinitely.
 *
 * Every gpio_pin_configure and set-write is checked and any error printed.
 * No per-loop spam — only a boot banner and error messages.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define D0_NODE DT_NODELABEL(gpio1)
#define D0_PIN  4
#define D1_PIN  5
#define D2_PIN  6

#define STATE_MS 100

/* Write all three pins in one call and return 0 on success, else print
 * the first error index and return -EIO.  Avoids per-loop log spam.
 */
static int set_all_pins(const struct device *dev, int d0_val, int d1_val, int d2_val)
{
	int ret;

	ret = gpio_pin_set(dev, D0_PIN, d0_val);
	if (ret != 0) {
		printk("ERROR: gpio_pin_set(D0) returned %d\n", ret);
		return -EIO;
	}
	ret = gpio_pin_set(dev, D1_PIN, d1_val);
	if (ret != 0) {
		printk("ERROR: gpio_pin_set(D1) returned %d\n", ret);
		return -EIO;
	}
	ret = gpio_pin_set(dev, D2_PIN, d2_val);
	if (ret != 0) {
		printk("ERROR: gpio_pin_set(D2) returned %d\n", ret);
		return -EIO;
	}
	return 0;
}

int main(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(D0_NODE);
	int ret;

	printk("\n=== nRF54L15 GPIO Prove-Placement ===\n");
	printk("States: A (D0 hi), B (D1 hi), C (D2 hi), D (all lo)\n");
	printk("Each state 100 ms, cycle 400 ms, repeat forever\n\n");

	if (!device_is_ready(gpio1)) {
		printk("ERROR: gpio1 not ready\n");
		return -1;
	}

	ret = gpio_pin_configure(gpio1, D0_PIN, GPIO_OUTPUT);
	if (ret != 0) {
		printk("ERROR: gpio_pin_configure(D0/P1.4) returned %d\n", ret);
		return ret;
	}
	ret = gpio_pin_configure(gpio1, D1_PIN, GPIO_OUTPUT);
	if (ret != 0) {
		printk("ERROR: gpio_pin_configure(D1/P1.5) returned %d\n", ret);
		return ret;
	}
	ret = gpio_pin_configure(gpio1, D2_PIN, GPIO_OUTPUT);
	if (ret != 0) {
		printk("ERROR: gpio_pin_configure(D2/P1.6) returned %d\n", ret);
		return ret;
	}

	printk("GPIO configured OK. Pattern running (no further prints).\n");

	for (;;) {
		/* State A: D0 high, D1 low, D2 low */
		if (set_all_pins(gpio1, 1, 0, 0) != 0) {
			break;
		}
		k_msleep(STATE_MS);

		/* State B: D0 low, D1 high, D2 low */
		if (set_all_pins(gpio1, 0, 1, 0) != 0) {
			break;
		}
		k_msleep(STATE_MS);

		/* State C: D0 low, D1 low, D2 high */
		if (set_all_pins(gpio1, 0, 0, 1) != 0) {
			break;
		}
		k_msleep(STATE_MS);

		/* State D: D0 low, D1 low, D2 low */
		if (set_all_pins(gpio1, 0, 0, 0) != 0) {
			break;
		}
		k_msleep(STATE_MS);
	}

	printk("ERROR: GPIO set failed, pattern stopped\n");
	return -1;
}
