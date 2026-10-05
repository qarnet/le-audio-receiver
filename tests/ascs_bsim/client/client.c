/* SPDX-License-Identifier: Apache-2.0 */
/* Only valid connection/group/CIS/TX plumbing is reused. Canonical cases are
 * compiled but unselected. New rejection bytes and state oracle are independent. */
#define test_installers          unselected_canonical_installers
#define test_bsim_client_install unselected_canonical_install
#define streams                  unselected_canonical_streams
#include "../../bsim/client/src/bsim_client_main.c"
#undef test_installers
#undef test_bsim_client_install
#undef streams
#include "lane.h"
#include <stdatomic.h>

#define WIRE_GENERATIONS 8
struct wire_generation;
struct owned_stream {
	struct bt_bap_stream stream;
	struct wire_generation *owner;
	unsigned index;
};

struct wire_generation {
	struct owned_stream owned_streams[2];
	atomic_bool tx_registered[2];
	struct bt_conn *conn;
	uint32_t generation;
	atomic_bool held, closing, retired, subscribed, cp_closed, disconnect_expected;
	atomic_bool discovery_pending, read_pending, write_pending, subscribe_pending;
	atomic_bool raw_active, helper_active, duplicate_response, subscribe_completed;
	atomic_uint procedure_id, procedure_owner;
	uint8_t expected_cp[8];
	size_t expected_cp_length;
	uint8_t expected_opcode, expected_ids[2], expected_count;
	uint8_t subscribe_att_error, read_att_error, write_att_error;
	int io_error;
	uint16_t service_start, service_end, cp_handle, cp_next_handle, cp_ccc_handle;
	uint16_t ase_handles[2], read_handle;
	uint8_t ids[2], read_value[128], cp_value[128], write_value[128];
	size_t ase_count, read_length, cp_length, write_length;
	uint32_t raw_transaction, response_count;
	struct bt_gatt_discover_params service_discovery, characteristic_discovery, ccc_discovery;
	struct bt_gatt_discover_params *pending_discovery;
	struct bt_gatt_read_params read_params;
	struct bt_gatt_write_params write_params;
	struct bt_gatt_subscribe_params subscription;
	struct k_sem discovery_done, read_done, write_done, response_done, subscribe_done,
		cp_closed_done, helper_done;
};
static struct wire_generation contexts[WIRE_GENERATIONS];
static _Atomic(struct wire_generation *) active;
static uint32_t phase, assertions, generation, next_transaction, next_procedure, response_records;
static atomic_uint closing_generation;
static atomic_bool wire_error;
static atomic_uint released_mask;
static K_SEM_DEFINE(released_done, 0, 2);
static struct bt_bap_stream_ops wire_stream_ops;

enum procedure_owner {
	PROCEDURE_NONE,
	PROCEDURE_RAW,
	PROCEDURE_HELPER,
};
struct expected_rsp {
	uint8_t id, code, reason;
};

static struct bt_bap_stream *stream_at(unsigned index)
{
	if (!active || index >= 2) {
		wire_error = true;
		return NULL;
	}
	return &active->owned_streams[index].stream;
}

static void print_raw(const uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		printk("%02x", data[i]);
	}
	printk("\n");
}

static void procedure_marker(const char *phase, struct wire_generation *ctx,
			     enum procedure_owner owner)
{
	printk("ASCS_PROCEDURE_%s procedure=%u generation=%u owner=%s opcode=%u ids=", phase,
	       atomic_load(&ctx->procedure_id), ctx->generation,
	       owner == PROCEDURE_RAW ? "raw" : "helper", ctx->expected_opcode);
	for (unsigned i = 0; i < ctx->expected_count; i++) {
		printk("%s%u", i ? "," : "", ctx->expected_ids[i]);
	}
	printk(" transaction=%u\n", owner == PROCEDURE_RAW ? ctx->raw_transaction : 0);
}

static int procedure_begin(struct wire_generation *ctx, enum procedure_owner owner, uint8_t opcode,
			   const struct expected_rsp *records, unsigned count, bool global)
{
	if (ctx != active || ctx->closing || ctx->retired || wire_error || ctx->raw_active ||
	    ctx->helper_active || count < 1 || count > 2 ||
	    (owner != PROCEDURE_RAW && owner != PROCEDURE_HELPER)) {
		return -EBUSY;
	}
	ctx->expected_opcode = opcode;
	ctx->expected_count = count;
	ctx->expected_cp[0] = opcode;
	ctx->expected_cp[1] = global ? 0xff : count;
	for (unsigned i = 0; i < count; i++) {
		ctx->expected_ids[i] = records[i].id;
		ctx->expected_cp[2 + 3 * i] = records[i].id;
		ctx->expected_cp[3 + 3 * i] = records[i].code;
		ctx->expected_cp[4 + 3 * i] = records[i].reason;
	}
	ctx->expected_cp_length = 2 + 3 * count;
	ctx->response_count = 0;
	ctx->cp_length = 0;
	ctx->duplicate_response = false;
	atomic_store(&ctx->procedure_id, ++next_procedure);
	atomic_store(&ctx->procedure_owner, owner);
	k_sem_reset(owner == PROCEDURE_RAW ? &ctx->response_done : &ctx->helper_done);
	atomic_store(owner == PROCEDURE_RAW ? &ctx->raw_active : &ctx->helper_active, true);
	procedure_marker("BEGIN", ctx, owner);
	return 0;
}

static void procedure_end(struct wire_generation *ctx, enum procedure_owner owner)
{
	if (ctx != active || atomic_load(&ctx->procedure_owner) != owner ||
	    !(owner == PROCEDURE_RAW ? ctx->raw_active : ctx->helper_active) ||
	    ctx->response_count != 1 || wire_error) {
		wire_error = true;
		return;
	}
	atomic_store(owner == PROCEDURE_RAW ? &ctx->raw_active : &ctx->helper_active, false);
	procedure_marker("END", ctx, owner);
	atomic_store(&ctx->procedure_owner, PROCEDURE_NONE);
}

static int helper_begin(uint8_t opcode, uint8_t first, uint8_t second, unsigned count)
{
	struct expected_rsp records[2] = {{first, 0, 0}, {second, 0, 0}};

	return procedure_begin(active, PROCEDURE_HELPER, opcode, records, count, false);
}

static int helper_wait_cp(void)
{
	struct wire_generation *ctx = active;
	if (k_sem_take(&ctx->helper_done, K_SECONDS(5))) {
		ctx->closing = true;
		return -ETIMEDOUT;
	}
	if (!ctx->helper_active || ctx->response_count != 1 || ctx->duplicate_response ||
	    ctx->cp_length != ctx->expected_cp_length ||
	    memcmp(ctx->cp_value, ctx->expected_cp, ctx->expected_cp_length) || wire_error) {
		return -EBADMSG;
	}
	return 0;
}

static int helper_complete(void)
{
	int ret = helper_wait_cp();

	if (ret) {
		return ret;
	}
	procedure_end(active, PROCEDURE_HELPER);
	return wire_error ? -EBADMSG : 0;
}

static bool callback_owned(struct wire_generation *ctx, struct bt_conn *conn, const char *kind)
{
	if (ctx->conn == conn && ctx == active && !ctx->retired && !ctx->closing) {
		return true;
	}
	if (!ctx->closing || ctx->conn != conn || ctx->retired || ctx != active) {
		wire_error = true;
	}
	printk("ASCS_CALLBACK kind=%s generation=%u active=%u closing=%u retired=%u "
	       "conn_match=%u\n",
	       kind, ctx->generation, active ? active->generation : 0, ctx->closing, ctx->retired,
	       ctx->conn == conn);
	return false;
}

static void cancel_pending(struct wire_generation *ctx)
{
	if (ctx->discovery_pending) {
		bt_gatt_cancel(ctx->conn, ctx->pending_discovery);
	}
	if (ctx->read_pending) {
		bt_gatt_cancel(ctx->conn, &ctx->read_params);
	}
	if (ctx->write_pending) {
		bt_gatt_cancel(ctx->conn, &ctx->write_params);
	}
	if (ctx->subscribe_pending) {
		bt_gatt_cancel(ctx->conn, &ctx->subscription);
	}
}

static int retirement_expired(struct wire_generation *ctx, int64_t deadline)
{
	if (k_uptime_get() < deadline) {
		return 0;
	}
	wire_error = true;
	printk("ASCS_CLEANUP generation=%u timeout discovery=%u read=%u write=%u "
	       "subscribe=%u cp=%u\n",
	       ctx->generation, ctx->discovery_pending, ctx->read_pending, ctx->write_pending,
	       ctx->subscribe_pending, ctx->cp_closed);
	return -ETIMEDOUT;
}

static int retire_tx_audits(struct wire_generation *ctx)
{
	for (unsigned index = 0; index < ARRAY_SIZE(ctx->owned_streams); index++) {
		struct bt_bap_stream *stream = &ctx->owned_streams[index].stream;
		struct bsim_tx_result retained;
		int ret;

		if (ctx->tx_registered[index]) {
			ret = bsim_tx_unregister(stream);
			printk("ASCS_TX_AUDIT stage=drain generation=%u index=%u ret=%d\n",
			       ctx->generation, index, ret);
			if (ret) {
				wire_error = true;
				return ret;
			}
			ctx->tx_registered[index] = false;
		}
		ret = bsim_tx_result(stream, &retained);
		if (ret == -ENODATA) {
			int forget = bsim_tx_forget_result(stream);

			printk("ASCS_TX_AUDIT stage=unused generation=%u index=%u result_ret=%d "
			       "forget_ret=%d\n",
			       ctx->generation, index, ret, forget);
			if (forget != -ENODATA) {
				wire_error = true;
				return -EBADMSG;
			}
			continue;
		}
		printk("ASCS_TX_AUDIT stage=retire generation=%u index=%u result_ret=%d "
		       "sends=%u fnv=%08x\n",
		       ctx->generation, index, ret, ret ? 0U : retained.send_count,
		       ret ? 0U : retained.fnv1a_hash);
		if (ret) {
			wire_error = true;
			return ret;
		}
		int forget = bsim_tx_forget_result(stream);

		printk("ASCS_TX_AUDIT stage=forget generation=%u index=%u ret=%d\n",
		       ctx->generation, index, forget);
		if (forget) {
			wire_error = true;
			return forget;
		}
		ret = bsim_tx_result(stream, &retained);
		printk("ASCS_TX_AUDIT stage=forgotten generation=%u index=%u result_ret=%d\n",
		       ctx->generation, index, ret);
		if (ret != -ENODATA) {
			wire_error = true;
			return -EBADMSG;
		}
	}
	return 0;
}

static int retire(struct wire_generation *ctx)
{
	if (!ctx || ctx->retired) {
		return 0;
	}
	if (ctx->raw_active || ctx->helper_active ||
	    atomic_load(&ctx->procedure_owner) != PROCEDURE_NONE) {
		wire_error = true;
		printk("ASCS_PROCEDURE_ERROR generation=%u error=retire-pending\n",
		       ctx->generation);
	}
	int64_t deadline = k_uptime_get() + 5000;
	ctx->closing = true;
	cancel_pending(ctx);
	if (retirement_expired(ctx, deadline)) {
		return -ETIMEDOUT;
	}
	if (ctx->subscribed && !ctx->cp_closed) {
		int ret = bt_gatt_unsubscribe(ctx->conn, &ctx->subscription);
		printk("ASCS_CP_CLOSE generation=%u unsubscribe=%d\n", ctx->generation, ret);
		if (retirement_expired(ctx, deadline)) {
			return -ETIMEDOUT;
		}
		if (ret == -ENOTCONN) {
			ctx->cp_closed = true;
		} else if (ret != 0) {
			wire_error = true;
			return ret;
		}
	}
	while (ctx->discovery_pending || ctx->read_pending || ctx->write_pending ||
	       ctx->subscribe_pending || (ctx->subscribed && !ctx->cp_closed)) {
		if (retirement_expired(ctx, deadline)) {
			return -ETIMEDOUT;
		}
		k_sleep(K_MSEC(1));
	}
	if (retirement_expired(ctx, deadline)) {
		return -ETIMEDOUT;
	}
	/* Retired ownership gates callbacks only after the audit drain
	 * succeeds: publish last, and on drain failure keep closing and the
	 * held connection ref so a later retire can finish the partially
	 * cleaned generation instead of leaving it unguarded mid-cleanup. */
	int audit_ret = retire_tx_audits(ctx);

	if (audit_ret) {
		return audit_ret;
	}
	ctx->retired = true;
	if (ctx->held) {
		bt_conn_unref(ctx->conn);
		ctx->held = false;
	}
	printk("ASCS_CLEANUP generation=%u retired=1 cp_closed=%u\n", ctx->generation,
	       ctx->cp_closed);
	return 0;
}

