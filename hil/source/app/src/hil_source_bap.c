/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Real Bluetooth/BAP backend for the dedicated LE Audio source fixture.
 *
 * Static storage: two bt_bap_stream, two sink endpoint pointers, one
 * unicast group, one app-owned connection.  Stream ops are registered
 * with the public bt_bap_stream_cb_register(), never direct `.ops`
 * assignment.
 *
 * Completion ownership: the ASCS response listeners only record the
 * first response code/reason and, on rejection, store the first
 * operation error and signal the operation semaphore once so the worker
 * fails promptly.  Successful endpoint-state callbacks
 * (stream_ops.configured / qos_set / enabled / connected / started /
 * disabled / released) are the only success completion gates, so no
 * stream can consume another stream's completion token.  Sink ASEs are
 * server-started: kick_start validates sink readiness and never calls
 * bt_bap_stream_start().
 *
 * One backend spinlock guards every callback-written / status-read
 * field (operation error, security fields, disconnect reason, ASCS
 * fields, endpoint pointers, connection pointer, group pointer).
 * Bluetooth APIs, object unrefs, and waits never run while it is held;
 * operations copy a pointer under the lock and call the API after
 * unlocking.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/iso.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include "hil_source_app.h"
#include "hil_source_bap.h"
#include "hil_source_controller_time.h"
#include "hil_source_tx.h"

/* ── static storage ──────────────────────────────────────────────── */

static struct bt_conn *default_conn;
static struct bt_bap_stream bap_streams[HIL_SOURCE_MAX_STREAMS];
static struct bt_bap_ep *sink_eps[HIL_SOURCE_MAX_STREAMS];
static struct bt_bap_unicast_group *unicast_group;

static enum hil_source_mode run_mode;
static enum hil_source_profile run_profile;
static uint8_t run_stream_count;
static struct bt_bap_lc3_preset run_presets[HIL_SOURCE_MAX_STREAMS];

/* One backend lock for every callback-written / status-read field. */
static struct k_spinlock backend_lock;

static int op_error; /* most recent async lifecycle operation error */
static uint8_t stream_connect_pending_idx = UINT8_MAX;
static uint8_t stream_connect_completion_idx = UINT8_MAX;
static enum hil_source_stream_connect_outcome stream_connect_completion_outcome =
	HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;

static uint8_t security_level_now;
static int security_error_now;
static uint8_t disconnect_reason_now;
static uint8_t first_ascs_code_now;
static uint8_t first_ascs_reason_now;
static bool first_ascs_recorded;

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_discovered, 0, 1);
static K_SEM_DEFINE(sem_configured, 0, 4);
static K_SEM_DEFINE(sem_qos, 0, 4);
static K_SEM_DEFINE(sem_enabled, 0, 4);
static K_SEM_DEFINE(sem_stream_connected, 0, 4);
static K_SEM_DEFINE(sem_started, 0, 4);
static K_SEM_DEFINE(sem_disabled, 0, 4);
static K_SEM_DEFINE(sem_released, 0, 8);
static K_SEM_DEFINE(sem_disconnected, 0, 1);

/* ── presets ─────────────────────────────────────────────────────── */

#define HIL_SOURCE_QOS_PHY_SELECTOR                                                                \
	((CONFIG_HIL_SOURCE_QOS_PHY == 1) ? BT_BAP_QOS_CFG_1M : BT_BAP_QOS_CFG_2M)

static struct bt_bap_lc3_preset preset_48_4_1_mono = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_MONO_AUDIO, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_mono = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_MONO_AUDIO, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_4_1_fl = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_4_1_fr = BT_BAP_LC3_UNICAST_PRESET_48_4_1(
	BT_AUDIO_LOCATION_FRONT_RIGHT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_fl = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);
static struct bt_bap_lc3_preset preset_48_3_1_fr = BT_BAP_LC3_UNICAST_PRESET_48_3_1(
	BT_AUDIO_LOCATION_FRONT_RIGHT, BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED);

/* Mode B: one ASE, [L frame][R frame] per SDU; per-channel octets equal
 * the mono preset rate, SDU = 2 x octets (matches
 * tests/bsim/client/src/bsim_client_main.c). */
static struct bt_bap_lc3_preset preset_modeb_10ms = {
	.codec_cfg = BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_10,
		(BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT), 120u, 1,
		BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
	.qos = BT_BAP_QOS_CFG(10000u, BT_BAP_QOS_CFG_FRAMING_UNFRAMED, HIL_SOURCE_QOS_PHY_SELECTOR,
			      240u, (uint8_t)CONFIG_HIL_SOURCE_QOS_RTN, 20u, 40000u),
};
static struct bt_bap_lc3_preset preset_modeb_7p5ms = {
	.codec_cfg = BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_7_5,
		(BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT), 90u, 1,
		BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED),
	.qos = BT_BAP_QOS_CFG(7500u, BT_BAP_QOS_CFG_FRAMING_UNFRAMED, HIL_SOURCE_QOS_PHY_SELECTOR,
			      180u, (uint8_t)CONFIG_HIL_SOURCE_QOS_RTN, 15u, 40000u),
};

