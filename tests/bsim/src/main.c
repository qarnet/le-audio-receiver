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

int main(void)
{
	bst_main();
	return 0;
}
