/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/net_buf.h>

#include <sdc_hci.h>
#include <sdc_hci_cmd_le.h>
#include <zephyr/bluetooth/hci_types.h>

struct net_buf;
struct bt_conn;

enum bt_buf_type {
	BT_BUF_EVT = 0x02,
	BT_BUF_ACL_IN = 0x08,
	BT_BUF_ISO_IN = 0x20,
};

static const sdc_hci_cmd_le_remove_iso_data_path_t *observed_params;
static sdc_hci_cmd_le_remove_iso_data_path_return_t *observed_return;
static uint8_t fake_status;
static uint8_t *observed_msg_out;
static sdc_hci_msg_type_t *observed_msg_type_out;
static int fake_msg_get_status;
static sdc_hci_msg_type_t fake_msg_type;
static uint8_t fake_msg[6];
static uint8_t *observed_cmd_in;
static int fake_cmd_put_status;
static int observed_lock_release_calls;
static struct k_work_q *observed_work_queue;
static struct k_work *observed_work;
static int observed_work_submit_calls;
static int fake_work_submit_status;
static bool fake_work_submit_invoke_msg_get;
static int observed_msg_get_calls;
static struct k_work_q fake_work_queue;
static struct k_work fake_work;
static k_tid_t fake_current_thread;
static k_tid_t fake_switch_in_thread;
static k_tid_t fake_resumed_thread;
static bool fake_real_unlock_switch_mpsl;
static struct k_thread fake_switch_foreign_thread;
static int observed_sched_unlock_calls;
static int observed_sched_unlock_entries;
static int observed_sched_unlock_returns;
static int observed_sched_unlock_real_order;
static int observed_sched_unlock_return_order;
static int scheduler_observation_order;
static bool fake_real_yield_switch_mpsl;
static int observed_real_yield_calls;
static int observed_real_yield_order;
struct k_work_q mpsl_work_q;

static int fake_scheduler_busy;
static const char *fake_scheduler_state;
static struct k_thread fake_scheduler_thread;
static k_tid_t fake_scheduler_queue_thread;
static int fake_scheduler_sender_priority;
static int fake_scheduler_mpsl_priority;
static int observed_scheduler_armed_calls;
static bool observed_scheduler_queue_is_mpsl;
static int observed_scheduler_busy_calls;
static const struct k_work *observed_scheduler_busy_work;
static int observed_scheduler_queue_calls;
static struct k_work_q *observed_scheduler_queue;
static int observed_scheduler_state_str_calls;
static k_tid_t observed_scheduler_state_thread;
static int observed_scheduler_state_calls;
static int observed_scheduler_state_order;
static int observed_scheduler_busy;
static char observed_scheduler_state[32];
static bool observed_scheduler_resumed_current_is_sender;
static unsigned int observed_scheduler_mpsl_switches_during_unlock;
static int observed_scheduler_sender_priority;
static int observed_scheduler_resumed_current_priority;
static int observed_scheduler_mpsl_priority;
static int observed_scheduler_priority_calls;
static k_tid_t observed_scheduler_priority_threads[3];
static int observed_scheduler_priority_values[3];
static int observed_scheduler_yield_armed_calls;
static int observed_scheduler_yield_entries;
static int observed_scheduler_yield_returns;
static int observed_scheduler_yield_arm_order;
static int observed_scheduler_yield_entry_order;
static int observed_scheduler_yield_state_order;
static int observed_scheduler_yield_return_order;
static int observed_scheduler_yield_state_calls;
static int observed_scheduler_yield_busy;
static char observed_scheduler_yield_state[32];
static bool observed_scheduler_yield_resumed_current_is_sender;
static unsigned int observed_scheduler_yield_mpsl_switches;

static int fake_snapshot_schedule_status;
static int fake_snapshot_busy;
static const char *fake_snapshot_state;
static struct k_thread fake_snapshot_thread;
static int observed_snapshot_schedule_calls;
static struct k_work *observed_snapshot_target;
static struct k_work_q *observed_snapshot_queue;
static const struct k_work *observed_snapshot_busy_work;
static k_tid_t observed_snapshot_state_thread;
static int observed_snapshot_busy;
static char observed_snapshot_state[32];

static uint8_t fake_evt_storage;
static uint8_t fake_rx_storage;
static struct net_buf *fake_evt_result;
static struct net_buf *fake_rx_result;
static bool fake_evt_invoke_rx;
static int observed_evt_calls;
static uint8_t observed_evt;
static bool observed_evt_discardable;
static k_timeout_t observed_evt_timeout;
static int observed_rx_calls;
static enum bt_buf_type observed_rx_type;
static k_timeout_t observed_rx_timeout;
static int fake_receive_busy;
static k_tid_t fake_receive_current_thread;
static k_tid_t fake_receive_queue_thread;
static int observed_receive_armed_calls;
static bool observed_receive_queue_is_mpsl;
static int observed_receive_fetch_entry_calls;
static int observed_receive_fetch_return_calls;
static int observed_receive_fetch_status;
static bool observed_receive_fetch_msg_type_valid;
static unsigned int observed_receive_fetch_msg_type;
static int observed_receive_evt_calls;
static unsigned int observed_receive_evt;
static bool observed_receive_evt_discardable;
static bool observed_receive_evt_available;
static int observed_receive_evt_busy;
static int observed_receive_rx_calls;
static unsigned int observed_receive_rx_type;
static bool observed_receive_rx_available;
static int observed_receive_rx_busy;

static int observed_lifetime_arm_calls;
static unsigned int observed_lifetime_capacity;
static int observed_lifetime_snapshot_calls;
static char observed_lifetime_snapshot_reason[16];
static unsigned int observed_lifetime_snapshot_capacity;
static unsigned int observed_lifetime_snapshot_outstanding;
static unsigned int observed_lifetime_snapshot_high_water;
static unsigned int observed_lifetime_snapshot_allocations;
static unsigned int observed_lifetime_snapshot_final_unrefs;
static unsigned int observed_lifetime_snapshot_callbacks_active;
static unsigned int observed_lifetime_snapshot_callbacks_total;
static int observed_lifetime_first_free_calls;
static unsigned int observed_lifetime_first_free_outstanding_before;
static unsigned int observed_lifetime_first_free_callbacks_active;
static unsigned int observed_lifetime_first_free_allocations;
static unsigned int observed_lifetime_first_free_final_unrefs;
static int observed_lifetime_final_unref_calls;
static unsigned int observed_lifetime_final_unref_outstanding;
static unsigned int observed_lifetime_final_unref_allocations;
static unsigned int observed_lifetime_final_unref_count;
static unsigned int observed_lifetime_final_unref_ref;
static int observed_lifetime_error_calls;
static char observed_lifetime_error_reason[32];
static int observed_lifetime_disposition_calls;
static char observed_lifetime_disposition_reason[16];
static unsigned int observed_lifetime_disposition_undispatched;
static unsigned int observed_lifetime_disposition_host_dispatched;
static unsigned int observed_lifetime_disposition_tx_notify_flush_entered;
static unsigned int observed_lifetime_disposition_tx_notify_flush_semaphore_entered;
static unsigned int observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered;
static unsigned int
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending;
static unsigned int observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered;
static unsigned int observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned;
static unsigned int observed_lifetime_disposition_tx_notify_flush_semaphore_returned;
static unsigned int observed_lifetime_disposition_tx_notify_flush_returned;
static unsigned int observed_lifetime_disposition_host_returned;
static unsigned int observed_lifetime_disposition_app_callback_seen;
static unsigned int observed_lifetime_disposition_unclassified;
static struct net_buf lifetime_buffers[4];
static struct net_buf *observed_net_buf_unref;
static int observed_net_buf_unref_calls;
static int observed_bt_conn_recv_calls;
static struct bt_conn *observed_bt_conn_recv_conn;
static struct net_buf *observed_bt_conn_recv_buf;
static uint8_t observed_bt_conn_recv_flags;
static int observed_k_work_flush_calls;
static struct k_work *observed_k_work_flush_work;
static struct k_work_sync *observed_k_work_flush_sync;
static bool observed_k_work_flush_result;
static bool fake_bt_conn_recv_invoke_flush;
static bool fake_bt_conn_recv_snapshot_after_flush;
static bool fake_bt_conn_recv_invoke_callback;
static bool fake_k_work_flush_snapshot_before_return;
static bool fake_k_work_flush_result;
static bool fake_k_work_flush_invoke_sem;
static bool fake_k_sem_take_wrong_sem;
static bool fake_k_sem_take_non_forever;
static bool fake_k_sem_take_snapshot_before_return;
static bool fake_k_sem_take_invoke_pend;
static bool fake_k_sem_take_snapshot_after_pend;
static bool fake_k_sem_take_wrong_wait_q;
static struct k_sem fake_foreign_sem;
static bool fake_pend_invoke_user_hook;
static k_tid_t fake_pend_hook_thread;
static bool fake_pend_invoke_give;
static bool fake_pend_give_wrong_sem;
static struct k_sem *observed_k_sem_take_sem;
static k_timeout_t observed_k_sem_take_timeout;
static int observed_k_sem_take_calls;
static int observed_k_sem_take_wrapper_result;
static int fake_k_sem_take_result;
static struct k_spinlock fake_pend_lock;
static k_spinlock_key_t fake_pend_key;
static struct k_spinlock *observed_pend_lock;
static k_spinlock_key_t observed_pend_key;
static _wait_q_t *observed_pend_wait_q;
static k_timeout_t observed_pend_timeout;
static int observed_pend_calls;
static int observed_pend_wrapper_result;
static int fake_pend_result;
static bool fake_pend_snapshot_before_return;
static int observed_k_sem_give_calls;
static struct k_sem *observed_k_sem_give_sem;
static _wait_q_t fake_foreign_wait_q;
static struct k_work fake_flush_work;
static struct k_work_sync fake_flush_sync;

int __wrap_hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out);
struct net_buf *__wrap_bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout);
struct net_buf *__wrap_bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout);
void __wrap_net_buf_unref(struct net_buf *buf);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open(void);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_enter(struct net_buf *buf);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_exit(void);
void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot(void);
void __wrap_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf, uint8_t flags);
bool __wrap_k_work_flush(struct k_work *work, struct k_work_sync *sync);
int __wrap_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout);
int __wrap_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key, _wait_q_t *wait_q,
		       k_timeout_t timeout);