static void bap_build_presets(enum hil_source_mode mode, enum hil_source_profile profile)
{
	bool p31 = (profile == HIL_SOURCE_PROFILE_48_3_1);

	switch (mode) {
	case HIL_SOURCE_MODE_MONO:
		run_presets[0] = p31 ? preset_48_3_1_mono : preset_48_4_1_mono;
		break;
	case HIL_SOURCE_MODE_A:
		run_presets[0] = p31 ? preset_48_3_1_fl : preset_48_4_1_fl;
		run_presets[1] = p31 ? preset_48_3_1_fr : preset_48_4_1_fr;
		break;
	case HIL_SOURCE_MODE_B:
		run_presets[0] = p31 ? preset_modeb_7p5ms : preset_modeb_10ms;
		break;
	default:
		break;
	}
}

/* Record the first async operation error; later success callbacks must
 * not overwrite it (kicks reset op_error once at their start). */
static void record_first_op_error(int err)
{
	k_spinlock_key_t key = k_spin_lock(&backend_lock);

	if (op_error == 0) {
		op_error = err;
	}
	k_spin_unlock(&backend_lock, key);
}

/* ── connection callbacks ────────────────────────────────────────── */

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	bool ours;
	struct bt_conn *owned;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	owned = default_conn;
	ours = (conn == owned);
	if (err != 0 && ours) {
		if (op_error == 0) {
			op_error = -EIO;
		}
		default_conn = NULL;
	} else if (!ours && op_error == 0) {
		/* Wrong-connection callback: record the first error and wake
		 * the pending connect wait so the worker fails promptly.  The
		 * backend-owned expected connection reference is untouched. */
		op_error = -EIO;
	}
	k_spin_unlock(&backend_lock, key);
	if (err != 0 && ours && owned != NULL) {
		bt_conn_unref(owned);
	}
	k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	struct bt_conn *owned;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	owned = default_conn;
	if (conn != owned) {
		k_spin_unlock(&backend_lock, key);
		return;
	}
	disconnect_reason_now = reason;
	default_conn = NULL;
	k_spin_unlock(&backend_lock, key);
	if (owned != NULL) {
		bt_conn_unref(owned);
	}
	k_sem_give(&sem_disconnected);
}

static void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	bool ours;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ours = (conn == default_conn);
	if (ours) {
		security_level_now = level;
		if (err != 0) {
			security_error_now = (int)err;
			if (op_error == 0) {
				op_error = -EACCES;
			}
		} else if (level >= BT_SECURITY_L2) {
			security_error_now = 0;
		}
	}
	k_spin_unlock(&backend_lock, key);
	if (ours && (err != 0 || level >= BT_SECURITY_L2)) {
		k_sem_give(&sem_security);
	}
}

BT_CONN_CB_DEFINE(hil_source_conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.security_changed = security_changed_cb,
};

/* ── pairing (Just Works) ────────────────────────────────────────── */

static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);
	return BT_SECURITY_ERR_SUCCESS;
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);
}

static struct bt_conn_auth_cb conn_auth_cb = {
	.pairing_accept = pairing_accept,
};

