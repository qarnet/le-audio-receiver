/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BT_BAP_H
#define BT_BAP_H

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

#endif /* BT_BAP_H */
