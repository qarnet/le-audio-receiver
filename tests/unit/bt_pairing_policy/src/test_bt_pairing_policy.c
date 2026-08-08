/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for bt_pairing_policy — pure OPEN/BONDED_ONLY pairing-mode
 * policy with desired access mode and persisted bond inventory as
 * independent state (P3): explicit mode setter, exact inventory
 * replacement, bonded marking without mode derivation, separate clear,
 * defense-in-depth pairing decisions, and atomic snapshots.
 */

#include <zephyr/ztest.h>

#include "bt_pairing_policy.h"

ZTEST_SUITE(bt_pairing_policy, NULL, NULL, NULL, NULL, NULL);

static bt_addr_le_t make_addr(uint8_t type, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
			      uint8_t b4, uint8_t b5)
{
	bt_addr_le_t addr;

	addr.type = type;
	addr.a.val[0] = b0;
	addr.a.val[1] = b1;
	addr.a.val[2] = b2;
	addr.a.val[3] = b3;
	addr.a.val[4] = b4;
	addr.a.val[5] = b5;
	return addr;
}

/* True when every byte of addr is zero (the BT_ADDR_LE_ANY representation
 * that bt_bap.c's collect_bond filters out).  The unit suite links no
 * Bluetooth library, so the bt_addr_le_any extern is unavailable. */
static bool addr_is_zero(const bt_addr_le_t *addr)
{
	const uint8_t *p = (const uint8_t *)addr;

	for (size_t i = 0; i < sizeof(*addr); i++) {
		if (p[i] != 0) {
			return false;
		}
	}
	return true;
}

/* init = OPEN + empty inventory. */
ZTEST(bt_pairing_policy, test_init_open_default)
{
	struct bt_pairing_policy p;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* set_mode accepts OPEN and BONDED_ONLY, is idempotent, and never touches
 * the inventory — empty BONDED_ONLY is legal. */
ZTEST(bt_pairing_policy, test_set_mode_idempotent_legal_empty_bonded_only)
{
	struct bt_pairing_policy p;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_OPEN), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);

	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);

	/* Idempotent: re-selecting the current mode is a success. */
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);

	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_OPEN), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
}

/* set_mode preserves the inventory exactly in both directions. */
ZTEST(bt_pairing_policy, test_set_mode_preserves_inventory)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);

	/* OPEN -> BONDED_ONLY keeps both entries. */
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);

	/* BONDED_ONLY -> OPEN keeps both entries too. */
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_OPEN), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* An invalid mode value returns -EINVAL and leaves the full policy
 * unchanged (mode, count, and every entry). */
ZTEST(bt_pairing_policy, test_set_mode_invalid_atomic)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[1];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 1), 0);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);

	zassert_equal(bt_pairing_policy_set_mode(&p, (enum bt_pairing_policy_mode)42), -EINVAL);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 1);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);

	/* Invalid mode on the fresh OPEN/empty state is atomic too. */
	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, (enum bt_pairing_policy_mode) - 1), -EINVAL);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* replace(0) keeps OPEN and clears the inventory. */
ZTEST(bt_pairing_policy, test_replace_zero_keeps_open)
{
	struct bt_pairing_policy p;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, NULL, 0), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* replace(0) keeps BONDED_ONLY: an empty BONDED_ONLY inventory is legal
 * and rejects every peer. */
ZTEST(bt_pairing_policy, test_replace_zero_keeps_bonded_only)
{
	struct bt_pairing_policy p;
	bt_addr_le_t addr = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, NULL, 0), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &addr), BT_PAIRING_POLICY_REJECT);
}

/* replace(nonempty) keeps OPEN: OPEN with preserved bonds is legal. */
ZTEST(bt_pairing_policy, test_replace_nonempty_keeps_open)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* replace(nonempty) keeps BONDED_ONLY with the exact entries. */
ZTEST(bt_pairing_policy, test_replace_nonempty_keeps_bonded_only)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* Replacement removes the old inventory, preserves the mode, and leaves
 * snapshot tail storage zeroed beyond count. */
ZTEST(bt_pairing_policy, test_replace_removes_old_inventory_zeroes_tail)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t old_bonds[2];
	bt_addr_le_t new_bond[1];

	old_bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	old_bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);
	new_bond[0] = make_addr(BT_ADDR_LE_RANDOM, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, old_bonds, 2), 0);

	zassert_equal(bt_pairing_policy_replace_bonds(&p, new_bond, 1), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	/* The removed second bond is no longer accepted. */
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &old_bonds[1]),
		      BT_PAIRING_POLICY_REJECT);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &new_bond[0]), BT_PAIRING_POLICY_ACCEPT);

	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 1);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &new_bond[0]), 0);
	/* Tail slots beyond count must be zero, never stale addresses. */
	for (size_t i = 1; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		zassert_true(addr_is_zero(&snap.entries[i]), "snapshot tail slot %zu not zeroed",
			     i);
	}
}

/* replace overflow is atomic: -ENOMEM with mode/count/entries unchanged. */
ZTEST(bt_pairing_policy, test_replace_overflow_atomic)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[BT_PAIRING_POLICY_MAX_ENTRIES + 1];
	bt_addr_le_t kept[1];

	kept[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, kept, 1), 0);

	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, BT_PAIRING_POLICY_MAX_ENTRIES + 1),
		      -ENOMEM);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.count, 1);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &kept[0]), 0);
}

/* replace with count > 0 and addrs == NULL is atomic: -EINVAL with no
 * change, independent of the current mode/count. */
