/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_bap.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

/* bt unpair — production pairing-mode reset: clear bonds, disconnect the
 * current peer, and return the receiver to open pairing mode. */
static int cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv)
{
	int ret = bt_bap_pairing_reset();
	if (ret == 0) {
		shell_print(sh, "Pairing mode reset: bonds cleared; open pairing enabled.");
	} else {
		shell_error(sh, "bt_bap_pairing_reset failed: %d", ret);
	}
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	bt_cmds,
	SHELL_CMD_ARG(unpair, NULL,
		      "Reset pairing mode: clear bonds, disconnect peer, open pairing.",
		      cmd_bt_unpair, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(bt, &bt_cmds, "Bluetooth test commands.", NULL);

#if defined(AUDIO_SHELL_TEST)
/* GCOVR_EXCL_START — test seam wrapper, absent from production builds */
/*
 * Narrow test seam for tests/unit/audio_shell*.  Exposes the otherwise-
 * static bt unpair handler so the real production command body is invoked
 * directly.  Never compiled into production firmware.
 */

int audio_shell_test_cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_bt_unpair(sh, argc, argv);
}

/* GCOVR_EXCL_STOP */
#endif /* AUDIO_SHELL_TEST */
