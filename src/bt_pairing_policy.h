/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pairing-mode policy for the LE Audio Receiver.
 *
 * Two modes:
 *   OPEN         — no connection filtering; any peer may pair/connect.
 *   BONDED_ONLY  — only persisted bonds may establish connections.  The
 *                  controller filter accept list (rebuilt at each
 *                  advertising restart by bt_bap.c) is the primary
 *                  enforcement; pairing_accept() is defense in depth.
 *
 * The mode derives from persisted bonds at boot: zero bonds -> OPEN, one
 * or more bonds -> BONDED_ONLY.  Successful bonded pairing marks
 * BONDED_ONLY as the desired state; the controller filter is rebuilt
 * later from normal thread context (bt_bap_restart_advertising()).
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
 * @brief Initialize the policy to OPEN with an empty snapshot.
 */
void bt_pairing_policy_init(struct bt_pairing_policy *policy);

/**
 * @brief Rebuild the bond snapshot from an enumeration.
 *
 * A count of zero selects OPEN; a count greater than zero selects
 * BONDED_ONLY.  Atomic: on overflow the snapshot and mode are left
 * unchanged.
 *
 * @retval 0        success
 * @retval -ENOMEM  count exceeds BT_PAIRING_POLICY_MAX_ENTRIES
 */
int bt_pairing_policy_set_bonds(struct bt_pairing_policy *policy, const bt_addr_le_t *addrs,
				size_t count);

/**
 * @brief Current desired mode.
 */
enum bt_pairing_policy_mode bt_pairing_policy_get_mode(struct bt_pairing_policy *policy);

/**
 * @brief Number of entries in the snapshot.
 */
size_t bt_pairing_policy_get_entry_count(struct bt_pairing_policy *policy);

/**
 * @brief Defense-in-depth pairing gate.
 *
 * OPEN accepts every peer.  BONDED_ONLY accepts only addresses present in
 * the snapshot (exact identity match).
 */
enum bt_pairing_policy_decision bt_pairing_policy_pairing_accept(struct bt_pairing_policy *policy,
								 const bt_addr_le_t *addr);

/**
 * @brief Mark the freshly-bonded peer and set desired state to BONDED_ONLY.
 *
 * Pure state update (no HCI) — safe from the BT RX workqueue callback
 * context.  The peer is added to the snapshot when absent and space
 * allows.
 *
 * @retval 0        success
 * @retval -ENOMEM  snapshot full (desired state still becomes BONDED_ONLY)
 */
int bt_pairing_policy_mark_bonded(struct bt_pairing_policy *policy, const bt_addr_le_t *addr);

/**
 * @brief Pairing reset: desired state to OPEN and snapshot cleared.
 *
 * The controller filter is cleared at the next advertising restart.
 */
void bt_pairing_policy_request_open(struct bt_pairing_policy *policy);

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
 *
 * @param policy  Policy to snapshot.
 * @param snap    Output snapshot.
 */
void bt_pairing_policy_snapshot(struct bt_pairing_policy *policy,
				struct bt_pairing_policy_snapshot *snap);

#endif /* BT_PAIRING_POLICY_H */