void __wrap_z_impl_k_sem_give(struct k_sem *sem);
void sys_trace_thread_pend_user(struct k_thread *thread);

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_arm_observed(unsigned int capacity)
{
	observed_lifetime_arm_calls++;
	observed_lifetime_capacity = capacity;
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot_observed(
	const char *reason, unsigned int capacity, unsigned int outstanding,
	unsigned int high_water, unsigned int allocations, unsigned int final_unrefs,
	unsigned int callbacks_active, unsigned int callbacks_total)
{
	observed_lifetime_snapshot_calls++;
	strncpy(observed_lifetime_snapshot_reason, reason,
		sizeof(observed_lifetime_snapshot_reason) - 1U);
	observed_lifetime_snapshot_reason[sizeof(observed_lifetime_snapshot_reason) - 1U] = '\0';
	observed_lifetime_snapshot_capacity = capacity;
	observed_lifetime_snapshot_outstanding = outstanding;
	observed_lifetime_snapshot_high_water = high_water;
	observed_lifetime_snapshot_allocations = allocations;
	observed_lifetime_snapshot_final_unrefs = final_unrefs;
	observed_lifetime_snapshot_callbacks_active = callbacks_active;
	observed_lifetime_snapshot_callbacks_total = callbacks_total;
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free_observed(
	unsigned int outstanding_before, unsigned int callbacks_active, unsigned int allocations,
	unsigned int final_unrefs)
{
	observed_lifetime_first_free_calls++;
	observed_lifetime_first_free_outstanding_before = outstanding_before;
	observed_lifetime_first_free_callbacks_active = callbacks_active;
	observed_lifetime_first_free_allocations = allocations;
	observed_lifetime_first_free_final_unrefs = final_unrefs;
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unref_observed(unsigned int outstanding,
									unsigned int allocations,
									unsigned int final_unrefs)
{
	observed_lifetime_final_unref_calls++;
	observed_lifetime_final_unref_outstanding = outstanding;
	observed_lifetime_final_unref_allocations = allocations;
	observed_lifetime_final_unref_count = final_unrefs;
	observed_lifetime_final_unref_ref = lifetime_buffers[0].ref;
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error_observed(const char *reason)
{
	observed_lifetime_error_calls++;
	strncpy(observed_lifetime_error_reason, reason,
		sizeof(observed_lifetime_error_reason) - 1U);
	observed_lifetime_error_reason[sizeof(observed_lifetime_error_reason) - 1U] = '\0';
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int tx_notify_flush_entered, unsigned int tx_notify_flush_semaphore_entered,
	unsigned int tx_notify_flush_semaphore_pend_entered,
	unsigned int tx_notify_flush_semaphore_pend_thread_marked_pending,
	unsigned int tx_notify_flush_semaphore_give_entered,
	unsigned int tx_notify_flush_semaphore_pend_returned,
	unsigned int tx_notify_flush_semaphore_returned, unsigned int tx_notify_flush_returned,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	observed_lifetime_disposition_calls++;
	strncpy(observed_lifetime_disposition_reason, reason,
		sizeof(observed_lifetime_disposition_reason) - 1U);
	observed_lifetime_disposition_reason[sizeof(observed_lifetime_disposition_reason) - 1U] =
		'\0';
	observed_lifetime_disposition_undispatched = undispatched;
	observed_lifetime_disposition_host_dispatched = host_dispatched;
	observed_lifetime_disposition_tx_notify_flush_entered = tx_notify_flush_entered;
	observed_lifetime_disposition_tx_notify_flush_semaphore_entered =
		tx_notify_flush_semaphore_entered;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered =
		tx_notify_flush_semaphore_pend_entered;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending =
		tx_notify_flush_semaphore_pend_thread_marked_pending;
	observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered =
		tx_notify_flush_semaphore_give_entered;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned =
		tx_notify_flush_semaphore_pend_returned;
	observed_lifetime_disposition_tx_notify_flush_semaphore_returned =
		tx_notify_flush_semaphore_returned;
	observed_lifetime_disposition_tx_notify_flush_returned = tx_notify_flush_returned;
	observed_lifetime_disposition_host_returned = host_returned;
	observed_lifetime_disposition_app_callback_seen = app_callback_seen;
	observed_lifetime_disposition_unclassified = unclassified;
}
void __wrap_multithreading_lock_release(void);
int __wrap_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);
void sdc_hci_remove_iso_path_trace_test_run_snapshot(void);
void sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(bool enabled);
void sdc_hci_remove_iso_path_trace_test_reset(void);
void __wrap_k_sched_unlock(void);
void __wrap_z_impl_k_yield(void);
void sys_trace_thread_switched_in_user(void);

k_tid_t sdc_hci_remove_iso_path_trace_current_thread_get(void)
{
	return fake_current_thread;
}

void sdc_hci_remove_iso_path_trace_scheduler_unlock_observed(bool entry)
{
	if (entry) {
		observed_sched_unlock_entries++;
	} else {
		observed_sched_unlock_returns++;
		observed_sched_unlock_return_order = ++scheduler_observation_order;
	}
}

int sdc_hci_remove_iso_path_trace_receive_busy_get(const struct k_work *work)
{
	observed_scheduler_busy_work = work;
	return fake_receive_busy;
}

k_tid_t sdc_hci_remove_iso_path_trace_receive_queue_thread_get(struct k_work_q *queue)
{
	observed_scheduler_queue = queue;
	return fake_receive_queue_thread;
}

k_tid_t sdc_hci_remove_iso_path_trace_receive_current_thread_get(void)
{
	return fake_receive_current_thread;
}

void sdc_hci_remove_iso_path_trace_receive_disposition_armed(bool queue_is_mpsl)
{
	observed_receive_armed_calls++;
	observed_receive_queue_is_mpsl = queue_is_mpsl;
}

void sdc_hci_remove_iso_path_trace_receive_disposition_fetch_entry_observed(void)
{
	observed_receive_fetch_entry_calls++;
}

void sdc_hci_remove_iso_path_trace_receive_disposition_fetch_return_observed(int status,
									     bool msg_type_valid,
									     unsigned int msg_type)
{
	observed_receive_fetch_return_calls++;
	observed_receive_fetch_status = status;
	observed_receive_fetch_msg_type_valid = msg_type_valid;
	observed_receive_fetch_msg_type = msg_type;
}

void sdc_hci_remove_iso_path_trace_receive_disposition_evt_observed(unsigned int evt,
								    bool discardable,
								    bool buffer_available,
								    int target_busy)
{
	observed_receive_evt_calls++;
	observed_receive_evt = evt;
	observed_receive_evt_discardable = discardable;
	observed_receive_evt_available = buffer_available;
	observed_receive_evt_busy = target_busy;
}

void sdc_hci_remove_iso_path_trace_receive_disposition_rx_observed(unsigned int type,
								   bool buffer_available,
								   int target_busy)
{
	observed_receive_rx_calls++;
	observed_receive_rx_type = type;
	observed_receive_rx_available = buffer_available;
	observed_receive_rx_busy = target_busy;
}

void sdc_hci_remove_iso_path_trace_scheduler_yield_armed(void)
{
	observed_scheduler_yield_armed_calls++;
	observed_scheduler_yield_arm_order = ++scheduler_observation_order;
}

void sdc_hci_remove_iso_path_trace_scheduler_yield_observed(bool entry)
{
	if (entry) {
		observed_scheduler_yield_entries++;
		observed_scheduler_yield_entry_order = ++scheduler_observation_order;
	} else {
		observed_scheduler_yield_returns++;
		observed_scheduler_yield_return_order = ++scheduler_observation_order;
	}
}

void sdc_hci_remove_iso_path_trace_scheduler_yield_state_observed(
	int busy, const char *mpsl_state, bool resumed_current_is_sender,
	unsigned int mpsl_switches_during_yield)
{
	observed_scheduler_yield_state_calls++;
	observed_scheduler_yield_state_order = ++scheduler_observation_order;
	observed_scheduler_yield_busy = busy;
	observed_scheduler_yield_resumed_current_is_sender = resumed_current_is_sender;
	observed_scheduler_yield_mpsl_switches = mpsl_switches_during_yield;
	strncpy(observed_scheduler_yield_state, mpsl_state,
		sizeof(observed_scheduler_yield_state) - 1U);
	observed_scheduler_yield_state[sizeof(observed_scheduler_yield_state) - 1U] = '\0';
}

void sdc_hci_remove_iso_path_trace_scheduler_unlock_armed(bool queue_is_mpsl)
{
	observed_scheduler_armed_calls++;
	observed_scheduler_queue_is_mpsl = queue_is_mpsl;
}

k_tid_t sdc_hci_remove_iso_path_trace_scheduler_unlock_switch_in_thread_get(void)
{
	return fake_switch_in_thread;
}

int sdc_hci_remove_iso_path_trace_scheduler_unlock_busy_get(const struct k_work *work)
{
	observed_scheduler_busy_calls++;
	observed_scheduler_busy_work = work;
	return fake_scheduler_busy;
}

k_tid_t sdc_hci_remove_iso_path_trace_scheduler_unlock_queue_thread_get(struct k_work_q *queue)
{
	observed_scheduler_queue_calls++;
	observed_scheduler_queue = queue;
	return fake_scheduler_queue_thread;
}

int sdc_hci_remove_iso_path_trace_scheduler_unlock_thread_priority_get(k_tid_t thread)
{
	int priority = thread == &fake_scheduler_thread ? fake_scheduler_mpsl_priority
							: fake_scheduler_sender_priority;

	if (observed_scheduler_priority_calls <
	    (int)(sizeof(observed_scheduler_priority_threads) /
		  sizeof(observed_scheduler_priority_threads[0]))) {
		observed_scheduler_priority_threads[observed_scheduler_priority_calls] = thread;
		observed_scheduler_priority_values[observed_scheduler_priority_calls] = priority;
	}
	observed_scheduler_priority_calls++;
	return priority;
}

const char *sdc_hci_remove_iso_path_trace_scheduler_unlock_state_str(k_tid_t thread, char *buf,
								     size_t buf_size)
{
	observed_scheduler_state_str_calls++;
	observed_scheduler_state_thread = thread;
	if (buf_size == 0U) {
		return "";
	}

	size_t len = strlen(fake_scheduler_state);
	if (len >= buf_size) {
		len = buf_size - 1U;
	}
	memcpy(buf, fake_scheduler_state, len);
	buf[len] = '\0';
	return buf;
}

void sdc_hci_remove_iso_path_trace_scheduler_unlock_state_observed(
	int busy, const char *mpsl_state, bool resumed_current_is_sender,
	unsigned int mpsl_switches_during_unlock, int sender_prio, int resumed_current_prio,
	int mpsl_prio)
{
	observed_scheduler_state_calls++;
	observed_scheduler_state_order = ++scheduler_observation_order;
	observed_scheduler_busy = busy;
	observed_scheduler_resumed_current_is_sender = resumed_current_is_sender;
	observed_scheduler_mpsl_switches_during_unlock = mpsl_switches_during_unlock;
	observed_scheduler_sender_priority = sender_prio;
	observed_scheduler_resumed_current_priority = resumed_current_prio;
	observed_scheduler_mpsl_priority = mpsl_prio;
	strncpy(observed_scheduler_state, mpsl_state, sizeof(observed_scheduler_state) - 1U);
	observed_scheduler_state[sizeof(observed_scheduler_state) - 1U] = '\0';
}

void __real_k_sched_unlock(void)
{
	observed_sched_unlock_calls++;
	observed_sched_unlock_real_order = ++scheduler_observation_order;
	if (fake_real_unlock_switch_mpsl) {
		fake_switch_in_thread = &fake_switch_foreign_thread;
		sys_trace_thread_switched_in_user();
		fake_switch_in_thread = &fake_scheduler_thread;
		sys_trace_thread_switched_in_user();
		fake_current_thread = fake_resumed_thread;
		fake_switch_in_thread = NULL;
	}
}

void __real_z_impl_k_yield(void)
{
	observed_real_yield_calls++;
	observed_real_yield_order = ++scheduler_observation_order;
	if (fake_real_yield_switch_mpsl) {
		fake_switch_in_thread = &fake_switch_foreign_thread;
		sys_trace_thread_switched_in_user();
		fake_switch_in_thread = &fake_scheduler_thread;
		sys_trace_thread_switched_in_user();
		fake_current_thread = fake_resumed_thread;
		fake_switch_in_thread = NULL;
	}
}

int sdc_hci_remove_iso_path_trace_snapshot_schedule(struct k_work *target_work, k_timeout_t delay);
int sdc_hci_remove_iso_path_trace_snapshot_busy_get(const struct k_work *work);
k_tid_t sdc_hci_remove_iso_path_trace_snapshot_queue_thread_get(struct k_work_q *queue);
const char *sdc_hci_remove_iso_path_trace_snapshot_state_str(k_tid_t thread, char *buf,
							     size_t buf_size);
void sdc_hci_remove_iso_path_trace_snapshot_observed(int busy, const char *mpsl_state);

uint8_t
__real_sdc_hci_cmd_le_remove_iso_data_path(const sdc_hci_cmd_le_remove_iso_data_path_t *p_params,
					   sdc_hci_cmd_le_remove_iso_data_path_return_t *p_return)
{
	observed_params = p_params;
	observed_return = p_return;
	return fake_status;
}

uint8_t
__wrap_sdc_hci_cmd_le_remove_iso_data_path(const sdc_hci_cmd_le_remove_iso_data_path_t *p_params,
					   sdc_hci_cmd_le_remove_iso_data_path_return_t *p_return);

int __real_hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out)
{
	observed_msg_get_calls++;
	observed_msg_out = msg_out;
	observed_msg_type_out = msg_type_out;
	if (fake_msg_get_status == 0) {
		memcpy(msg_out, fake_msg, sizeof(fake_msg));
		*msg_type_out = fake_msg_type;
	}
	return fake_msg_get_status;
}

struct net_buf *__real_bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout)
{
	observed_evt_calls++;
	observed_evt = evt;
	observed_evt_discardable = discardable;
	observed_evt_timeout = timeout;

	if (fake_evt_invoke_rx) {
		return __wrap_bt_buf_get_rx(BT_BUF_EVT, timeout);
	}

	return fake_evt_result;
}

struct net_buf *__real_bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout)
{
	observed_rx_calls++;
	observed_rx_type = type;
	observed_rx_timeout = timeout;
	return fake_rx_result;
}