static void init_context(struct wire_generation *ctx, struct bt_conn *conn, uint32_t number)
{
	ctx->generation = number;
	ctx->conn = bt_conn_ref(conn);
	ctx->held = true;
	for (unsigned i = 0; i < ARRAY_SIZE(ctx->owned_streams); i++) {
		struct owned_stream *owned = &ctx->owned_streams[i];

		owned->owner = ctx;
		owned->index = i;
		bt_bap_stream_cb_register(&owned->stream, &wire_stream_ops);
	}
	k_sem_init(&ctx->discovery_done, 0, 1);
	k_sem_init(&ctx->read_done, 0, 1);
	k_sem_init(&ctx->write_done, 0, 1);
	k_sem_init(&ctx->response_done, 0, 1);
	k_sem_init(&ctx->subscribe_done, 0, 1);
	k_sem_init(&ctx->cp_closed_done, 0, 1);
	k_sem_init(&ctx->helper_done, 0, 1);
}

static uint8_t service_found(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	struct wire_generation *ctx =
		CONTAINER_OF(params, struct wire_generation, service_discovery);
	bool owned = callback_owned(ctx, conn, "service");
	if (!owned || !attr) {
		ctx->discovery_pending = false;
		k_sem_give(&ctx->discovery_done);
		return BT_GATT_ITER_STOP;
	}
	if (attr) {
		const struct bt_gatt_service_val *svc = attr->user_data;
		ctx->service_start = attr->handle + 1;
		ctx->service_end = svc->end_handle;
	}
	ctx->discovery_pending = false;
	k_sem_give(&ctx->discovery_done);
	return BT_GATT_ITER_STOP;
}
static uint8_t characteristic_found(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    struct bt_gatt_discover_params *params)
{
	struct wire_generation *ctx =
		CONTAINER_OF(params, struct wire_generation, characteristic_discovery);
	if (!callback_owned(ctx, conn, "characteristic") || !attr) {
		ctx->discovery_pending = false;
		k_sem_give(&ctx->discovery_done);
		return BT_GATT_ITER_STOP;
	}
	const struct bt_gatt_chrc *chrc = attr->user_data;
	if (ctx->cp_handle && attr->handle > ctx->cp_handle &&
	    (!ctx->cp_next_handle || attr->handle < ctx->cp_next_handle)) {
		ctx->cp_next_handle = attr->handle;
	}
	if (!bt_uuid_cmp(chrc->uuid, BT_UUID_ASCS_ASE_CP)) {
		if (ctx->cp_handle) {
			ctx->io_error = -EEXIST;
			ctx->discovery_pending = false;
			k_sem_give(&ctx->discovery_done);
			return BT_GATT_ITER_STOP;
		}
		ctx->cp_handle = chrc->value_handle;
	}
	if (!bt_uuid_cmp(chrc->uuid, BT_UUID_ASCS_ASE_SNK)) {
		if (ctx->ase_count >= 2) {
			ctx->io_error = -EOVERFLOW;
			ctx->discovery_pending = false;
			k_sem_give(&ctx->discovery_done);
			return BT_GATT_ITER_STOP;
		}
		ctx->ase_handles[ctx->ase_count++] = chrc->value_handle;
	}
	return BT_GATT_ITER_CONTINUE;
}
static uint8_t ccc_found(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 struct bt_gatt_discover_params *params)
{
	struct wire_generation *ctx = CONTAINER_OF(params, struct wire_generation, ccc_discovery);
	if (!callback_owned(ctx, conn, "ccc") || !attr) {
		ctx->discovery_pending = false;
		k_sem_give(&ctx->discovery_done);
		return BT_GATT_ITER_STOP;
	}
	if (bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
		ctx->io_error = -EBADMSG;
	} else if (ctx->cp_ccc_handle) {
		ctx->io_error = -EEXIST;
	} else {
		ctx->cp_ccc_handle = attr->handle;
		printk("ASCS_CCC stage=found generation=%u cp=%u ccc=%u\n", ctx->generation,
		       ctx->cp_handle, ctx->cp_ccc_handle);
		return BT_GATT_ITER_CONTINUE;
	}
	ctx->discovery_pending = false;
	k_sem_give(&ctx->discovery_done);
	return BT_GATT_ITER_STOP;
}
static uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
		       const void *data, uint16_t length)
{
	struct wire_generation *ctx = CONTAINER_OF(params, struct wire_generation, read_params);
	if (!callback_owned(ctx, conn, "read")) {
		ctx->read_pending = false;
		k_sem_give(&ctx->read_done);
		return BT_GATT_ITER_STOP;
	}
	if (err || !data) {
		if (err) {
			printk("ASCS_READ stage=callback generation=%u att=%u\n", ctx->generation,
			       err);
		}
		ctx->read_att_error = err;
		ctx->io_error = err ? -EIO : ctx->io_error;
		ctx->read_pending = false;
		k_sem_give(&ctx->read_done);
		return BT_GATT_ITER_STOP;
	}
	if (length > sizeof(ctx->read_value) - ctx->read_length) {
		ctx->io_error = -EOVERFLOW;
		ctx->read_pending = false;
		k_sem_give(&ctx->read_done);
		return BT_GATT_ITER_STOP;
	}
	memcpy(ctx->read_value + ctx->read_length, data, length);
	ctx->read_length += length;
	return BT_GATT_ITER_CONTINUE;
}
static int read_attribute(uint16_t handle)
{
	struct wire_generation *ctx = active;
	if (ctx->read_pending || ctx->closing || wire_error) {
		return -EBUSY;
	}
	ctx->read_params = (struct bt_gatt_read_params){
		.func = read_cb, .handle_count = 1, .single = {.handle = handle, .offset = 0}};
	ctx->read_handle = handle;
	ctx->read_length = 0;
	ctx->read_att_error = 0;
	ctx->io_error = 0;
	k_sem_reset(&ctx->read_done);
	ctx->read_pending = true;
	int ret = bt_gatt_read(ctx->conn, &ctx->read_params);
	if (ret) {
		ctx->read_pending = false;
		return ret;
	}
	if (k_sem_take(&ctx->read_done, K_SECONDS(5))) {
		ctx->closing = true;
		bt_gatt_cancel(ctx->conn, &ctx->read_params);
		return -ETIMEDOUT;
	}
	if (!ctx->io_error && ctx->read_length >= 2 &&
	    (handle == ctx->ase_handles[0] || handle == ctx->ase_handles[1])) {
		printk("ASCS_READ generation=%u handle=%u id=%u state=%u raw=", ctx->generation,
		       handle, ctx->read_value[0], ctx->read_value[1]);
		print_raw(ctx->read_value, ctx->read_length);
	}
	return ctx->io_error;
}
static int read_ase(unsigned index)
{
	int ret = read_attribute(active->ase_handles[index]);

	return ret ? ret : active->read_length < 2 ? -EBADMSG : 0;
}
static int state(unsigned index, uint8_t expected)
{
	for (unsigned i = 0; i < 500; i++) {
		int ret = read_ase(index);
		if (ret) {
			return ret;
		}
		if (active->read_value[0] != active->ids[index]) {
			return -ESTALE;
		}
		if (active->read_value[1] == expected) {
			return wire_error ? -EBADMSG : 0;
		}
		k_sleep(K_MSEC(10));
	}
	return -ETIMEDOUT;
}
static uint8_t notify(struct bt_conn *conn, struct bt_gatt_subscribe_params *params,
		      const void *data, uint16_t length)
{
	struct wire_generation *ctx = CONTAINER_OF(params, struct wire_generation, subscription);
	if (!data) {
		if (ctx->conn != conn || ctx != active || ctx->retired ||
		    (!ctx->closing && !ctx->disconnect_expected)) {
			wire_error = true;
		}
		printk("ASCS_CP_CLOSE generation=%u notify=NULL closing=%u\n", ctx->generation,
		       ctx->closing);
		ctx->cp_closed = true;
		ctx->subscribed = false;
		k_sem_give(&ctx->cp_closed_done);
		params->value_handle = 0;
		return BT_GATT_ITER_STOP;
	}
	if (!callback_owned(ctx, conn, "cp")) {
		wire_error = true;
		printk("ASCS_CP_UNOWNED generation=%u error=stale-context raw=", ctx->generation);
		print_raw(data, length);
		return BT_GATT_ITER_STOP;
	}
	const uint8_t *raw = data;
	bool owned_raw = ctx->raw_active;
	bool owned_helper = ctx->helper_active;
	if (owned_raw == owned_helper ||
	    atomic_load(&ctx->procedure_owner) != (owned_raw ? PROCEDURE_RAW : PROCEDURE_HELPER)) {
		wire_error = true;
		printk("ASCS_CP_UNOWNED generation=%u error=unsolicited raw=", ctx->generation);
		print_raw(raw, length);
		return BT_GATT_ITER_CONTINUE;
	}
	printk("ASCS_CP transaction=%u generation=%u procedure=%u owner=%s raw=",
	       owned_raw ? ctx->raw_transaction : 0, ctx->generation,
	       atomic_load(&ctx->procedure_id), owned_raw ? "raw" : "helper");
	print_raw(raw, length);
	ctx->response_count++;
	if (ctx->response_count != 1) {
		ctx->duplicate_response = true;
		wire_error = true;
	}
	if (length > sizeof(ctx->cp_value) || length > sizeof(ctx->expected_cp) ||
	    length != ctx->expected_cp_length || memcmp(raw, ctx->expected_cp, length)) {
		ctx->io_error = -EBADMSG;
		wire_error = true;
	} else {
		memcpy(ctx->cp_value, raw, length);
		ctx->cp_length = length;
	}
	if (owned_raw) {
		k_sem_give(&ctx->response_done);
	} else {
		k_sem_give(&ctx->helper_done);
	}
	return BT_GATT_ITER_CONTINUE;
}
static void subscribed(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	struct wire_generation *ctx = CONTAINER_OF(params, struct wire_generation, subscription);
	bool owned = callback_owned(ctx, conn, "subscribe");
	ctx->subscribe_att_error = err;
	ctx->subscribe_completed = true;
	ctx->subscribe_pending = false;
	printk("ASCS_SUBSCRIBE stage=callback generation=%u cp=%u ccc=%u att=%u\n", ctx->generation,
	       ctx->cp_handle, ctx->cp_ccc_handle, err);
	if (owned && err) {
		wire_error = true;
	}
	k_sem_give(&ctx->subscribe_done);
}
static void wrote(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params)
{
	struct wire_generation *ctx = CONTAINER_OF(params, struct wire_generation, write_params);
	bool owned = callback_owned(ctx, conn, "write");
	ctx->write_att_error = err;
	ctx->write_pending = false;
	printk("ASCS_WRITE transaction=%u generation=%u att_error=%u ret=0\n", ctx->raw_transaction,
	       ctx->generation, err);
	if (owned && err) {
		ctx->io_error = -EIO;
	}
	k_sem_give(&ctx->write_done);
}

