/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Real Bluetooth/BAP backend for the dedicated LE Audio source fixture
 * (RH1B).  Owns the connection, the BAP unicast client callbacks, the two
 * sink streams/endpoints, the unicast group, and the teardown primitives;
 * exposes them to the coordinator through the production backend ops
 * table.  Never drives lifecycle itself: the coordinator worker owns all
 * blocking operations.
 */

#ifndef HIL_SOURCE_BAP_H
#define HIL_SOURCE_BAP_H

#include "hil_source_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Boot the Bluetooth/BAP backend: bt_enable(NULL), settings_load(),
 * require bt_is_ready(), register connection auth/auth-info callbacks,
 * GATT and BAP unicast client callbacks, register the stream ops, and
 * initialize the TX driver.  Returns 0 or a negative errno. */
int hil_source_bap_init(void);

/* The production backend ops table bound to the real BAP/TX modules. */
const struct hil_source_backend_ops *hil_source_bap_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* HIL_SOURCE_BAP_H */