void __real_net_buf_unref(struct net_buf *buf)
{
	observed_net_buf_unref_calls++;
	observed_net_buf_unref = buf;
	if (buf != NULL && buf->ref > 0U) {
		buf->ref--;
	}
}

void __real_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf, uint8_t flags)
{
	observed_bt_conn_recv_calls++;
	observed_bt_conn_recv_conn = conn;
	observed_bt_conn_recv_buf = buf;
	observed_bt_conn_recv_flags = flags;
	if (fake_bt_conn_recv_invoke_flush) {
		observed_k_work_flush_result =
			__wrap_k_work_flush(&fake_flush_work, &fake_flush_sync);
	}
	if (fake_bt_conn_recv_snapshot_after_flush) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	}
	if (fake_bt_conn_recv_invoke_callback) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_enter(buf);
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_exit();
	}
}

bool __real_k_work_flush(struct k_work *work, struct k_work_sync *sync)
{
	observed_k_work_flush_calls++;
	observed_k_work_flush_work = work;
	observed_k_work_flush_sync = sync;
	if (fake_k_work_flush_invoke_sem) {
		struct k_sem *sem = sync == NULL ? NULL : &sync->flusher.sem;
		k_timeout_t timeout = K_FOREVER;

		if (fake_k_sem_take_wrong_sem) {
			sem = &fake_foreign_sem;
		}
		if (fake_k_sem_take_non_forever) {
			timeout = K_NO_WAIT;
		}
		observed_k_sem_take_wrapper_result = __wrap_z_impl_k_sem_take(sem, timeout);
	}
	if (fake_k_work_flush_snapshot_before_return) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	}
	return fake_k_work_flush_result;
}

int __real_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	observed_k_sem_take_calls++;
	observed_k_sem_take_sem = sem;
	observed_k_sem_take_timeout = timeout;
	if (fake_k_sem_take_snapshot_before_return) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	}

	int ret = fake_k_sem_take_result;
	if (fake_k_sem_take_invoke_pend && sem != NULL) {
		_wait_q_t *wait_q = &sem->wait_q;
		if (fake_k_sem_take_wrong_wait_q) {
			wait_q = &fake_foreign_wait_q;
		}
		observed_pend_wrapper_result =
			__wrap_z_pend_curr(&fake_pend_lock, fake_pend_key, wait_q, timeout);
		ret = observed_pend_wrapper_result;
		if (fake_k_sem_take_snapshot_after_pend) {
			sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
		}
	}

	return ret;
}

void __real_z_impl_k_sem_give(struct k_sem *sem)
{
	observed_k_sem_give_calls++;
	observed_k_sem_give_sem = sem;
}

int __real_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key, _wait_q_t *wait_q,
		       k_timeout_t timeout)
{
	observed_pend_calls++;
	observed_pend_lock = lock;
	observed_pend_key = key;
	observed_pend_wait_q = wait_q;
	observed_pend_timeout = timeout;
	if (fake_pend_invoke_user_hook) {
		sys_trace_thread_pend_user(fake_pend_hook_thread);
	}
	if (fake_pend_invoke_give) {
		struct k_sem *sem =
			fake_pend_give_wrong_sem ? &fake_foreign_sem : &fake_flush_sync.flusher.sem;
		__wrap_z_impl_k_sem_give(sem);
	}
	if (fake_pend_snapshot_before_return) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	}
	return fake_pend_result;
}

void __real_multithreading_lock_release(void)
{
	observed_lock_release_calls++;
}

int __real_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	observed_work_queue = queue;
	observed_work = work;
	observed_work_submit_calls++;

	if (fake_work_submit_invoke_msg_get) {
		uint8_t msg_out[sizeof(fake_msg)] = {0};
		sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;

		(void)__wrap_hci_internal_msg_get(msg_out, &msg_type_out);
	}

	return fake_work_submit_status;
}

int __real_hci_internal_cmd_put(uint8_t *cmd_in)
{
	observed_cmd_in = cmd_in;
	return fake_cmd_put_status;
}
int __wrap_hci_internal_cmd_put(uint8_t *cmd_in);

int sdc_hci_remove_iso_path_trace_snapshot_schedule(struct k_work *target_work, k_timeout_t delay)
{
	ARG_UNUSED(delay);
	observed_snapshot_schedule_calls++;
	observed_snapshot_target = target_work;
	return fake_snapshot_schedule_status;
}

int sdc_hci_remove_iso_path_trace_snapshot_busy_get(const struct k_work *work)
{
	observed_snapshot_busy_work = work;
	return fake_snapshot_busy;
}

k_tid_t sdc_hci_remove_iso_path_trace_snapshot_queue_thread_get(struct k_work_q *queue)
{
	observed_snapshot_queue = queue;
	return &fake_snapshot_thread;
}

const char *sdc_hci_remove_iso_path_trace_snapshot_state_str(k_tid_t thread, char *buf,
							     size_t buf_size)
{
	observed_snapshot_state_thread = thread;
	if (buf_size == 0U) {
		return "";
	}

	size_t len = strlen(fake_snapshot_state);
	if (len >= buf_size) {
		len = buf_size - 1U;
	}
	memcpy(buf, fake_snapshot_state, len);
	buf[len] = '\0';
	return buf;
}

void sdc_hci_remove_iso_path_trace_snapshot_observed(int busy, const char *mpsl_state)
{
	observed_snapshot_busy = busy;
	strncpy(observed_snapshot_state, mpsl_state, sizeof(observed_snapshot_state) - 1U);
	observed_snapshot_state[sizeof(observed_snapshot_state) - 1U] = '\0';
}

static void reset_receive_observations(void)
{
	fake_evt_result = (struct net_buf *)&fake_evt_storage;
	fake_rx_result = (struct net_buf *)&fake_rx_storage;
	fake_evt_invoke_rx = false;
	observed_evt_calls = 0;
	observed_evt = 0U;
	observed_evt_discardable = false;
	observed_rx_calls = 0;
	observed_rx_type = BT_BUF_EVT;
	observed_receive_armed_calls = 0;
	observed_receive_queue_is_mpsl = false;
	observed_receive_fetch_entry_calls = 0;
	observed_receive_fetch_return_calls = 0;
	observed_receive_fetch_status = 0;
	observed_receive_fetch_msg_type_valid = false;
	observed_receive_fetch_msg_type = 0U;
	observed_receive_evt_calls = 0;
	observed_receive_evt = 0U;
	observed_receive_evt_discardable = false;
	observed_receive_evt_available = false;
	observed_receive_evt_busy = 0;
	observed_receive_rx_calls = 0;
	observed_receive_rx_type = 0U;
	observed_receive_rx_available = false;
	observed_receive_rx_busy = 0;
}

static void arm_receive_target_work_impl(bool reset_trace)
{
	uint8_t cmd_in[] = {0x6fU, 0x20U};

	reset_receive_observations();
	if (reset_trace) {
		sdc_hci_remove_iso_path_trace_test_reset();
	}
	fake_receive_busy = K_WORK_RUNNING | K_WORK_QUEUED;
	fake_receive_current_thread = &fake_scheduler_thread;
	fake_receive_queue_thread = &fake_scheduler_thread;
	fake_cmd_put_status = 0;
	fake_work_submit_status = 1;
	fake_work_submit_invoke_msg_get = false;

	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), 0,
		      "receive disposition target command must be accepted");
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), 1,
		      "receive disposition target work must be accepted");
	zassert_equal(observed_receive_armed_calls, 1,
		      "accepted target work must arm receive disposition trace");
	zassert_true(observed_receive_queue_is_mpsl,
		     "receive disposition trace must capture MPSL queue");
}

static void arm_receive_target_work(void)
{
	arm_receive_target_work_impl(true);
}

static void arm_receive_target_work_preserve_lifetime(void)
{
	arm_receive_target_work_impl(false);
}

static void reset_lifetime_observations(void)
{
	observed_lifetime_arm_calls = 0;
	observed_lifetime_capacity = 0U;
	observed_lifetime_snapshot_calls = 0;
	observed_lifetime_snapshot_reason[0] = '\0';
	observed_lifetime_snapshot_capacity = 0U;
	observed_lifetime_snapshot_outstanding = 0U;
	observed_lifetime_snapshot_high_water = 0U;
	observed_lifetime_snapshot_allocations = 0U;
	observed_lifetime_snapshot_final_unrefs = 0U;
	observed_lifetime_snapshot_callbacks_active = 0U;
	observed_lifetime_snapshot_callbacks_total = 0U;
	observed_lifetime_first_free_calls = 0;
	observed_lifetime_first_free_outstanding_before = 0U;
	observed_lifetime_first_free_callbacks_active = 0U;
	observed_lifetime_first_free_allocations = 0U;
	observed_lifetime_first_free_final_unrefs = 0U;
	observed_lifetime_final_unref_calls = 0;
	observed_lifetime_final_unref_outstanding = 0U;
	observed_lifetime_final_unref_allocations = 0U;
	observed_lifetime_final_unref_count = 0U;
	observed_lifetime_final_unref_ref = 0U;
	observed_lifetime_error_calls = 0;
	observed_lifetime_error_reason[0] = '\0';
	observed_lifetime_disposition_calls = 0;
	observed_lifetime_disposition_reason[0] = '\0';
	observed_lifetime_disposition_undispatched = 0U;
	observed_lifetime_disposition_host_dispatched = 0U;
	observed_lifetime_disposition_tx_notify_flush_entered = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_entered = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned = 0U;
	observed_lifetime_disposition_tx_notify_flush_semaphore_returned = 0U;
	observed_lifetime_disposition_tx_notify_flush_returned = 0U;
	observed_lifetime_disposition_host_returned = 0U;
	observed_lifetime_disposition_app_callback_seen = 0U;
	observed_lifetime_disposition_unclassified = 0U;
	observed_net_buf_unref = NULL;
	observed_net_buf_unref_calls = 0;
	observed_bt_conn_recv_calls = 0;
	observed_bt_conn_recv_conn = NULL;
	observed_bt_conn_recv_buf = NULL;
	observed_bt_conn_recv_flags = 0U;
	observed_k_work_flush_calls = 0;
	observed_k_work_flush_work = NULL;
	observed_k_work_flush_sync = NULL;
	observed_k_work_flush_result = false;
	fake_k_work_flush_invoke_sem = false;
	fake_k_sem_take_wrong_sem = false;
	fake_k_sem_take_non_forever = false;
	fake_k_sem_take_snapshot_before_return = false;
	fake_k_sem_take_invoke_pend = false;
	fake_k_sem_take_snapshot_after_pend = false;
	fake_k_sem_take_wrong_wait_q = false;
	fake_pend_invoke_user_hook = false;
	fake_pend_hook_thread = NULL;
	fake_pend_invoke_give = false;
	fake_pend_give_wrong_sem = false;
	observed_k_sem_take_sem = NULL;
	observed_k_sem_take_timeout = K_NO_WAIT;
	observed_k_sem_take_calls = 0;
	observed_k_sem_take_wrapper_result = 0;
	fake_k_sem_take_result = 0;
	observed_pend_lock = NULL;
	memset(&observed_pend_key, 0, sizeof(observed_pend_key));
	observed_pend_wait_q = NULL;
	observed_pend_timeout = K_NO_WAIT;
	observed_pend_calls = 0;
	observed_pend_wrapper_result = 0;
	fake_pend_result = 0;
	fake_pend_snapshot_before_return = false;
	observed_k_sem_give_calls = 0;
	observed_k_sem_give_sem = NULL;
	fake_bt_conn_recv_invoke_flush = false;
	fake_bt_conn_recv_snapshot_after_flush = false;
	fake_bt_conn_recv_invoke_callback = false;
	fake_k_work_flush_snapshot_before_return = false;
	fake_k_work_flush_result = false;
	memset(lifetime_buffers, 0, sizeof(lifetime_buffers));
}

static void init_lifetime_buffer(struct net_buf *buf, uint8_t ref)
{
	memset(buf, 0, sizeof(*buf));
	buf->ref = ref;
}