#define service_start       (active->service_start)
#define service_end         (active->service_end)
#define cp_handle           (active->cp_handle)
#define cp_next_handle      (active->cp_next_handle)
#define cp_ccc_handle       (active->cp_ccc_handle)
#define ase_handles         (active->ase_handles)
#define ase_count           (active->ase_count)
#define ids                 (active->ids)
#define read_value          (active->read_value)
#define read_length         (active->read_length)
#define io_error            (active->io_error)
#define cp_subscription     (active->subscription)
#define discovery_done      (active->discovery_done)
#define read_done           (active->read_done)
#define write_done          (active->write_done)
#define response_done       (active->response_done)
#define subscribe_done      (active->subscribe_done)
#define cp_value            (active->cp_value)
#define cp_length           (active->cp_length)
#define raw_active          (active->raw_active)
#define duplicate_response  (active->duplicate_response)
#define subscribe_completed (active->subscribe_completed)
#define subscribe_att_error (active->subscribe_att_error)
static int discover(struct bt_gatt_discover_params *params)
{
	if (active->discovery_pending || active->closing || wire_error) {
		return -EBUSY;
	}
	k_sem_reset(&discovery_done);
	active->pending_discovery = params;
	active->discovery_pending = true;
	int ret = bt_gatt_discover(active->conn, params);
	if (ret) {
		active->discovery_pending = false;
		return ret;
	}
	if (k_sem_take(&discovery_done, K_SECONDS(5))) {
		active->closing = true;
		bt_gatt_cancel(active->conn, params);
		return -ETIMEDOUT;
	}
	return io_error;
}
static int discover_wire(void)
{
	active->service_discovery =
		(struct bt_gatt_discover_params){.uuid = BT_UUID_ASCS,
						 .func = service_found,
						 .start_handle = 1,
						 .end_handle = 0xffff,
						 .type = BT_GATT_DISCOVER_PRIMARY};
	service_start = service_end = cp_handle = cp_next_handle = cp_ccc_handle = 0;
	ase_count = 0;
	io_error = 0;
	printk("ASCS_DISCOVERY stage=service generation=%u mtu=%u\n", generation,
	       bt_gatt_get_mtu(default_conn));
	int ret = discover(&active->service_discovery);
	if (ret) {
		return ret;
	}
	if (!service_end) {
		return -EBADMSG;
	}
	printk("ASCS_DISCOVERY stage=service-found generation=%u start=%u end=%u\n", generation,
	       service_start, service_end);
	active->characteristic_discovery =
		(struct bt_gatt_discover_params){.func = characteristic_found,
						 .start_handle = service_start,
						 .end_handle = service_end,
						 .type = BT_GATT_DISCOVER_CHARACTERISTIC};
	ret = discover(&active->characteristic_discovery);
	if (ret) {
		return ret;
	}
	if (io_error || ase_count != 2 || !cp_handle) {
		return io_error ? io_error : -EBADMSG;
	}
	uint16_t cp_end = cp_next_handle ? cp_next_handle - 1U : service_end;
	if (cp_end <= cp_handle || cp_end > service_end) {
		return -EBADMSG;
	}
	printk("ASCS_DISCOVERY stage=characteristics generation=%u cp=%u end=%u ase0=%u ase1=%u "
	       "mtu=%u\n",
	       generation, cp_handle, cp_end, ase_handles[0], ase_handles[1],
	       bt_gatt_get_mtu(default_conn));
	for (unsigned i = 0; i < 2; i++) {
		ret = read_ase(i);
		if (ret) {
			return ret;
		}
		ids[i] = read_value[0];
		if (!ids[i] || read_value[1]) {
			return -EBADMSG;
		}
	}
	if (ids[0] == ids[1]) {
		return -EBADMSG;
	}
	active->ccc_discovery =
		(struct bt_gatt_discover_params){.uuid = BT_UUID_GATT_CCC,
						 .func = ccc_found,
						 .start_handle = cp_handle + 1U,
						 .end_handle = cp_end,
						 .type = BT_GATT_DISCOVER_DESCRIPTOR};
	io_error = 0;
	printk("ASCS_CCC stage=discover generation=%u cp=%u start=%u end=%u mtu=%u\n", generation,
	       cp_handle, active->ccc_discovery.start_handle, active->ccc_discovery.end_handle,
	       bt_gatt_get_mtu(default_conn));
	ret = discover(&active->ccc_discovery);
	if (ret) {
		printk("ASCS_CCC stage=discover-error generation=%u ret=%d\n", generation, ret);
		return ret;
	}
	if (io_error || !cp_ccc_handle) {
		printk("ASCS_CCC stage=discover-failed generation=%u error=%d ccc=%u\n", generation,
		       io_error, cp_ccc_handle);
		return io_error ? io_error : -EBADMSG;
	}
	ret = read_attribute(cp_ccc_handle);
	if (ret || read_length != 2) {
		printk("ASCS_CCC stage=before-read-error generation=%u ret=%d length=%u\n",
		       generation, ret, (unsigned)read_length);
		return ret ? ret : -EBADMSG;
	}
	uint16_t before = sys_get_le16(read_value);
	printk("ASCS_CCC stage=before generation=%u cp=%u ccc=%u value=%04x\n", generation,
	       cp_handle, cp_ccc_handle, before);
	memset(&cp_subscription, 0, sizeof(cp_subscription));
	cp_subscription.notify = notify;
	cp_subscription.subscribe = subscribed;
	cp_subscription.value_handle = cp_handle;
	cp_subscription.ccc_handle = cp_ccc_handle;
	cp_subscription.value = BT_GATT_CCC_NOTIFY;
	atomic_set_bit(cp_subscription.flags, BT_GATT_SUBSCRIBE_FLAG_VOLATILE);
	k_sem_reset(&subscribe_done);
	subscribe_completed = false;
	subscribe_att_error = 0;
	active->subscribe_pending = !(before & BT_GATT_CCC_NOTIFY);
	ret = bt_gatt_subscribe(active->conn, &cp_subscription);
	printk("ASCS_SUBSCRIBE stage=return generation=%u cp=%u ccc=%u ret=%d before=%04x mtu=%u\n",
	       generation, cp_handle, cp_ccc_handle, ret, before, bt_gatt_get_mtu(default_conn));
	if (ret) {
		active->subscribe_pending = false;
		return ret;
	}
	active->subscribed = true;
	if (!(before & BT_GATT_CCC_NOTIFY) && k_sem_take(&subscribe_done, K_SECONDS(5))) {
		active->closing = true;
		bt_gatt_cancel(active->conn, &cp_subscription);
		printk("ASCS_SUBSCRIBE stage=callback-timeout generation=%u cp=%u ccc=%u\n",
		       generation, cp_handle, cp_ccc_handle);
		return -EIO;
	}
	if (subscribe_completed && subscribe_att_error) {
		return -EIO;
	}
	ret = read_attribute(cp_ccc_handle);
	if (ret || read_length != 2) {
		printk("ASCS_CCC stage=after-read-error generation=%u ret=%d length=%u\n",
		       generation, ret, (unsigned)read_length);
		return ret ? ret : -EBADMSG;
	}
	uint16_t enabled = sys_get_le16(read_value);
	printk("ASCS_CCC stage=after generation=%u cp=%u ccc=%u value=%04x callback=%u att=%u\n",
	       generation, cp_handle, cp_ccc_handle, enabled, subscribe_completed,
	       subscribe_att_error);
	if (!(enabled & BT_GATT_CCC_NOTIFY) || (subscribe_completed && subscribe_att_error)) {
		return -EBADMSG;
	}
	printk("ASCS_DISCOVERY generation=%u cp=%u ccc=%u ase0=%u/%u ase1=%u/%u mtu=%u\n",
	       generation, cp_handle, cp_ccc_handle, ids[0], ase_handles[0], ids[1], ase_handles[1],
	       bt_gatt_get_mtu(default_conn));
	return bt_gatt_get_mtu(default_conn) == 65 ? 0 : -EBADMSG;
}
static int exchange(const uint8_t *request, size_t length, const struct expected_rsp *expected,
		    unsigned count, bool global, const char *name)
{
	if (active->write_pending || raw_active || active->helper_active || active->closing ||
	    wire_error || length > sizeof(active->write_value)) {
		return -EBUSY;
	}
	memcpy(active->write_value, request, length);
	active->write_length = length;
	active->write_params = (struct bt_gatt_write_params){.func = wrote,
							     .handle = cp_handle,
							     .offset = 0,
							     .data = active->write_value,
							     .length = length};
	io_error = 0;
	active->write_att_error = 0;
	active->raw_transaction = ++next_transaction;
	int ret = procedure_begin(active, PROCEDURE_RAW, request[0], expected, count, global);

	if (ret) {
		return ret;
	}
	k_sem_reset(&write_done);
	printk("ASCS_REQUEST transaction=%u name=%s generation=%u raw=", active->raw_transaction,
	       name, generation);
	print_raw(active->write_value, length);
	active->write_pending = true;
	ret = bt_gatt_write(active->conn, &active->write_params);
	if (ret) {
		active->write_pending = false;
		printk("ASCS_WRITE transaction=%u generation=%u att_error=0 ret=%d\n",
		       active->raw_transaction, generation, ret);
		return ret;
	}
	if (k_sem_take(&write_done, K_SECONDS(5))) {
		active->closing = true;
		bt_gatt_cancel(active->conn, &active->write_params);
		return -ETIMEDOUT;
	}
	if (active->write_att_error || io_error) {
		return -EIO;
	}
	if (k_sem_take(&response_done, K_SECONDS(5))) {
		active->closing = true;
		return -ETIMEDOUT;
	}
	if (io_error || duplicate_response || active->response_count != 1 || wire_error) {
		return -EBADMSG;
	}
	if (cp_length != 2 + 3 * count || cp_value[0] != request[0] ||
	    cp_value[1] != (global ? 255 : count)) {
		return -EBADMSG;
	}
	for (unsigned i = 0; i < count; i++) {
		if (cp_value[2 + 3 * i] != expected[i].id ||
		    cp_value[3 + 3 * i] != expected[i].code ||
		    cp_value[4 + 3 * i] != expected[i].reason) {
			return -EBADMSG;
		}
	}
	if (wire_error) {
		return -EBADMSG;
	}
	procedure_end(active, PROCEDURE_RAW);
	if (wire_error) {
		return -EBADMSG;
	}
	/* One checked exchange can carry multiple independently checked ASE results. */
	response_records += count;
	assertions++;
	return 0;
}
static int one(const uint8_t *request, size_t length, uint8_t id, uint8_t code, uint8_t reason,
	       const char *name)
{
	struct expected_rsp expected = {id, code, reason};
	return exchange(request, length, &expected, 1, code == 1 || code == 2, name);
}
static int preserve(const uint8_t *request, size_t length, uint8_t id, uint8_t code, uint8_t reason,
		    const char *name)
{
	uint8_t before[2][128];
	size_t sizes[2];
	for (unsigned i = 0; i < 2; i++) {
		int ret = read_ase(i);
		if (ret) {
			return ret;
		}
		sizes[i] = read_length;
		memcpy(before[i], read_value, read_length);
		printk("ASCS_PRESERVE phase=before name=%s transaction=%u generation=%u index=%u "
		       "raw=",
		       name, next_transaction + 1, generation, i);
		print_raw(read_value, read_length);
	}
	int ret = one(request, length, id, code, reason, name);
	if (ret) {
		return ret;
	}
	for (unsigned i = 0; i < 2; i++) {
		ret = read_ase(i);
		if (ret) {
			return ret;
		}
		printk("ASCS_PRESERVE phase=after name=%s transaction=%u generation=%u index=%u "
		       "raw=",
		       name, active->raw_transaction, generation, i);
		print_raw(read_value, read_length);
		if (read_length != sizes[i] || memcmp(before[i], read_value, read_length)) {
			return -EBADMSG;
		}
	}
	return wire_error ? -EBADMSG : 0;
}
static void reset_waits(void)
{
	cfg_rsp_cnt = rel_rsp_cnt = dis_rsp_cnt = 0;
	k_sem_reset(&sem_cfg_rsp);
	k_sem_reset(&sem_rel_rsp);
	k_sem_reset(&sem_dis_rsp);
	k_sem_reset(&sem_stream_configured);
	k_sem_reset(&sem_stream_qos);
	k_sem_reset(&sem_stream_enabled);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_stream_started);
}
static int connect_new(void)
{
	if (active && !active->retired) {
		int cleanup = retire(active);
		if (cleanup) {
			return cleanup;
		}
	}
	if (generation >= WIRE_GENERATIONS || wire_error) {
		return -ENOSPC;
	}
	int ret = scan_and_connect();
	if (ret) {
		return ret;
	}
	struct wire_generation *ctx = &contexts[generation];
	init_context(ctx, default_conn, ++generation);
	active = ctx;
	reset_waits();
	ret = discover_sinks();
	if (ret) {
		return ret;
	}
	return discover_wire();
}
static struct bt_bap_lc3_preset *presets[] = {&preset_48_4_1_fl, &preset_48_4_1_fr};
static int wait_client_enabling(unsigned index);

static int tracked_config_expect(unsigned index, const struct bt_audio_codec_cfg *cfg)
{
	if (index >= 2) {
		return -EINVAL;
	}
	int ret = helper_begin(1, ids[index], 0, 1);

	if (ret) {
		return ret;
	}
	ret = config_expect(stream_at(index), sink_eps[index], cfg, BT_BAP_ASCS_RSP_CODE_SUCCESS,
			    BT_BAP_ASCS_REASON_NONE);
	return ret ? ret : helper_complete();
}

static int create_owned_group(struct bt_bap_lc3_preset **cfg)
{
	struct bt_bap_unicast_group_stream_pair_param pairs[2] = {0};
	struct bt_bap_unicast_group_stream_param streams_param[2] = {0};
	struct bt_bap_unicast_group_param param;

	for (unsigned i = 0; i < 2; i++) {
		streams_param[i].stream = stream_at(i);
		streams_param[i].qos = &cfg[i]->qos;
		pairs[i].tx_param = &streams_param[i];
	}
	param.params = pairs;
	param.params_count = 2;
	param.packing = BT_ISO_PACKING_SEQUENTIAL;
	int ret = bt_bap_unicast_group_create(&param, &unicast_group);

	if (ret) {
		printk("ASCS_GROUP_ERROR generation=%u ret=%d\n", generation, ret);
	}
	return ret;
}

static int tracked_set_stream_qos(void)
{
	int ret = helper_begin(2, ids[0], ids[1], 2);

	if (ret) {
		return ret;
	}
	ret = set_stream_qos(2);
	return ret ? ret : helper_complete();
}