static struct bt_conn_auth_info_cb conn_auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ── GATT MTU ────────────────────────────────────────────────────── */

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(tx);
	ARG_UNUSED(rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

/* ── ASCS response listeners ───────────────────────────────────────
 *
 * Listeners only record the first response code/reason and, on
 * rejection, store the first operation error and wake the current wait
 * once.  Successful completions are signaled exclusively by the
 * endpoint-state callbacks, so exactly one completion token exists per
 * stream per operation.
 */

static void listener_config(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			    enum bt_bap_ascs_reason reason)
{
	bool rejected;

	ARG_UNUSED(stream);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (!first_ascs_recorded) {
		first_ascs_code_now = (uint8_t)rsp_code;
		first_ascs_reason_now = (uint8_t)reason;
		first_ascs_recorded = true;
	}
	rejected = (rsp_code != BT_BAP_ASCS_RSP_CODE_SUCCESS);
	if (rejected && op_error == 0) {
		op_error = -EBADMSG;
	}
	k_spin_unlock(&backend_lock, key);
	if (rejected) {
		k_sem_give(&sem_configured);
	}
}

static void listener_qos(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			 enum bt_bap_ascs_reason reason)
{
	bool rejected;

	ARG_UNUSED(stream);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (!first_ascs_recorded) {
		first_ascs_code_now = (uint8_t)rsp_code;
		first_ascs_reason_now = (uint8_t)reason;
		first_ascs_recorded = true;
	}
	rejected = (rsp_code != BT_BAP_ASCS_RSP_CODE_SUCCESS);
	if (rejected && op_error == 0) {
		op_error = -EBADMSG;
	}
	k_spin_unlock(&backend_lock, key);
	if (rejected) {
		k_sem_give(&sem_qos);
	}
}

static void listener_enable(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			    enum bt_bap_ascs_reason reason)
{
	bool rejected;

	ARG_UNUSED(stream);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (!first_ascs_recorded) {
		first_ascs_code_now = (uint8_t)rsp_code;
		first_ascs_reason_now = (uint8_t)reason;
		first_ascs_recorded = true;
	}
	rejected = (rsp_code != BT_BAP_ASCS_RSP_CODE_SUCCESS);
	if (rejected && op_error == 0) {
		op_error = -EBADMSG;
	}
	k_spin_unlock(&backend_lock, key);
	if (rejected) {
		k_sem_give(&sem_enabled);
	}
}

static void listener_disable(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			     enum bt_bap_ascs_reason reason)
{
	bool rejected;

	ARG_UNUSED(stream);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (!first_ascs_recorded) {
		first_ascs_code_now = (uint8_t)rsp_code;
		first_ascs_reason_now = (uint8_t)reason;
		first_ascs_recorded = true;
	}
	rejected = (rsp_code != BT_BAP_ASCS_RSP_CODE_SUCCESS);
	if (rejected && op_error == 0) {
		op_error = -EBADMSG;
	}
	k_spin_unlock(&backend_lock, key);
	if (rejected) {
		k_sem_give(&sem_disabled);
	}
}

static void listener_release(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code rsp_code,
			     enum bt_bap_ascs_reason reason)
{
	bool rejected;

	ARG_UNUSED(stream);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (!first_ascs_recorded) {
		first_ascs_code_now = (uint8_t)rsp_code;
		first_ascs_reason_now = (uint8_t)reason;
		first_ascs_recorded = true;
	}
	rejected = (rsp_code != BT_BAP_ASCS_RSP_CODE_SUCCESS);
	if (rejected && op_error == 0) {
		op_error = -EBADMSG;
	}
	k_spin_unlock(&backend_lock, key);
	if (rejected) {
		k_sem_give(&sem_released);
	}
}

static void endpoint_cb(struct bt_conn *conn, enum bt_audio_dir dir, struct bt_bap_ep *ep)
{
	uint8_t i;
	struct bt_conn *owned;

	if (dir != BT_AUDIO_DIR_SINK || ep == NULL) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	owned = default_conn;
	if (conn == NULL || conn != owned) {
		/* Wrong-connection endpoint: never store the endpoint, and
		 * record the first error so the discovery wait fails. */
		if (op_error == 0) {
			op_error = -EIO;
		}
		k_spin_unlock(&backend_lock, key);
		return;
	}
	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		if (sink_eps[i] == NULL) {
			sink_eps[i] = ep;
			break;
		}
	}
	k_spin_unlock(&backend_lock, key);
}

static void discover_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	struct bt_conn *owned;
	bool ours;

	ARG_UNUSED(dir);
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	owned = default_conn;
	ours = (conn != NULL && conn == owned);
	if (ours) {
		if (err != 0 && err != BT_ATT_ERR_ATTRIBUTE_NOT_FOUND && op_error == 0) {
			op_error = -EIO;
		}
	} else if (op_error == 0) {
		/* Wrong-connection discovery completion: record the first
		 * error and wake the wait so the worker exits promptly. */
		op_error = -EIO;
	}
	k_spin_unlock(&backend_lock, key);
	k_sem_give(&sem_discovered);
}

static struct bt_bap_unicast_client_cb unicast_client_cbs = {
	.config = listener_config,
	.qos = listener_qos,
	.enable = listener_enable,
	.release = listener_release,
	.disable = listener_disable,
	.discover = discover_cb,
	.endpoint = endpoint_cb,
};

/* ── stream ops ──────────────────────────────────────────────────── */

static uint8_t stream_index(const struct bt_bap_stream *stream)
{
	uint8_t i;

	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		if (stream == &bap_streams[i]) {
			return i;
		}
	}
	return UINT8_MAX;
}

static void stream_configured(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg_pref *pref)
{
	ARG_UNUSED(pref);
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_configured);
}

static void stream_qos_set(struct bt_bap_stream *stream)
{
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_qos);
}

static void stream_enabled(struct bt_bap_stream *stream)
{
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_enabled);
}

