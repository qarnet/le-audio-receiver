/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_bap.h"

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#if defined(CONFIG_USER_PAIRING_INPUT)
#include "pairing_mode.h"
#endif

/* bt unpair — production pairing-mode reset: clear bonds, disconnect the
 * current peer, and return the receiver to open pairing mode. */
static int cmd_bt_unpair(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_USER_PAIRING_INPUT)
	/* P5 full-stack path: the pairing-mode controller owns the whole
	 * RESET transition (disconnect → bond deletion → one-second rapid
	 * LED feedback → BONDING advertising).  The synchronous call returns
	 * only after BONDING advertising is active; -ETIMEDOUT means the
	 * transition continues asynchronously — print the error, never claim
	 * success, never issue a second reset.  No direct bond/advertising
	 * call exists in this path. */
	int ret =
		pairing_mode_request_reset_sync(K_MSEC(CONFIG_USER_PAIRING_SHELL_RESET_TIMEOUT_MS));

	if (ret == 0) {
		shell_print(sh,
			    "Pairing reset complete: bonds cleared; BONDING advertising active.");
	} else {
		shell_error(sh, "pairing_mode reset failed: %d", ret);
	}
	return ret;
#else
	/* Legacy path (feature-off builds): direct reset preserving the exact
	 * historical output consumed by the BlueZ/WirePlumber gate fixtures. */
	int ret = bt_bap_pairing_reset();

	if (ret == 0) {
		shell_print(sh, "Pairing mode reset: bonds cleared; open pairing enabled.");
	} else {
		shell_error(sh, "bt_bap_pairing_reset failed: %d", ret);
	}
	return ret;
#endif
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