static int tracked_enable_streams(void)
{
	for (unsigned index = 0; index < 2; index++) {
		struct bt_audio_codec_cfg *cfg = &presets[index]->codec_cfg;
		k_sem_reset(&sem_stream_enabled);
		int ret = helper_begin(3, ids[index], 0, 1);

		if (ret) {
			return ret;
		}
		ret = bt_bap_stream_enable(stream_at(index), cfg->meta, cfg->meta_len);
		if (ret) {
			return ret;
		}
		if (k_sem_take(&sem_stream_enabled, K_SECONDS(5))) {
			active->closing = true;
			return -ETIMEDOUT;
		}
		ret = helper_wait_cp();
		if (ret) {
			return ret;
		}
		ret = wait_client_enabling(index);
		if (ret) {
			return ret;
		}
		procedure_end(active, PROCEDURE_HELPER);
		if (wire_error) {
			return -EBADMSG;
		}
	}
	return 0;
}

static int owned_tx_register(unsigned index, const struct tx_param *parameter)
{
	struct bsim_tx_config cfg = {
		.octets_per_frame = parameter->octets_per_frame,
		.freq_hz = parameter->freq_hz,
		.frame_duration_us = parameter->frame_duration_us,
		.chan_count = parameter->chan_count,
		.channel_idx = parameter->channel_idx,
	};

	if (index >= 2) {
		return -EINVAL;
	}
	int ret = bsim_tx_register(stream_at(index), &cfg);

	printk("ASCS_TX_REGISTER generation=%u phase=%u index=%u ret=%d\n", generation, phase,
	       index, ret);
	if (ret == 0) {
		active->tx_registered[index] = true;
	}
	return ret;
}

static int config_only(void)
{
	reset_waits();
	for (unsigned i = 0; i < 2; i++) {
		int ret = tracked_config_expect(i, &presets[i]->codec_cfg);
		if (ret) {
			return ret;
		}
	}
	return 0;
}
static int config_qos(void)
{
	int ret = config_only();
	if (ret) {
		return ret;
	}
	ret = create_owned_group(presets);
	if (ret) {
		return ret;
	}
	return tracked_set_stream_qos();
}
static int wait_client_enabling(unsigned index)
{
	for (unsigned n = 0; n < 500; n++) {
		struct bt_bap_ep_info info;
		int ret = bt_bap_ep_get_info(sink_eps[index], &info);
		if (ret) {
			return ret;
		}
		if (info.id != ids[index] || info.dir != BT_AUDIO_DIR_SINK) {
			return -ESTALE;
		}
		if (info.state == BT_BAP_EP_STATE_ENABLING) {
			return wire_error ? -EBADMSG : 0;
		}
		k_sleep(K_MSEC(10));
	}
	return -ETIMEDOUT;
}
static int complete_stream(bool reverse)
{
	for (unsigned n = 0; n < 2; n++) {
		unsigned i = reverse ? 1 - n : n;
		int ret = wait_client_enabling(i);
		if (ret) {
			return ret;
		}
		ret = bt_bap_stream_connect(stream_at(i));
		if (ret && ret != -EALREADY) {
			return ret;
		}
		if (!ret && k_sem_take(&sem_stream_connected, K_SECONDS(10))) {
			return -ETIMEDOUT;
		}
	}
	for (unsigned i = 0; i < 2; i++) {
		int ret = state(i, 4);
		if (ret) {
			return ret;
		}
	}
	uint32_t value;
	phase++;
	lane_send(ARM, phase, 0);
	int ret = lane_wait(ARMED, phase, &value, 5000);
	if (ret) {
		return ret;
	}
	bsim_tx_set_required_streams(2);
	for (unsigned i = 0; i < 2; i++) {
		struct tx_param tx = {.octets_per_frame = 120,
				      .freq_hz = 48000,
				      .frame_duration_us = 10000,
				      .chan_count = 1,
				      .channel_idx = i};
		ret = owned_tx_register(i, &tx);
		if (ret) {
			return ret;
		}
		ret = bsim_tx_forget_result(stream_at(i));
		printk("ASCS_TX_AUDIT stage=active generation=%u phase=%u index=%u forget_ret=%d\n",
		       generation, phase, i, ret);
		if (ret != -EBUSY) {
			wire_error = true;
			return -EBADMSG;
		}
		bsim_tx_set_send_limit(stream_at(i), 30);
	}
	for (unsigned i = 0; i < 2; i++) {
		ret = bsim_tx_wait_send_limit(stream_at(i), 5000);
		if (ret) {
			return ret;
		}
	}
	lane_send(CHECK, phase, 0);
	ret = lane_wait(RENDERED, phase, &value, 5000);
	if (ret || !value) {
		return ret ? ret : -ENODATA;
	}
	printk("ASCS_RECOVERY phase=%u generation=%u rendered=%u reverse=%u\n", phase, generation,
	       value, reverse);
	for (unsigned i = 0; i < 2; i++) {
		uint32_t sent = bsim_tx_send_count(stream_at(i));
		printk("ASCS_TX generation=%u phase=%u index=%u sends=%u expected=30\n", generation,
		       phase, i, sent);
		if (sent != 30) {
			return -EBADMSG;
		}
		ret = bsim_tx_unregister(stream_at(i));
		printk("ASCS_TX generation=%u phase=%u index=%u unregister=%d\n", generation, phase,
		       i, ret);
		if (ret) {
			return ret;
		}
		active->tx_registered[i] = false;
		struct bsim_tx_result retained;

		ret = bsim_tx_result(stream_at(i), &retained);
		printk("ASCS_TX_AUDIT stage=retained generation=%u phase=%u index=%u ret=%d "
		       "sends=%u fnv=%08x\n",
		       generation, phase, i, ret, ret ? 0U : retained.send_count,
		       ret ? 0U : retained.fnv1a_hash);
		if (ret || retained.send_count != sent) {
			wire_error = true;
			return ret ? ret : -EBADMSG;
		}
	}
	return 0;
}
static int release_all(void)
{
	for (unsigned i = 0; i < 2; i++) {
		int ret = read_ase(i);
		if (ret) {
			return ret;
		}
		if (read_value[1]) {
			uint8_t request[] = {8, 1, ids[i]};
			ret = one(request, sizeof(request), ids[i], 0, 0, "legal-release");
			if (ret) {
				return ret;
			}
			ret = state(i, 0);
			if (ret) {
				return ret;
			}
		}
	}
	if (unicast_group) {
		for (unsigned n = 0; n < 500; n++) {
			int ret = bt_bap_unicast_group_delete(unicast_group);
			if (!ret) {
				unicast_group = NULL;
				return 0;
			}
			if (ret != -EBUSY) {
				return ret;
			}
			k_sleep(K_MSEC(10));
		}
		return -ETIMEDOUT;
	}
	return 0;
}
static int recovery(void)
{
	int ret = release_all();
	if (ret) {
		return ret;
	}
	ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = tracked_enable_streams();
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	if (ret) {
		return ret;
	}
	return release_all();
}

struct control_frame_case {
	const char *name;
	uint8_t bytes[8], length, code;
};
static const struct control_frame_case control_frames[] = {
	{"control_unknown_opcode", {0xff, 1}, 2, 1},
	{"control_zero_ase_count", {3, 0}, 2, 2},
	{"control_missing_codec_record", {1, 1}, 2, 2},
	{"control_metadata_outer_truncated", {3, 1, 0, 3, 2, 0xf0}, 6, 2},
	{"control_release_trailing", {8, 1, 0, 0}, 4, 2},
	{"control_count_above_exposure", {8, 3, 0, 0, 0}, 5, 2},
	{"control_missing_count", {5}, 1, 2},
};
static unsigned control_steps;

static int validate_control_frames(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(control_frames); i++) {
		const struct control_frame_case *test = &control_frames[i];
		uint8_t request[8];
		uint32_t first_phase = phase;
		uint32_t first_assertions = assertions;
		memcpy(request, test->bytes, test->length);
		if (i == 3 || i == 4 || i == 5) {
			request[2] = ids[0];
		}
		if (i == 5) {
			request[3] = ids[1];
		}
		printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n",
		       control_steps + 1, test->name, generation, phase);
		if (wire_error) {
			return -EBADMSG;
		}
		int ret = state(0, BT_BAP_EP_STATE_IDLE);
		if (ret) {
			return ret;
		}
		ret = state(1, BT_BAP_EP_STATE_IDLE);
		if (ret) {
			return ret;
		}
		ret = preserve(request, test->length, 0, test->code, 0, test->name);
		if (ret) {
			return ret;
		}
		ret = recovery();
		if (ret) {
			return ret;
		}
		if (wire_error || phase != first_phase + 1 || assertions != first_assertions + 3) {
			return -EBADMSG;
		}
		control_steps++;
		printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u assertions=3 "
		       "preserved=1\n",
		       control_steps, test->name, generation, phase);
	}
	return control_steps == 7 && phase == 7 && assertions == 21 && !wire_error ? 0 : -EBADMSG;
}
struct metadata_case {
	const char *name;
	uint8_t bad[4], bad_len, good[5], good_len, reason;
};
static const struct metadata_case metadata_cases[] = {
	{"metadata_zero_entry", {0x00}, 1, {0x03, 0x02, 0x04, 0x00}, 4, 0x00},
	{"metadata_clear_overrun", {0x04, 0xf0, 0xaa}, 3, {0x02, 0xf0, 0xaa}, 3, 0x00},
	{"metadata_exact_end_value", {0x02, 0xf0}, 2, {0x02, 0xf0, 0xaa}, 3, 0x00},
	{"metadata_exact_end_type", {0x01}, 1, {0x01, 0xf0}, 2, 0x00},
	{"metadata_preferred_context_length",
	 {0x02, 0x01, 0x04},
	 3,
	 {0x03, 0x01, 0x04, 0x00},
	 4,
	 0x01},
	{"metadata_stream_context_length",
	 {0x02, 0x02, 0x04},
	 3,
	 {0x03, 0x02, 0x04, 0x00},
	 4,
	 0x02},
	{"metadata_zero_stream_context",
	 {0x03, 0x02, 0x00, 0x00},
	 4,
	 {0x03, 0x02, 0x04, 0x00},
	 4,
	 0x02},
	{"metadata_language_length",
	 {0x03, 0x04, 0x65, 0x6e},
	 4,
	 {0x04, 0x04, 0x65, 0x6e, 0x67},
	 5,
	 0x04},
	{"metadata_parental_length", {0x01, 0x06}, 2, {0x02, 0x06, 0x01}, 3, 0x06},
	{"metadata_audio_state_length", {0x01, 0x08}, 2, {0x02, 0x08, 0x00}, 3, 0x08},
	{"metadata_broadcast_immediate_length", {0x02, 0x09, 0x01}, 3, {0x01, 0x09}, 2, 0x09},
};
static unsigned metadata_steps;

static int metadata_begin(const char *name)
{
	printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n", metadata_steps + 1,
	       name, generation, phase);
	return wire_error ? -EBADMSG : 0;
}

static int metadata_end(const char *name, uint32_t start_phase, unsigned render_phases)
{
	if (wire_error || phase != start_phase + render_phases) {
		return -EBADMSG;
	}
	metadata_steps++;
	printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u metadata_observed=1 "
	       "preserved=1\n",
	       metadata_steps, name, generation, phase);
	return 0;
}

static int metadata_request(uint8_t opcode, unsigned index, const uint8_t *meta, size_t length,
			    uint8_t code, uint8_t reason, const char *name)
{
	if (length > 16) {
		return -EINVAL;
	}
	uint8_t request[4 + 16] = {opcode, 1, ids[index], (uint8_t)length};
	memcpy(request + 4, meta, length);
	return one(request, 4 + length, ids[index], code, reason, name);
}

static int metadata_qos(uint8_t qos[2][2])
{
	for (unsigned i = 0; i < 2; i++) {
		int ret = state(i, BT_BAP_EP_STATE_QOS_CONFIGURED);
		if (ret) {
			return ret;
		}
		if (read_length < 4 || read_value[0] != ids[i]) {
			return -EBADMSG;
		}
		qos[i][0] = read_value[2];
		qos[i][1] = read_value[3];
	}
	return 0;
}

static int metadata_observe(unsigned index, uint8_t expected_state, const uint8_t *meta,
			    size_t length, const uint8_t qos[2][2])
{
	for (unsigned n = 0; n < 500; n++) {
		int ret = read_ase(index);
		if (ret) {
			return ret;
		}
		if (read_value[0] != ids[index]) {
			return -ESTALE;
		}
		if (read_value[1] == expected_state) {
			if (read_length != 5 + length || read_value[2] != qos[index][0] ||
			    read_value[3] != qos[index][1] || read_value[4] != length ||
			    memcmp(read_value + 5, meta, length)) {
				return -EBADMSG;
			}
			printk("ASCS_METADATA generation=%u index=%u state=%u cig=%u cis=%u raw=",
			       generation, index, expected_state, read_value[2], read_value[3]);
			print_raw(read_value, read_length);
			return wire_error ? -EBADMSG : 0;
		}
		k_sleep(K_MSEC(10));
	}
	return -ETIMEDOUT;
}

