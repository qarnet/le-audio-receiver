/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure pairing-mode policy.  See bt_pairing_policy.h.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/kernel.h>

#include "bt_pairing_policy.h"

void bt_pairing_policy_init(struct bt_pairing_policy *policy)
{
	/* A zeroed struct k_spinlock is the unlocked state; no init call
	 * exists.  memset also clears the snapshot and entry count. */
	memset(policy, 0, sizeof(*policy));
	policy->mode = BT_PAIRING_POLICY_MODE_OPEN;
}

int bt_pairing_policy_set_mode(struct bt_pairing_policy *policy, enum bt_pairing_policy_mode mode)
{
	k_spinlock_key_t key;

	/* Only OPEN and BONDED_ONLY exist; any other enum value leaves the
	 * full policy (mode, count, and every entry) unchanged. */
	if (mode != BT_PAIRING_POLICY_MODE_OPEN && mode != BT_PAIRING_POLICY_MODE_BONDED_ONLY) {
		return -EINVAL;
	}

	key = k_spin_lock(&policy->lock);
	policy->mode = mode;
	k_spin_unlock(&policy->lock, key);

	return 0;
}

int bt_pairing_policy_replace_bonds(struct bt_pairing_policy *policy, const bt_addr_le_t *addrs,
				    size_t count)
{
	k_spinlock_key_t key;

	/* count == 0 accepts addrs == NULL and clears the inventory; a
	 * nonzero count with a NULL pointer is a caller bug (-EINVAL). */
	if (count == 0) {
		/* clear path below */
	} else if (addrs == NULL) {
		return -EINVAL;
	} else if (count > BT_PAIRING_POLICY_MAX_ENTRIES) {
		return -ENOMEM;
	}

	key = k_spin_lock(&policy->lock);
	policy->entry_count = count;
	for (size_t i = 0; i < count; i++) {
		bt_addr_le_copy(&policy->entries[i], &addrs[i]);
	}
	/* Zero unused/tail slots so snapshots never retain stale addresses
	 * beyond count. */
	for (size_t i = count; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		memset(&policy->entries[i], 0, sizeof(policy->entries[i]));
	}
	k_spin_unlock(&policy->lock, key);

	return 0;
}

enum bt_pairing_policy_mode bt_pairing_policy_get_mode(struct bt_pairing_policy *policy)
{
	k_spinlock_key_t key = k_spin_lock(&policy->lock);
	enum bt_pairing_policy_mode mode = policy->mode;

	k_spin_unlock(&policy->lock, key);
	return mode;
}

size_t bt_pairing_policy_get_entry_count(struct bt_pairing_policy *policy)
{
	k_spinlock_key_t key = k_spin_lock(&policy->lock);
	size_t count = policy->entry_count;

	k_spin_unlock(&policy->lock, key);
	return count;
}

enum bt_pairing_policy_decision bt_pairing_policy_pairing_accept(struct bt_pairing_policy *policy,
								 const bt_addr_le_t *addr)
{
	enum bt_pairing_policy_decision decision;
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

	/* OPEN accepts every peer regardless of inventory; BONDED_ONLY with
	 * zero entries rejects every peer (the loop below never matches). */
	if (policy->mode == BT_PAIRING_POLICY_MODE_OPEN) {
		decision = BT_PAIRING_POLICY_ACCEPT;
	} else {
		decision = BT_PAIRING_POLICY_REJECT;
		for (size_t i = 0; i < policy->entry_count; i++) {
			if (bt_addr_le_cmp(&policy->entries[i], addr) == 0) {
				decision = BT_PAIRING_POLICY_ACCEPT;
				break;
			}
		}
	}
	k_spin_unlock(&policy->lock, key);
	return decision;
}

int bt_pairing_policy_mark_bonded(struct bt_pairing_policy *policy, const bt_addr_le_t *addr)
{
	k_spinlock_key_t key;
	bool found = false;
	int ret = 0;

	if (addr == NULL) {
		return -EINVAL;
	}

	key = k_spin_lock(&policy->lock);
	for (size_t i = 0; i < policy->entry_count; i++) {
		if (bt_addr_le_cmp(&policy->entries[i], addr) == 0) {
			found = true;
			break;
		}
	}
	if (!found) {
		if (policy->entry_count >= BT_PAIRING_POLICY_MAX_ENTRIES) {
			ret = -ENOMEM;
		} else {
			bt_addr_le_copy(&policy->entries[policy->entry_count], addr);
			policy->entry_count++;
		}
	}
	/* Mode is never derived from or mutated by the inventory. */
	k_spin_unlock(&policy->lock, key);
	return ret;
}

void bt_pairing_policy_clear_bonds(struct bt_pairing_policy *policy)
{
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

	policy->entry_count = 0;
	for (size_t i = 0; i < BT_PAIRING_POLICY_MAX_ENTRIES; i++) {
		memset(&policy->entries[i], 0, sizeof(policy->entries[i]));
	}
	k_spin_unlock(&policy->lock, key);
}

void bt_pairing_policy_snapshot(struct bt_pairing_policy *policy,
				struct bt_pairing_policy_snapshot *snap)
{
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

	snap->mode = policy->mode;
	snap->count = policy->entry_count;
	/* Zero output entry storage first so the caller never sees stale
	 * tail data beyond count. */
	memset(snap->entries, 0, sizeof(snap->entries));
	for (size_t i = 0; i < policy->entry_count; i++) {
		bt_addr_le_copy(&snap->entries[i], &policy->entries[i]);
	}
	k_spin_unlock(&policy->lock, key);
}
