/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared control-ACK correlation engine for FLPR cpuapp control
 * transactions (R8).
 *
 * ONE owner of the request/ACK correlation machinery used by both the
 * production coordinated reset (flpr_ring_mgr) and the acceptance
 * FLPR-stall requests (flpr_acceptance).  Never duplicate this logic:
 * both modules register their own flpr_control_ack instance here.
 *
 * Exact behaviors preserved from the pre-R8 flpr_ring_mgr private
 * engine (R1 design):
 *   - an armed request carries the expected data and a monotonically
 *     incremented nonzero 16-bit request token in flpr_msg.seq;
 *   - the FIRST ACK while armed AND with a matching sequence wins;
 *     ACKs while disarmed or with a stale sequence are counted and
 *     ignored (per-instance stale_count);
 *   - tokens are never reused inside one FLPR session: after 0xFFFF the
 *     next arm returns -EOVERFLOW until flpr_control_ack_reset_session()
 *     (the known quiescence boundary, called by
 *     flpr_ring_mgr_remote_restarted());
 *   - at most one request per instance is armed at a time;
 *   - the semaphore give always happens outside the lock.
 *
 * Lock discipline: the engine has one private leaf spinlock.  Callers
 * may hold ring_data_lock / ring_lock / acc_lock when calling engine
 * functions; the engine lock is never taken while any other lock is
 * held by a callee, so nesting is always safe.
 */

#include "flpr_control_ack.h"

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>

/* Registry of session-scoped instances cleared by reset_session().
 * Fixed capacity: reset (flpr_ring_mgr) + stall (flpr_acceptance). */
#define FLPR_CONTROL_ACK_MAX 4

static struct flpr_control_ack *ack_registry[FLPR_CONTROL_ACK_MAX];
static uint32_t ack_registry_count;

static struct k_spinlock ack_lock;

int flpr_control_ack_init(struct flpr_control_ack *ctl, struct k_sem *sem)
{
	if (!ctl || !sem) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	ctl->sem = sem;
	ctl->payload = 0;
	ctl->expected_data = 0;
	ctl->expected_seq = 0;
	ctl->armed = false;
	ctl->next_token = 1;
	ctl->stale_count = 0;

	k_spin_unlock(&ack_lock, key);
	return 0;
}

void flpr_control_ack_register(struct flpr_control_ack *ctl)
{
	if (!ctl) {
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	for (uint32_t i = 0; i < ack_registry_count; i++) {
		if (ack_registry[i] == ctl) {
			k_spin_unlock(&ack_lock, key);
			return; /* idempotent */
		}
	}
	if (ack_registry_count < FLPR_CONTROL_ACK_MAX) {
		ack_registry[ack_registry_count++] = ctl;
	}

	k_spin_unlock(&ack_lock, key);
}

/* Disarm + clear the request state under the lock, then drain stale
 * semaphore tokens while disarmed (handlers ignore ACKs and do not
 * give). */
void flpr_control_ack_begin(struct flpr_control_ack *ctl)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	ctl->armed = false;
	ctl->expected_seq = 0;
	ctl->expected_data = 0;
	ctl->payload = 0;

	k_spin_unlock(&ack_lock, key);

	while (k_sem_take(ctl->sem, K_NO_WAIT) == 0) {
	}
}

/* Allocate the next nonzero 16-bit token without wrap, install the
 * expected data/token, and arm under the lock.  Returns the token, or
 * -EOVERFLOW when the token space of the current session is exhausted
 * (cleared only by flpr_control_ack_reset_session()). */
int flpr_control_ack_arm(struct flpr_control_ack *ctl, uint32_t expected_data)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	if (ctl->next_token > 0xFFFFU) {
		k_spin_unlock(&ack_lock, key);
		return -EOVERFLOW;
	}
	int token = ctl->next_token;

	ctl->next_token++;
	ctl->expected_data = expected_data;
	ctl->expected_seq = (uint16_t)token;
	ctl->payload = 0;
	ctl->armed = true;

	k_spin_unlock(&ack_lock, key);
	return token;
}

