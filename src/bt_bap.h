/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_BAP_H
#define BT_BAP_H

#include "pairing_mode.h" /* enum pairing_access_mode (P4 public surface) */

/**
 * Initialize BAP unicast server, PACS, pairing callbacks, and
 * create (but do not start) extended advertising.
 *
 * Must be called after bt_enable() and settings_load().
 * Returns 0 on success, negative errno on failure.
 */
int bt_bap_init(void);

/**
 * Start or restart extended advertising.
 * Returns 0 on success, negative errno on failure.
 */
int bt_bap_restart_advertising(void);

/**
 * Block until the current connection disconnects.
 * Used by the advertising restart loop in main().
 */
void bt_bap_wait_disconnect(void);

/**
 * Production pairing-mode reset: clear all persisted bonds, disconnect the
 * current peer, and return the receiver to open pairing mode.
 *
 * Safe to call from shell/work/thread context.  On success the receiver
 * advertises in OPEN mode (no connection filtering) — immediately when no
 * connection is active, otherwise on the advertising restart that follows
 * the disconnect.
 *
 * @retval 0   full success
 * @retval <0  first negative errno of the failing step (partial failure is
 *             reported, never claimed as success)
 */
int bt_bap_pairing_reset(void);

/**
 * Shell-routable audio-path stop (R1): force-close the BAP lifecycle gate
 * and sink push admission, drain every admitted push and DROP the DMA,
 * then stop the offload pipeline only after the drain.
 *
 * Thread-context and idempotent.  The shell thread never clears/resets
 * Mode A, decoder, sequence, stats, or any other BT-RX-owned state; the
 * gate-close observer event fires exactly once (first open→closed).
 */
void bt_bap_audio_path_stop(void);

#if defined(CONFIG_USER_PAIRING_CONTROL)
/*
 * P4: pairing-mode Bluetooth operations.  Signatures exactly match the
 * P1 `struct pairing_mode_ops` slots; `ctx` is accepted (P1 has one
 * shared context) and ignored — the adapter uses the context installed at
 * bt_bap_init().  Thin calls into the private singleton adapter
 * (bt_bap_pairing_adapter.c).  pairing_mode remains the sole transition
 * owner.
 */

/*
 * Apply the access mode to the pairing policy:
 *   NORMAL    -> BONDED_ONLY (empty inventory legal); inventory preserved;
 *   BONDING   -> OPEN; inventory preserved;
 *   SUSPENDED -> policy untouched (advertising already stopped by the
 *                suspend operation; duplicate publication is benign).
 * Returns 0, -EINVAL for an invalid mode (no change), or the exact policy
 * errno (no change, applied state not published).
 */
int bt_bap_pairing_set_access_mode(enum pairing_access_mode mode, void *ctx);

/*
 * Stop advertising (idempotent) under the advertising lock and publish
 * applied SUSPENDED only on success.  Returns 0 or the exact stop errno.
 */
int bt_bap_pairing_advertising_suspend(void *ctx);

/*
 * Start advertising under the advertising lock: reject applied SUSPENDED
 * with -EACCES (the owner must select NORMAL/BONDING first), then stop,
 * clear the controller filter, enumerate persisted bonds, snapshot the
 * policy atomically, rebuild the filter per the snapshot MODE (never the
 * count; BONDED_ONLY filters even with zero entries), update parameters
 * and start.  Propagates the first exact errno; never reports false
 * success.
 */
int bt_bap_pairing_advertising_start(void *ctx);

/*
 * Request the disconnect of the live peer, if any.  Sets *pending true
 * when a peer is connected and a disconnect was requested (or is already
 * DISCONNECTING), false when there is no peer; never issues a duplicate
 * command.  *pending NULL returns -EINVAL.  Returns 0 or the exact
 * info/disconnect errno.
 */
int bt_bap_pairing_disconnect_peer(bool *pending, void *ctx);

/*
 * Delete every persisted bond, then clear the in-memory inventory only
 * after storage deletion succeeded (the desired mode is preserved).  No
 * disconnect here — the owner guarantees the connection is closed first.
 * Returns the exact storage errno.
 */
int bt_bap_pairing_delete_all_bonds(void *ctx);

/*
 * Request BT_SECURITY_L2 on the live CONNECTED peer.  Returns 0 on
 * success, -ENOTCONN when no CONNECTED peer exists, or the exact
 * set_security errno.
 */
int bt_bap_pairing_request_security(void *ctx);

/*
 * One-way, idempotent notification enable.  P5 calls it only after a
 * successful pairing_mode_init(); before enable, Bluetooth callbacks
 * preserve legacy behavior and never invoke the controller.
 */
void bt_bap_pairing_notifications_enable(void);
#endif /* CONFIG_USER_PAIRING_CONTROL */

#endif /* BT_BAP_H */
