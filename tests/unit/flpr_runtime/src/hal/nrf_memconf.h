#ifndef FLPR_TEST_NRF_MEMCONF_H_
#define FLPR_TEST_NRF_MEMCONF_H_

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	struct {
		uint32_t CONTROL;
		uint32_t RET;
		uint32_t RET2;
	} POWER[2];
} NRF_MEMCONF_Type;

extern NRF_MEMCONF_Type flpr_rt_test_memconf;
#define NRF_MEMCONF (&flpr_rt_test_memconf)

void nrf_memconf_ramblock_ret_enable_set(NRF_MEMCONF_Type *reg, uint8_t power, uint8_t block,
					 bool enable);
bool nrf_memconf_ramblock_ret_enable_check(NRF_MEMCONF_Type const *reg, uint8_t power,
					   uint8_t block);

#endif