static int metadata_enable_b(const uint8_t qos[2][2], const char *name)
{
	const struct bt_audio_codec_cfg *cfg = &presets[1]->codec_cfg;
	int ret = metadata_request(3, 1, cfg->meta, cfg->meta_len, 0, 0, name);

	if (ret) {
		return ret;
	}
	return metadata_observe(1, BT_BAP_EP_STATE_ENABLING, cfg->meta, cfg->meta_len, qos);
}

static int metadata_negative_case(const struct metadata_case *test)
{
	uint8_t qos[2][2];
	uint8_t request[4 + 16] = {3, 1, ids[0], test->bad_len};
	uint32_t first_phase = phase;
	int ret = metadata_begin(test->name);
	if (ret) {
		return ret;
	}
	ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	memcpy(request + 4, test->bad, test->bad_len);
	ret = preserve(request, 4 + test->bad_len, ids[0], 0x0c, test->reason, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_request(3, 0, test->good, test->good_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, test->good, test->good_len, qos);
	if (ret) {
		return ret;
	}
	ret = metadata_enable_b(qos, test->name);
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	if (ret) {
		return ret;
	}
	ret = release_all();
	return ret ? ret : metadata_end(test->name, first_phase, 1);
}

static int metadata_unknown_enabling(void)
{
	const char *name = "metadata_unknown_enable_update_enabling";
	const uint8_t initial[] = {2, 0xf0, 0xaa};
	const uint8_t updated[] = {2, 0xf0, 0xbb};
	uint8_t qos[2][2];
	uint8_t request[] = {7, 1, ids[0], 1, 0};
	uint32_t first_phase = phase;
	int ret = metadata_begin(name);
	if (ret) {
		return ret;
	}
	ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	ret = metadata_request(3, 0, initial, sizeof(initial), 0, 0, name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, initial, sizeof(initial), qos);
	if (ret) {
		return ret;
	}
	ret = metadata_request(7, 0, updated, sizeof(updated), 0, 0, name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, updated, sizeof(updated), qos);
	if (ret) {
		return ret;
	}
	ret = preserve(request, sizeof(request), ids[0], 0x0c, 0, name);
	if (ret) {
		return ret;
	}
	ret = metadata_enable_b(qos, name);
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	if (ret) {
		return ret;
	}
	ret = release_all();
	return ret ? ret : metadata_end(name, first_phase, 1);
}

static int metadata_update_streaming(void)
{
	const char *name = "metadata_update_streaming";
	const uint8_t updated[] = {2, 0xf0, 0xbb};
	uint8_t qos[2][2];
	uint8_t request[] = {7, 1, ids[0], 3, 4, 0xf0, 0xaa};
	uint32_t first_phase = phase;
	int ret = metadata_begin(name);
	if (ret) {
		return ret;
	}
	ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	ret = tracked_enable_streams();
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	if (ret) {
		return ret;
	}
	ret = metadata_request(7, 0, updated, sizeof(updated), 0, 0, name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_STREAMING, updated, sizeof(updated), qos);
	if (ret) {
		return ret;
	}
	ret = preserve(request, sizeof(request), ids[0], 0x0c, 0, name);
	if (ret) {
		return ret;
	}
	ret = release_all();
	if (ret) {
		return ret;
	}
	ret = recovery();
	return ret ? ret : metadata_end(name, first_phase, 2);
}

static int validate_metadata_family(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(metadata_cases); i++) {
		int ret = metadata_negative_case(&metadata_cases[i]);
		if (ret) {
			return ret;
		}
	}
	int ret = metadata_unknown_enabling();
	if (ret) {
		return ret;
	}
	ret = metadata_update_streaming();
	if (ret) {
		return ret;
	}
	return metadata_steps == 13 && phase == 14 && !wire_error ? 0 : -EBADMSG;
}

enum codec_qos_kind {
	CODEC_VALID,
	CODEC_OCTETS,
	CODEC_ZERO_LTV,
	CODEC_FREQUENCY_LENGTH,
	QOS_INTERVAL,
	QOS_FRAMING,
	QOS_PHY,
	QOS_VALID,
};
struct codec_qos_case {
	const char *name;
	enum codec_qos_kind kind;
	uint8_t value, reason;
};
static const struct codec_qos_case codec_qos_cases[] = {
	{"codec_octets_19", CODEC_OCTETS, 19, 2},
	{"codec_octets_121", CODEC_OCTETS, 121, 2},
	{"codec_zero_ltv", CODEC_ZERO_LTV, 0, 2},
	{"codec_frequency_length", CODEC_FREQUENCY_LENGTH, 0, 2},
	{"qos_interval_254", QOS_INTERVAL, 254, 3},
	{"qos_framing_2", QOS_FRAMING, 2, 4},
	{"qos_phy_80", QOS_PHY, 0x80, 5},
};
static unsigned codec_qos_steps;

static int codec_config_request(const struct codec_qos_case *test, uint8_t request[43],
				size_t *request_len)
{
	const struct bt_audio_codec_cfg *cfg = &presets[0]->codec_cfg;
	if (cfg->data_len == 0 || cfg->data_len > 32) {
		return -EINVAL;
	}
	request[0] = 1;
	request[1] = 1;
	request[2] = ids[0];
	request[3] = cfg->target_latency;
	request[4] = cfg->target_phy;
	request[5] = cfg->id;
	sys_put_le16(cfg->cid, request + 6);
	sys_put_le16(cfg->vid, request + 8);
	request[10] = (uint8_t)cfg->data_len;
	memcpy(request + 11, cfg->data, cfg->data_len);
	size_t octets = SIZE_MAX;
	size_t frequency = SIZE_MAX;
	for (size_t pos = 0; pos < cfg->data_len;) {
		uint8_t length = cfg->data[pos];
		if (length < 1 || (size_t)length >= cfg->data_len - pos) {
			return -EBADMSG;
		}
		if (cfg->data[pos + 1] == 4) {
			if (octets != SIZE_MAX || length != 3) {
				return -EBADMSG;
			}
			octets = pos;
		}
		if (cfg->data[pos + 1] == 1) {
			if (frequency != SIZE_MAX || length != 2) {
				return -EBADMSG;
			}
			frequency = pos;
		}
		pos += 1U + length;
	}
	if (octets == SIZE_MAX || frequency != 0) {
		return -EBADMSG;
	}
	*request_len = 11 + cfg->data_len;
	if (test->kind == CODEC_OCTETS) {
		sys_put_le16(test->value, request + 11 + octets + 2);
	} else if (test->kind == CODEC_ZERO_LTV) {
		request[11] = 0;
	} else if (test->kind == CODEC_FREQUENCY_LENGTH) {
		if (cfg->data_len >= 32 || cfg->data[frequency + 2] != 8) {
			return -EBADMSG;
		}
		size_t data_end = 11 + cfg->data_len;
		size_t insert = 11 + frequency + 3;
		memmove(request + insert + 1, request + insert, data_end - insert);
		request[11 + frequency] = 3;
		request[insert] = 0;
		request[10]++;
		(*request_len)++;
	} else if (test->kind != CODEC_VALID) {
		return -EINVAL;
	}
	return *request_len <= sizeof(active->write_value) &&
			       *request_len <= bt_gatt_get_mtu(active->conn) - 3U
		       ? 0
		       : -EMSGSIZE;
}

static int qos_request(const struct codec_qos_case *test, uint8_t request[18])
{
	int ret = read_ase(0);
	if (ret) {
		return ret;
	}
	if (read_length != 17 || read_value[0] != ids[0] ||
	    read_value[1] != BT_BAP_EP_STATE_QOS_CONFIGURED) {
		return -EBADMSG;
	}
	request[0] = 2;
	request[1] = 1;
	request[2] = ids[0];
	memcpy(request + 3, read_value + 2, 15);
	if (test->kind == QOS_INTERVAL) {
		request[5] = test->value;
		request[6] = request[7] = 0;
	} else if (test->kind == QOS_FRAMING) {
		request[8] = test->value;
	} else if (test->kind == QOS_PHY) {
		request[9] = test->value;
	} else if (test->kind != QOS_VALID) {
		return -EINVAL;
	}
	return sizeof(uint8_t[18]) <= bt_gatt_get_mtu(active->conn) - 3U ? 0 : -EMSGSIZE;
}

static int codec_qos_case_run(const struct codec_qos_case *test)
{
	uint8_t request[43] = {0};
	size_t request_len;
	uint32_t first_phase = phase;
	unsigned first_assertions = assertions;
	printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n", codec_qos_steps + 1,
	       test->name, generation, phase);
	if (wire_error) {
		return -EBADMSG;
	}
	int ret;
	if (test->kind <= CODEC_FREQUENCY_LENGTH) {
		ret = codec_config_request(test, request, &request_len);
		if (ret) {
			return ret;
		}
		ret = preserve(request, request_len, ids[0], 8, test->reason, test->name);
		if (ret) {
			return ret;
		}
	} else {
		ret = config_qos();
		if (ret) {
			return ret;
		}
		ret = state(0, BT_BAP_EP_STATE_QOS_CONFIGURED);
		if (ret) {
			return ret;
		}
		ret = state(1, BT_BAP_EP_STATE_QOS_CONFIGURED);
		if (ret) {
			return ret;
		}
		ret = qos_request(test, request);
		if (ret) {
			return ret;
		}
		ret = preserve(request, 18, ids[0], 9, test->reason, test->name);
		if (ret) {
			return ret;
		}
		/* Recovery releases both QoS ASEs before fresh codec setup. */
	}
	ret = recovery();
	if (ret) {
		return ret;
	}
	unsigned expected = test->kind <= CODEC_FREQUENCY_LENGTH ? 3U : 5U;
	if (wire_error || phase != first_phase + 1 || assertions != first_assertions + expected) {
		return -EBADMSG;
	}
	codec_qos_steps++;
	printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u assertions=%u "
	       "preserved=1\n",
	       codec_qos_steps, test->name, generation, phase, assertions - first_assertions);
	return 0;
}

static int validate_codec_qos(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(codec_qos_cases); i++) {
		int ret = codec_qos_case_run(&codec_qos_cases[i]);
		if (ret) {
			return ret;
		}
	}
	return codec_qos_steps == 7 && phase == 7 && assertions == 27 && !wire_error ? 0 : -EBADMSG;
}

enum lifecycle_setup {
	LIFECYCLE_IDLE,
	LIFECYCLE_CODEC,
	LIFECYCLE_QOS,
	LIFECYCLE_ENABLING,
	LIFECYCLE_STREAMING,
};
struct lifecycle_case {
	const char *name;
	enum lifecycle_setup setup;
	uint8_t opcode, rejected_code, legal_opcode;
};
static const struct lifecycle_case lifecycle_cases[] = {
	{"idle_enable_reject", LIFECYCLE_IDLE, 3, 4, 0},
	{"idle_qos_reject", LIFECYCLE_IDLE, 2, 4, 0},
	{"idle_disable_reject", LIFECYCLE_IDLE, 5, 4, 0},
	{"idle_release_reject", LIFECYCLE_IDLE, 8, 4, 0},
	{"codec_enable_reject", LIFECYCLE_CODEC, 3, 4, 0},
	{"qos_disable_reject", LIFECYCLE_QOS, 5, 4, 0},
	{"enabling_config_reject", LIFECYCLE_ENABLING, 1, 4, 0},
	{"enabling_qos_reject", LIFECYCLE_ENABLING, 2, 4, 0},
	{"streaming_config_reject", LIFECYCLE_STREAMING, 1, 4, 0},
	{"streaming_enable_reject", LIFECYCLE_STREAMING, 3, 4, 0},
	{"sink_start_ready_direction_reject", LIFECYCLE_QOS, 4, 5, 0},
	{"sink_stop_ready_direction_reject", LIFECYCLE_QOS, 6, 5, 0},
	{"disable_from_enabling", LIFECYCLE_ENABLING, 5, 4, 5},
	{"disable_from_streaming", LIFECYCLE_STREAMING, 5, 4, 5},
	{"release_from_codec", LIFECYCLE_CODEC, 8, 4, 8},
	{"release_from_qos", LIFECYCLE_QOS, 8, 4, 8},
	{"release_from_enabling", LIFECYCLE_ENABLING, 8, 4, 8},
	{"release_from_streaming", LIFECYCLE_STREAMING, 8, 4, 8},
	{"invalid_ase_id", LIFECYCLE_IDLE, 8, 3, 0},
};
static unsigned lifecycle_steps;

