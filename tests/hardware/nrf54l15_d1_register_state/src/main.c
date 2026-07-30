/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * D1 (P1.5) register-state diagnostic for logic-analyzer + OpenOCD capture.
 *
 * - P1.5 toggles: 2 seconds low, 2 seconds high, repeat forever.
 * - P1.4 and P1.6 driven low throughout (sanity channels).
 * - One boot banner; no loop log spam.
 * - Every gpio_pin_configure / gpio_pin_set return code checked.
 *
 * Target: official xiao_nrf54l15/nrf54l15/cpuapp board.
 * UART20 console default: P1.9 TX / P1.8 RX (SAMD11 CDC bridge).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define GPIOn  DT_NODELABEL(gpio1)
#define D0_PIN 4 /* P1.04 — sanity low */
#define D1_PIN 5 /* P1.05 — toggling test pin */
#define D2_PIN 6 /* P1.06 — sanity low */

#define TOGGLE_MS 2000

static int drive_low(const struct device *dev, int pin, const char *name)
{
	int ret = gpio_pin_set(dev, pin, 0);
	if (ret != 0) {
		printk("ERROR: gpio_pin_set(%s/P1.%d) returned %d\n", name, pin, ret);
	}
	return ret;
}

int main(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(GPIOn);
	int ret;

	printk("\n=== nRF54L15 D1 Register-State Diagnostic ===\n");
	printk("P1.5 toggles 2s low / 2s high; P1.4+P1.6 held low\n");
	printk("Target: xiao_nrf54l15/nrf54l15/cpuapp\n\n");

	if (!device_is_ready(gpio1)) {
		printk("ERROR: gpio1 not ready\n");
		return -1;
	}

	/* Configure all three pins as push-pull outputs */
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

	/* Initial state: all low */
	if (drive_low(gpio1, D0_PIN, "D0") != 0) {
		return -1;
	}
	if (drive_low(gpio1, D1_PIN, "D1") != 0) {
		return -1;
	}
	if (drive_low(gpio1, D2_PIN, "D2") != 0) {
		return -1;
	}

	printk("GPIO configured OK. Pattern running (no further prints).\n");

	for (;;) {
		/* Drive D1 low, D0/D2 stay low */
		if (drive_low(gpio1, D1_PIN, "D1") != 0) {
			break;
		}
		k_msleep(TOGGLE_MS);

		/* Drive D1 high, D0/D2 stay low */
		ret = gpio_pin_set(gpio1, D1_PIN, 1);
		if (ret != 0) {
			printk("ERROR: gpio_pin_set(D1/P1.5) high returned %d\n", ret);
			break;
		}
		k_msleep(TOGGLE_MS);
	}

	printk("ERROR: GPIO set failed, pattern stopped\n");
	return -1;
}
