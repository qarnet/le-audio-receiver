/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bt_bap.h"

#include <errno.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_USER_PAIRING_INPUT)
#include "pairing_mode.h"
#endif

/* Production Bluetooth builds expose CONFIG_BT_ID_MAX. The narrow native shell
 * suites deliberately avoid a Bluetooth Kconfig selection and provide only the
 * public bt_id_get seam, so retain a bounded test-only-compatible capacity.
 * bt_id_get treats count as input capacity and returns no more entries than it
 * copied. */
#if defined(CONFIG_BT_ID_MAX)
#define BT_SHELL_ID_CAPACITY CONFIG_BT_ID_MAX
#else
#define BT_SHELL_ID_CAPACITY 8U
#endif

/* bt identity — zero-argument read-only identity boundary for the system
 * HIL runner.  Requires at least one usable (non-BT_ADDR_LE_ANY) identity
 * and prints the first usable identity (identity 0 in the normal boot
 * order) exactly as "Identity: XX:XX:XX:XX:XX:XX (public|random)".
 * No mutation, no controller command, no settings write. */
static int cmd_bt_identity(const struct shell *sh, size_t argc, char **argv)
{
	bt_addr_le_t ids[BT_SHELL_ID_CAPACITY];
	size_t count = ARRAY_SIZE(ids);
	size_t i;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	bt_id_get(ids, &count);
	for (i = 0U; i < count; i++) {
		bt_addr_le_t identity = ids[i];
		char buf[BT_ADDR_LE_STR_LEN];
		int written;

		/* BT_ADDR_LE_ANY is type public plus an all-zero address. Avoid the
		 * external sentinel symbol so native shell tests can link the public
		 * bt_id_get seam without a full Bluetooth host object. */
		if (identity.type == BT_ADDR_LE_PUBLIC &&
		    bt_addr_cmp(&identity.a, &(bt_addr_t){0}) == 0) {
			continue;
		}
		/* bt_id_get() normally returns public or random identity types. Keep
		 * output within the HIL wire contract if a public-id/random-id form
		 * reaches this read-only shell boundary. */
		if (identity.type == BT_ADDR_LE_PUBLIC_ID) {
			identity.type = BT_ADDR_LE_PUBLIC;
		} else if (identity.type == BT_ADDR_LE_RANDOM_ID) {
			identity.type = BT_ADDR_LE_RANDOM;
		} else if (identity.type != BT_ADDR_LE_PUBLIC &&
			   identity.type != BT_ADDR_LE_RANDOM) {
			continue;
		}
		written = bt_addr_le_to_str(&identity, buf, sizeof(buf));
		if (written < 0 || (size_t)written >= sizeof(buf)) {
			shell_error(sh, "Identity unavailable.");
			return -EIO;
		}
		shell_print(sh, "Identity: %s", buf);
		return 0;
	}
	shell_error(sh, "Identity unavailable.");
	return -ENOENT;
}

#if defined(CONFIG_USER_PAIRING_INPUT)
static void bt_bond_count_cb(const struct bt_bond_info *info, void *user_data)
{
	size_t *count = user_data;

	ARG_UNUSED(info);
	(*count)++;
}
#endif

/* bt bonds — read-only bond inventory boundary for system HIL cleanup.
 * The runner requires exact "Bond count: 0" after `bt unpair`, proving the
 * receiver-side persisted-bond inventory is empty instead of inferring it
 * from a successful reset message. */
static int cmd_bt_bonds(const struct shell *sh, size_t argc, char **argv)
{
#if defined(CONFIG_USER_PAIRING_INPUT)
	size_t count = 0U;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	bt_foreach_bond(BT_ID_DEFAULT, bt_bond_count_cb, &count);
	shell_print(sh, "Bond count: %zu", count);
	return 0;
#else
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_error(sh, "Bond inventory unavailable.");
	return -ENOTSUP;
#endif
}

#if defined(CONFIG_SOC_NRF54L15)
/* bt iso quality — one read-only controller snapshot per active sink CIS. */
static int cmd_bt_iso_quality(const struct shell *sh, size_t argc, char **argv)
{
	struct bt_bap_iso_link_quality snapshots[CONFIG_BT_ASCS_MAX_ASE_SNK_COUNT];
	size_t count;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = bt_bap_iso_link_quality_get_active(snapshots, ARRAY_SIZE(snapshots), &count);
	if (ret != 0) {
		shell_print(sh, "ISO link quality unavailable: %d", ret);
		return ret;
	}

	shell_print(sh, "--- ISO link quality ---");
	for (size_t i = 0U; i < count; i++) {
		shell_print(sh,
			    "  Stream[%zu] handle=0x%04X tx_unacked=%u tx_flushed=%u "
			    "tx_last_subevent=%u retransmitted=%u crc_error=%u "
			    "rx_unreceived=%u duplicate=%u iso_interval_1250us=%u nse=%u "
			    "cig_sync_us=%u cis_sync_us=%u c_max_pdu=%u c_phy=%u c_bn=%u "
			    "c_flush_1250us=%u",
			    snapshots[i].slot, (unsigned int)snapshots[i].handle,
			    (unsigned int)snapshots[i].tx_unacked_packets,
			    (unsigned int)snapshots[i].tx_flushed_packets,
			    (unsigned int)snapshots[i].tx_last_subevent_packets,
			    (unsigned int)snapshots[i].retransmitted_packets,
			    (unsigned int)snapshots[i].crc_error_packets,
			    (unsigned int)snapshots[i].rx_unreceived_packets,
			    (unsigned int)snapshots[i].duplicate_packets,
			    (unsigned int)snapshots[i].iso_interval_1250us,
			    (unsigned int)snapshots[i].nse, (unsigned int)snapshots[i].cig_sync_us,
			    (unsigned int)snapshots[i].cis_sync_us,
			    (unsigned int)snapshots[i].c_max_pdu, (unsigned int)snapshots[i].c_phy,
			    (unsigned int)snapshots[i].c_bn,
			    (unsigned int)snapshots[i].c_flush_1250us);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	bt_iso_cmds,
	SHELL_CMD_ARG(quality, NULL,
		      "Print ISO link-quality counters and selected C-to-P CIS parameters.",
		      cmd_bt_iso_quality, 1, 0),
	SHELL_SUBCMD_SET_END);
#endif /* CONFIG_SOC_NRF54L15 */

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
	SHELL_CMD_ARG(bonds, NULL, "Print the persisted Bluetooth bond count.", cmd_bt_bonds, 1, 0),
	SHELL_CMD_ARG(identity, NULL, "Print the primary Bluetooth identity address.",
		      cmd_bt_identity, 1, 0),
#if defined(CONFIG_SOC_NRF54L15)
	SHELL_CMD(iso, &bt_iso_cmds, "ISO diagnostics.", NULL),
#endif
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

int audio_shell_test_cmd_bt_identity(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_bt_identity(sh, argc, argv);
}

int audio_shell_test_cmd_bt_bonds(const struct shell *sh, size_t argc, char **argv)
{
	return cmd_bt_bonds(sh, argc, argv);
}

/* GCOVR_EXCL_STOP */
#endif /* AUDIO_SHELL_TEST */
