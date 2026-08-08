/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pairing-mode policy for the LE Audio Receiver.
 *
 * Two independent pieces of state:
 *   mode       — desired access mode: OPEN or BONDED_ONLY.
 *   inventory  — the persisted bond snapshot (up to
 *                BT_PAIRING_POLICY_MAX_ENTRIES addresses).
 *
 * The two are fully independent (P3): mode changes never mutate the
 * inventory, and inventory changes never derive or mutate the mode.
 * BONDED_ONLY with an empty inventory is legal (rejects every peer);
 * OPEN with a nonempty inventory is legal (accepts any peer while the
 * known bonds stay preserved).
 *
 *   OPEN         — no connection filtering; any peer may pair/connect.
 *   BONDED_ONLY  — only persisted bonds may establish connections.  The
 *                  controller filter accept list (rebuilt at each
 *                  advertising restart by bt_bap.c) is the primary
 *                  enforcement; pairing_accept() is defense in depth.
 *
 * The module is deliberately pure: it stores an address snapshot and
 * makes decisions only.  All controller/HCI work lives in bt_bap.c.
 * State is protected by a spinlock so callbacks on the BT RX workqueue
 * (cooperative context) may safely read and update it.
 */

#ifndef BT_PAIRING_POLICY_H
#define BT_PAIRING_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/kernel.h>

/* nRF controller filter-accept-list limit (SW Split FAL_SIZE, SDC). */
#define BT_PAIRING_POLICY_MAX_ENTRIES 8

enum bt_pairing_policy_mode {
	BT_PAIRING_POLICY_MODE_OPEN = 0,
	BT_PAIRING_POLICY_MODE_BONDED_ONLY,
};

enum bt_pairing_policy_decision {
	BT_PAIRING_POLICY_ACCEPT = 0,
	BT_PAIRING_POLICY_REJECT,
};

struct bt_pairing_policy {
	enum bt_pairing_policy_mode mode;
	bt_addr_le_t entries[BT_PAIRING_POLICY_MAX_ENTRIES];
	size_t entry_count;
	struct k_spinlock lock;
};

/**
 * @brief Initialize the policy to OPEN with an empty inventory.
 *
 * Mode and inventory are independent; init sets both to their neutral
 * state (OPEN + empty), preserving the legacy feature-off behavior.
 */
void bt_pairing_policy_init(struct bt_pairing_policy *policy);

/**
 * @brief Set the desired access mode without touching the inventory.
 *
 * Only OPEN and BONDED_ONLY are accepted.  Any other enum value returns
 * -EINVAL and leaves the full policy unchanged.  Idempotent; on success
 * the entry count and every entry are preserved.
 *
 * @retval 0        success
 * @retval -EINVAL  mode is neither OPEN nor BONDED_ONLY (no change)
 */
int bt_pairing_policy_set_mode(struct bt_pairing_policy *policy, enum bt_pairing_policy_mode mode);

/**
 * @brief Replace the bond inventory exactly, preserving the mode.
 *
 * A count of zero clears the inventory (addrs may be NULL).  A nonzero
 * count with a NULL addrs pointer returns -EINVAL and a count beyond
 * BT_PAIRING_POLICY_MAX_ENTRIES returns -ENOMEM — both fully atomic
 * with no change.  On success unused/tail slots are zeroed so snapshots
 * never retain stale addresses beyond count.  Duplicate input addresses
 * may remain duplicate.
 *
 * @retval 0        success
 * @retval -EINVAL  count > 0 with addrs == NULL (no change)
 * @retval -ENOMEM  count exceeds BT_PAIRING_POLICY_MAX_ENTRIES (no change)
 */
int bt_pairing_policy_replace_bonds(struct bt_pairing_policy *policy, const bt_addr_le_t *addrs,
				    size_t count);

/**
 * @brief Current desired mode.
 */
enum bt_pairing_policy_mode bt_pairing_policy_get_mode(struct bt_pairing_policy *policy);

/**
 * @brief Number of entries in the inventory.
 */
size_t bt_pairing_policy_get_entry_count(struct bt_pairing_policy *policy);

/**
 * @brief Defense-in-depth pairing gate.
 *
 * OPEN accepts every peer regardless of the inventory.  BONDED_ONLY with
 * zero entries rejects every peer; BONDED_ONLY with entries accepts only
 * addresses present in the inventory (exact identity match).
 */
enum bt_pairing_policy_decision bt_pairing_policy_pairing_accept(struct bt_pairing_policy *policy,
								 const bt_addr_le_t *addr);

/**
 * @brief Mark a freshly-bonded peer in the inventory without touching mode.
 *
 * Pure state update (no HCI) — safe from the BT RX workqueue callback
 * context.  The peer is added when absent and capacity allows; marking a
 * duplicate is idempotent.  The mode is preserved for success,
 * duplicate, and overflow.
 *
 * @retval 0        success (added or already present)
 * @retval -ENOMEM  inventory full and peer absent (inventory unchanged)
 * @retval -EINVAL  addr == NULL (full policy unchanged)
 */
int bt_pairing_policy_mark_bonded(struct bt_pairing_policy *policy, const bt_addr_le_t *addr);

/**
 * @brief Clear the bond inventory, preserving the mode exactly.
 *
 * Zeroes the entry count and every entry slot.  Idempotent.  The
 * controller filter is cleared at the next advertising restart.
 */
void bt_pairing_policy_clear_bonds(struct bt_pairing_policy *policy);

struct bt_pairing_policy_snapshot {
	enum bt_pairing_policy_mode mode;
	bt_addr_le_t entries[BT_PAIRING_POLICY_MAX_ENTRIES];
	size_t count;
};

/**
 * @brief Atomic snapshot of mode and bond entries.
 *
 * Copies the full policy state under one spinlock hold so a concurrent
 * writer (pairing_complete's mark_bonded on the BT RX workqueue) can never
 * yield a torn mode/entries view during a controller-filter rebuild.
 * Output entry storage is zeroed before copying the active entries so the
 * caller never sees stale tail data.
 *
 * @param policy  Policy to snapshot.
 * @param snap    Output snapshot.
 */
void bt_pairing_policy_snapshot(struct bt_pairing_policy *policy,
				struct bt_pairing_policy_snapshot *snap);

#endif /* BT_PAIRING_POLICY_H */
