/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM receiver — main entry point.
 * Delegates to bst_main() which dispatches to the test framework.
 * BT init, BAP registration, and audio sink init happen in the
 * test_main_f callback, exactly like upstream bsim tests.
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
