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

#endif /* BT_BAP_H */
