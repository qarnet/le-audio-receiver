/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standalone I2S20 master-mode sine wave test for nRF54L15.
 *
 * Generates continuous 1 kHz stereo sine wave via I2S DMA output
 * for at least 15 seconds.  No BLE, no LC3, no drift control.
 * This test proves or disproves I2S20 master output independently
 * from the main receiver pipeline.
 *
 * I2S config: 48 kHz, 16-bit, stereo, master (BCK+FRAME clock).
 * Blocks: 480 samples × 2 ch × 2 bytes = 1920 bytes each, 12 blocks.
 *
 * Only start/config/trigger errors and periodic status printed.
 * No per-block log spam.
 */

#include <math.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/util.h>

#define SAMPLE_RATE       48000
#define BIT_WIDTH         16
#define CHANNELS          2
#define SAMPLES_PER_FRAME 480                                              /* 10 ms at 48 kHz */
#define BLOCK_SIZE        (SAMPLES_PER_FRAME * CHANNELS * (BIT_WIDTH / 8)) /* 1920 */
#define BLOCK_COUNT       12

#define SINE_FREQ 1000
#define AMPLITUDE 26214 /* ~80% of 32767 */

#define TEST_DURATION_SEC 20

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * The I2S node is aliased as i2s-audio in the board overlay.
 */
#define I2S_NODE DT_ALIAS(i2s_audio)

K_MEM_SLAB_DEFINE_STATIC(i2s_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static const struct device *i2s_dev;

/* Fill one block with a slice of the sine wave, continuing from global phase. */
static void fill_block(int16_t *block, uint32_t *phase_accum)
{
	for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
		double t = (double)(*phase_accum + i) / (double)SAMPLE_RATE;
		int16_t sample = (int16_t)(AMPLITUDE * sin(2.0 * M_PI * SINE_FREQ * t));

		block[i * 2] = sample;     /* L */
		block[i * 2 + 1] = sample; /* R */
	}
	*phase_accum += SAMPLES_PER_FRAME;
}

int main(void)
{
	int ret;
	uint32_t phase = 0;

	printk("\n=== nRF54L15 I2S20 Standalone Sine Wave Test ===\n");
	printk("Sample rate: %u Hz, channels: %u, bit width: %u\n", SAMPLE_RATE, CHANNELS,
	       BIT_WIDTH);
	printk("Block size: %u bytes, block count: %u\n", BLOCK_SIZE, BLOCK_COUNT);
	printk("Duration: %u seconds\n\n", TEST_DURATION_SEC);

	i2s_dev = DEVICE_DT_GET(I2S_NODE);
	if (!device_is_ready(i2s_dev)) {
		printk("ERROR: I2S device not ready\n");
		return -ENODEV;
	}
	printk("I2S device ready.\n");

	/* --- Configure I2S as master transmitter --- */
	struct i2s_config cfg = {
		.word_size = BIT_WIDTH,
		.channels = CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = SAMPLE_RATE,
		.mem_slab = &i2s_slab,
		.block_size = BLOCK_SIZE,
		.timeout = 0,
	};

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
	if (ret != 0) {
		printk("ERROR: i2s_configure failed: %d\n", ret);
		return ret;
	}
	printk("I2S configured OK.\n");

	/* --- Pre-fill slab with distinct sine blocks --- */
	void *blocks[BLOCK_COUNT];
	for (int i = 0; i < BLOCK_COUNT; i++) {
		ret = k_mem_slab_alloc(&i2s_slab, &blocks[i], K_FOREVER);
		if (ret != 0) {
			printk("ERROR: slab alloc %d failed: %d\n", i, ret);
			return ret;
		}
		fill_block((int16_t *)blocks[i], &phase);
		ret = i2s_write(i2s_dev, blocks[i], BLOCK_SIZE);
		if (ret != 0) {
			printk("ERROR: i2s_write %d failed: %d\n", i, ret);
			return ret;
		}
	}
	printk("Pre-filled %u blocks (%u bytes total). Phase=%u\n", BLOCK_COUNT,
	       BLOCK_COUNT * BLOCK_SIZE, phase);

	/* --- Trigger the transfer --- */
	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
	if (ret != 0) {
		printk("ERROR: i2s_trigger START failed: %d\n", ret);
		return ret;
	}
	printk("I2S started. Feeding for %u seconds...\n", TEST_DURATION_SEC);

	/* --- Feed loop: refill buffers as DMA consumes them --- */
	uint32_t blocks_fed = 0;
	uint64_t start_ms = k_uptime_get();

	while ((k_uptime_get() - start_ms) < (TEST_DURATION_SEC * 1000)) {
		void *block;
		ret = k_mem_slab_alloc(&i2s_slab, &block, K_NO_WAIT);
		if (ret == -ENOMEM) {
			/* Slab full: DMA hasn't freed a block yet. */
			k_sleep(K_MSEC(1));
			continue;
		}
		if (ret != 0) {
			printk("ERROR: slab alloc in feed loop: %d\n", ret);
			break;
		}

		fill_block((int16_t *)block, &phase);
		ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
		if (ret != 0) {
			printk("ERROR: i2s_write in feed loop: %d\n", ret);
			break;
		}
		blocks_fed++;

		/* Periodic status every 4800 blocks (~48 seconds worth of
		 * data, but the loop wakes every ~10 ms, so ~every 48 s).
		 * This fires roughly once mid-test. */
		if ((blocks_fed % 4800) == 0) {
			int free_now = k_mem_slab_num_free_get(&i2s_slab);
			printk("STATUS: %u blocks fed, free=%d\n", (unsigned int)blocks_fed,
			       free_now);
		}
	}

	uint64_t elapsed_ms = k_uptime_get() - start_ms;
	printk("\nTest complete: %u blocks fed in %llu ms\n", (unsigned int)blocks_fed,
	       (unsigned long long)elapsed_ms);

	/* --- Stop --- */
	ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
	if (ret != 0) {
		printk("WARNING: i2s_trigger DROP: %d\n", ret);
	} else {
		printk("I2S stopped.\n");
	}

	/* Drain slab to allow clean exit */
	int freed_blocks = 0;
	for (int i = 0; i < BLOCK_COUNT; i++) {
		void *b;
		if (k_mem_slab_alloc(&i2s_slab, &b, K_NO_WAIT) == 0) {
			k_mem_slab_free(&i2s_slab, b);
			freed_blocks++;
		}
	}
	printk("Drained %d blocks from slab.\n", freed_blocks);

	return 0;
}
