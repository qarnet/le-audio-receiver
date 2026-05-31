/* Net Core Bootloader - programs hci_ipc firmware into net core flash on first boot.
 * Writes directly to network core NVMC registers at 0x41080000, bypassing the
 * Zephyr flash API which uses the app core NVMC at 0x50039000.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(net_boot, LOG_LEVEL_INF);

#include "net_core_fw.h"

#define NET_NVMC_BASE    0x41080000
#define NET_NVMC_READY   (NET_NVMC_BASE + 0x400)
#define NET_NVMC_CONFIG  (NET_NVMC_BASE + 0x504)
#define NET_NVMC_ERASE   (NET_NVMC_BASE + 0x508)
#define NET_FLASH_BASE   0x01000000
#define NET_PAGE_SIZE    4096
#define NVMC_WEN_WRITE   0x00000001

static inline void nvmc_wait_ready(void)
{
	while ((*(volatile uint32_t *)NET_NVMC_READY & 1) == 0) {
	}
}

void net_core_bootloader_check_and_program(void)
{
	uint32_t first_word = *(volatile uint32_t *)NET_FLASH_BASE;

	if (first_word != 0xFFFFFFFF) {
		LOG_INF("Net core fw present (0x%08x), skip", first_word);
		return;
	}

	LOG_INF("Net core blank, programming %zu bytes...",
		net_core_firmware_len);

	*(volatile uint32_t *)NET_NVMC_CONFIG = NVMC_WEN_WRITE;
	nvmc_wait_ready();

	uint32_t num_pages = (net_core_firmware_len + NET_PAGE_SIZE - 1)
			     / NET_PAGE_SIZE;
	LOG_INF("Erasing %u pages...", num_pages);
	for (uint32_t p = 0; p < num_pages; p++) {
		uint32_t addr = NET_FLASH_BASE + p * NET_PAGE_SIZE;
		*(volatile uint32_t *)NET_NVMC_ERASE = addr;
		nvmc_wait_ready();
	}

	LOG_INF("Writing...");
	for (size_t i = 0; i < net_core_firmware_len; i += 4) {
		uint32_t addr = NET_FLASH_BASE + i;
		uint32_t word;
		memcpy(&word, &net_core_firmware[i],
		       MIN(4U, net_core_firmware_len - i));
		*(volatile uint32_t *)addr = word;
		nvmc_wait_ready();
	}

	*(volatile uint32_t *)NET_NVMC_CONFIG = 0;
	nvmc_wait_ready();

	first_word = *(volatile uint32_t *)NET_FLASH_BASE;
	if (first_word == 0xFFFFFFFF) {
		LOG_ERR("Net core verify FAILED");
	} else {
		LOG_INF("Net core programmed OK (0x%08x)", first_word);
	}
}