ZTEST(bt_pairing_policy, test_replace_nonzero_null_atomic)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 7, 8, 9, 10, 11, 12);

	/* Fresh OPEN/empty policy. */
	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, NULL, 1), -EINVAL);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);

	/* Populated BONDED_ONLY policy. */
	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, NULL, 2), -EINVAL);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* OPEN with an empty inventory accepts any peer. */
ZTEST(bt_pairing_policy, test_accept_open_accepts_any)
{
	struct bt_pairing_policy p;
	bt_addr_le_t addr = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &addr), BT_PAIRING_POLICY_ACCEPT);
}

/* OPEN with a nonempty inventory still accepts any peer. */
ZTEST(bt_pairing_policy, test_accept_open_with_bonds_accepts_any)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[1];
	bt_addr_le_t unbonded = make_addr(BT_ADDR_LE_RANDOM, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 1), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &bonds[0]), BT_PAIRING_POLICY_ACCEPT);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &unbonded), BT_PAIRING_POLICY_ACCEPT);
}

/* BONDED_ONLY with an empty inventory rejects every peer. */
ZTEST(bt_pairing_policy, test_accept_bonded_only_empty_rejects_all)
{
	struct bt_pairing_policy p;
	bt_addr_le_t addr = make_addr(BT_ADDR_LE_RANDOM, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &addr), BT_PAIRING_POLICY_REJECT);
}

/* BONDED_ONLY accepts exact snapshot members and rejects others. */
ZTEST(bt_pairing_policy, test_accept_bonded_only_members_only)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonded = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bt_addr_le_t unbonded = make_addr(BT_ADDR_LE_RANDOM, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, &bonded, 1), 0);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &bonded), BT_PAIRING_POLICY_ACCEPT);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &unbonded), BT_PAIRING_POLICY_REJECT);
}

/* mark_bonded adds the peer while preserving OPEN. */
ZTEST(bt_pairing_policy, test_mark_preserves_open)
{
	struct bt_pairing_policy p;
	bt_addr_le_t peer = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &peer), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	/* Defense-in-depth still accepts the fresh peer mid-session. */
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &peer), BT_PAIRING_POLICY_ACCEPT);
	/* Duplicate marking is idempotent and keeps OPEN. */
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &peer), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
}

/* mark_bonded adds the peer while preserving BONDED_ONLY. */
ZTEST(bt_pairing_policy, test_mark_preserves_bonded_only)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonded = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bt_addr_le_t extra = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, &bonded, 1), 0);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &extra), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &extra), BT_PAIRING_POLICY_ACCEPT);
	/* Duplicate marking keeps BONDED_ONLY and the count. */
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &extra), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
}

/* mark_bonded on a full inventory reports -ENOMEM and preserves both mode
 * and inventory. */
ZTEST(bt_pairing_policy, test_mark_overflow_preserves_mode_and_inventory)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[BT_PAIRING_POLICY_MAX_ENTRIES];
	bt_addr_le_t extra = make_addr(BT_ADDR_LE_RANDOM, 0xfe, 0xed, 0xfa, 0xce, 0xbe, 0xef);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	for (size_t i = 0; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		bonds[i] = make_addr(BT_ADDR_LE_PUBLIC, (uint8_t)i, 0x10, 0x20, 0x30, 0x40, 0x50);
	}
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, BT_PAIRING_POLICY_MAX_ENTRIES), 0);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &extra), -ENOMEM);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), BT_PAIRING_POLICY_MAX_ENTRIES);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &extra), BT_PAIRING_POLICY_REJECT);
}

/* mark_bonded(NULL) returns -EINVAL and leaves the full policy
 * unchanged. */
ZTEST(bt_pairing_policy, test_mark_null_atomic)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonded = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, &bonded, 1), 0);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, NULL), -EINVAL);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &bonded), BT_PAIRING_POLICY_ACCEPT);
}

/* clear_bonds preserves OPEN and is idempotent. */
ZTEST(bt_pairing_policy, test_clear_preserves_open_idempotent)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 7, 8, 9, 10, 11, 12);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	bt_pairing_policy_clear_bonds(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
	/* Idempotent: a second clear stays OPEN/empty. */
	bt_pairing_policy_clear_bonds(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* clear_bonds preserves BONDED_ONLY exactly (empty BONDED_ONLY after
 * clear) and is idempotent. */
ZTEST(bt_pairing_policy, test_clear_preserves_bonded_only_idempotent)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 7, 8, 9, 10, 11, 12);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	bt_pairing_policy_clear_bonds(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
	/* Idempotent: a second clear stays BONDED_ONLY/empty. */
	bt_pairing_policy_clear_bonds(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 0);
	/* Storage was zeroed: every snapshot entry is BT_ADDR_LE_ANY. */
	for (size_t i = 0; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		zassert_true(addr_is_zero(&snap.entries[i]), "cleared entry slot %zu not zeroed",
			     i);
	}
}

/* Snapshot copies mode, count, and exact entries atomically. */
ZTEST(bt_pairing_policy, test_snapshot_atomic)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_BONDED_ONLY), 0);
	zassert_equal(bt_pairing_policy_replace_bonds(&p, bonds, 2), 0);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* Snapshot of an OPEN policy with zero entries is OPEN with an empty
 * inventory and zeroed tail storage. */
ZTEST(bt_pairing_policy, test_snapshot_empty)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_mode(&p, BT_PAIRING_POLICY_MODE_OPEN), 0);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(snap.count, 0);
	for (size_t i = 0; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		zassert_true(addr_is_zero(&snap.entries[i]), "snapshot slot %zu not zeroed", i);
	}
}