static void stream_connected_cb(struct bt_bap_stream *stream)
{
	uint8_t idx = stream_index(stream);
	uint8_t pending_idx;
	bool wake = false;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	pending_idx = stream_connect_pending_idx;
	if (pending_idx != UINT8_MAX) {
		stream_connect_completion_idx = pending_idx;
		stream_connect_pending_idx = UINT8_MAX;
		if (idx == pending_idx) {
			stream_connect_completion_outcome =
				HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED;
		} else {
			stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
			if (op_error == 0) {
				op_error = -EIO;
			}
		}
		wake = true;
	} else if (idx == UINT8_MAX && op_error == 0) {
		op_error = -EIO;
	}
	k_spin_unlock(&backend_lock, key);
	if (wake) {
		k_sem_give(&sem_stream_connected);
	}
}

static void stream_started(struct bt_bap_stream *stream)
{
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_started);
}

static void stream_disabled(struct bt_bap_stream *stream)
{
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_disabled);
}

static void stream_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	ARG_UNUSED(stream);
	ARG_UNUSED(reason);
}

static void stream_disconnected_cb(struct bt_bap_stream *stream, uint8_t reason)
{
	ARG_UNUSED(reason);
	uint8_t idx = stream_index(stream);
	uint8_t pending_idx;
	bool wake = false;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	pending_idx = stream_connect_pending_idx;
	if (pending_idx != UINT8_MAX) {
		stream_connect_completion_idx = pending_idx;
		stream_connect_pending_idx = UINT8_MAX;
		if (idx == pending_idx) {
			stream_connect_completion_outcome =
				HIL_SOURCE_STREAM_CONNECT_OUTCOME_RETRYABLE_FAILURE;
		} else {
			stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
			if (op_error == 0) {
				op_error = -EIO;
			}
		}
		wake = true;
	}
	k_spin_unlock(&backend_lock, key);
	if (wake) {
		k_sem_give(&sem_stream_connected);
	}
}

static void stream_released(struct bt_bap_stream *stream)
{
	if (stream_index(stream) == UINT8_MAX) {
		record_first_op_error(-EIO);
		return;
	}
	k_sem_give(&sem_released);
}

static void stream_sent(struct bt_bap_stream *stream)
{
	uint8_t idx = stream_index(stream);

	if (idx < HIL_SOURCE_MAX_STREAMS) {
		/* Do not hold the backend lock while invoking the app sent
		 * callback; the app has its own mutex for TX bookkeeping. */
		hil_source_app_tx_sent(idx);
	}
}

static struct bt_bap_stream_ops stream_ops = {
	.configured = stream_configured,
	.qos_set = stream_qos_set,
	.enabled = stream_enabled,
	.connected = stream_connected_cb,
	.started = stream_started,
	.disabled = stream_disabled,
	.stopped = stream_stopped,
	.disconnected = stream_disconnected_cb,
	.released = stream_released,
	.sent = stream_sent,
};

/* ── ops implementations ─────────────────────────────────────────── */

static int bap_get_identity(bt_addr_le_t *out)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);

	if (out == NULL) {
		return -EINVAL;
	}
	bt_id_get(addrs, &count);
	if (count == 0U) {
		return -ENODEV;
	}
	*out = addrs[0];
	return 0;
}

struct bond_counter {
	uint32_t count;
};

static void count_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bond_counter *c = user_data;

	ARG_UNUSED(info);
	c->count++;
}

static int bap_bond_count(void)
{
	struct bond_counter c = {0};

	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &c);
	return (int)c.count;
}

static int bap_unpair(const bt_addr_le_t *peer)
{
	if (peer == NULL) {
		return -EINVAL;
	}
	/* bt_unpair with a specific address always reports success for a
	 * missing key (no-key is success); a NULL address is forbidden. */
	return bt_unpair(BT_ID_DEFAULT, peer);
}

struct bond_match {
	const bt_addr_le_t *peer;
	bool found;
};

static void match_bond(const struct bt_bond_info *info, void *user_data)
{
	struct bond_match *m = user_data;

	if (bt_addr_le_eq(&info->addr, m->peer)) {
		m->found = true;
	}
}

/* Exact configured-peer bond check (address AND type), never a broad
 * bond-count substitute.  On a fresh SC pairing the key is installed
 * before the successful security_changed; on a bonded reconnect the keys
 * were restored by settings_load(). */
static bool bap_peer_bonded(const bt_addr_le_t *peer)
{
	struct bond_match m = {.peer = peer, .found = false};

	if (peer == NULL) {
		return false;
	}
	bt_foreach_bond(BT_ID_DEFAULT, match_bond, &m);
	return m.found;
}

