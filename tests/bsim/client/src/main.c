/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM valid-LC3 client — main entry point.
 */

#include <bstests.h>
#include <zephyr/kernel.h>

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	ARG_UNUSED(file);
	ARG_UNUSED(line);
	k_panic();
}

int main(void)
{
	bst_main();
	return 0;
}
