/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private Bluetooth pairing adapter (P4 of the user pairing control plan,
 * docs/development/user-pairing-control-plan.md).
 *
 * Project-private seam between the pairing-mode transition owner
 * (src/pairing_mode.c) and the concrete Zephyr Bluetooth owner
 * (src/bt_bap.c).  The adapter owns ONLY:
 *
 *   - the applied access state (NORMAL / BONDING / SUSPENDED);
 *   - the operation mechanics for the six P1 Bluetooth operations;
 *   - the callback-event translation gate for pairing_mode notifications.
 *
 * It holds no visible transition state (pairing_mode remains the sole
 * transition owner) and no Bluetooth objects.  bt_bap.c keeps every
 * struct bt_conn, the advertising set, callback registration, and ref
 * lifetime detail; the adapter reaches them only through the injected
 * backend table.
 *
 * The header is deliberately free of Zephyr Bluetooth connection and
 * advertising types: peer objects and the advertising set stay inside
 * bt_bap.c.  Opaque context pointers (void *ctx) pass through untouched;
 * the adapter itself never stores a peer object.
 */

#ifndef BT_BAP_PAIRING_ADAPTER_H
#define BT_BAP_PAIRING_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>

#include "bt_pairing_policy.h" /* enum bt_pairing_policy_mode for the snapshot */
#include "pairing_mode.h"      /* enum pairing_access_mode (P1 slot contract) */

/*
 * Policy snapshot handed to the adapter for its filtering decision.  Mode
 * is the authority (NORMAL/BONDED_ONLY filters, BONDING/OPEN does not) —
 * never the entry count.  No address storage crosses the adapter boundary;
 * the backend re-reads the inventory when it rebuilds the controller
 * filter.
 */
struct bt_bap_pairing_policy_snap {
	enum bt_pairing_policy_mode mode;
	size_t count;
};

/* Advertising (re)start request decided by the adapter from the policy
 * snapshot mode.  Backend fills the controller filter from the policy
 * inventory when filter_connections is true. */
struct bt_bap_pairing_adv_req {
	bool filter_connections;
};

/* Peer state reported by the backend.  The backend owns every acquired
 * connection ref and must release it before returning; the adapter never
 * stores or dereferences a peer. */
enum bt_bap_pairing_peer_state {
	BT_BAP_PAIRING_PEER_ABSENT = 0,
	BT_BAP_PAIRING_PEER_CONNECTED,
	BT_BAP_PAIRING_PEER_DISCONNECTING,
	BT_BAP_PAIRING_PEER_OTHER,
};

/* Typed peer result: state at lookup plus the exact operation errno
 * (0 on success). */
struct bt_bap_pairing_peer_result {
	enum bt_bap_pairing_peer_state state;
	int err;
};

/*
 * Immutable backend supplied once by bt_bap.c.  Every slot is mandatory;
 * init rejects a NULL backend or any missing slot atomically.
 *
 * adv_locked runs fn(ctx) while holding the advertising lock
 * (pairing_adv_lock); the advertising step slots (adv_stop, fal_clear,
 * bonds_replace, policy_snapshot, adv_update_param, adv_start) are only
 * invoked from inside that runner and must not take the lock themselves.
 */
struct bt_bap_pairing_backend {
	/* Advertising lock runner (bt_bap.c pairing_adv_lock). */
	int (*adv_locked)(int (*fn)(void *ctx), void *ctx);

	/* Advertising / FAL / bond-enumeration steps (lock held). */
	int (*adv_stop)(void *ctx);      /* idempotent ext-adv stop */
	int (*fal_clear)(void *ctx);     /* clear controller filter accept list */
	int (*bonds_replace)(void *ctx); /* enumerate persisted bonds + policy replace */
	void (*policy_snapshot)(struct bt_bap_pairing_policy_snap *snap, void *ctx);
	int (*adv_update_param)(const struct bt_bap_pairing_adv_req *req, void *ctx);
	int (*adv_start)(void *ctx);

	/* Policy mutation (pure, no advertising lock required). */
	int (*policy_set_mode)(enum bt_pairing_policy_mode mode, void *ctx);
	void (*policy_clear)(void *ctx); /* clear inventory, preserve mode */

	/* Peer operations (backend takes/releases its own conn ref). */
	int (*peer_disconnect)(struct bt_bap_pairing_peer_result *result, void *ctx);
	int (*peer_security)(struct bt_bap_pairing_peer_result *result, void *ctx);

	/* Persisted-bond storage deletion (bt_unpair all). */
	int (*storage_delete)(void *ctx);

	/* Pairing-mode notification enqueue (returns 0 / -ECANCELED after
	 * fatal / other unexpected errno; the adapter logs but never
	 * retries or performs controller work). */
	int (*notify_connected)(void *ctx);
	int (*notify_disconnected)(void *ctx);
	int (*notify_pairing_complete)(bool bonded, void *ctx);
	int (*notify_pairing_failed)(void *ctx);
	int (*notify_security_changed)(bool success, bool bonded, void *ctx);
};

/*
 * Validate and install one immutable backend/context and start the
 * applied access state at SUSPENDED with notifications disabled.  Returns
 * 0, -EINVAL when the backend or any slot is missing (atomic: no state
 * change), -EALREADY on a second initialization.
 */
int bt_bap_pairing_adapter_init(const struct bt_bap_pairing_backend *backend, void *ctx);

/*
 * P1 operation mechanics.  Each applies exactly one requested operation;
 * pairing_mode remains the sole transition owner.  ctx is accepted for
 * signature parity with the pairing_mode_ops slots and ignored — the
 * backend always receives the context installed at init.  Returns -EINVAL
 * before a successful init.
 */
int bt_bap_pairing_adapter_set_access_mode(enum pairing_access_mode mode, void *ctx);
int bt_bap_pairing_adapter_advertising_suspend(void *ctx);
int bt_bap_pairing_adapter_advertising_start(void *ctx);
int bt_bap_pairing_adapter_disconnect_peer(bool *pending, void *ctx);
int bt_bap_pairing_adapter_delete_all_bonds(void *ctx);
int bt_bap_pairing_adapter_request_security(void *ctx);

/*
 * One-way, idempotent notification-enable.  Before it is called the
 * callback translation functions below preserve legacy behavior and never
 * invoke a pairing-mode notification (avoids -EINVAL before controller
 * init).  P5 calls it only after a successful pairing_mode_init().
 */
void bt_bap_pairing_adapter_notifications_enable(void);
bool bt_bap_pairing_adapter_notifications_enabled(void);

/*
 * Callback-event translation, called from bt_bap.c Bluetooth callbacks
 * (BT RX context).  Each checks the notification gate, forwards the event
 * through the backend with its exact payload, and logs unexpected enqueue
 * results without retry, blocking, HCI work, or a second fatal owner.
 * No inline transition or controller action ever runs here.
 */
void bt_bap_pairing_adapter_notify_connected(void);
void bt_bap_pairing_adapter_notify_disconnected(void);
void bt_bap_pairing_adapter_notify_pairing_complete(bool bonded);
void bt_bap_pairing_adapter_notify_pairing_failed(void);
void bt_bap_pairing_adapter_notify_security_changed(bool success, bool bonded);

#ifdef BT_BAP_PAIRING_ADAPTER_TEST
/* Test-only seam (never compiled into production firmware): reset every
 * file-static module state between tests.  Declared here so the direct
 * test suite can call it. */
void bt_bap_pairing_adapter_test_reset(void);
#endif

#endif /* BT_BAP_PAIRING_ADAPTER_H */