static int lifecycle_request(const struct lifecycle_case *test, uint8_t request[43], size_t *length)
{
	static const struct codec_qos_case valid_codec = {.kind = CODEC_VALID};
	static const struct codec_qos_case valid_qos = {.kind = QOS_VALID};
	if (test->opcode == 1) {
		return codec_config_request(&valid_codec, request, length);
	}
	if (test->opcode == 2) {
		if (test->setup != LIFECYCLE_IDLE) {
			int ret = qos_request(&valid_qos, request);
			*length = 18;
			return ret;
		}
		/* Idle has no observed QoS; use the fixed legal profile. */
		const uint8_t idle_qos[] = {2, 1,   0, 0, 0,  0x10, 0x27, 0,    0,
					    2, 120, 0, 5, 20, 0,    0x40, 0x9c, 0};
		memcpy(request, idle_qos, sizeof(idle_qos));
		request[2] = ids[0];
		*length = sizeof(idle_qos);
		return *length <= bt_gatt_get_mtu(active->conn) - 3U ? 0 : -EMSGSIZE;
	}
	if (test->opcode == 3) {
		const struct bt_audio_codec_cfg *cfg = &presets[0]->codec_cfg;
		if (cfg->meta_len > 16) {
			return -EMSGSIZE;
		}
		request[0] = 3;
		request[1] = 1;
		request[2] = ids[0];
		request[3] = (uint8_t)cfg->meta_len;
		memcpy(request + 4, cfg->meta, cfg->meta_len);
		*length = 4 + cfg->meta_len;
		return *length <= bt_gatt_get_mtu(active->conn) - 3U ? 0 : -EMSGSIZE;
	}
	if (test->opcode == 4 || test->opcode == 5 || test->opcode == 6 || test->opcode == 8) {
		request[0] = test->opcode;
		request[1] = 1;
		request[2] = !strcmp(test->name, "invalid_ase_id") ? 0 : ids[0];
		*length = 3;
		return 0;
	}
	return -EINVAL;
}

static int lifecycle_setup_case(const struct lifecycle_case *test, uint8_t qos_request_a[18])
{
	int ret;
	if (test->setup == LIFECYCLE_IDLE) {
		ret = state(0, 0);
		return ret ? ret : state(1, 0);
	}
	if (test->setup == LIFECYCLE_CODEC) {
		ret = config_only();
		if (ret) {
			return ret;
		}
		ret = state(0, 1);
		return ret ? ret : state(1, 1);
	}
	ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = state(0, 2);
	if (ret) {
		return ret;
	}
	ret = state(1, 2);
	if (ret || test->setup == LIFECYCLE_QOS) {
		return ret;
	}
	uint8_t qos[2][2];
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	if (test->opcode == 2) {
		static const struct codec_qos_case valid_qos = {.kind = QOS_VALID};
		ret = qos_request(&valid_qos, qos_request_a);
		if (ret) {
			return ret;
		}
	}
	if (test->setup == LIFECYCLE_STREAMING) {
		ret = tracked_enable_streams();
		return ret ? ret : complete_stream(false);
	}
	const struct bt_audio_codec_cfg *cfg = &presets[0]->codec_cfg;
	ret = metadata_request(3, 0, cfg->meta, cfg->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, cfg->meta, cfg->meta_len, qos);
	return ret ? ret : state(1, 2);
}

static int lifecycle_case_run(const struct lifecycle_case *test)
{
	uint32_t first_phase = phase;
	unsigned first_assertions = assertions;
	uint8_t qos_request_a[18] = {0};
	uint8_t request[43] = {0};
	size_t length = 0;
	printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n", lifecycle_steps + 1,
	       test->name, generation, phase);
	if (wire_error) {
		return -EBADMSG;
	}
	int ret = lifecycle_setup_case(test, qos_request_a);
	if (ret) {
		return ret;
	}
	if (test->opcode == 2 && test->setup == LIFECYCLE_ENABLING) {
		memcpy(request, qos_request_a, sizeof(qos_request_a));
		length = sizeof(qos_request_a);
	} else {
		ret = lifecycle_request(test, request, &length);
		if (ret) {
			return ret;
		}
	}
	unsigned extra = test->setup == LIFECYCLE_ENABLING ? 1U : 0U;
	if (test->legal_opcode) {
		uint8_t legal[] = {test->legal_opcode, 1, ids[0]};
		ret = one(legal, sizeof(legal), ids[0], 0, 0, test->name);
		if (ret) {
			return ret;
		}
		extra++;
		ret = state(0, test->legal_opcode == 5 ? 2 : 0);
		if (ret) {
			return ret;
		}
		uint8_t other = test->setup == LIFECYCLE_STREAMING ? 4
				: test->setup == LIFECYCLE_CODEC   ? 1
								   : 2;
		ret = state(1, other);
		if (ret) {
			return ret;
		}
	}
	ret = preserve(request, length, request[2], test->rejected_code, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = recovery();
	if (ret) {
		return ret;
	}
	unsigned initial = test->setup == LIFECYCLE_STREAMING ? 1U : 0U;
	unsigned pre_releases = test->setup == LIFECYCLE_IDLE ? 0U
				: test->legal_opcode == 8     ? 1U
							      : 2U;
	unsigned expected = 1U + extra + pre_releases + 2U;
	if (wire_error || phase != first_phase + initial + 1 ||
	    assertions != first_assertions + expected) {
		return -EBADMSG;
	}
	lifecycle_steps++;
	printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u assertions=%u "
	       "preserved=1\n",
	       lifecycle_steps, test->name, generation, phase, assertions - first_assertions);
	return 0;
}

static int validate_lifecycle(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(lifecycle_cases); i++) {
		int ret = lifecycle_case_run(&lifecycle_cases[i]);
		if (ret) {
			return ret;
		}
	}
	return lifecycle_steps == 19 && phase == 23 && assertions == 91 && !wire_error ? 0
										       : -EBADMSG;
}

enum dual_kind {
	DUAL_ENABLE,
	DUAL_QOS,
	DUAL_DISABLE,
	DUAL_RELEASE
};
struct dual_case {
	const char *name;
	enum dual_kind kind;
	bool reverse_order, reverse_cis;
};
static const struct dual_case dual_cases[] = {
	{"dual_enable_order_forward_cis_forward", DUAL_ENABLE, false, false},
	{"dual_enable_order_forward_cis_reverse", DUAL_ENABLE, false, true},
	{"dual_enable_order_reverse_cis_forward", DUAL_ENABLE, true, false},
	{"dual_enable_order_reverse_cis_reverse", DUAL_ENABLE, true, true},
	{"dual_qos_order_forward", DUAL_QOS, false, false},
	{"dual_qos_order_reverse", DUAL_QOS, true, false},
	{"dual_disable_order_forward", DUAL_DISABLE, false, false},
	{"dual_disable_order_reverse", DUAL_DISABLE, true, false},
	{"dual_release_order_forward", DUAL_RELEASE, false, false},
	{"dual_release_order_reverse", DUAL_RELEASE, true, false},
};
static unsigned dual_steps;

static int dual_read(unsigned index, uint8_t snapshot[128], size_t *length, const char *phase,
		     const char *name)
{
	int ret = read_ase(index);
	if (ret) {
		return ret;
	}
	*length = read_length;
	memcpy(snapshot, read_value, read_length);
	printk("ASCS_DUAL_READ phase=%s name=%s generation=%u index=%u state=%u raw=", phase, name,
	       generation, index, read_value[1]);
	print_raw(read_value, read_length);
	return 0;
}

static int dual_unchanged(unsigned index, const uint8_t snapshot[128], size_t length,
			  const char *name)
{
	int ret = read_ase(index);
	if (ret) {
		return ret;
	}
	printk("ASCS_DUAL_READ phase=after name=%s generation=%u index=%u state=%u raw=", name,
	       generation, index, read_value[1]);
	print_raw(read_value, read_length);
	return read_length == length && !memcmp(read_value, snapshot, length) && !wire_error
		       ? 0
		       : -EBADMSG;
}

static int dual_batch(const struct dual_case *test, const uint8_t qos_record[2][16])
{
	uint8_t request[64] = {0};
	struct expected_rsp expected[2];
	unsigned order[2] = {test->reverse_order ? 1U : 0U, test->reverse_order ? 0U : 1U};
	request[0] = test->kind == DUAL_ENABLE    ? 3
		     : test->kind == DUAL_QOS     ? 2
		     : test->kind == DUAL_DISABLE ? 5
						  : 8;
	request[1] = 2;
	size_t size = 2;
	for (unsigned n = 0; n < 2; n++) {
		unsigned index = order[n];
		expected[n] = (struct expected_rsp){
			.id = ids[index],
			.code = index == 0                  ? 0
				: test->kind == DUAL_ENABLE ? 0x0c
				: test->kind == DUAL_QOS    ? 9
							    : 4,
			.reason = index == 1 && test->kind == DUAL_QOS ? 3 : 0};
		if (test->kind == DUAL_ENABLE) {
			const struct bt_audio_codec_cfg *cfg = &presets[index]->codec_cfg;
			if (cfg->meta_len > 16 || size + 2 + cfg->meta_len > sizeof(request)) {
				return -EMSGSIZE;
			}
			request[size++] = ids[index];
			request[size++] = index == 0 ? (uint8_t)cfg->meta_len : 1;
			if (index == 0) {
				memcpy(request + size, cfg->meta, cfg->meta_len);
				size += cfg->meta_len;
			} else {
				request[size++] = 0;
			}
		} else if (test->kind == DUAL_QOS) {
			if (size + 16 > sizeof(request)) {
				return -EMSGSIZE;
			}
			memcpy(request + size, qos_record[index], 16);
			if (index == 1) {
				request[size + 3] = 0xfe;
				request[size + 4] = request[size + 5] = 0;
			}
			size += 16;
		} else {
			request[size++] = ids[index];
		}
	}
	if (size > bt_gatt_get_mtu(active->conn) - 3U) {
		return -EMSGSIZE;
	}
	return exchange(request, size, expected, 2, false, test->name);
}

static int dual_configured_qos(uint8_t qos[2][2])
{
	int ret = config_qos();
	if (ret) {
		return ret;
	}
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	printk("ASCS_DUAL_QOS generation=%u cig_a=%u cis_a=%u cig_b=%u cis_b=%u\n", generation,
	       qos[0][0], qos[0][1], qos[1][0], qos[1][1]);
	return qos[0][0] == qos[1][0] && qos[0][1] != qos[1][1] ? 0 : -EBADMSG;
}

