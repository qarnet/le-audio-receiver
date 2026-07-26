/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO pattern test: drives D0 (P1.4), D1 (P1.5), D2 (P1.6) with known
 * repeating waveforms for logic-analyzer probe placement verification.
 *
 * D0: 1 Hz square wave (500 ms HIGH, 500 ms LOW)
 * D1: two 50 ms pulses per second (at 200 ms and 600 ms)
 * D2: three 50 ms pulses per second (at 100 ms, 400 ms, 700 ms)
 *
 * Cycle period: 1000 ms, 10 ms step resolution (100 steps/cycle).
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* D0 (P1.4) — 1 Hz square wave */
#define D0_NODE DT_NODELABEL(gpio1)
#define D0_PIN  4

/* D1 (P1.5) — two 50 ms pulses / sec */
#define D1_NODE DT_NODELABEL(gpio1)
#define D1_PIN  5

/* D2 (P1.6) — three 50 ms pulses / sec */
#define D2_NODE DT_NODELABEL(gpio1)
#define D2_PIN  6

#define STEP_MS         10
#define STEPS_PER_CYCLE 100

int main(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(D0_NODE);

	printk("\n=== nRF54L15 GPIO Pattern Test ===\n");
	printk("D0 (P1.4): 1 Hz square wave\n");
	printk("D1 (P1.5): two 50 ms pulses/sec\n");
	printk("D2 (P1.6): three 50 ms pulses/sec\n");
	printk("Cycle: 1000 ms, step: 10 ms\n\n");

	if (!device_is_ready(gpio1)) {
		printk("ERROR: gpio1 not ready\n");
		return -1;
	}

	gpio_pin_configure(gpio1, D0_PIN, GPIO_OUTPUT);
	gpio_pin_configure(gpio1, D1_PIN, GPIO_OUTPUT);
	gpio_pin_configure(gpio1, D2_PIN, GPIO_OUTPUT);

	printk("GPIO configured. Pattern running.\n");

	for (;;) {
		for (int step = 0; step < STEPS_PER_CYCLE; step++) {
			/* D0: HIGH steps 0–49, LOW steps 50–99 */
			gpio_pin_set(gpio1, D0_PIN, (step < 50));

			/* D1: HIGH at 20–24 (200–250 ms) and 60–64 (600–650 ms) */
			bool d1_high = (step >= 20 && step < 25) || (step >= 60 && step < 65);
			gpio_pin_set(gpio1, D1_PIN, d1_high ? 1 : 0);

			/* D2: HIGH at 10–14 (100–150 ms), 40–44 (400–450 ms),
			 *     70–74 (700–750 ms) */
			bool d2_high = (step >= 10 && step < 15) || (step >= 40 && step < 45) ||
				       (step >= 70 && step < 75);
			gpio_pin_set(gpio1, D2_PIN, d2_high ? 1 : 0);

			k_msleep(STEP_MS);
		}
	}

	return 0;
}