static void bap_set_run_shape(enum hil_source_mode mode, enum hil_source_profile profile)
{
	run_mode = mode;
	run_profile = profile;
	run_stream_count = hil_source_mode_stream_count(mode);
	bap_build_presets(mode, profile);
}

static int bap_kick_connect(const bt_addr_le_t *addr)
{
	if (addr == NULL) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (default_conn != NULL) {
		k_spin_unlock(&backend_lock, key);
		return -EINVAL;
	}
	op_error = 0;
	k_spin_unlock(&backend_lock, key);
	return bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_BAP_CONN_PARAM_RELAXED,
				 &default_conn);
}

static int bap_kick_security(void)
{
	struct bt_conn *ref;
	int err;

	/* Temporary connection reference taken under the lock so the
	 * disconnect callback cannot unref the connection while the API runs
	 * after the unlock. */
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ref = default_conn;
	op_error = 0;
	if (ref != NULL) {
		bt_conn_ref(ref);
	}
	k_spin_unlock(&backend_lock, key);
	if (ref == NULL) {
		return -EINVAL;
	}
	err = bt_conn_set_security(ref, BT_SECURITY_L2);
	if (err == 0) {
		bt_security_t level = bt_conn_get_security(ref);

		if (level >= BT_SECURITY_L2) {
			/* Already-secure bonded reconnect: no security_changed
			 * callback will fire.  Update the status gate exactly
			 * as the callback would, then give the wait token
			 * (duplicate gives are harmless, semaphore max one). */
			k_spinlock_key_t key2 = k_spin_lock(&backend_lock);

			security_level_now = level;
			security_error_now = 0;
			k_spin_unlock(&backend_lock, key2);
			k_sem_give(&sem_security);
		}
	}
	bt_conn_unref(ref);
	return err;
}

