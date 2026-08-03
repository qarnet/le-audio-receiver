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

int bt_pairing_policy_set_bonds(struct bt_pairing_policy *policy, const bt_addr_le_t *addrs,
				size_t count)
{
	k_spinlock_key_t key;

	if (count > BT_PAIRING_POLICY_MAX_ENTRIES) {
		return -ENOMEM;
	}

	key = k_spin_lock(&policy->lock);
	for (size_t i = 0; i < count; i++) {
		bt_addr_le_copy(&policy->entries[i], &addrs[i]);
	}
	policy->entry_count = count;
	policy->mode =
		(count > 0) ? BT_PAIRING_POLICY_MODE_BONDED_ONLY : BT_PAIRING_POLICY_MODE_OPEN;
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

const bt_addr_le_t *bt_pairing_policy_get_entry(struct bt_pairing_policy *policy, size_t index)
{
	const bt_addr_le_t *entry;
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

	if (index < policy->entry_count) {
		entry = &policy->entries[index];
	} else {
		entry = NULL;
	}
	k_spin_unlock(&policy->lock, key);
	return entry;
}

enum bt_pairing_policy_decision bt_pairing_policy_pairing_accept(struct bt_pairing_policy *policy,
								 const bt_addr_le_t *addr)
{
	enum bt_pairing_policy_decision decision;
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

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
	policy->mode = BT_PAIRING_POLICY_MODE_BONDED_ONLY;
	k_spin_unlock(&policy->lock, key);
	return ret;
}

void bt_pairing_policy_request_open(struct bt_pairing_policy *policy)
{
	k_spinlock_key_t key = k_spin_lock(&policy->lock);

	policy->mode = BT_PAIRING_POLICY_MODE_OPEN;
	policy->entry_count = 0;
	k_spin_unlock(&policy->lock, key);
}
