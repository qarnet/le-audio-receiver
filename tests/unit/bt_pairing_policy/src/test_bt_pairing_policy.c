/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for bt_pairing_policy — pure OPEN/BONDED_ONLY pairing-mode
 * policy: mode derivation from bond enumeration, exact snapshot entries,
 * defense-in-depth pairing decisions, bonded marking, and reset.
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

/* Zero bonds -> OPEN with an empty snapshot. */
ZTEST(bt_pairing_policy, test_init_open_default)
{
	struct bt_pairing_policy p;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* Persisted bonds rebuild the exact entries and select BONDED_ONLY. */
ZTEST(bt_pairing_policy, test_set_bonds_bonded_only)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x01);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, 2), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 2);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* set_bonds(0) selects OPEN (rebuild after reset). */
ZTEST(bt_pairing_policy, test_set_bonds_zero_selects_open)
{
	struct bt_pairing_policy p;

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, NULL, 0), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* Overflow is atomic: snapshot and mode unchanged, -ENOMEM returned. */
ZTEST(bt_pairing_policy, test_set_bonds_overflow_atomic)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[BT_PAIRING_POLICY_MAX_ENTRIES + 1];

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, BT_PAIRING_POLICY_MAX_ENTRIES + 1),
		      -ENOMEM);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
}

/* OPEN accepts any peer. */
ZTEST(bt_pairing_policy, test_pairing_accept_open_accepts_any)
{
	struct bt_pairing_policy p;
	bt_addr_le_t addr = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &addr), BT_PAIRING_POLICY_ACCEPT);
}

/* BONDED_ONLY accepts a bonded peer and rejects an unbonded peer. */
ZTEST(bt_pairing_policy, test_pairing_accept_bonded_only_rejects_unbonded)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonded = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);
	bt_addr_le_t unbonded = make_addr(BT_ADDR_LE_RANDOM, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, &bonded, 1), 0);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &bonded), BT_PAIRING_POLICY_ACCEPT);
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &unbonded), BT_PAIRING_POLICY_REJECT);
}

/* mark_bonded adds the peer, sets BONDED_ONLY, and is idempotent. */
ZTEST(bt_pairing_policy, test_mark_bonded_adds_and_sets_bonded_only)
{
	struct bt_pairing_policy p;
	bt_addr_le_t peer = make_addr(BT_ADDR_LE_RANDOM, 0xaa, 0x0c, 0x05, 0xa2, 0xaa, 0xdb);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &peer), 0);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
	/* Defense-in-depth accepts the freshly-bonded peer mid-session. */
	zassert_equal(bt_pairing_policy_pairing_accept(&p, &peer), BT_PAIRING_POLICY_ACCEPT);
	/* Re-marking the same peer is idempotent. */
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &peer), 0);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 1);
}

/* mark_bonded on a full snapshot reports -ENOMEM (desired state still set). */
ZTEST(bt_pairing_policy, test_mark_bonded_full_snapshot)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[BT_PAIRING_POLICY_MAX_ENTRIES];
	bt_addr_le_t extra = make_addr(BT_ADDR_LE_RANDOM, 0xfe, 0xed, 0xfa, 0xce, 0xbe, 0xef);

	bt_pairing_policy_init(&p);
	for (size_t i = 0; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		bonds[i] = make_addr(BT_ADDR_LE_PUBLIC, (uint8_t)i, 0x10, 0x20, 0x30, 0x40, 0x50);
	}
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, BT_PAIRING_POLICY_MAX_ENTRIES), 0);
	zassert_equal(bt_pairing_policy_mark_bonded(&p, &extra), -ENOMEM);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), BT_PAIRING_POLICY_MAX_ENTRIES);
}

/* request_open resets to OPEN and clears the snapshot; idempotent. */
ZTEST(bt_pairing_policy, test_request_open_resets_to_open)
{
	struct bt_pairing_policy p;
	bt_addr_le_t bonds[2];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);
	bonds[1] = make_addr(BT_ADDR_LE_PUBLIC, 7, 8, 9, 10, 11, 12);

	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, 2), 0);
	bt_pairing_policy_request_open(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(bt_pairing_policy_get_entry_count(&p), 0);
	/* Idempotent: a second reset stays OPEN. */
	bt_pairing_policy_request_open(&p);
	zassert_equal(bt_pairing_policy_get_mode(&p), BT_PAIRING_POLICY_MODE_OPEN);
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
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, 2), 0);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_BONDED_ONLY);
	zassert_equal(snap.count, 2);
	zassert_equal(bt_addr_le_cmp(&snap.entries[0], &bonds[0]), 0);
	zassert_equal(bt_addr_le_cmp(&snap.entries[1], &bonds[1]), 0);
}

/* Snapshot of a reset policy is OPEN with zero entries. */
ZTEST(bt_pairing_policy, test_snapshot_empty)
{
	struct bt_pairing_policy p;
	struct bt_pairing_policy_snapshot snap;
	bt_addr_le_t bonds[1];

	bonds[0] = make_addr(BT_ADDR_LE_RANDOM, 1, 2, 3, 4, 5, 6);
	bt_pairing_policy_init(&p);
	zassert_equal(bt_pairing_policy_set_bonds(&p, bonds, 1), 0);
	bt_pairing_policy_request_open(&p);
	bt_pairing_policy_snapshot(&p, &snap);
	zassert_equal(snap.mode, BT_PAIRING_POLICY_MODE_OPEN);
	zassert_equal(snap.count, 0);
}