static int bap_kick_discover(void)
{
	struct bt_conn *ref;
	int err;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ref = default_conn;
	op_error = 0;
	if (ref != NULL) {
		bt_conn_ref(ref);
	}
	k_spin_unlock(&backend_lock, key);
	if (ref == NULL) {
		return -EINVAL;
	}
	err = bt_bap_unicast_client_discover(ref, BT_AUDIO_DIR_SINK);
	bt_conn_unref(ref);
	return err;
}
static int bap_kick_configure(uint8_t stream_count)
{
	struct bt_bap_ep *eps[HIL_SOURCE_MAX_STREAMS];
	struct bt_conn *ref;
	bool missing_ep = false;
	uint8_t i;
	int err;

	if (stream_count == 0U || stream_count > HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ref = default_conn;
	op_error = 0;
	if (ref != NULL) {
		bt_conn_ref(ref);
	}
	for (i = 0U; i < stream_count; i++) {
		/* Copy the endpoint pointers while locked. */
		eps[i] = sink_eps[i];
		if (eps[i] == NULL) {
			missing_ep = true;
			break;
		}
	}
	k_spin_unlock(&backend_lock, key);
	if (missing_ep) {
		if (ref != NULL) {
			bt_conn_unref(ref);
		}
		return -ENODEV;
	}
	if (ref == NULL) {
		return -EINVAL;
	}
	for (i = 0U; i < stream_count; i++) {
		err = bt_bap_stream_config(ref, &bap_streams[i], eps[i], &run_presets[i].codec_cfg);
		if (err != 0) {
			bt_conn_unref(ref);
			return err;
		}
	}
	bt_conn_unref(ref);
	return 0;
}

static int bap_kick_qos(uint8_t stream_count)
{
	struct bt_bap_unicast_group_stream_pair_param pair_params[HIL_SOURCE_MAX_STREAMS];
	struct bt_bap_unicast_group_stream_param stream_params[HIL_SOURCE_MAX_STREAMS];
	struct bt_bap_unicast_group_param param;
	struct bt_bap_unicast_group *group = NULL;
	struct bt_conn *ref;
	bool group_exists;
	uint8_t i;
	int err;

	if (stream_count == 0U || stream_count > HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ref = default_conn;
	op_error = 0;
	if (ref != NULL) {
		bt_conn_ref(ref);
	}
	group_exists = (unicast_group != NULL);
	k_spin_unlock(&backend_lock, key);
	if (group_exists) {
		if (ref != NULL) {
			bt_conn_unref(ref);
		}
		return -EALREADY;
	}
	if (ref == NULL) {
		return -EINVAL;
	}

	memset(pair_params, 0, sizeof(pair_params));
	memset(stream_params, 0, sizeof(stream_params));
	for (i = 0U; i < stream_count; i++) {
		stream_params[i].stream = &bap_streams[i];
		stream_params[i].qos = &run_presets[i].qos;
		pair_params[i].tx_param = &stream_params[i];
	}

	param.params = pair_params;
	param.params_count = stream_count;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;

	err = bt_bap_unicast_group_create(&param, &group);
	if (err != 0) {
		bt_conn_unref(ref);
		return err;
	}
	{
		k_spinlock_key_t key2 = k_spin_lock(&backend_lock);

		unicast_group = group;
		k_spin_unlock(&backend_lock, key2);
	}
	err = bt_bap_stream_qos(ref, group);
	bt_conn_unref(ref);
	return err;
}

static int bap_kick_enable(uint8_t stream_idx)
{
	if (stream_idx >= run_stream_count || stream_idx >= HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	op_error = 0;
	k_spin_unlock(&backend_lock, key);
	return bt_bap_stream_enable(&bap_streams[stream_idx],
				    run_presets[stream_idx].codec_cfg.meta,
				    run_presets[stream_idx].codec_cfg.meta_len);
}

/* bt_bap_stream_connect() owns one CIS connection attempt per call. NCS
 * rejects another pending attempt with -EBUSY while current CIS is CONNECTING
 * or ENCRYPT_PENDING. Coordinator calls this once per stream and waits for
 * matching stream_ops.connected() before kicking next stream. Matching
 * failed-CIS stream_ops.disconnected() completion is retryable; coordinator
 * worker makes one retry of same stream. */
static int bap_kick_stream_connect(uint8_t stream_idx)
{
	int err;

	if (stream_idx >= run_stream_count || stream_idx >= HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	op_error = 0;
	stream_connect_pending_idx = stream_idx;
	stream_connect_completion_idx = UINT8_MAX;
	stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	k_spin_unlock(&backend_lock, key);
	err = bt_bap_stream_connect(&bap_streams[stream_idx]);
	if (err == -EALREADY) {
		/* Preserve already-connected stream completion semantics without
		 * hiding any other synchronous error. */
		key = k_spin_lock(&backend_lock);
		stream_connect_completion_idx = stream_idx;
		stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_CONNECTED;
		stream_connect_pending_idx = UINT8_MAX;
		k_spin_unlock(&backend_lock, key);
		k_sem_give(&sem_stream_connected);
		return 0;
	}
	if (err != 0) {
		key = k_spin_lock(&backend_lock);
		if (stream_connect_pending_idx == stream_idx) {
			stream_connect_pending_idx = UINT8_MAX;
			stream_connect_completion_idx = UINT8_MAX;
			stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
		}
		k_spin_unlock(&backend_lock, key);
	}
	return err;
}

static enum hil_source_stream_connect_outcome bap_stream_connect_outcome(uint8_t stream_idx)
{
	enum hil_source_stream_connect_outcome outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	if (stream_connect_completion_idx == stream_idx) {
		outcome = stream_connect_completion_outcome;
	}
	k_spin_unlock(&backend_lock, key);
	return outcome;
}

/* Sink ASEs are started by the unicast server as soon as the CIS
 * connects; the client never sends bt_bap_stream_start() for a sink.
 * This kick only validates sink readiness; the worker consumes the
 * retained/future stream_ops.started completions.  A missing endpoint or
 * a wrong-direction endpoint is an error, never synthetic success. */
static int bap_kick_start(uint8_t stream_count)
{
	struct bt_bap_ep_info ep_info;
	uint8_t i;

	if (stream_count == 0U || stream_count > HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	op_error = 0;
	k_spin_unlock(&backend_lock, key);
	for (i = 0U; i < stream_count; i++) {
		if (bap_streams[i].ep == NULL) {
			return -EINVAL;
		}
		if (bt_bap_ep_get_info(bap_streams[i].ep, &ep_info) != 0) {
			return -EIO;
		}
		if (ep_info.dir != BT_AUDIO_DIR_SINK) {
			return -EINVAL;
		}
	}
	return 0;
}

static int bap_tx_start(uint8_t stream_count)
{
	return hil_source_tx_attach(bap_streams, stream_count);
}

static int bap_kick_disable(uint8_t stream_idx)
{
	struct bt_bap_ep_info ep_info;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	op_error = 0;
	k_spin_unlock(&backend_lock, key);
	if (stream_idx >= HIL_SOURCE_MAX_STREAMS || bap_streams[stream_idx].ep == NULL) {
		k_sem_give(&sem_disabled);
		return 0;
	}
	if (bt_bap_ep_get_info(bap_streams[stream_idx].ep, &ep_info) != 0) {
		k_sem_give(&sem_disabled);
		return 0;
	}
	if (ep_info.state != BT_BAP_EP_STATE_STREAMING &&
	    ep_info.state != BT_BAP_EP_STATE_ENABLING) {
		/* Not streaming/enabling: nothing to disable. */
		k_sem_give(&sem_disabled);
		return 0;
	}
	return bt_bap_stream_disable(&bap_streams[stream_idx]);
}

static int bap_kick_release(uint8_t stream_idx)
{
	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	op_error = 0;
	k_spin_unlock(&backend_lock, key);
	if (stream_idx >= HIL_SOURCE_MAX_STREAMS) {
		return -EINVAL;
	}
	if (bap_streams[stream_idx].ep == NULL) {
		/* Release-on-IDLE may detach synchronously; nothing to
		 * release, complete immediately. */
		k_sem_give(&sem_released);
		return 0;
	}
	return bt_bap_stream_release(&bap_streams[stream_idx]);
}

static int bap_kick_disconnect(void)
{
	struct bt_conn *ref;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	ref = default_conn;
	if (ref != NULL) {
		bt_conn_ref(ref);
	}
	k_spin_unlock(&backend_lock, key);
	if (ref == NULL) {
		k_sem_give(&sem_disconnected);
		return 0;
	}
	{
		int err = bt_conn_disconnect(ref, BT_HCI_ERR_REMOTE_USER_TERM_CONN);

		bt_conn_unref(ref);
		return err;
	}
}

static bool bap_conn_present(void)
{
	bool present;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	present = (default_conn != NULL);
	k_spin_unlock(&backend_lock, key);
	return present;
}

static bool bap_stream_attached(uint8_t stream_idx)
{
	if (stream_idx >= HIL_SOURCE_MAX_STREAMS) {
		return false;
	}
	return bap_streams[stream_idx].ep != NULL;
}

static int bap_kick_group_delete(void)
{
	struct bt_bap_unicast_group *group;
	int err;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	group = unicast_group;
	k_spin_unlock(&backend_lock, key);
	if (group == NULL) {
		return 0;
	}
	err = bt_bap_unicast_group_delete(group);
	if (err != 0) {
		/* Retain the group pointer on failure so abort cleanup or a
		 * later idle can retry the delete; the group is not reported
		 * absent until a delete actually succeeds. */
		return err;
	}
	key = k_spin_lock(&backend_lock);
	if (unicast_group == group) {
		unicast_group = NULL;
	}
	k_spin_unlock(&backend_lock, key);
	return 0;
}

static void bap_conn_unref(void)
{
	struct bt_conn *conn;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	conn = default_conn;
	default_conn = NULL;
	k_spin_unlock(&backend_lock, key);
	if (conn != NULL) {
		bt_conn_unref(conn);
	}
}

static void bap_reset_segment(void)
{
	uint8_t i;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		sink_eps[i] = NULL;
	}
	op_error = 0;
	security_level_now = 0;
	security_error_now = 0;
	disconnect_reason_now = 0;
	first_ascs_code_now = 0;
	first_ascs_reason_now = 0;
	first_ascs_recorded = false;
	stream_connect_pending_idx = UINT8_MAX;
	stream_connect_completion_idx = UINT8_MAX;
	stream_connect_completion_outcome = HIL_SOURCE_STREAM_CONNECT_OUTCOME_ERROR;
	run_stream_count = 0U;
	k_spin_unlock(&backend_lock, key);
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_security);
	k_sem_reset(&sem_discovered);
	k_sem_reset(&sem_configured);
	k_sem_reset(&sem_qos);
	k_sem_reset(&sem_enabled);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_started);
	k_sem_reset(&sem_disabled);
	k_sem_reset(&sem_released);
	k_sem_reset(&sem_disconnected);
}

static struct k_sem *bap_sem_connected(void)
{
	return &sem_connected;
}

static struct k_sem *bap_sem_security(void)
{
	return &sem_security;
}

static struct k_sem *bap_sem_discovered(void)
{
	return &sem_discovered;
}

static struct k_sem *bap_sem_configured(void)
{
	return &sem_configured;
}

static struct k_sem *bap_sem_qos(void)
{
	return &sem_qos;
}

static struct k_sem *bap_sem_enabled(void)
{
	return &sem_enabled;
}

static struct k_sem *bap_sem_stream_connected(void)
{
	return &sem_stream_connected;
}

static struct k_sem *bap_sem_started(void)
{
	return &sem_started;
}

static struct k_sem *bap_sem_disabled(void)
{
	return &sem_disabled;
}

static struct k_sem *bap_sem_released(void)
{
	return &sem_released;
}

static struct k_sem *bap_sem_disconnected(void)
{
	return &sem_disconnected;
}

static int bap_op_error(void)
{
	int e;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	e = op_error;
	k_spin_unlock(&backend_lock, key);
	return e;
}

static uint8_t bap_security_level(void)
{
	uint8_t level;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	level = security_level_now;
	k_spin_unlock(&backend_lock, key);
	return level;
}

static int bap_security_error(void)
{
	int e;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	e = security_error_now;
	k_spin_unlock(&backend_lock, key);
	return e;
}

static uint8_t bap_discovered_sink_count(void)
{
	uint8_t i;
	uint8_t n = 0U;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		if (sink_eps[i] != NULL) {
			n++;
		}
	}
	k_spin_unlock(&backend_lock, key);
	return n;
}

static bool bap_group_present(void)
{
	bool present;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	present = (unicast_group != NULL);
	k_spin_unlock(&backend_lock, key);
	return present;
}

static uint8_t bap_disconnect_reason(void)
{
	uint8_t reason;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	reason = disconnect_reason_now;
	k_spin_unlock(&backend_lock, key);
	return reason;
}

static uint8_t bap_first_ascs_code(void)
{
	uint8_t code;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	code = first_ascs_code_now;
	k_spin_unlock(&backend_lock, key);
	return code;
}

static uint8_t bap_first_ascs_reason(void)
{
	uint8_t reason;

	k_spinlock_key_t key = k_spin_lock(&backend_lock);
	reason = first_ascs_reason_now;
	k_spin_unlock(&backend_lock, key);
	return reason;
}

static const struct hil_source_backend_ops production_ops = {
	.get_identity = bap_get_identity,
	.bond_count = bap_bond_count,
	.unpair = bap_unpair,
	.peer_bonded = bap_peer_bonded,
	.set_run_shape = bap_set_run_shape,
	.kick_connect = bap_kick_connect,
	.kick_security = bap_kick_security,
	.kick_discover = bap_kick_discover,
	.kick_configure = bap_kick_configure,
	.kick_qos = bap_kick_qos,
	.kick_enable = bap_kick_enable,
	.kick_stream_connect = bap_kick_stream_connect,
	.kick_start = bap_kick_start,
	.tx_start = bap_tx_start,
	.sem_connected = bap_sem_connected,
	.sem_security = bap_sem_security,
	.sem_discovered = bap_sem_discovered,
	.sem_configured = bap_sem_configured,
	.sem_qos = bap_sem_qos,
	.sem_enabled = bap_sem_enabled,
	.sem_stream_connected = bap_sem_stream_connected,
	.stream_connect_outcome = bap_stream_connect_outcome,
	.sem_started = bap_sem_started,
	.op_error = bap_op_error,
	.tx_send = hil_source_tx_send,
	.tx_send_ts = hil_source_tx_send_ts,
	.tx_read_tx_ts = hil_source_tx_read_tx_ts,
	.tx_time_get = hil_source_controller_time_get,
	.tx_read_sync = hil_source_tx_read_sync,
	.tx_stop = hil_source_tx_stop,
	.kick_disable = bap_kick_disable,
	.sem_disabled = bap_sem_disabled,
	.kick_release = bap_kick_release,
	.sem_released = bap_sem_released,
	.kick_disconnect = bap_kick_disconnect,
	.sem_disconnected = bap_sem_disconnected,
	.conn_present = bap_conn_present,
	.stream_attached = bap_stream_attached,
	.kick_group_delete = bap_kick_group_delete,
	.conn_unref = bap_conn_unref,
	.reset_segment = bap_reset_segment,
	.security_level = bap_security_level,
	.security_error = bap_security_error,
	.discovered_sink_count = bap_discovered_sink_count,
	.group_present = bap_group_present,
	.disconnect_reason = bap_disconnect_reason,
	.first_ascs_code = bap_first_ascs_code,
	.first_ascs_reason = bap_first_ascs_reason,
};

const struct hil_source_backend_ops *hil_source_bap_ops(void)
{
	return &production_ops;
}

int hil_source_bap_init(void)
{
	uint8_t i;
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		return err;
	}

	err = settings_load();
	if (err != 0) {
		return err;
	}

	if (!bt_is_ready()) {
		return -EAGAIN;
	}

	/* Both auth registrations return errors in NCS v3.3.0; fail the
	 * init when they cannot be installed.  bt_gatt_cb_register() and
	 * bt_bap_stream_cb_register() are void in the installed SDK. */
	err = bt_conn_auth_cb_register(&conn_auth_cb);
	if (err != 0) {
		return err;
	}
	err = bt_conn_auth_info_cb_register(&conn_auth_info_cb);
	if (err != 0) {
		return err;
	}
	bt_gatt_cb_register(&gatt_callbacks);

	err = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (err != 0) {
		return err;
	}

	for (i = 0U; i < HIL_SOURCE_MAX_STREAMS; i++) {
		bt_bap_stream_cb_register(&bap_streams[i], &stream_ops);
	}

	err = hil_source_tx_init();
	if (err != 0) {
		return err;
	}

	return 0;
}