static int dual_enable_case(const struct dual_case *test)
{
	uint8_t qos[2][2], b_before[128];
	size_t b_length;
	int ret = dual_configured_qos(qos);
	if (ret) {
		return ret;
	}
	ret = dual_read(1, b_before, &b_length, "before", test->name);
	if (ret) {
		return ret;
	}
	ret = dual_batch(test, NULL);
	if (ret) {
		return ret;
	}
	const struct bt_audio_codec_cfg *cfg_a = &presets[0]->codec_cfg;
	const struct bt_audio_codec_cfg *cfg_b = &presets[1]->codec_cfg;
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, cfg_a->meta, cfg_a->meta_len, qos);
	if (ret) {
		return ret;
	}
	ret = dual_unchanged(1, b_before, b_length, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_request(3, 1, cfg_b->meta, cfg_b->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(1, BT_BAP_EP_STATE_ENABLING, cfg_b->meta, cfg_b->meta_len, qos);
	if (ret) {
		return ret;
	}
	ret = complete_stream(test->reverse_cis);
	return ret ? ret : release_all();
}

static int dual_qos_case(const struct dual_case *test)
{
	uint8_t qos[2][2], before[2][128], record[2][16];
	size_t length[2];
	int ret = dual_configured_qos(qos);
	if (ret) {
		return ret;
	}
	for (unsigned i = 0; i < 2; i++) {
		ret = dual_read(i, before[i], &length[i], "before", test->name);
		if (ret) {
			return ret;
		}
		if (length[i] != 17 || before[i][0] != ids[i] || before[i][1] != 2 ||
		    before[i][2] != qos[i][0] || before[i][3] != qos[i][1]) {
			return -EBADMSG;
		}
		record[i][0] = ids[i];
		memcpy(record[i] + 1, before[i] + 2, 15);
	}
	ret = dual_batch(test, record);
	if (ret) {
		return ret;
	}
	for (unsigned i = 0; i < 2; i++) {
		ret = dual_unchanged(i, before[i], length[i], test->name);
		if (ret) {
			return ret;
		}
	}
	uint8_t correction[18] = {2, 1};
	memcpy(correction + 2, record[1], sizeof(record[1]));
	ret = one(correction, sizeof(correction), ids[1], 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = dual_unchanged(1, before[1], length[1], test->name);
	if (ret) {
		return ret;
	}
	ret = tracked_enable_streams();
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	return ret ? ret : release_all();
}

static int dual_disable_case(const struct dual_case *test)
{
	uint8_t qos[2][2], b_before[128];
	size_t b_length;
	int ret = dual_configured_qos(qos);
	if (ret) {
		return ret;
	}
	const struct bt_audio_codec_cfg *cfg_a = &presets[0]->codec_cfg;
	const struct bt_audio_codec_cfg *cfg_b = &presets[1]->codec_cfg;
	ret = metadata_request(3, 0, cfg_a->meta, cfg_a->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, cfg_a->meta, cfg_a->meta_len, qos);
	if (ret) {
		return ret;
	}
	ret = dual_read(1, b_before, &b_length, "before", test->name);
	if (ret) {
		return ret;
	}
	ret = dual_batch(test, NULL);
	if (ret) {
		return ret;
	}
	ret = state(0, BT_BAP_EP_STATE_QOS_CONFIGURED);
	if (ret) {
		return ret;
	}
	ret = dual_unchanged(1, b_before, b_length, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_request(3, 1, cfg_b->meta, cfg_b->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(1, BT_BAP_EP_STATE_ENABLING, cfg_b->meta, cfg_b->meta_len, qos);
	if (ret) {
		return ret;
	}
	uint8_t disable_b[] = {5, 1, ids[1]};
	ret = one(disable_b, sizeof(disable_b), ids[1], 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = dual_unchanged(1, b_before, b_length, test->name);
	if (ret) {
		return ret;
	}
	k_sem_reset(&sem_stream_enabled);
	ret = tracked_enable_streams();
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	return ret ? ret : release_all();
}

static int dual_release_case(const struct dual_case *test)
{
	uint8_t b_before[128];
	size_t b_length;
	reset_waits();
	int ret = tracked_config_expect(0, &presets[0]->codec_cfg);
	if (ret) {
		return ret;
	}
	ret = state(0, BT_BAP_EP_STATE_CODEC_CONFIGURED);
	if (ret) {
		return ret;
	}
	ret = state(1, BT_BAP_EP_STATE_IDLE);
	if (ret) {
		return ret;
	}
	ret = dual_read(1, b_before, &b_length, "before", test->name);
	if (ret) {
		return ret;
	}
	ret = dual_batch(test, NULL);
	if (ret) {
		return ret;
	}
	ret = state(0, BT_BAP_EP_STATE_IDLE);
	if (ret) {
		return ret;
	}
	ret = dual_unchanged(1, b_before, b_length, test->name);
	if (ret) {
		return ret;
	}
	reset_waits();
	ret = tracked_config_expect(1, &presets[1]->codec_cfg);
	if (ret) {
		return ret;
	}
	ret = tracked_config_expect(0, &presets[0]->codec_cfg);
	if (ret) {
		return ret;
	}
	ret = state(1, BT_BAP_EP_STATE_CODEC_CONFIGURED);
	if (ret) {
		return ret;
	}
	ret = state(0, BT_BAP_EP_STATE_CODEC_CONFIGURED);
	if (ret) {
		return ret;
	}
	ret = create_owned_group(presets);
	if (ret) {
		return ret;
	}
	ret = tracked_set_stream_qos();
	if (ret) {
		return ret;
	}
	ret = tracked_enable_streams();
	if (ret) {
		return ret;
	}
	ret = complete_stream(false);
	return ret ? ret : release_all();
}

static int validate_dual(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(dual_cases); i++) {
		const struct dual_case *test = &dual_cases[i];
		uint32_t first_phase = phase;
		uint32_t first_assertions = assertions;
		uint32_t first_records = response_records;
		printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n",
		       dual_steps + 1, test->name, generation, phase);
		if (wire_error) {
			return -EBADMSG;
		}
		int ret = test->kind == DUAL_ENABLE    ? dual_enable_case(test)
			  : test->kind == DUAL_QOS     ? dual_qos_case(test)
			  : test->kind == DUAL_DISABLE ? dual_disable_case(test)
						       : dual_release_case(test);
		if (ret) {
			return ret;
		}
		unsigned exchanges = test->kind == DUAL_DISABLE   ? 6
				     : test->kind == DUAL_RELEASE ? 3
								  : 4;
		if (wire_error || phase != first_phase + 1 ||
		    assertions != first_assertions + exchanges ||
		    response_records != first_records + exchanges + 1) {
			return -EBADMSG;
		}
		dual_steps++;
		printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u exchanges=%u "
		       "records=%u preserved=1\n",
		       dual_steps, test->name, generation, phase, assertions - first_assertions,
		       response_records - first_records);
	}
	return dual_steps == 10 && phase == 10 && assertions == 42 && response_records == 52 &&
			       !wire_error
		       ? 0
		       : -EBADMSG;
}
struct reconnect_case {
	const char *name;
	uint8_t stage;
};
static const struct reconnect_case reconnect_cases[] = {
	{"reconnect_after_codec", 1},
	{"reconnect_after_qos", 2},
	{"reconnect_after_partial_enable", 3},
	{"reconnect_after_partial_streaming", 4},
};
static unsigned reconnect_steps;

static int reconnect_partial_stream(void)
{
	bsim_tx_set_required_streams(1);
	int ret = wait_client_enabling(0);
	if (ret) {
		return ret;
	}
	ret = bt_bap_stream_connect(stream_at(0));
	if (ret || k_sem_take(&sem_stream_connected, K_SECONDS(10))) {
		return ret ? ret : -ETIMEDOUT;
	}
	ret = state(0, BT_BAP_EP_STATE_STREAMING);
	if (ret) {
		return ret;
	}
	ret = state(1, BT_BAP_EP_STATE_ENABLING);
	if (ret) {
		return ret;
	}
	struct bt_bap_ep_info info;
	ret = bt_bap_ep_get_info(sink_eps[0], &info);
	if (ret || info.id != ids[0] || info.dir != BT_AUDIO_DIR_SINK ||
	    info.state != BT_BAP_EP_STATE_STREAMING || !info.can_send) {
		return ret ? ret : -EBADMSG;
	}
	struct tx_param tx = {.octets_per_frame = 120,
			      .freq_hz = 48000,
			      .frame_duration_us = 10000,
			      .chan_count = 1,
			      .channel_idx = 0};
	ret = owned_tx_register(0, &tx);
	if (ret) {
		return ret;
	}
	bsim_tx_set_send_limit(stream_at(0), 10);
	ret = bsim_tx_wait_send_limit(stream_at(0), 5000);
	if (ret) {
		return ret;
	}
	uint32_t sent = bsim_tx_send_count(stream_at(0));
	printk("ASCS_PARTIAL_SOURCE generation=%u index=0 accepted=%u expected=10 "
	       "peer_delivery=unproved\n",
	       generation, sent);
	if (sent != 10) {
		return -EBADMSG;
	}
	ret = bsim_tx_unregister(stream_at(0));
	printk("ASCS_PARTIAL_SOURCE generation=%u unregister=%d\n", generation, ret);
	if (ret) {
		return ret;
	}
	active->tx_registered[0] = false;
	struct bsim_tx_result retained;

	ret = bsim_tx_result(stream_at(0), &retained);
	printk("ASCS_TX_AUDIT stage=partial generation=%u phase=%u index=0 ret=%d "
	       "sends=%u fnv=%08x\n",
	       generation, phase, ret, ret ? 0U : retained.send_count,
	       ret ? 0U : retained.fnv1a_hash);
	if (ret || retained.send_count != sent) {
		wire_error = true;
		return ret ? ret : -EBADMSG;
	}
	bsim_tx_set_required_streams(2);
	return wire_error ? -EBADMSG : 0;
}

static int reconnect_prepare(const struct reconnect_case *test)
{
	int ret = test->stage == 1 ? config_only() : config_qos();
	if (ret || test->stage < 3) {
		return ret;
	}
	uint8_t qos[2][2];
	ret = metadata_qos(qos);
	if (ret) {
		return ret;
	}
	const struct bt_audio_codec_cfg *cfg_a = &presets[0]->codec_cfg;
	ret = metadata_request(3, 0, cfg_a->meta, cfg_a->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(0, BT_BAP_EP_STATE_ENABLING, cfg_a->meta, cfg_a->meta_len, qos);
	if (ret) {
		return ret;
	}
	if (test->stage == 3) {
		return state(1, BT_BAP_EP_STATE_QOS_CONFIGURED);
	}
	const struct bt_audio_codec_cfg *cfg_b = &presets[1]->codec_cfg;
	ret = metadata_request(3, 1, cfg_b->meta, cfg_b->meta_len, 0, 0, test->name);
	if (ret) {
		return ret;
	}
	ret = metadata_observe(1, BT_BAP_EP_STATE_ENABLING, cfg_b->meta, cfg_b->meta_len, qos);
	return ret ? ret : reconnect_partial_stream();
}

static int reconnect_pre_disconnect(const struct reconnect_case *test)
{
	uint8_t expected_a = test->stage == 1 ? 1 : test->stage == 2 ? 2 : test->stage == 3 ? 3 : 4;
	uint8_t expected_b = test->stage == 1 ? 1 : test->stage == 2 ? 2 : test->stage == 3 ? 2 : 3;
	for (unsigned i = 0; i < 2; i++) {
		int ret = read_ase(i);
		if (ret) {
			return ret;
		}
		if (read_value[0] != ids[i] || read_value[1] != (i ? expected_b : expected_a)) {
			return -EBADMSG;
		}
		printk("ASCS_PRE_DISCONNECT step=%u name=%s generation=%u index=%u state=%u raw=",
		       reconnect_steps + 1, test->name, generation, i, read_value[1]);
		print_raw(read_value, read_length);
	}
	return wire_error ? -EBADMSG : 0;
}

static int reconnect_disconnect(void)
{
	struct wire_generation *ctx = active;
	uint32_t old_generation = ctx->generation;
	atomic_store(&closing_generation, old_generation);
	ctx->disconnect_expected = true;
	int ret = retire(ctx);
	if (ret) {
		return ret;
	}
	k_sem_reset(&sem_disconnected);
	ret = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	printk("ASCS_DISCONNECT generation=%u request_ret=%d\n", old_generation, ret);
	if (ret) {
		return ret;
	}
	if (k_sem_take(&sem_disconnected, K_SECONDS(10)) || default_conn != NULL) {
		wire_error = true;
		return -ETIMEDOUT;
	}
	printk("ASCS_DISCONNECT generation=%u observed=1 default_conn_null=1\n", old_generation);
	int64_t deadline = k_uptime_get() + 5000;
	while (atomic_load(&released_mask) != 3U) {
		if (wire_error || k_uptime_get() >= deadline) {
			wire_error = true;
			printk("ASCS_RELEASED generation=%u timeout mask=%u\n", old_generation,
			       atomic_load(&released_mask));
			return -ETIMEDOUT;
		}
		(void)k_sem_take(&released_done, K_MSEC(10));
	}
	printk("ASCS_RELEASED generation=%u barrier=both mask=%u\n", old_generation,
	       atomic_load(&released_mask));
	if (unicast_group != NULL) {
		deadline = k_uptime_get() + 5000;
		while (true) {
			ret = bt_bap_unicast_group_delete(unicast_group);
			if (!ret) {
				unicast_group = NULL;
				break;
			}
			if (ret != -EBUSY || k_uptime_get() >= deadline) {
				printk("ASCS_GROUP_DELETE generation=%u ret=%d\n", old_generation,
				       ret);
				return ret;
			}
			k_sleep(K_MSEC(10));
		}
	}
	printk("ASCS_GROUP_DELETE generation=%u ret=0\n", old_generation);
	if (wire_error) {
		return -EBADMSG;
	}
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_mtu_exchanged);
	k_sem_reset(&sem_security_updated);
	k_sem_reset(&sem_sinks_discovered);
	memset(sink_eps, 0, sizeof(sink_eps));
	ret = connect_new();
	if (ret) {
		return ret;
	}
	ret = state(0, 0);
	if (ret) {
		return ret;
	}
	ret = state(1, 0);
	if (ret) {
		return ret;
	}
	printk("ASCS_RECONNECT old_generation=%u new_generation=%u cp=%u ccc=%u ase0=%u/%u "
	       "ase1=%u/%u mtu=%u idle=both\n",
	       old_generation, generation, cp_handle, cp_ccc_handle, ids[0], ase_handles[0], ids[1],
	       ase_handles[1], bt_gatt_get_mtu(default_conn));
	return generation == old_generation + 1 && bt_gatt_get_mtu(default_conn) == 65 &&
			       !wire_error
		       ? 0
		       : -EBADMSG;
}

static int validate_reconnect(void)
{
	for (unsigned i = 0; i < ARRAY_SIZE(reconnect_cases); i++) {
		const struct reconnect_case *test = &reconnect_cases[i];
		uint32_t first_phase = phase;
		uint32_t first_assertions = assertions;
		printk("CASE_BEGIN step=%u name=%s generation=%u recovery_phase=%u\n",
		       reconnect_steps + 1, test->name, generation, phase);
		if (wire_error) {
			return -EBADMSG;
		}
		atomic_store(&released_mask, 0);
		k_sem_reset(&released_done);
		int ret = reconnect_prepare(test);
		if (ret) {
			return ret;
		}
		ret = reconnect_pre_disconnect(test);
		if (ret) {
			return ret;
		}
		ret = reconnect_disconnect();
		if (ret) {
			return ret;
		}
		bsim_tx_set_required_streams(2);
		ret = recovery();
		if (ret) {
			return ret;
		}
		unsigned expected = test->stage == 3 ? 3U : test->stage == 4 ? 4U : 2U;
		if (wire_error || phase != first_phase + 1 ||
		    assertions != first_assertions + expected) {
			return -EBADMSG;
		}
		reconnect_steps++;
		printk("CASE_END step=%u name=%s generation=%u recovery_phase=%u assertions=%u "
		       "fresh_idle=1 rendered=1\n",
		       reconnect_steps, test->name, generation, phase,
		       assertions - first_assertions);
	}
	return reconnect_steps == 4 && generation == 5 && assertions == 11 && phase == 4 &&
			       !wire_error
		       ? 0
		       : -EBADMSG;
}
static void ascs_setup(void)
{
	bst_result = In_progress;
	bst_ticker_set_next_tick_absolute(240000000);
}
static void ascs_timeout(bs_time_t now)
{
	(void)now;
	if (bst_result != Passed) {
		FAIL("ASCS client timeout\n");
	}
}

static struct owned_stream *stream_owner(struct bt_bap_stream *stream)
{
	return CONTAINER_OF(stream, struct owned_stream, stream);
}

static bool stream_callback_owned(struct bt_bap_stream *stream, const char *kind)
{
	struct owned_stream *owned = stream_owner(stream);
	struct wire_generation *ctx = owned->owner;

	if (owned->index < 2 && ctx == active && ctx->held && !ctx->retired && !ctx->closing &&
	    stream->conn == ctx->conn) {
		return true;
	}
	if (ctx->closing && ctx->disconnect_expected &&
	    ctx->generation == atomic_load(&closing_generation) && owned->index < 2) {
		printk("ASCS_TEARDOWN_OBSERVER kind=%s generation=%u index=%u\n", kind,
		       ctx->generation, owned->index);
		return false;
	}
	wire_error = true;
	printk("ASCS_STREAM_CALLBACK_ERROR kind=%s generation=%u index=%u active=%u\n", kind,
	       ctx->generation, owned->index, active ? active->generation : 0);
	return false;
}

static void owned_configured(struct bt_bap_stream *stream, const struct bt_bap_qos_cfg_pref *pref)
{
	if (stream_callback_owned(stream, "configured")) {
		stream_configured(stream, pref);
	}
}

static void owned_qos_set(struct bt_bap_stream *stream)
{
	if (stream_callback_owned(stream, "qos_set")) {
		stream_qos_set(stream);
	}
}

static void owned_enabled(struct bt_bap_stream *stream)
{
	if (stream_callback_owned(stream, "enabled")) {
		stream_enabled(stream);
	}
}

static void owned_connected(struct bt_bap_stream *stream)
{
	if (stream_callback_owned(stream, "connected")) {
		stream_connected_cb(stream);
	}
}

static void owned_started(struct bt_bap_stream *stream)
{
	if (stream_callback_owned(stream, "started")) {
		stream_started(stream);
	}
}

static void owned_stopped(struct bt_bap_stream *stream, uint8_t reason)
{
	if (stream_callback_owned(stream, "stopped")) {
		stream_stopped(stream, reason);
	}
}

static void owned_listener_config(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code code,
				  enum bt_bap_ascs_reason reason)
{
	if (stream_callback_owned(stream, "config-rsp")) {
		listener_config(stream, code, reason);
	}
}

static void owned_listener_release(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code code,
				   enum bt_bap_ascs_reason reason)
{
	struct owned_stream *owned = stream_owner(stream);
	struct wire_generation *ctx = owned->owner;
	/* Installed SDK may reset stream->conn before delivering Release.
	 * Ownership comes from the immutable containing stream instead. */
	if (owned->index < 2 && ctx == active && ctx->held && !ctx->retired && !ctx->closing) {
		listener_release(stream, code, reason);
	} else if (owned->index < 2 && ctx->closing && ctx->disconnect_expected &&
		   ctx->generation == atomic_load(&closing_generation)) {
		printk("ASCS_TEARDOWN_OBSERVER kind=release-rsp generation=%u index=%u\n",
		       ctx->generation, owned->index);
	} else {
		wire_error = true;
		printk("ASCS_STREAM_CALLBACK_ERROR kind=release-rsp generation=%u index=%u\n",
		       ctx->generation, owned->index);
	}
}

static void owned_listener_disable(struct bt_bap_stream *stream, enum bt_bap_ascs_rsp_code code,
				   enum bt_bap_ascs_reason reason)
{
	if (stream_callback_owned(stream, "disable-rsp")) {
		listener_disable(stream, code, reason);
	}
}
static void metadata_updated_observed(struct bt_bap_stream *stream)
{
	struct owned_stream *owned = stream_owner(stream);
	struct wire_generation *ctx = owned->owner;
	unsigned index = owned->index;
	if (!stream_callback_owned(stream, "metadata")) {
		return;
	}
	if (!stream->ep) {
		wire_error = true;
		printk("ASCS_METADATA_CALLBACK_ERROR generation=%u index=%u error=endpoint\n",
		       ctx->generation, index);
		return;
	}
	struct bt_bap_ep_info info;
	int ret = bt_bap_ep_get_info(stream->ep, &info);
	if (ret || info.id != ids[index] || info.dir != BT_AUDIO_DIR_SINK ||
	    (info.state != BT_BAP_EP_STATE_ENABLING && info.state != BT_BAP_EP_STATE_STREAMING)) {
		wire_error = true;
		printk("ASCS_METADATA_CALLBACK_ERROR generation=%u index=%u error=%d id=%u "
		       "state=%u dir=%u\n",
		       ctx->generation, index, ret, ret ? 0 : info.id, ret ? 0 : info.state,
		       ret ? 0 : info.dir);
		return;
	}
	printk("ASCS_METADATA_CALLBACK generation=%u id=%u state=%u index=%u\n", ctx->generation,
	       info.id, info.state, index);
}
static void disabled_observed(struct bt_bap_stream *stream)
{
	struct owned_stream *owned = stream_owner(stream);
	struct wire_generation *ctx = owned->owner;
	unsigned index = owned->index;
	if (!stream_callback_owned(stream, "disabled")) {
		return;
	}
	if (!stream->ep) {
		wire_error = true;
		printk("ASCS_DISABLED_ERROR generation=%u index=%u error=endpoint\n",
		       ctx->generation, index);
		return;
	}
	struct bt_bap_ep_info info;
	int ret = bt_bap_ep_get_info(stream->ep, &info);
	if (ret || info.id != ids[index] || info.dir != BT_AUDIO_DIR_SINK ||
	    info.state != BT_BAP_EP_STATE_QOS_CONFIGURED) {
		wire_error = true;
		printk("ASCS_DISABLED_ERROR generation=%u index=%u error=%d id=%u state=%u "
		       "dir=%u\n",
		       ctx->generation, index, ret, ret ? 0 : info.id, ret ? 0 : info.state,
		       ret ? 0 : info.dir);
		return;
	}
	printk("ASCS_DISABLED generation=%u id=%u state=%u index=%u\n", ctx->generation, info.id,
	       info.state, index);
}
static void released_observed(struct bt_bap_stream *stream)
{
	struct owned_stream *owned = stream_owner(stream);
	struct wire_generation *ctx = owned->owner;
	unsigned index = owned->index;
	if (index >= 2 || (ctx->closing && ctx->generation != atomic_load(&closing_generation))) {
		wire_error = true;
		printk("ASCS_RELEASED_ERROR generation=%u index=%u\n", ctx->generation, index);
		return;
	}
	if (!ctx->closing || !ctx->disconnect_expected) {
		if (ctx != active || ctx->retired || !ctx->held) {
			wire_error = true;
			return;
		}
		printk("ASCS_RELEASED stage=normal generation=%u index=%u\n", ctx->generation,
		       index);
		return;
	}
	if (ctx != active) {
		wire_error = true;
		printk("ASCS_RELEASED_ERROR generation=%u index=%u error=old-owner\n",
		       ctx->generation, index);
		return;
	}
	unsigned bit = 1U << index;
	unsigned prior = atomic_fetch_or(&released_mask, bit);
	printk("ASCS_RELEASED stage=acl generation=%u index=%u mask=%u\n", ctx->generation, index,
	       prior | bit);
	if (prior & bit) {
		wire_error = true;
	}
	k_sem_give(&released_done);
}
static struct bt_bap_stream_ops wire_stream_ops = {
	.configured = owned_configured,
	.qos_set = owned_qos_set,
	.enabled = owned_enabled,
	.connected = owned_connected,
	.started = owned_started,
	.stopped = owned_stopped,
	.metadata_updated = metadata_updated_observed,
	.disabled = disabled_observed,
	.released = released_observed,
};
static void run_named(const char *name)
{
	int ret = client_setup();
	if (!ret) {
		unicast_client_cbs.config = owned_listener_config;
		unicast_client_cbs.release = owned_listener_release;
		unicast_client_cbs.disable = owned_listener_disable;
		ret = connect_new();
	}
	if (!ret) {
		if (!strcmp(name, "control_frame_validation")) {
			ret = validate_control_frames();
		} else if (!strcmp(name, "metadata_length_validation")) {
			ret = validate_metadata_family();
		} else if (!strcmp(name, "codec_qos")) {
			ret = validate_codec_qos();
		} else if (!strcmp(name, "lifecycle")) {
			ret = validate_lifecycle();
		} else if (!strcmp(name, "dual")) {
			ret = validate_dual();
		} else if (!strcmp(name, "reconnect")) {
			ret = validate_reconnect();
		} else {
			ret = -EINVAL;
		}
	}
	if (!ret) {
		uint32_t value;
		lane_send(FINISH, phase, phase);
		ret = lane_wait(FINISHED, phase, &value, 5000);
		if (!ret && (value != phase || !assertions || !phase)) {
			ret = -EBADMSG;
		}
	}
	int cleanup = retire(active);
	if (!ret && cleanup) {
		ret = cleanup;
	}
	if (ret || wire_error) {
		FAIL("ASCS_CLIENT case=%s error=%d wire_error=%u cleanup=%d assertions=%u "
		     "phases=%u\n",
		     name, ret, wire_error, cleanup, assertions, phase);
		return;
	}
	PASS("ASCS_CLIENT case=%s cases=%u assertions=%u phases=%u\n", name,
	     !strcmp(name, "codec_qos")                  ? codec_qos_steps
	     : !strcmp(name, "lifecycle")                ? lifecycle_steps
	     : !strcmp(name, "dual")                     ? dual_steps
	     : !strcmp(name, "reconnect")                ? reconnect_steps
	     : !strcmp(name, "control_frame_validation") ? control_steps
							 : metadata_steps,
	     assertions, phase);
}
static void run_control_frame_validation(void)
{
	run_named("control_frame_validation");
}
static void run_metadata_length_validation(void)
{
	run_named("metadata_length_validation");
}
static void run_codec_qos(void)
{
	run_named("codec_qos");
}
static void run_lifecycle(void)
{
	run_named("lifecycle");
}
static void run_dual(void)
{
	run_named("dual");
}
static void run_reconnect(void)
{
	run_named("reconnect");
}
static const struct bst_test_instance ascs_cases[] = {
	{.test_id = "control_frame_validation",
	 .test_descr = "Control-frame validation and valid retry",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_control_frame_validation,
	 .test_tick_f = ascs_timeout},
	{.test_id = "metadata_length_validation",
	 .test_descr = "Metadata-length validation and valid retry",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_metadata_length_validation,
	 .test_tick_f = ascs_timeout},
	{.test_id = "codec_qos",
	 .test_descr = "Encoded codec and QoS rejection with valid recovery",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_codec_qos,
	 .test_tick_f = ascs_timeout},
	{.test_id = "lifecycle",
	 .test_descr = "Encoded ASCS state and direction lifecycle with valid recovery",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_lifecycle,
	 .test_tick_f = ascs_timeout},
	{.test_id = "dual",
	 .test_descr = "Dual ASE partial results and valid stereo recovery",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_dual,
	 .test_tick_f = ascs_timeout},
	{.test_id = "reconnect",
	 .test_descr = "Partial ASCS states, owned reconnect and full rendered reuse",
	 .test_pre_init_f = ascs_setup,
	 .test_main_f = run_reconnect,
	 .test_tick_f = ascs_timeout},
	BSTEST_END_MARKER};
static struct bst_test_list *install_ascs(struct bst_test_list *tests)
{
	return bst_add_tests(tests, ascs_cases);
}
bst_test_install_t test_installers[] = {install_ascs, NULL};