/* Disarm under the lock (send failure / timeout).  The payload is
 * cleared so a late ACK is ignored even when its data equals a later
 * retry — the sequence differs. */
void flpr_control_ack_disarm(struct flpr_control_ack *ctl)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	ctl->armed = false;
	ctl->expected_seq = 0;
	ctl->expected_data = 0;
	ctl->payload = 0;

	k_spin_unlock(&ack_lock, key);
}

/* First-ACK-while-armed-and-matching handler: stores the payload,
 * disarms, then gives the semaphore outside the lock.  Stale/duplicate
 * ACKs are counted and ignored.  Called from IPC receive context. */
void flpr_control_ack_handle(struct flpr_control_ack *ctl, const struct flpr_msg *msg)
{
	bool give = false;

	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	if (ctl->armed && msg->seq == ctl->expected_seq) {
		ctl->payload = msg->data;
		ctl->armed = false;
		give = true;
	} else {
		ctl->stale_count++;
	}

	k_spin_unlock(&ack_lock, key);

	if (give) {
		k_sem_give(ctl->sem);
	}
}

/* Wait for the ACK, then snapshot the stored payload under lock and
 * verify the exact expected data.  Timeout disarms; same-sequence wrong
 * data preserves the fail-fast -EIO. */
int flpr_control_ack_wait(struct flpr_control_ack *ctl, uint32_t timeout_ms, uint32_t expected_data)
{
	int ret = k_sem_take(ctl->sem, K_MSEC(timeout_ms));

	if (ret != 0) {
		flpr_control_ack_disarm(ctl);
		return -ETIMEDOUT;
	}

	k_spinlock_key_t key = k_spin_lock(&ack_lock);
	uint32_t payload = ctl->payload;
	k_spin_unlock(&ack_lock, key);

	if (payload != expected_data) {
		return -EIO;
	}
	return 0;
}

/* Last acked payload (for diagnostics). */
uint32_t flpr_control_ack_payload(struct flpr_control_ack *ctl)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);
	uint32_t v = ctl->payload;
	k_spin_unlock(&ack_lock, key);
	return v;
}

/* Clear every registered instance's request state and reset the token
 * counters to 1 (known quiescence boundary: remote restart), then drain
 * every instance's semaphore.  Semaphore drain happens after all state
 * is cleared while disarmed (handlers ignore ACKs and do not give). */
void flpr_control_ack_reset_session(void)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);

	for (uint32_t i = 0; i < ack_registry_count; i++) {
		struct flpr_control_ack *ctl = ack_registry[i];

		ctl->armed = false;
		ctl->expected_seq = 0;
		ctl->expected_data = 0;
		ctl->payload = 0;
		ctl->next_token = 1;
	}

	k_spin_unlock(&ack_lock, key);

	for (uint32_t i = 0; i < ack_registry_count; i++) {
		while (k_sem_take(ack_registry[i]->sem, K_NO_WAIT) == 0) {
		}
	}
}

#if defined(FLPR_CONTROL_ACK_NATIVE_TEST)
/* GCOVR_EXCL_START — test-only helpers, absent from production builds */

void flpr_control_ack_test_set_next_token(struct flpr_control_ack *ctl, uint16_t token)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);
	ctl->next_token = token;
	k_spin_unlock(&ack_lock, key);
}

uint32_t flpr_control_ack_test_stale_count(struct flpr_control_ack *ctl)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);
	uint32_t v = ctl->stale_count;
	k_spin_unlock(&ack_lock, key);
	return v;
}

void flpr_control_ack_test_reset_registry(void)
{
	k_spinlock_key_t key = k_spin_lock(&ack_lock);
	ack_registry_count = 0;
	k_spin_unlock(&ack_lock, key);
}

/* GCOVR_EXCL_STOP */
#endif /* FLPR_CONTROL_ACK_NATIVE_TEST */
