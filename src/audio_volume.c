/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_volume.h"
#include "audio_perf.h"

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(audio_volume, LOG_LEVEL_INF);

/* Pack volume (8-bit) and mute (1-bit) into one atomic word.
 * High byte: mute flag.  Low byte: volume level. */
#define VOL_PACK(vol, mute) (((uint32_t)(mute) << 8) | (uint8_t)(vol))
#define VOL_UNPACK_VOL(v)   ((uint8_t)((v) & 0xFFU))
#define VOL_UNPACK_MUTE(v)  ((uint8_t)(((v) >> 8) & 0x1U))

static atomic_t vol_state = ATOMIC_INIT(VOL_PACK(195, 0));

#if defined(CONFIG_BT_VCP_VOL_REND)
#include <zephyr/bluetooth/audio/vcp.h>

static void vcp_state_cb(struct bt_conn *conn, int err, uint8_t volume, uint8_t mute)
{
	if (err) {
		LOG_WRN("VCP state error: %d", err);
		return;
	}
	atomic_set(&vol_state, VOL_PACK(volume, mute));
	LOG_INF("Volume: %u  mute: %u", volume, mute);
}

static struct bt_vcp_vol_rend_cb vcp_cb = {
	.state = vcp_state_cb,
};

int audio_volume_init(void)
{
	struct bt_vcp_vol_rend_register_param param = {
		.mute = BT_VCP_STATE_UNMUTED,
		.volume = CONFIG_BT_AUDIO_VOL_DEFAULT,
		.step = 16,
		.cb = &vcp_cb,
	};

	atomic_set(&vol_state, VOL_PACK(CONFIG_BT_AUDIO_VOL_DEFAULT, 0));

	int err = bt_vcp_vol_rend_register(&param);

	if (err) {
		LOG_ERR("VCP register failed: %d", err);
	} else {
		LOG_INF("VCP ready (default vol=%u)", CONFIG_BT_AUDIO_VOL_DEFAULT);
	}
	return err;
}

#else /* !CONFIG_BT_VCP_VOL_REND */

int audio_volume_init(void)
{
	return 0;
}

#endif /* CONFIG_BT_VCP_VOL_REND */

uint8_t audio_volume_get(void)
{
	return VOL_UNPACK_VOL(atomic_get(&vol_state));
}

bool audio_volume_is_muted(void)
{
	return VOL_UNPACK_MUTE(atomic_get(&vol_state)) != 0;
}

void audio_volume_apply(int16_t *buf, size_t samples)
{
	uint32_t t0 = audio_perf_cycle_start();

	if (buf == NULL || samples == 0) {
		/* Safe no-data exit: NULL with any sample count and zero
		 * samples with any pointer are deterministic no-ops.  This
		 * must precede the state read so mute/zero scaling never
		 * runs memset on a NULL pointer (not a portable C
		 * guarantee).
		 */
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);
		return;
	}

	uint32_t state = (uint32_t)atomic_get(&vol_state);
	uint8_t vol = VOL_UNPACK_VOL(state);
	uint8_t muted = VOL_UNPACK_MUTE(state);

	if (muted || vol == 0) {
		memset(buf, 0, samples * sizeof(int16_t));
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);
		return;
	}
	if (vol == 255) {
		audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);
		return;
	}
	for (size_t i = 0; i < samples; i++) {
		buf[i] = (int16_t)(((int32_t)buf[i] * vol) / 255);
	}

	audio_perf_cycle_end(t0, AUDIO_PERF_PATH_VOLUME);
}