ZTEST_SUITE(sdc_hci_remove_iso_path_trace, NULL, NULL, NULL, NULL, NULL);

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_tracks_allocation_callbacks_and_unref)
{
	struct net_buf untracked;

	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 2U);
	fake_rx_result = &lifetime_buffers[0];
	observed_rx_calls = 0;

	struct net_buf *result = __wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	zassert_equal(result, &lifetime_buffers[0],
		      "successful ISO allocation must return exact real pointer");
	zassert_equal(observed_rx_calls, 1, "successful ISO allocation must forward once");
	zassert_equal(observed_lifetime_arm_calls, 1,
		      "lifetime session must emit one arm observation");
	zassert_equal(observed_lifetime_capacity, 3U,
		      "native lifetime fixture capacity must be three");

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_enter(result);
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_exit();

	/* First unref is nonfinal. It must forward while retaining tracked occupancy. */
	__wrap_net_buf_unref(result);
	zassert_equal(observed_net_buf_unref_calls, 1,
		      "nonfinal tracked unref must forward exactly once");
	zassert_equal(observed_net_buf_unref, result,
		      "nonfinal tracked unref must forward exact pointer");
	zassert_equal(result->ref, 1U, "real nonfinal unref must decrement fake refcount");

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "disable must emit one lifetime snapshot");
	zassert_equal(strcmp(observed_lifetime_snapshot_reason, "disable"), 0,
		      "disable snapshot reason must be exact");
	zassert_equal(observed_lifetime_snapshot_outstanding, 1U,
		      "nonfinal unref must retain outstanding occupancy");
	zassert_equal(observed_lifetime_snapshot_high_water, 1U,
		      "successful allocation must update high water");
	zassert_equal(observed_lifetime_snapshot_allocations, 1U,
		      "successful allocation must update allocation count");
	zassert_equal(observed_lifetime_snapshot_final_unrefs, 0U,
		      "nonfinal unref must not update final-unref count");
	zassert_equal(observed_lifetime_snapshot_callbacks_active, 0U,
		      "callback exit must clear active callback count");
	zassert_equal(observed_lifetime_snapshot_callbacks_total, 1U,
		      "callback enter must update total callback count");

	/* Final tracked unref updates diagnostic state before real unref forwarding. */
	__wrap_net_buf_unref(result);
	zassert_equal(observed_lifetime_final_unref_calls, 1,
		      "tracked final unref must emit one test observation");
	zassert_equal(observed_lifetime_final_unref_outstanding, 0U,
		      "tracked final unref must clear outstanding occupancy");
	zassert_equal(observed_lifetime_final_unref_allocations, 1U,
		      "tracked final unref must retain allocation count");
	zassert_equal(observed_lifetime_final_unref_count, 1U,
		      "tracked final unref must increment final-unref count");
	zassert_equal(observed_lifetime_final_unref_ref, 1U,
		      "diagnostic final-unref observation must precede real unref");
	zassert_equal(result->ref, 0U, "real final unref must run after diagnostic update");

	init_lifetime_buffer(&untracked, 1U);
	__wrap_net_buf_unref(&untracked);
	zassert_equal(observed_net_buf_unref_calls, 3,
		      "nonfinal, final, and untracked unrefs must all forward");
	zassert_equal(observed_net_buf_unref, &untracked,
		      "untracked unref must forward exact pointer");
	zassert_equal(observed_lifetime_final_unref_calls, 1,
		      "untracked unref must not change tracked final state");
	zassert_equal(observed_lifetime_error_calls, 0,
		      "valid lifetime fixture must not disable tracking");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_disposition_tracks_progression)
{
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();

	/* Semaphore-entry snapshot must precede the real semaphore return. */
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "semaphore-entry fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_snapshot_before_return = true;
	fake_k_sem_take_result = -EAGAIN;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0xa5U);
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "semaphore entry must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_entered, 0U,
		      "semaphore-entry snapshot must advance beyond flush entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_entered, 1U,
		      "semaphore-entry snapshot must contain one semaphore entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "semaphore-entry snapshot must precede semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "semaphore-entry snapshot must precede flush return");
	zassert_equal(observed_k_sem_take_calls, 1,
		      "matching semaphore call must forward exactly once");
	zassert_equal(observed_k_sem_take_sem, &fake_flush_sync.flusher.sem,
		      "matching semaphore pointer must be exact sync flusher semaphore");
	zassert_true(K_TIMEOUT_EQ(observed_k_sem_take_timeout, K_FOREVER),
		     "matching semaphore timeout must be K_FOREVER");
	zassert_equal(observed_k_sem_take_wrapper_result, -EAGAIN,
		      "matching semaphore result must be preserved");
	zassert_equal(observed_bt_conn_recv_calls, 1,
		      "flush-entry fixture must forward host receive once");
	zassert_is_null(observed_bt_conn_recv_conn,
			"host receive must preserve exact NULL connection");
	zassert_equal(observed_bt_conn_recv_buf, &lifetime_buffers[0],
		      "host receive must preserve flush-entry buffer");
	zassert_equal(observed_bt_conn_recv_flags, 0xa5U,
		      "host receive must preserve flush-entry flags");
	zassert_equal(observed_k_work_flush_calls, 1, "scoped flush must forward exactly once");
	zassert_equal(observed_k_work_flush_work, &fake_flush_work,
		      "flush wrapper must preserve work pointer");
	zassert_equal(observed_k_work_flush_sync, &fake_flush_sync,
		      "flush wrapper must preserve sync pointer");
	zassert_true(observed_k_work_flush_result,
		     "flush wrapper must preserve true Boolean return");

	/* Semaphore-return snapshot must precede the real flush return. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "semaphore-return fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_work_flush_snapshot_before_return = true;
	fake_k_sem_take_result = 0;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x5aU);
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "semaphore return must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_entered, 0U,
		      "semaphore-return snapshot must advance beyond flush entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_entered, 0U,
		      "semaphore-return snapshot must advance beyond semaphore entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 1U,
		      "semaphore-return snapshot must contain one semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "semaphore-return snapshot must precede flush return");
	zassert_equal(observed_k_work_flush_work, &fake_flush_work,
		      "flush wrapper must preserve work pointer");
	zassert_equal(observed_k_work_flush_sync, &fake_flush_sync,
		      "flush wrapper must preserve sync pointer");
	zassert_true(observed_k_work_flush_result,
		     "flush wrapper must preserve true Boolean return");

	/* Flush-return snapshot must preserve the existing final flush stage. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "flush-return fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_bt_conn_recv_snapshot_after_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_work_flush_result = false;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x5aU);
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "flush return must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "flush-return snapshot must advance beyond semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 1U,
		      "flush-return snapshot must contain one flush return");
	zassert_false(observed_k_work_flush_result,
		      "flush wrapper must preserve false Boolean return");

	/* Foreign semaphore calls forward without an active receive/flush scope. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	fake_k_sem_take_result = -EIO;
	zassert_equal(__wrap_z_impl_k_sem_take(&fake_foreign_sem, K_FOREVER), -EIO,
		      "foreign semaphore result must be preserved");
	zassert_equal(observed_k_sem_take_calls, 1,
		      "foreign semaphore call must forward exactly once");
	zassert_equal(observed_k_sem_take_sem, &fake_foreign_sem,
		      "foreign semaphore pointer must be preserved");
	zassert_true(K_TIMEOUT_EQ(observed_k_sem_take_timeout, K_FOREVER),
		     "foreign semaphore timeout must be preserved");

	/* Wrong semaphore pointer forwards without changing the tracked stage. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "wrong-semaphore fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_bt_conn_recv_snapshot_after_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_wrong_sem = true;
	fake_k_sem_take_result = -EBUSY;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0U);
	zassert_equal(observed_k_sem_take_calls, 1,
		      "wrong semaphore call must forward exactly once");
	zassert_equal(observed_k_sem_take_sem, &fake_foreign_sem,
		      "wrong semaphore pointer must be preserved");
	zassert_true(K_TIMEOUT_EQ(observed_k_sem_take_timeout, K_FOREVER),
		     "wrong semaphore timeout must be preserved");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_entered, 0U,
		      "wrong semaphore must not mark semaphore entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "wrong semaphore must not mark semaphore return");

	/* Non-K_FOREVER timeout forwards without changing the tracked stage. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "non-forever fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_bt_conn_recv_snapshot_after_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_non_forever = true;
	fake_k_sem_take_result = -EAGAIN;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0U);
	zassert_equal(observed_k_sem_take_calls, 1,
		      "non-forever semaphore call must forward exactly once");
	zassert_equal(observed_k_sem_take_sem, &fake_flush_sync.flusher.sem,
		      "non-forever semaphore pointer must be preserved");
	zassert_true(K_TIMEOUT_EQ(observed_k_sem_take_timeout, K_NO_WAIT),
		     "non-forever semaphore timeout must be preserved");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_entered, 0U,
		      "non-forever semaphore must not mark semaphore entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "non-forever semaphore must not mark semaphore return");

	/* Fresh session proves post-return, callback, and undispatched states. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();

	for (size_t i = 0; i < 3U; i++) {
		init_lifetime_buffer(&lifetime_buffers[i], 1U);
		fake_rx_result = &lifetime_buffers[i];
		zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[i],
			      "tracked ISO allocation must return exact real pointer");
	}

	__wrap_bt_conn_recv(NULL, &lifetime_buffers[1], 0U);
	zassert_equal(observed_bt_conn_recv_calls, 1,
		      "host dispatch wrapper must forward second buffer once");
	zassert_is_null(observed_bt_conn_recv_conn,
			"host dispatch wrapper must forward exact NULL connection");
	zassert_equal(observed_bt_conn_recv_buf, &lifetime_buffers[1],
		      "host dispatch wrapper must forward exact second buffer");
	zassert_equal(observed_bt_conn_recv_flags, 0U,
		      "host dispatch wrapper must forward exact flags");

	fake_bt_conn_recv_invoke_callback = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[2], 0U);
	zassert_equal(observed_bt_conn_recv_calls, 2,
		      "host dispatch wrapper must forward third buffer once");
	zassert_equal(observed_bt_conn_recv_buf, &lifetime_buffers[2],
		      "host dispatch wrapper must forward exact third buffer");

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();

	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "disable must emit one lifetime snapshot");
	zassert_equal(observed_lifetime_disposition_calls, 1,
		      "disable must emit one disposition snapshot");
	zassert_equal(strcmp(observed_lifetime_disposition_reason, "disable"), 0,
		      "disposition snapshot reason must be exact");
	zassert_equal(observed_lifetime_disposition_undispatched, 1U,
		      "undispatched count must contain first buffer");
	zassert_equal(observed_lifetime_disposition_host_dispatched, 0U,
		      "host-dispatched count must be empty after host return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_entered, 0U,
		      "fresh no-flush fixture must have no flush entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "fresh no-flush fixture must have no flush return");
	zassert_equal(observed_lifetime_disposition_host_returned, 1U,
		      "host-returned count must contain second buffer");
	zassert_equal(observed_lifetime_disposition_app_callback_seen, 1U,
		      "app-callback count must contain third buffer");
	zassert_equal(observed_lifetime_disposition_unclassified, 0U,
		      "unclassified count must be empty for staged buffers");
	zassert_equal(observed_lifetime_snapshot_callbacks_active, 0U,
		      "callback exit must clear active callback count");
	zassert_equal(observed_lifetime_snapshot_callbacks_total, 1U,
		      "real host receive stub must invoke callback for third buffer");
	zassert_equal(observed_lifetime_final_unref_calls, 0,
		      "disposition fixture must not observe final unrefs");
	zassert_equal(observed_lifetime_error_calls, 0,
		      "disposition fixture must not disable tracking");

	/* Fresh session proves foreign flush cannot alter tracked lifetime stage. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "foreign-flush fixture must track exact allocation");

	fake_k_work_flush_result = true;
	zassert_true(__wrap_k_work_flush(&fake_flush_work, &fake_flush_sync),
		     "foreign flush call must preserve true Boolean return");
	zassert_equal(observed_k_work_flush_calls, 1,
		      "foreign flush call must forward exactly once");
	zassert_equal(observed_k_work_flush_work, &fake_flush_work,
		      "foreign flush call must preserve work pointer");
	zassert_equal(observed_k_work_flush_sync, &fake_flush_sync,
		      "foreign flush call must preserve sync pointer");

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();

	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "foreign flush fixture must emit one lifetime snapshot");
	zassert_equal(observed_lifetime_disposition_calls, 1,
		      "foreign flush fixture must emit one disposition snapshot");
	zassert_equal(strcmp(observed_lifetime_disposition_reason, "disable"), 0,
		      "foreign flush disposition reason must be exact");
	zassert_equal(observed_lifetime_disposition_undispatched, 1U,
		      "foreign flush must leave tracked buffer undispatched");
	zassert_equal(observed_lifetime_disposition_host_dispatched, 0U,
		      "foreign flush must not mark host dispatch");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_entered, 0U,
		      "foreign flush must not mark flush entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "foreign flush must not mark flush return");
	zassert_equal(observed_lifetime_disposition_host_returned, 0U,
		      "foreign flush must not mark host return");
	zassert_equal(observed_lifetime_disposition_app_callback_seen, 0U,
		      "foreign flush must not mark app callback");
	zassert_equal(observed_lifetime_disposition_unclassified, 0U,
		      "foreign flush fixture must not classify buffer as unclassified");
	zassert_equal(observed_lifetime_error_calls, 0,
		      "foreign flush fixture must not disable tracking");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_pend_entry_snapshot_is_scoped)
{
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "pend-entry fixture must track exact allocation");

	memset(&fake_pend_key, 0x5a, sizeof(fake_pend_key));
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_pend_invoke_user_hook = true;
	fake_pend_hook_thread = k_current_get();
	fake_pend_snapshot_before_return = true;
	fake_pend_result = -EAGAIN;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x33U);

	zassert_equal(observed_pend_calls, 1, "matching pend must forward exactly once");
	zassert_equal(observed_pend_lock, &fake_pend_lock,
		      "matching pend must preserve exact lock pointer");
	zassert_mem_equal(&observed_pend_key, &fake_pend_key, sizeof(fake_pend_key),
			  "matching pend must preserve exact spinlock key");
	zassert_equal(observed_pend_wait_q, &fake_flush_sync.flusher.sem.wait_q,
		      "matching pend must use exact flusher semaphore wait queue");
	zassert_true(K_TIMEOUT_EQ(observed_pend_timeout, K_FOREVER),
		     "matching pend must preserve K_FOREVER timeout");
	zassert_equal(observed_pend_wrapper_result, -EAGAIN,
		      "matching pend must preserve fake real result");
	zassert_equal(observed_k_sem_take_wrapper_result, -EAGAIN,
		      "semaphore wrapper must preserve matching pend result");
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "pend entry must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 0U,
		      "scheduler hook snapshot must advance beyond pend entry");
	zassert_equal(
		observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending,
		1U, "scheduler hook snapshot must contain one marked-pending stage");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered, 0U,
		      "pend-entry snapshot must contain no give entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 0U,
		      "pend-entry snapshot must precede pend return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "pend-entry snapshot must precede semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "pend-entry snapshot must precede flush return");
	zassert_equal(observed_k_sem_give_calls, 0,
		      "pend-entry fixture must not call fake semaphore give");

	/* The exact captured flusher semaphore give is observed before the fake pend returns. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "give-entry fixture must track exact allocation");
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_pend_invoke_user_hook = true;
	fake_pend_hook_thread = k_current_get();
	fake_pend_invoke_give = true;
	fake_pend_snapshot_before_return = true;
	fake_pend_result = -EAGAIN;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x34U);

	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "give entry must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 0U,
		      "give-entry snapshot must advance beyond pend entry");
	zassert_equal(
		observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending,
		0U, "give-entry snapshot must advance beyond marked-pending stage");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered, 1U,
		      "give-entry snapshot must contain one give entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 0U,
		      "give-entry snapshot must precede pend return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "give-entry snapshot must precede semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "give-entry snapshot must precede flush return");
	zassert_equal(observed_lifetime_disposition_host_returned, 0U,
		      "give-entry snapshot must precede outer receive return");
	zassert_equal(observed_k_sem_give_calls, 1, "matching give must forward exactly once");
	zassert_equal(observed_k_sem_give_sem, &fake_flush_sync.flusher.sem,
		      "matching give must preserve exact flusher semaphore pointer");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_pend_return_snapshot_is_scoped)
{
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	zassert_equal(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT), &lifetime_buffers[0],
		      "pend-return fixture must track exact allocation");

	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_k_sem_take_snapshot_after_pend = true;
	fake_pend_result = -EIO;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x44U);

	zassert_equal(observed_pend_calls, 1, "matching pend return must forward exactly once");
	zassert_equal(observed_pend_wrapper_result, -EIO,
		      "matching pend return must preserve fake real result");
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "pend return must trigger one bounded snapshot");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 0U,
		      "pend-return snapshot must advance beyond pend entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 1U,
		      "pend-return snapshot must contain one pend return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_returned, 0U,
		      "pend-return snapshot must precede semaphore return");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_returned, 0U,
		      "pend-return snapshot must precede flush return");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_pend_rejects_foreign_scope_and_timeout)
{
	memset(&fake_pend_key, 0xa5, sizeof(fake_pend_key));

	/* No active receive scope forwards a foreign wait queue untouched. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	fake_pend_result = -EBUSY;
	zassert_equal(
		__wrap_z_pend_curr(&fake_pend_lock, fake_pend_key, &fake_foreign_wait_q, K_FOREVER),
		-EBUSY, "unscoped pend must preserve fake real result");
	zassert_equal(observed_pend_calls, 1, "unscoped pend must forward exactly once");
	zassert_equal(observed_pend_lock, &fake_pend_lock,
		      "unscoped pend must preserve exact lock pointer");
	zassert_mem_equal(&observed_pend_key, &fake_pend_key, sizeof(fake_pend_key),
			  "unscoped pend must preserve exact spinlock key");
	zassert_equal(observed_pend_wait_q, &fake_foreign_wait_q,
		      "unscoped pend must preserve foreign wait queue");
	zassert_true(K_TIMEOUT_EQ(observed_pend_timeout, K_FOREVER),
		     "unscoped pend must preserve K_FOREVER timeout");
	zassert_equal(observed_lifetime_snapshot_calls, 0,
		      "unscoped pend must not emit a lifetime snapshot");
	/* Unarmed scheduler hook and unscoped give forward without H39 stage changes. */
	sys_trace_thread_pend_user(k_current_get());
	__wrap_z_impl_k_sem_give(&fake_foreign_sem);
	zassert_equal(observed_k_sem_give_calls, 1, "unscoped give must forward exactly once");
	zassert_equal(observed_k_sem_give_sem, &fake_foreign_sem,
		      "unscoped give must preserve foreign semaphore pointer");
	zassert_equal(observed_lifetime_disposition_calls, 0,
		      "unarmed hook and unscoped give must not emit a disposition snapshot");

	/* A foreign wait queue remains outside H38 while H37 semaphore scope matches. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_k_sem_take_wrong_wait_q = true;
	fake_k_work_flush_snapshot_before_return = true;
	fake_pend_result = -EIO;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0U);
	zassert_equal(observed_pend_lock, &fake_pend_lock,
		      "foreign wait queue call must preserve exact lock pointer");
	zassert_mem_equal(&observed_pend_key, &fake_pend_key, sizeof(fake_pend_key),
			  "foreign wait queue call must preserve exact spinlock key");
	zassert_equal(observed_pend_wait_q, &fake_foreign_wait_q,
		      "foreign wait queue must be forwarded exactly");
	zassert_true(K_TIMEOUT_EQ(observed_pend_timeout, K_FOREVER),
		     "foreign wait queue call must preserve K_FOREVER timeout");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 0U,
		      "foreign wait queue must not mark pend entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 0U,
		      "foreign wait queue must not mark pend return");

	/* A non-forever call remains outside H38 even with exact semaphore scope. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_k_sem_take_non_forever = true;
	fake_k_work_flush_snapshot_before_return = true;
	fake_pend_result = -EAGAIN;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0U);
	zassert_equal(observed_pend_lock, &fake_pend_lock,
		      "non-forever call must preserve exact lock pointer");
	zassert_mem_equal(&observed_pend_key, &fake_pend_key, sizeof(fake_pend_key),
			  "non-forever call must preserve exact spinlock key");
	zassert_equal(observed_pend_wait_q, &fake_flush_sync.flusher.sem.wait_q,
		      "non-forever call must preserve exact wait queue");
	zassert_true(K_TIMEOUT_EQ(observed_pend_timeout, K_NO_WAIT),
		     "non-forever call must preserve exact timeout");
	zassert_equal(observed_pend_wrapper_result, -EAGAIN,
		      "non-forever call must preserve fake real result");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 0U,
		      "non-forever call must not mark pend entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 0U,
		      "non-forever call must not mark pend return");

	/* A wrong scheduler-hook thread forwards the exact pend call without H39 progress. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_pend_invoke_user_hook = true;
	fake_pend_hook_thread = &fake_switch_foreign_thread;
	fake_pend_snapshot_before_return = true;
	fake_pend_result = -EIO;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x55U);
	zassert_equal(observed_pend_calls, 1, "wrong-hook pend must forward exactly once");
	zassert_equal(observed_pend_wait_q, &fake_flush_sync.flusher.sem.wait_q,
		      "wrong-hook pend must preserve exact wait queue");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_entered, 1U,
		      "wrong hook must retain pend-entry stage");
	zassert_equal(
		observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending,
		0U, "wrong hook must not mark pending");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered, 0U,
		      "wrong hook fixture must not mark give entry");

	/* A wrong give semaphore forwards from an exact pending scope without H39 give progress. */
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);
	fake_rx_result = &lifetime_buffers[0];
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	fake_bt_conn_recv_invoke_flush = true;
	fake_k_work_flush_invoke_sem = true;
	fake_k_sem_take_invoke_pend = true;
	fake_pend_invoke_user_hook = true;
	fake_pend_hook_thread = k_current_get();
	fake_pend_invoke_give = true;
	fake_pend_give_wrong_sem = true;
	fake_pend_snapshot_before_return = true;
	fake_pend_result = -EIO;
	fake_k_work_flush_result = true;
	__wrap_bt_conn_recv(NULL, &lifetime_buffers[0], 0x56U);
	zassert_equal(observed_k_sem_give_calls, 1, "wrong give must forward exactly once");
	zassert_equal(observed_k_sem_give_sem, &fake_foreign_sem,
		      "wrong give must preserve foreign semaphore pointer");
	zassert_equal(
		observed_lifetime_disposition_tx_notify_flush_semaphore_pend_thread_marked_pending,
		1U, "wrong give fixture must retain scheduler-hook stage");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_give_entered, 0U,
		      "wrong give must not mark give entry");
	zassert_equal(observed_lifetime_disposition_tx_notify_flush_semaphore_pend_returned, 0U,
		      "wrong give fixture must precede pend return");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_iso_rx_lifetime_unarmed_unref_forwards_without_tracking)
{
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	init_lifetime_buffer(&lifetime_buffers[0], 1U);

	__wrap_net_buf_unref(&lifetime_buffers[0]);
	zassert_equal(observed_net_buf_unref_calls, 1, "unarmed unref must forward exactly once");
	zassert_equal(observed_net_buf_unref, &lifetime_buffers[0],
		      "unarmed unref must forward exact pointer");
	zassert_equal(lifetime_buffers[0].ref, 0U, "real unref must decrement fake refcount");
	zassert_equal(observed_lifetime_arm_calls, 0,
		      "unarmed unref must not emit an arm observation");
	zassert_equal(observed_lifetime_snapshot_calls, 0,
		      "unarmed unref must not emit a snapshot");
	zassert_equal(observed_lifetime_first_free_calls, 0,
		      "unarmed unref must not emit a first-free observation");
	zassert_equal(observed_lifetime_final_unref_calls, 0,
		      "unarmed unref must not emit a final-unref observation");
	zassert_equal(observed_lifetime_error_calls, 0,
		      "unarmed unref must not emit a tracking error");
}

