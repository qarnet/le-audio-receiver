/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-local shadow of <zephyr/bluetooth/audio/vcp.h> for
 * tests/unit/volume.  Contains only the exact NCS v3.3.0 VCP Volume
 * Renderer types used by production src/audio_volume.c, with zero
 * VOCS/AICS instance counts (matching the NCS defaults), plus the fake
 * bt_vcp_vol_rend_register() used to capture registration and invoke the
 * real production state callback.  This include directory is searched
 * before the Zephyr includes for this suite only; the production firmware
 * never sees this file.
 *
 * Layout of struct bt_vcp_vol_rend_register_param and the callback
 * signature match zephyr/include/zephyr/bluetooth/audio/vcp.h in NCS
 * v3.3.0 (Zephyr 3.7): step, mute, volume, vocs_param, aics_param, cb.
 */

#ifndef ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCP_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The volume state is unmuted */
#define BT_VCP_STATE_UNMUTED 0x00
/** The volume state is muted */
#define BT_VCP_STATE_MUTED   0x01

/* Opaque Bluetooth connection (never dereferenced by the test). */
struct bt_conn;

/* Zero VOCS/AICS instance counts (NCS defaults); the element types are
 * never referenced by production code. */
struct bt_vocs_register_param {
};
struct bt_aics_register_param {
};

#define BT_VCP_VOL_REND_VOCS_CNT 0
#define BT_VCP_VOL_REND_AICS_CNT 0

/** Register structure for Volume Control Service (NCS v3.3.0 layout). */
struct bt_vcp_vol_rend_register_param {
	/** Initial step size (1-255) */
	uint8_t step;

	/** Initial mute state (0-1) */
	uint8_t mute;

	/** Initial volume level (0-255) */
	uint8_t volume;

	/** Register parameters for Volume Offset Control Services */
	struct bt_vocs_register_param vocs_param[BT_VCP_VOL_REND_VOCS_CNT];

	/** Register parameters for Audio Input Control Services */
	struct bt_aics_register_param aics_param[BT_VCP_VOL_REND_AICS_CNT];

	/** Volume Control Service callback structure. */
	struct bt_vcp_vol_rend_cb *cb;
};

/** Volume Renderer callbacks (NCS v3.3.0 layout). */
struct bt_vcp_vol_rend_cb {
	void (*state)(struct bt_conn *conn, int err, uint8_t volume, uint8_t mute);
	void (*flags)(struct bt_conn *conn, int err, uint8_t flags);
};

/*
 * Fake registration entry point (implemented in src/fake_vcp.c).
 * Records the initial volume/mute/step and captures the callback
 * pointer (a copy, never the stack registration parameter), returns a
 * test-controllable error, and lets tests invoke the real production
 * state callback.
 */
int bt_vcp_vol_rend_register(struct bt_vcp_vol_rend_register_param *param);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_SERVICES_VCP_H_ */