ZTEST(sdc_hci_remove_iso_path_trace,
      test_iso_rx_lifetime_full_fixture_reports_unavailable_and_first_free_once)
{
	sdc_hci_remove_iso_path_trace_test_reset();
	reset_lifetime_observations();
	arm_receive_target_work();
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open();

	for (size_t i = 0; i < 3U; i++) {
		init_lifetime_buffer(&lifetime_buffers[i], 1U);
		fake_rx_result = &lifetime_buffers[i];
		struct net_buf *result = __wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
		zassert_equal(result, &lifetime_buffers[i],
			      "full-fixture ISO allocation must return exact real pointer");
	}

	arm_receive_target_work_preserve_lifetime();
	fake_rx_result = NULL;
	zassert_is_null(__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT),
			"full fixture allocation must return real NULL");
	zassert_equal(observed_lifetime_snapshot_calls, 1,
		      "unavailable allocation must emit one snapshot");
	zassert_equal(strcmp(observed_lifetime_snapshot_reason, "unavailable"), 0,
		      "unavailable snapshot reason must be exact");
	zassert_equal(observed_lifetime_snapshot_capacity, 3U,
		      "unavailable snapshot capacity must match fixture");
	zassert_equal(observed_lifetime_snapshot_outstanding, 3U,
		      "unavailable snapshot must report full occupancy");
	zassert_equal(observed_lifetime_snapshot_high_water, 3U,
		      "unavailable snapshot must report full high water");
	zassert_equal(observed_lifetime_snapshot_allocations, 3U,
		      "unavailable snapshot must report three allocations");
	zassert_equal(observed_lifetime_snapshot_final_unrefs, 0U,
		      "unavailable snapshot must precede final unrefs");

	__wrap_net_buf_unref(&lifetime_buffers[0]);
	zassert_equal(observed_lifetime_first_free_calls, 1,
		      "first tracked final release must emit one first-free observation");
	zassert_equal(observed_lifetime_first_free_outstanding_before, 3U,
		      "first-free observation must retain pre-call occupancy");
	zassert_equal(observed_lifetime_first_free_allocations, 3U,
		      "first-free observation must retain allocation count");
	zassert_equal(observed_lifetime_first_free_final_unrefs, 1U,
		      "first-free observation must report first final unref");

	__wrap_net_buf_unref(&lifetime_buffers[1]);
	__wrap_net_buf_unref(&lifetime_buffers[2]);
	zassert_equal(observed_lifetime_first_free_calls, 1,
		      "later tracked final releases must emit no second first-free marker");
	zassert_equal(observed_lifetime_final_unref_calls, 3,
		      "all tracked final releases must update final state");
	zassert_equal(observed_lifetime_final_unref_outstanding, 0U,
		      "all tracked final releases must drain occupancy");
	zassert_equal(observed_lifetime_final_unref_count, 3U,
		      "all tracked final releases must count exactly once");

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot();
	zassert_equal(observed_lifetime_snapshot_calls, 2,
		      "session must emit only unavailable and disable snapshots");
	zassert_equal(strcmp(observed_lifetime_snapshot_reason, "disable"), 0,
		      "final snapshot reason must be disable");
	zassert_equal(observed_lifetime_snapshot_outstanding, 0U,
		      "disable snapshot must report drained occupancy");
	zassert_equal(observed_lifetime_snapshot_high_water, 3U,
		      "disable snapshot must retain high water");
	zassert_equal(observed_net_buf_unref_calls, 3,
		      "every tracked final unref must forward exactly once");
	zassert_equal(observed_lifetime_arm_calls, 1, "full fixture must emit one arm observation");
	zassert_equal(observed_lifetime_error_calls, 0, "full fixture must not disable tracking");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_forward_preserves_pointers_and_status)
{
	sdc_hci_cmd_le_remove_iso_data_path_t params = {0};
	sdc_hci_cmd_le_remove_iso_data_path_return_t result = {0};

	fake_status = 0U;
	observed_params = NULL;
	observed_return = NULL;
	zassert_equal(__wrap_sdc_hci_cmd_le_remove_iso_data_path(&params, &result), 0U,
		      "successful SDC status must be preserved");
	zassert_equal(observed_params, &params, "parameter pointer must be forwarded");
	zassert_equal(observed_return, &result, "return pointer must be forwarded");

	fake_status = 0x0cU;
	observed_params = NULL;
	observed_return = NULL;
	zassert_equal(__wrap_sdc_hci_cmd_le_remove_iso_data_path(&params, &result), 0x0cU,
		      "nonzero SDC status must be preserved");
	zassert_equal(observed_params, &params, "parameter pointer must be forwarded");
	zassert_equal(observed_return, &result, "return pointer must be forwarded");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_completion_fetch_forwards_success_and_errno)
{
	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;

	fake_msg[0] = BT_HCI_EVT_CMD_COMPLETE;
	fake_msg[1] = 4U;
	fake_msg[2] = 1U;
	fake_msg[3] = 0x6fU;
	fake_msg[4] = 0x20U;
	fake_msg[5] = 0U;
	fake_msg_type = SDC_HCI_MSG_TYPE_EVT;
	fake_msg_get_status = 0;
	observed_msg_out = NULL;
	observed_msg_type_out = NULL;

	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), 0,
		      "successful HCI fetch status must be preserved");
	zassert_equal(observed_msg_out, msg_out, "message pointer must be forwarded");
	zassert_equal(observed_msg_type_out, &msg_type_out,
		      "message type pointer must be forwarded");
	zassert_mem_equal(msg_out, fake_msg, sizeof(fake_msg),
			  "synthetic Command Complete must be forwarded");
	zassert_equal(msg_type_out, SDC_HCI_MSG_TYPE_EVT,
		      "synthetic message type must be forwarded");

	fake_msg_get_status = -EIO;
	observed_msg_out = NULL;
	observed_msg_type_out = NULL;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), -EIO,
		      "negative HCI fetch status must be preserved");
	zassert_equal(observed_msg_out, msg_out, "message pointer must be forwarded");
	zassert_equal(observed_msg_type_out, &msg_type_out,
		      "message type pointer must be forwarded");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_iso_allocation_is_scoped_once)
{
	arm_receive_target_work();
	fake_rx_result = NULL;

	k_timeout_t timeout = K_NO_WAIT;
	struct net_buf *result = __wrap_bt_buf_get_rx(BT_BUF_ISO_IN, timeout);

	zassert_is_null(result, "fake ISO allocation must return NULL");
	zassert_equal(observed_rx_calls, 1, "ISO allocation must forward one call");
	zassert_equal(observed_rx_type, BT_BUF_ISO_IN,
		      "ISO allocation type must be forwarded unchanged");
	zassert_equal(observed_rx_timeout.ticks, timeout.ticks,
		      "ISO allocation timeout must be forwarded unchanged");
	zassert_equal(observed_receive_rx_calls, 1,
		      "target ISO allocation must emit one scoped record");
	zassert_equal(observed_receive_rx_type, BT_BUF_ISO_IN,
		      "scoped ISO allocation must retain numeric buffer type");
	zassert_false(observed_receive_rx_available,
		      "NULL ISO allocation must report unavailable buffer");
	zassert_equal(observed_receive_rx_busy, K_WORK_RUNNING | K_WORK_QUEUED,
		      "scoped ISO allocation must retain running target busy flags");

	result = __wrap_bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
	zassert_is_null(result, "second fake ISO allocation must return NULL");
	zassert_equal(observed_rx_calls, 2, "second ISO allocation must still forward");
	zassert_equal(observed_receive_rx_calls, 1,
		      "one target work must emit at most one allocation record");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_fetch_forwards_and_records_result)
{
	arm_receive_target_work();
	fake_msg_get_status = 0;
	fake_msg_type = SDC_HCI_MSG_TYPE_ISO;

	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;
	int status = __wrap_hci_internal_msg_get(msg_out, &msg_type_out);

	zassert_equal(status, 0, "target fetch status must be forwarded");
	zassert_equal(observed_msg_out, msg_out, "target fetch message pointer must be forwarded");
	zassert_equal(observed_msg_type_out, &msg_type_out,
		      "target fetch type pointer must be forwarded");
	zassert_equal(msg_type_out, SDC_HCI_MSG_TYPE_ISO,
		      "target fetch message type must be forwarded");
	zassert_equal(observed_receive_fetch_entry_calls, 1,
		      "target fetch must emit one scoped entry");
	zassert_equal(observed_receive_fetch_return_calls, 1,
		      "target fetch must emit one scoped return");
	zassert_equal(observed_receive_fetch_status, 0, "scoped fetch return must retain status");
	zassert_true(observed_receive_fetch_msg_type_valid,
		     "successful scoped fetch must retain message type");
	zassert_equal(observed_receive_fetch_msg_type, SDC_HCI_MSG_TYPE_ISO,
		      "scoped fetch return must retain numeric message type");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_fetch_error_clears_scope)
{
	arm_receive_target_work();
	fake_msg_get_status = -EIO;

	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), -EIO,
		      "target fetch error status must be forwarded");
	zassert_equal(observed_receive_fetch_entry_calls, 1,
		      "target fetch error must emit one scoped entry");
	zassert_equal(observed_receive_fetch_return_calls, 1,
		      "target fetch error must emit one scoped return");
	zassert_equal(observed_receive_fetch_status, -EIO, "scoped fetch error must retain status");

	fake_rx_result = NULL;
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	zassert_equal(observed_rx_calls, 1, "post-error RX allocation must still forward once");
	zassert_equal(observed_receive_rx_calls, 0,
		      "fetch error must clear target allocation scope");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_unsupported_fetch_clears_scope)
{
	arm_receive_target_work();
	fake_msg_get_status = 0;
	fake_msg_type = SDC_HCI_MSG_TYPE_NONE;

	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), 0,
		      "unsupported fetch status must be forwarded");
	zassert_equal(observed_receive_fetch_entry_calls, 1,
		      "unsupported fetch must emit one scoped entry");
	zassert_equal(observed_receive_fetch_return_calls, 1,
		      "unsupported fetch must emit one scoped return");
	zassert_true(observed_receive_fetch_msg_type_valid,
		     "unsupported fetch must retain valid message type");

	fake_rx_result = NULL;
	(void)__wrap_bt_buf_get_rx(BT_BUF_ISO_IN, K_NO_WAIT);
	zassert_equal(observed_rx_calls, 1,
		      "post-unsupported RX allocation must still forward once");
	zassert_equal(observed_receive_rx_calls, 0,
		      "unsupported fetch must clear target allocation scope");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_event_does_not_double_record_inner_rx)
{
	arm_receive_target_work();
	fake_evt_result = NULL;
	fake_evt_invoke_rx = true;
	fake_rx_result = NULL;

	k_timeout_t timeout = K_NO_WAIT;
	struct net_buf *result = __wrap_bt_buf_get_evt(BT_HCI_EVT_CMD_COMPLETE, true, timeout);

	zassert_is_null(result, "fake event allocation must return NULL");
	zassert_equal(observed_evt_calls, 1, "event allocation must forward one outer call");
	zassert_equal(observed_evt, BT_HCI_EVT_CMD_COMPLETE,
		      "event code must be forwarded unchanged");
	zassert_true(observed_evt_discardable, "event discardability must be forwarded unchanged");
	zassert_equal(observed_evt_timeout.ticks, timeout.ticks,
		      "event timeout must be forwarded unchanged");
	zassert_equal(observed_rx_calls, 1, "event fake must exercise one inner RX allocation");
	zassert_equal(observed_rx_type, BT_BUF_EVT, "inner event RX type must remain event type");
	zassert_equal(observed_receive_evt_calls, 1,
		      "outer event must emit one scoped allocation record");
	zassert_equal(observed_receive_evt, BT_HCI_EVT_CMD_COMPLETE,
		      "scoped event record must retain event code");
	zassert_true(observed_receive_evt_discardable,
		     "scoped event record must retain discardability");
	zassert_false(observed_receive_evt_available,
		      "NULL outer event allocation must report unavailable buffer");
	zassert_equal(observed_receive_rx_calls, 0,
		      "inner wrapped RX must not emit duplicate scoped record");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_receive_disposition_outside_target_context_only_forwards)
{
	arm_receive_target_work();
	fake_rx_result = (struct net_buf *)&fake_rx_storage;
	fake_receive_current_thread = &fake_switch_foreign_thread;

	k_timeout_t timeout = K_NO_WAIT;
	struct net_buf *result = __wrap_bt_buf_get_rx(BT_BUF_ISO_IN, timeout);

	zassert_equal(result, (struct net_buf *)&fake_rx_storage,
		      "foreign-context RX call must return exact real result");
	zassert_equal(observed_rx_calls, 1, "foreign-context RX call must forward once");
	zassert_equal(observed_rx_type, BT_BUF_ISO_IN, "foreign-context RX type must be unchanged");
	zassert_equal(observed_receive_rx_calls, 0,
		      "foreign-context RX call must emit no scoped record");

	fake_msg_get_status = -EIO;
	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), -EIO,
		      "foreign-context fetch must forward status unchanged");
	zassert_equal(observed_receive_fetch_entry_calls, 0,
		      "foreign-context fetch must emit no scoped entry");
	zassert_equal(observed_receive_fetch_return_calls, 0,
		      "foreign-context fetch must emit no scoped return");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_cmd_put_forwards_target_pointer_and_status)
{
	uint8_t cmd_in[] = {0x6fU, 0x20U};
	uint8_t msg_out[sizeof(fake_msg)] = {0};
	sdc_hci_msg_type_t msg_type_out = SDC_HCI_MSG_TYPE_NONE;

	sdc_hci_remove_iso_path_trace_test_reset();
	fake_cmd_put_status = 0;
	observed_cmd_in = NULL;
	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), 0,
		      "successful hci_internal_cmd_put status must be preserved");
	zassert_equal(observed_cmd_in, cmd_in, "command pointer must be forwarded");

	observed_lock_release_calls = 0;
	__wrap_multithreading_lock_release();
	zassert_equal(observed_lock_release_calls, 1,
		      "target sequence must forward lock release once");

	fake_msg[0] = BT_HCI_EVT_CMD_COMPLETE;
	fake_msg[1] = 4U;
	fake_msg[2] = 1U;
	fake_msg[3] = 0x6fU;
	fake_msg[4] = 0x20U;
	fake_msg[5] = 0U;
	fake_msg_type = SDC_HCI_MSG_TYPE_EVT;
	fake_msg_get_status = 0;
	fake_work_submit_status = 1;
	fake_work_submit_invoke_msg_get = true;
	fake_snapshot_schedule_status = 1;
	fake_snapshot_busy = K_WORK_QUEUED;
	fake_snapshot_state = "PENDING";
	observed_snapshot_schedule_calls = 0;
	observed_snapshot_target = NULL;
	observed_snapshot_queue = NULL;
	observed_snapshot_busy_work = NULL;
	observed_snapshot_state_thread = NULL;
	observed_snapshot_busy = 0;
	observed_snapshot_state[0] = '\0';
	observed_work_queue = NULL;
	observed_work = NULL;
	observed_work_submit_calls = 0;
	observed_msg_get_calls = 0;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), 1,
		      "successful work-submit status must be preserved");
	zassert_equal(observed_work_queue, &fake_work_queue,
		      "work queue pointer must be forwarded");
	zassert_equal(observed_work, &fake_work, "work pointer must be forwarded");
	zassert_equal(observed_work_submit_calls, 1,
		      "target sequence must forward work submit once");
	zassert_equal(observed_msg_get_calls, 1, "target sequence must reach one msg_get");
	sdc_hci_remove_iso_path_trace_test_run_snapshot();
	zassert_equal(observed_snapshot_schedule_calls, 1,
		      "successful target submit must request one snapshot");
	zassert_equal(observed_snapshot_target, &fake_work,
		      "snapshot request must use submitted work item");
	zassert_equal(observed_snapshot_busy_work, &fake_work,
		      "snapshot busy read must use submitted work item");
	zassert_equal(observed_snapshot_queue, &mpsl_work_q,
		      "snapshot worker read must use MPSL queue");
	zassert_equal(observed_snapshot_state_thread, &fake_snapshot_thread,
		      "snapshot state read must use MPSL worker thread");
	zassert_equal(observed_snapshot_busy, K_WORK_QUEUED,
		      "snapshot must retain observed queued work state");
	zassert_equal(strcmp(observed_snapshot_state, "PENDING"), 0,
		      "snapshot must retain observed worker state");

	fake_cmd_put_status = 0;
	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), 0,
		      "second target command status must be preserved");
	__wrap_multithreading_lock_release();
	fake_work_submit_status = 2;
	fake_snapshot_schedule_status = -EIO;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), 2,
		      "scheduling error must not alter original work-submit status");
	zassert_equal(observed_snapshot_schedule_calls, 2,
		      "second nonnegative target submit must request one snapshot");
	zassert_equal(observed_snapshot_target, &fake_work,
		      "failed snapshot request must retain submitted work item");

	fake_cmd_put_status = 0;
	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), 0,
		      "third target command status must be preserved");
	__wrap_multithreading_lock_release();
	fake_work_submit_status = -EIO;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), -EIO,
		      "negative work-submit status must be preserved");
	zassert_equal(observed_snapshot_schedule_calls, 2,
		      "negative work submit must not request a snapshot");

	fake_work_submit_invoke_msg_get = false;
	fake_work_submit_status = 0;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), 0,
		      "zero work-submit status must be preserved");
	fake_work_submit_status = 2;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), 2,
		      "queued-running work-submit status must be preserved");
	fake_work_submit_status = -EIO;
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), -EIO,
		      "negative work-submit status must be preserved");

	observed_msg_out = NULL;
	observed_msg_type_out = NULL;
	fake_msg_get_status = 0;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), 0,
		      "successful hci_internal_msg_get status must be preserved");
	zassert_equal(observed_msg_out, msg_out, "message pointer must be forwarded");
	zassert_equal(observed_msg_type_out, &msg_type_out,
		      "message type pointer must be forwarded");
	zassert_mem_equal(msg_out, fake_msg, sizeof(fake_msg),
			  "synthetic message must be forwarded after target cmd_put");
	zassert_equal(msg_type_out, SDC_HCI_MSG_TYPE_EVT,
		      "synthetic message type must be forwarded after target cmd_put");

	fake_msg_get_status = -EIO;
	observed_msg_out = NULL;
	observed_msg_type_out = NULL;
	zassert_equal(__wrap_hci_internal_msg_get(msg_out, &msg_type_out), -EIO,
		      "negative hci_internal_msg_get status must be preserved");
	zassert_equal(observed_msg_out, msg_out, "message pointer must be forwarded");
	zassert_equal(observed_msg_type_out, &msg_type_out,
		      "message type pointer must be forwarded");

	fake_cmd_put_status = -EIO;
	observed_cmd_in = NULL;
	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), -EIO,
		      "negative hci_internal_cmd_put status must be preserved");
	zassert_equal(observed_cmd_in, cmd_in, "command pointer must be forwarded");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_scheduler_unlock_probe_records_switch_in_window)
{
	static struct k_thread sender_thread;
	static struct k_thread foreign_thread;
	uint8_t cmd_in[] = {0x6fU, 0x20U};

	sdc_hci_remove_iso_path_trace_test_reset();
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	observed_sched_unlock_calls = 0;
	observed_sched_unlock_entries = 0;
	observed_sched_unlock_returns = 0;
	observed_sched_unlock_real_order = 0;
	observed_sched_unlock_return_order = 0;
	scheduler_observation_order = 0;
	observed_scheduler_armed_calls = 0;
	observed_scheduler_queue_is_mpsl = false;
	observed_scheduler_busy_calls = 0;
	observed_scheduler_busy_work = NULL;
	observed_scheduler_queue_calls = 0;
	observed_scheduler_queue = NULL;
	observed_scheduler_state_str_calls = 0;
	observed_scheduler_state_thread = NULL;
	observed_scheduler_state_calls = 0;
	observed_scheduler_state_order = 0;
	observed_scheduler_busy = 0;
	observed_scheduler_state[0] = '\0';
	observed_scheduler_resumed_current_is_sender = false;
	observed_scheduler_mpsl_switches_during_unlock = 0U;
	observed_scheduler_sender_priority = 0;
	observed_scheduler_resumed_current_priority = 0;
	observed_scheduler_mpsl_priority = 0;
	observed_scheduler_priority_calls = 0;
	memset(observed_scheduler_priority_threads, 0, sizeof(observed_scheduler_priority_threads));
	memset(observed_scheduler_priority_values, 0, sizeof(observed_scheduler_priority_values));
	fake_scheduler_busy = K_WORK_QUEUED;
	fake_scheduler_state = "PENDING";
	fake_scheduler_queue_thread = &fake_scheduler_thread;
	fake_scheduler_sender_priority = -10;
	fake_scheduler_mpsl_priority = -6;
	fake_current_thread = &sender_thread;
	fake_switch_in_thread = NULL;
	fake_resumed_thread = &sender_thread;
	fake_real_unlock_switch_mpsl = false;
	fake_cmd_put_status = 0;
	fake_work_submit_status = 1;
	fake_work_submit_invoke_msg_get = false;

	zassert_equal(__wrap_hci_internal_cmd_put(cmd_in), 0,
		      "target command status must be preserved before scheduler trace");
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), 1,
		      "target work-submit status must be preserved before scheduler trace");

	/* Switch-ins before the exact wrapped unlock window are ignored. */
	fake_switch_in_thread = &fake_scheduler_thread;
	sys_trace_thread_switched_in_user();
	fake_switch_in_thread = NULL;

	fake_current_thread = &foreign_thread;
	__wrap_k_sched_unlock();
	zassert_equal(observed_sched_unlock_calls, 1,
		      "foreign scheduler unlock must forward exactly once");
	zassert_equal(observed_sched_unlock_entries, 0,
		      "foreign scheduler unlock must not consume sender ownership");
	zassert_equal(observed_sched_unlock_returns, 0,
		      "foreign scheduler unlock must not emit a return observation");
	zassert_equal(observed_scheduler_state_calls, 0,
		      "foreign scheduler unlock must not emit target state observation");
	zassert_equal(observed_scheduler_priority_calls, 0,
		      "foreign scheduler unlock must not query priorities");

	fake_current_thread = &sender_thread;
	fake_real_unlock_switch_mpsl = true;
	__wrap_k_sched_unlock();
	zassert_equal(observed_sched_unlock_calls, 2,
		      "recorded sender scheduler unlock must forward exactly once");
	zassert_equal(observed_sched_unlock_entries, 1,
		      "recorded sender must emit one scheduler unlock entry");
	zassert_equal(observed_sched_unlock_returns, 1,
		      "recorded sender must emit one scheduler unlock return");
	zassert_equal(observed_scheduler_armed_calls, 1,
		      "target submit must emit one scheduler arm observation");
	zassert_true(observed_scheduler_queue_is_mpsl,
		     "MPSL queue identity must be true for the MPSL queue");
	zassert_equal(observed_scheduler_busy_calls, 1, "target state must read work busy once");
	zassert_equal(observed_scheduler_busy_work, &fake_work,
		      "target state busy read must use submitted work item");
	zassert_equal(observed_scheduler_queue_calls, 1,
		      "target arm must read one MPSL worker queue");
	zassert_equal(observed_scheduler_queue, &mpsl_work_q,
		      "target arm must use MPSL work queue");
	zassert_equal(observed_scheduler_state_str_calls, 1,
		      "target state must read one MPSL worker state");
	zassert_equal(observed_scheduler_state_thread, &fake_scheduler_thread,
		      "target state must use MPSL worker thread");
	zassert_equal(observed_scheduler_state_calls, 1,
		      "recorded sender must emit one target state observation");
	zassert_equal(observed_scheduler_busy, K_WORK_QUEUED,
		      "target state observation must retain fake busy state");
	zassert_equal(strcmp(observed_scheduler_state, "PENDING"), 0,
		      "target state observation must retain fake MPSL state");
	zassert_true(observed_scheduler_resumed_current_is_sender,
		     "post-unlock current thread must be the claimed sender");
	zassert_equal(observed_scheduler_mpsl_switches_during_unlock, 1U,
		      "MPSL switch-in during real unlock must be observed once");
	zassert_equal(observed_scheduler_priority_calls, 3,
		      "target state must read sender, resumed current, and MPSL priorities once");
	zassert_equal(observed_scheduler_priority_threads[0], &sender_thread,
		      "sender priority lookup must use claimed sender");
	zassert_equal(observed_scheduler_priority_threads[1], &sender_thread,
		      "resumed current priority lookup must use post-unlock current thread");
	zassert_equal(observed_scheduler_priority_threads[2], &fake_scheduler_thread,
		      "MPSL priority lookup must use MPSL worker thread");
	zassert_equal(observed_scheduler_priority_values[0], fake_scheduler_sender_priority,
		      "sender priority must retain fake public lookup value");
	zassert_equal(observed_scheduler_priority_values[1], fake_scheduler_sender_priority,
		      "current priority must retain fake public lookup value");
	zassert_equal(observed_scheduler_priority_values[2], fake_scheduler_mpsl_priority,
		      "MPSL priority must retain fake public lookup value");
	zassert_equal(observed_scheduler_sender_priority, fake_scheduler_sender_priority,
		      "state observer must receive sender priority");
	zassert_equal(observed_scheduler_resumed_current_priority, fake_scheduler_sender_priority,
		      "state observer must receive resumed current priority");
	zassert_equal(observed_scheduler_mpsl_priority, fake_scheduler_mpsl_priority,
		      "state observer must receive MPSL priority");
	zassert_true(observed_sched_unlock_real_order < observed_scheduler_state_order,
		     "target state must occur after real scheduler unlock");
	zassert_true(observed_scheduler_state_order < observed_sched_unlock_return_order,
		     "target state must occur before scheduler unlock return observation");

	/* Switch-ins after the window are ignored, and an unarmed unlock observes nothing. */
	fake_switch_in_thread = &fake_scheduler_thread;
	sys_trace_thread_switched_in_user();
	fake_switch_in_thread = NULL;
	__wrap_k_sched_unlock();
	zassert_equal(observed_sched_unlock_calls, 3,
		      "second sender scheduler unlock must still forward");
	zassert_equal(observed_sched_unlock_entries, 1,
		      "second sender scheduler unlock must not emit another entry");
	zassert_equal(observed_sched_unlock_returns, 1,
		      "second sender scheduler unlock must not emit another return");
	zassert_equal(observed_scheduler_state_calls, 1,
		      "unarmed scheduler unlock must not emit target state observation");
	zassert_equal(observed_scheduler_priority_calls, 3,
		      "unarmed scheduler unlock must not query priorities");

	/* A second armed unlock with no MPSL switch records zero. */
	sdc_hci_remove_iso_path_trace_test_reset();
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	observed_scheduler_state_calls = 0;
	observed_scheduler_priority_calls = 0;
	observed_sched_unlock_calls = 0;
	observed_sched_unlock_entries = 0;
	observed_sched_unlock_returns = 0;
	fake_current_thread = &sender_thread;
	fake_real_unlock_switch_mpsl = false;
	fake_work_submit_status = 1;
	__wrap_hci_internal_cmd_put(cmd_in);
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), 1,
		      "second target work-submit status must be preserved");
	__wrap_k_sched_unlock();
	zassert_equal(observed_scheduler_state_calls, 1,
		      "second armed unlock must emit one state observation");
	zassert_equal(observed_scheduler_mpsl_switches_during_unlock, 0U,
		      "no MPSL switch during real unlock must record zero");
	zassert_true(observed_scheduler_resumed_current_is_sender,
		     "no-switch unlock must still report resumed sender");

	/* Negative work submission never arms the probe. */
	sdc_hci_remove_iso_path_trace_test_reset();
	fake_current_thread = &sender_thread;
	fake_cmd_put_status = 0;
	fake_work_submit_status = -EIO;
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	__wrap_hci_internal_cmd_put(cmd_in);
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&fake_work_queue, &fake_work), -EIO,
		      "negative target work-submit status must be preserved");
	observed_scheduler_state_calls = 0;
	observed_scheduler_priority_calls = 0;
	observed_sched_unlock_calls = 0;
	observed_sched_unlock_entries = 0;
	observed_sched_unlock_returns = 0;
	__wrap_k_sched_unlock();
	zassert_equal(observed_sched_unlock_calls, 1,
		      "negative work-submit path must still forward unlock");
	zassert_equal(observed_sched_unlock_entries, 0,
		      "negative work-submit path must not arm scheduler trace");
	zassert_equal(observed_sched_unlock_returns, 0,
		      "negative work-submit path must not emit scheduler return");
	zassert_equal(observed_scheduler_state_calls, 0,
		      "negative work-submit path must not emit target state observation");
	zassert_equal(observed_scheduler_priority_calls, 0,
		      "negative work-submit path must not query priorities");
}

ZTEST(sdc_hci_remove_iso_path_trace, test_post_unlock_yield_probe_records_switch_in_window)
{
	static struct k_thread sender_thread;
	static struct k_thread foreign_thread;
	uint8_t cmd_in[] = {0x6fU, 0x20U};

	sdc_hci_remove_iso_path_trace_test_reset();
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	observed_sched_unlock_return_order = 0;
	observed_scheduler_yield_armed_calls = 0;
	observed_scheduler_yield_entries = 0;
	observed_scheduler_yield_returns = 0;
	observed_scheduler_yield_arm_order = 0;
	observed_scheduler_yield_entry_order = 0;
	observed_scheduler_yield_state_order = 0;
	observed_scheduler_yield_return_order = 0;
	observed_scheduler_yield_state_calls = 0;
	observed_scheduler_yield_busy = 0;
	observed_scheduler_yield_state[0] = '\0';
	observed_scheduler_yield_resumed_current_is_sender = false;
	observed_scheduler_yield_mpsl_switches = 0U;
	observed_real_yield_calls = 0;
	observed_real_yield_order = 0;
	observed_scheduler_busy_calls = 0;
	observed_scheduler_busy_work = NULL;
	observed_scheduler_state_str_calls = 0;
	observed_scheduler_state_thread = NULL;
	observed_scheduler_queue_calls = 0;
	observed_scheduler_queue = NULL;
	observed_scheduler_priority_calls = 0;
	observed_scheduler_state_calls = 0;
	observed_scheduler_state_order = 0;
	observed_sched_unlock_calls = 0;
	observed_sched_unlock_entries = 0;
	observed_sched_unlock_returns = 0;
	observed_sched_unlock_real_order = 0;
	scheduler_observation_order = 0;
	fake_scheduler_busy = K_WORK_QUEUED;
	fake_scheduler_state = "PENDING";
	fake_scheduler_queue_thread = &fake_scheduler_thread;
	fake_current_thread = &sender_thread;
	fake_switch_in_thread = NULL;
	fake_resumed_thread = &sender_thread;
	fake_real_unlock_switch_mpsl = false;
	fake_real_yield_switch_mpsl = true;
	fake_cmd_put_status = 0;
	fake_work_submit_status = 1;

	/* Real unlock completes v9 observation, then arms one sender-owned yield. */
	__wrap_hci_internal_cmd_put(cmd_in);
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), 1,
		      "target work-submit status must be preserved before yield trace");
	__wrap_k_sched_unlock();
	zassert_equal(observed_scheduler_yield_armed_calls, 1,
		      "exact sender unlock must arm one post-unlock yield");
	zassert_true(observed_sched_unlock_return_order < observed_scheduler_yield_arm_order,
		     "yield arm must follow v9 unlock return observation");
	zassert_equal(observed_scheduler_queue, &mpsl_work_q,
		      "yield probe must retain MPSL queue identity");
	zassert_equal(observed_scheduler_busy_work, &fake_work,
		      "yield probe must retain submitted target work");

	/* Foreign yield cannot consume armed sender or query target state. */
	int busy_calls_after_unlock = observed_scheduler_busy_calls;
	int state_calls_after_unlock = observed_scheduler_state_str_calls;
	fake_current_thread = &foreign_thread;
	fake_real_yield_switch_mpsl = false;
	__wrap_z_impl_k_yield();
	zassert_equal(observed_real_yield_calls, 1, "foreign yield must forward to real yield");
	zassert_equal(observed_scheduler_yield_entries, 0,
		      "foreign yield must not consume armed sender");
	zassert_equal(observed_scheduler_yield_state_calls, 0,
		      "foreign yield must not observe target state");
	zassert_equal(observed_scheduler_busy_calls, busy_calls_after_unlock,
		      "foreign yield must not query target busy state");
	zassert_equal(observed_scheduler_state_str_calls, state_calls_after_unlock,
		      "foreign yield must not query MPSL state");

	/* First sender yield observes foreign and MPSL switch-ins inside real yield. */
	fake_current_thread = &sender_thread;
	fake_real_yield_switch_mpsl = true;
	__wrap_z_impl_k_yield();
	zassert_equal(observed_real_yield_calls, 2,
		      "claimed yield must forward to real yield once");
	zassert_equal(observed_scheduler_yield_entries, 1,
		      "claimed sender must emit one yield entry");
	zassert_equal(observed_scheduler_yield_state_calls, 1,
		      "claimed sender must emit one yield state");
	zassert_equal(observed_scheduler_yield_returns, 1,
		      "claimed sender must emit one yield return");
	zassert_equal(observed_scheduler_yield_busy, K_WORK_QUEUED,
		      "yield state must retain target busy state");
	zassert_equal(strcmp(observed_scheduler_yield_state, "PENDING"), 0,
		      "yield state must retain MPSL worker state");
	zassert_true(observed_scheduler_yield_resumed_current_is_sender,
		     "yield must resume claimed sender");
	zassert_equal(observed_scheduler_yield_mpsl_switches, 1U,
		      "yield must count MPSL switch after ignoring foreign switch");
	zassert_true(observed_scheduler_yield_arm_order < observed_scheduler_yield_entry_order,
		     "yield entry must follow yield arm");
	zassert_true(observed_scheduler_yield_entry_order < observed_real_yield_order,
		     "real yield must follow yield entry");
	zassert_true(observed_real_yield_order < observed_scheduler_yield_state_order,
		     "yield state must follow real yield return");
	zassert_true(observed_scheduler_yield_state_order < observed_scheduler_yield_return_order,
		     "yield return must follow yield state");
	zassert_equal(observed_scheduler_state_thread, &fake_scheduler_thread,
		      "yield state must use MPSL worker thread");
	zassert_equal(observed_scheduler_priority_calls, 3,
		      "yield state must not query priorities");

	/* Armed yield remains consumed once; later sender yield is unarmed. */
	fake_real_yield_switch_mpsl = false;
	__wrap_z_impl_k_yield();
	zassert_equal(observed_real_yield_calls, 3,
		      "unarmed sender yield must forward to real yield");
	zassert_equal(observed_scheduler_yield_entries, 1,
		      "unarmed sender yield must emit no second entry");
	zassert_equal(observed_scheduler_yield_state_calls, 1,
		      "unarmed sender yield must not observe target state");
	zassert_equal(observed_scheduler_busy_calls, busy_calls_after_unlock + 1,
		      "unarmed sender yield must not query target busy state");

	/* A second armed sender yield with no MPSL switch records zero. */
	sdc_hci_remove_iso_path_trace_test_reset();
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	observed_scheduler_yield_armed_calls = 0;
	observed_scheduler_yield_entries = 0;
	observed_scheduler_yield_returns = 0;
	observed_scheduler_yield_state_calls = 0;
	observed_scheduler_yield_mpsl_switches = 0U;
	observed_real_yield_calls = 0;
	observed_scheduler_busy_calls = 0;
	observed_scheduler_state_str_calls = 0;
	observed_scheduler_queue_calls = 0;
	observed_scheduler_priority_calls = 0;
	observed_sched_unlock_return_order = 0;
	scheduler_observation_order = 0;
	fake_current_thread = &sender_thread;
	fake_real_yield_switch_mpsl = false;
	__wrap_hci_internal_cmd_put(cmd_in);
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), 1,
		      "zero-switch target work-submit status must be preserved");
	__wrap_k_sched_unlock();
	__wrap_z_impl_k_yield();
	zassert_equal(observed_scheduler_yield_armed_calls, 1,
		      "zero-switch sender must arm one yield");
	zassert_equal(observed_scheduler_yield_entries, 1,
		      "zero-switch sender must emit one yield entry");
	zassert_equal(observed_scheduler_yield_state_calls, 1,
		      "zero-switch sender must emit one yield state");
	zassert_equal(observed_scheduler_yield_mpsl_switches, 0U,
		      "no MPSL switch during real yield must record zero");

	/* Negative submit cannot arm or cause target-state queries. */
	sdc_hci_remove_iso_path_trace_test_reset();
	sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(true);
	observed_scheduler_yield_armed_calls = 0;
	observed_scheduler_yield_entries = 0;
	observed_scheduler_yield_state_calls = 0;
	observed_scheduler_busy_calls = 0;
	observed_scheduler_state_str_calls = 0;
	observed_real_yield_calls = 0;
	fake_current_thread = &sender_thread;
	fake_work_submit_status = -EIO;
	__wrap_hci_internal_cmd_put(cmd_in);
	__wrap_multithreading_lock_release();
	zassert_equal(__wrap_k_work_submit_to_queue(&mpsl_work_q, &fake_work), -EIO,
		      "negative work-submit status must be preserved");
	__wrap_z_impl_k_yield();
	zassert_equal(observed_scheduler_yield_armed_calls, 0,
		      "negative submit must not arm yield probe");
	zassert_equal(observed_scheduler_yield_entries, 0,
		      "negative submit must not consume yield probe");
	zassert_equal(observed_scheduler_yield_state_calls, 0,
		      "negative submit must not observe target state");
	zassert_equal(observed_scheduler_busy_calls, 0,
		      "negative submit must not query target busy state");
	zassert_equal(observed_scheduler_state_str_calls, 0,
		      "negative submit must not query MPSL state");
}
