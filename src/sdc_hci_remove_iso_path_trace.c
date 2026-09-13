/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <sdc_hci.h>
#include <sdc_hci_cmd_le.h>
#if defined(CONFIG_BT)
#include <zephyr/bluetooth/buf.h>
#else
struct net_buf;

enum bt_buf_type {
	BT_BUF_EVT = 0x02,
	BT_BUF_ACL_IN = 0x08,
	BT_BUF_ISO_IN = 0x20,
};
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME) ||                           \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME)
#include <zephyr/net_buf.h>
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION) ||                       \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_RECEIVE_DISPOSITION)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME) ||                           \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY CONFIG_BT_ISO_RX_BUF_COUNT
#else
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY 3U
#endif
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION) ||               \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH) ||           \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE) || \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED
#endif

#if defined(                                                                                        \
	CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND) || \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED
#endif

#if defined(                                                                                                   \
	CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE) || \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                       \
	defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                      \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST) ||                                             \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
#include <mpsl/mpsl_work.h>
#endif
#if defined(CONFIG_TRACING_USER) &&                                                                                  \
	(defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED) || \
	 defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                                       \
	 defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK) ||                                             \
	 defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                                    \
	 defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST) ||                                                              \
	 defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED))
#include <tracing_user.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(sdc_hci_remove_iso_path_trace, LOG_LEVEL_INF);

static atomic_t sdc_hci_remove_iso_path_trace_msg_get_pending = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_lock_release_pending = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_work_submit_pending = ATOMIC_INIT(0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_receive_target = ATOMIC_PTR_INIT(NULL);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_receive_mpsl_thread = ATOMIC_PTR_INIT(NULL);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_receive_event_thread = ATOMIC_PTR_INIT(NULL);
static atomic_t sdc_hci_remove_iso_path_trace_receive_armed = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_receive_fetch_observed = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_receive_allocation_observed = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_receive_event_active = ATOMIC_INIT(0);
#endif
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots
	[SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY];
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_unavailable = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot_armed =
	ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_total = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking_error = ATOMIC_INIT(0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
enum sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage {
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED = 0,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED = 1,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED = 2,
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED = 3,
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENTERED = 4,
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED)
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENTERED = 5,
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_THREAD_MARKED_PENDING =
		6,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_GIVE_ENTERED = 7,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_RETURNED = 8,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED = 9,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED = 10,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED = 11,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN = 12,
#else
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_RETURNED = 6,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED = 7,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED = 8,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED = 9,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN = 10,
#endif
#else
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED = 5,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED = 6,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED = 7,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN = 8,
#endif
#else
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED = 4,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED = 5,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN = 6,
#endif
#else
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED = 3,
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN = 4,
#endif
};

static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages
	[SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY];
#endif
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf = ATOMIC_PTR_INIT(NULL);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread =
	ATOMIC_PTR_INIT(NULL);
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active = ATOMIC_INIT(0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem =
	ATOMIC_PTR_INIT(NULL);
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
static atomic_t sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed =
	ATOMIC_INIT(0);
#endif
#endif

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_diag_context_clear(void)
{
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active, 0);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf, NULL);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread, NULL);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem, NULL);
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed, 0);
#endif
#endif
}

static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_context_matches(struct net_buf *buf)
{
	k_tid_t current = k_current_get();

	return atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active) != 0 &&
	       atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf) == buf &&
	       atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread) ==
		       current;
}
#endif
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                          \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_scheduler_sender = ATOMIC_PTR_INIT(NULL);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_scheduler_target = ATOMIC_PTR_INIT(NULL);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread = ATOMIC_PTR_INIT(NULL);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_unlock_window_active = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_unlock_switches = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_unlock_last_switches = ATOMIC_INIT(0);
static atomic_ptr_t sdc_hci_remove_iso_path_trace_scheduler_yield_sender = ATOMIC_PTR_INIT(NULL);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_yield_window_active = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_yield_switches = ATOMIC_INIT(0);
static atomic_t sdc_hci_remove_iso_path_trace_scheduler_yield_last_switches = ATOMIC_INIT(0);
#endif
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                       \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST)
static atomic_ptr_t sdc_hci_remove_iso_path_trace_snapshot_target = ATOMIC_PTR_INIT(NULL);
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                       \
	defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                      \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST)
#define SDC_HCI_REMOVE_ISO_PATH_TRACE_MPSL_STATE_MAX 32
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
__attribute__((weak)) int sdc_hci_remove_iso_path_trace_receive_busy_get(const struct k_work *work)
{
	return k_work_busy_get(work);
}

__attribute__((weak)) k_tid_t
sdc_hci_remove_iso_path_trace_receive_queue_thread_get(struct k_work_q *queue)
{
	return k_work_queue_thread_get(queue);
}

__attribute__((weak)) k_tid_t sdc_hci_remove_iso_path_trace_receive_current_thread_get(void)
{
	return k_current_get();
}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
__attribute__((weak)) void
sdc_hci_remove_iso_path_trace_receive_disposition_armed(bool queue_is_mpsl)
{
	ARG_UNUSED(queue_is_mpsl);
}

__attribute__((weak)) void
sdc_hci_remove_iso_path_trace_receive_disposition_fetch_entry_observed(void)
{
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_receive_disposition_fetch_return_observed(
	int status, bool msg_type_valid, unsigned int msg_type)
{
	ARG_UNUSED(status);
	ARG_UNUSED(msg_type_valid);
	ARG_UNUSED(msg_type);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_receive_disposition_evt_observed(
	unsigned int evt, bool discardable, bool buffer_available, int target_busy)
{
	ARG_UNUSED(evt);
	ARG_UNUSED(discardable);
	ARG_UNUSED(buffer_available);
	ARG_UNUSED(target_busy);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_receive_disposition_rx_observed(
	unsigned int type, bool buffer_available, int target_busy)
{
	ARG_UNUSED(type);
	ARG_UNUSED(buffer_available);
	ARG_UNUSED(target_busy);
}
#endif

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
__attribute__((weak)) void
sdc_hci_remove_iso_path_trace_iso_rx_lifetime_arm_observed(unsigned int capacity)
{
	ARG_UNUSED(capacity);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot_observed(
	const char *reason, unsigned int capacity, unsigned int outstanding,
	unsigned int high_water, unsigned int allocations, unsigned int final_unrefs,
	unsigned int callbacks_active, unsigned int callbacks_total)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(capacity);
	ARG_UNUSED(outstanding);
	ARG_UNUSED(high_water);
	ARG_UNUSED(allocations);
	ARG_UNUSED(final_unrefs);
	ARG_UNUSED(callbacks_active);
	ARG_UNUSED(callbacks_total);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free_observed(
	unsigned int outstanding_before, unsigned int callbacks_active, unsigned int allocations,
	unsigned int final_unrefs)
{
	ARG_UNUSED(outstanding_before);
	ARG_UNUSED(callbacks_active);
	ARG_UNUSED(allocations);
	ARG_UNUSED(final_unrefs);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unref_observed(
	unsigned int outstanding, unsigned int allocations, unsigned int final_unrefs)
{
	ARG_UNUSED(outstanding);
	ARG_UNUSED(allocations);
	ARG_UNUSED(final_unrefs);
}

__attribute__((weak)) void
sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error_observed(const char *reason)
{
	ARG_UNUSED(reason);
}
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION)
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int tx_notify_flush_entered, unsigned int tx_notify_flush_semaphore_entered,
	unsigned int tx_notify_flush_semaphore_pend_entered,
	unsigned int tx_notify_flush_semaphore_pend_thread_marked_pending,
	unsigned int tx_notify_flush_semaphore_give_entered,
	unsigned int tx_notify_flush_semaphore_pend_returned,
	unsigned int tx_notify_flush_semaphore_returned, unsigned int tx_notify_flush_returned,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(undispatched);
	ARG_UNUSED(host_dispatched);
	ARG_UNUSED(tx_notify_flush_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_pend_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_pend_thread_marked_pending);
	ARG_UNUSED(tx_notify_flush_semaphore_give_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_pend_returned);
	ARG_UNUSED(tx_notify_flush_semaphore_returned);
	ARG_UNUSED(tx_notify_flush_returned);
	ARG_UNUSED(host_returned);
	ARG_UNUSED(app_callback_seen);
	ARG_UNUSED(unclassified);
}
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED)
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int tx_notify_flush_entered, unsigned int tx_notify_flush_semaphore_entered,
	unsigned int tx_notify_flush_semaphore_pend_entered,
	unsigned int tx_notify_flush_semaphore_pend_returned,
	unsigned int tx_notify_flush_semaphore_returned, unsigned int tx_notify_flush_returned,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(undispatched);
	ARG_UNUSED(host_dispatched);
	ARG_UNUSED(tx_notify_flush_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_pend_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_pend_returned);
	ARG_UNUSED(tx_notify_flush_semaphore_returned);
	ARG_UNUSED(tx_notify_flush_returned);
	ARG_UNUSED(host_returned);
	ARG_UNUSED(app_callback_seen);
	ARG_UNUSED(unclassified);
}
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int tx_notify_flush_entered, unsigned int tx_notify_flush_semaphore_entered,
	unsigned int tx_notify_flush_semaphore_returned, unsigned int tx_notify_flush_returned,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(undispatched);
	ARG_UNUSED(host_dispatched);
	ARG_UNUSED(tx_notify_flush_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_entered);
	ARG_UNUSED(tx_notify_flush_semaphore_returned);
	ARG_UNUSED(tx_notify_flush_returned);
	ARG_UNUSED(host_returned);
	ARG_UNUSED(app_callback_seen);
	ARG_UNUSED(unclassified);
}
#else
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int tx_notify_flush_entered, unsigned int tx_notify_flush_returned,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(undispatched);
	ARG_UNUSED(host_dispatched);
	ARG_UNUSED(tx_notify_flush_entered);
	ARG_UNUSED(tx_notify_flush_returned);
	ARG_UNUSED(host_returned);
	ARG_UNUSED(app_callback_seen);
	ARG_UNUSED(unclassified);
}
#endif
#else
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
	const char *reason, unsigned int undispatched, unsigned int host_dispatched,
	unsigned int host_returned, unsigned int app_callback_seen, unsigned int unclassified)
{
	ARG_UNUSED(reason);
	ARG_UNUSED(undispatched);
	ARG_UNUSED(host_dispatched);
	ARG_UNUSED(host_returned);
	ARG_UNUSED(app_callback_seen);
	ARG_UNUSED(unclassified);
}
#endif
#endif
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED)
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
	struct net_buf *buf, enum sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage target)
{
	if (buf == NULL) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if ((struct net_buf *)atomic_ptr_get(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) != buf) {
			continue;
		}

		atomic_val_t current;
		do {
			current = atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i]);
			if (current >= (atomic_val_t)target) {
				return;
			}
		} while (!atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i],
				     current, (atomic_val_t)target));
		return;
	}
}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slot_contains(struct net_buf *buf)
{
	if (buf == NULL) {
		return false;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if ((struct net_buf *)atomic_ptr_get(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) == buf) {
			return true;
		}
	}

	return false;
}

#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
static bool
sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_context_matches(struct k_thread *thread,
									 struct net_buf **buf_out)
{
	struct net_buf *buf = (struct net_buf *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf);
	k_tid_t captured_thread =
		(k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread);

	if (thread == NULL || captured_thread != thread ||
	    atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active) == 0 ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slot_contains(buf)) {
		return false;
	}

	if (buf_out != NULL) {
		*buf_out = buf;
	}

	return true;
}

static bool
sdc_hci_remove_iso_path_trace_iso_rx_lifetime_give_context_matches(struct k_sem *sem,
								   struct net_buf **buf_out)
{
	struct net_buf *buf = (struct net_buf *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf);
	struct k_sem *captured_sem = (struct k_sem *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem);

	if (sem == NULL || captured_sem == NULL || sem != captured_sem || buf == NULL ||
	    atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active) == 0 ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slot_contains(buf)) {
		return false;
	}

	if (buf_out != NULL) {
		*buf_out = buf;
	}

	return true;
}
#endif

static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sem_context_matches(
	struct k_sem *sem, k_timeout_t timeout, struct net_buf **buf_out)
{
	struct net_buf *buf = (struct net_buf *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf);
	struct k_sem *captured_sem = (struct k_sem *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem);

	if (captured_sem == NULL || captured_sem != sem || !K_TIMEOUT_EQ(timeout, K_FOREVER) ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_context_matches(buf) ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slot_contains(buf)) {
		return false;
	}

	if (buf_out != NULL) {
		*buf_out = buf;
	}

	return true;
}
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED)
static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_pend_context_matches(
	_wait_q_t *wait_q, k_timeout_t timeout, struct net_buf **buf_out)
{
	struct net_buf *buf = (struct net_buf *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf);
	struct k_sem *captured_sem = (struct k_sem *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem);

	if (captured_sem == NULL || wait_q != &captured_sem->wait_q ||
	    !K_TIMEOUT_EQ(timeout, K_FOREVER) ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_context_matches(buf) ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slot_contains(buf)) {
		return false;
	}

	if (buf_out != NULL) {
		*buf_out = buf;
	}

	return true;
}
#endif

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_snapshot(const char *reason)
{
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	unsigned int counts[13] = {0U};

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) ==
		    NULL) {
			continue;
		}

		switch (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i])) {
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED:
			counts[1]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED:
			counts[2]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED:
			counts[3]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENTERED:
			counts[4]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENTERED:
			counts[5]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_THREAD_MARKED_PENDING:
			counts[6]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_GIVE_ENTERED:
			counts[7]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_RETURNED:
			counts[8]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED:
			counts[9]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED:
			counts[10]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED:
			counts[11]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN:
			counts[12]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED:
		default:
			counts[0]++;
			break;
		}
	}

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=%s "
		"undispatched=%u host_dispatched=%u tx_notify_flush_entered=%u "
		"tx_notify_flush_semaphore_entered=%u "
		"tx_notify_flush_semaphore_pend_entered=%u "
		"tx_notify_flush_semaphore_pend_thread_marked_pending=%u "
		"tx_notify_flush_semaphore_give_entered=%u "
		"tx_notify_flush_semaphore_pend_returned=%u "
		"tx_notify_flush_semaphore_returned=%u tx_notify_flush_returned=%u "
		"host_returned=%u app_callback_seen=%u unclassified=%u",
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[9], counts[10], counts[11], counts[12], counts[0]);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[9], counts[10], counts[11], counts[12], counts[0]);
#endif
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED)
	unsigned int counts[11] = {0U};

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) ==
		    NULL) {
			continue;
		}

		switch (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i])) {
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED:
			counts[1]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED:
			counts[2]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED:
			counts[3]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENTERED:
			counts[4]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENTERED:
			counts[5]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_RETURNED:
			counts[6]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED:
			counts[7]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED:
			counts[8]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED:
			counts[9]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN:
			counts[10]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED:
		default:
			counts[0]++;
			break;
		}
	}

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=%s "
		"undispatched=%u host_dispatched=%u tx_notify_flush_entered=%u "
		"tx_notify_flush_semaphore_entered=%u "
		"tx_notify_flush_semaphore_pend_entered=%u "
		"tx_notify_flush_semaphore_pend_returned=%u "
		"tx_notify_flush_semaphore_returned=%u tx_notify_flush_returned=%u "
		"host_returned=%u app_callback_seen=%u unclassified=%u",
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[9], counts[10], counts[0]);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[9], counts[10], counts[0]);
#endif
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
	unsigned int counts[9] = {0U};

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) ==
		    NULL) {
			continue;
		}

		switch (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i])) {
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED:
			counts[1]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED:
			counts[2]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED:
			counts[3]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENTERED:
			counts[4]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED:
			counts[5]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED:
			counts[6]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED:
			counts[7]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN:
			counts[8]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED:
		default:
			counts[0]++;
			break;
		}
	}

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=%s "
		"undispatched=%u host_dispatched=%u tx_notify_flush_entered=%u "
		"tx_notify_flush_semaphore_entered=%u tx_notify_flush_semaphore_returned=%u "
		"tx_notify_flush_returned=%u host_returned=%u app_callback_seen=%u "
		"unclassified=%u",
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[0]);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6], counts[7],
		counts[8], counts[0]);
#endif
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	unsigned int counts[7] = {0U};

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) ==
		    NULL) {
			continue;
		}

		switch (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i])) {
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED:
			counts[1]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED:
			counts[2]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED:
			counts[3]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED:
			counts[4]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED:
			counts[5]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN:
			counts[6]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED:
		default:
			counts[0]++;
			break;
		}
	}

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=%s "
		"undispatched=%u host_dispatched=%u tx_notify_flush_entered=%u "
		"tx_notify_flush_returned=%u host_returned=%u "
		"app_callback_seen=%u unclassified=%u",
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6],
		counts[0]);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
		reason, counts[1], counts[2], counts[3], counts[4], counts[5], counts[6],
		counts[0]);
#endif
#else
	unsigned int counts[5] = {0U};

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) ==
		    NULL) {
			continue;
		}

		switch (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i])) {
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED:
			counts[1]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED:
			counts[2]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED:
			counts[3]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN:
			counts[4]++;
			break;
		case SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED:
		default:
			counts[0]++;
			break;
		}
	}

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime disposition snapshot: reason=%s "
		"undispatched=%u host_dispatched=%u host_returned=%u app_callback_seen=%u "
		"unclassified=%u",
		reason, counts[1], counts[2], counts[3], counts[4], counts[0]);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME_DISPOSITION
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_observed(
		reason, counts[1], counts[2], counts[3], counts[4], counts[0]);
#endif
#endif
}
#endif

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot(const char *reason)
{
	unsigned int outstanding = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding);
	unsigned int high_water =
		(unsigned int)atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water);
	unsigned int allocations = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations);
	unsigned int final_unrefs = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs);
	unsigned int callbacks_active = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active);
	unsigned int callbacks_total = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_total);

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime snapshot: reason=%s capacity=%u "
		"outstanding=%u high_water=%u allocations=%u final_unrefs=%u "
		"callbacks_active=%u callbacks_total=%u",
		reason, (unsigned int)SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY,
		outstanding, high_water, allocations, final_unrefs, callbacks_active,
		callbacks_total);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disposition_snapshot(reason);
#endif
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot_observed(
		reason, (unsigned int)SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY,
		outstanding, high_water, allocations, final_unrefs, callbacks_active,
		callbacks_total);
#endif
}

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error(const char *reason)
{
	if (atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking_error, 0, 1)) {
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking, 0);
		LOG_ERR("SDC LE Remove ISO Data Path ISO RX lifetime tracking disabled: reason=%s",
			reason);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error_observed(reason);
#endif
	}
}

static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_counter_inc_bounded(atomic_t *counter,
									      atomic_val_t *after)
{
	atomic_val_t current;

	do {
		current = atomic_get(counter);
		if (current >=
		    (atomic_val_t)SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY) {
			return false;
		}
	} while (!atomic_cas(counter, current, current + 1));
	if (after != NULL) {
		*after = current + 1;
	}

	return true;
}

static bool sdc_hci_remove_iso_path_trace_iso_rx_lifetime_counter_dec_nonzero(atomic_t *counter,
									      atomic_val_t *before)
{
	atomic_val_t current;

	do {
		current = atomic_get(counter);
		if (current <= 0) {
			return false;
		}
	} while (!atomic_cas(counter, current, current - 1));

	if (before != NULL) {
		*before = current;
	}

	return true;
}

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water_update(atomic_val_t value)
{
	atomic_val_t current;

	do {
		current = atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water);
		if (value <= current) {
			return;
		}
	} while (!atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water, current,
			     value));
}

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_track(struct net_buf *buf)
{
	int slot = -1;
	atomic_val_t outstanding;

	if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) == 0 ||
	    atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking) == 0) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if ((struct net_buf *)atomic_ptr_get(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) == buf) {
			sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error("duplicate");
			return;
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if (atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i], NULL,
				   buf)) {
			slot = (int)i;
			break;
		}
	}

	if (slot < 0) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error("no_free_slot");
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if ((int)i != slot &&
		    (struct net_buf *)atomic_ptr_get(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) == buf) {
			atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[slot],
				       buf, NULL);
			sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error("duplicate");
			return;
		}
	}

	if (!sdc_hci_remove_iso_path_trace_iso_rx_lifetime_counter_inc_bounded(
		    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding, &outstanding)) {
		atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[slot], buf,
			       NULL);
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error("outstanding_over_capacity");
		return;
	}

	atomic_inc(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations);
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water_update(outstanding);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[slot],
		   SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNDISPATCHED);
#endif
}

static void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_release(struct net_buf *buf)
{
	atomic_val_t outstanding_before;

	if (buf == NULL || atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) == 0) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		if ((struct net_buf *)atomic_ptr_get(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i]) != buf) {
			continue;
		}

		if (buf->ref != 1U ||
		    !atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i], buf,
				    NULL)) {
			return;
		}
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i],
			   SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED);
#endif

		if (!sdc_hci_remove_iso_path_trace_iso_rx_lifetime_counter_dec_nonzero(
			    &sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding,
			    &outstanding_before)) {
			sdc_hci_remove_iso_path_trace_iso_rx_lifetime_error(
				"outstanding_underflow");
			return;
		}

		atomic_inc(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unref_observed(
			(unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding),
			(unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations),
			(unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs));
#endif
		if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_unavailable) != 0 &&
		    atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free, 0, 1)) {
			unsigned int callbacks_active = (unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active);
			unsigned int allocations = (unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations);
			unsigned int final_unrefs = (unsigned int)atomic_get(
				&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs);

			LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime first free after "
				"unavailable: "
				"outstanding_before=%u callbacks_active=%u allocations=%u "
				"final_unrefs=%u",
				(unsigned int)outstanding_before, callbacks_active, allocations,
				final_unrefs);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
			sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free_observed(
				(unsigned int)outstanding_before, callbacks_active, allocations,
				final_unrefs);
#endif
		}
		return;
	}
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_session_open(void)
{
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_diag_context_clear();
#endif
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed, 0);
#endif
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed, 0);
	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i], NULL);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i],
			   SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED);
#endif
	}
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking, 1);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_unavailable, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot_armed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_total, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking_error, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed, 1);

	LOG_INF("SDC LE Remove ISO Data Path ISO RX lifetime trace armed: capacity=%u",
		(unsigned int)SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_ISO_RX_LIFETIME
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_arm_observed(
		(unsigned int)SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_CAPACITY);
#endif
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_enter(struct net_buf *buf)
{
	if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) != 0) {
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf, SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_APP_CALLBACK_SEEN);
#endif
		atomic_inc(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_total);
		atomic_inc(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active);
	}
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callback_exit(void)
{
	if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) != 0) {
		(void)sdc_hci_remove_iso_path_trace_iso_rx_lifetime_counter_dec_nonzero(
			&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active, NULL);
	}
}

void sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot(void)
{
	if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) != 0 &&
	    atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot_armed, 0,
		       1)) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot("disable");
	}
}

#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
void sys_trace_thread_pend_user(struct k_thread *thread)
{
	struct net_buf *buf = NULL;

	if (atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed) == 0 ||
	    !sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_context_matches(thread,
										      &buf)) {
		return;
	}

	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
		buf,
		SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_THREAD_MARKED_PENDING);
}
#endif
#endif

static void sdc_hci_remove_iso_path_trace_receive_disposition_clear(void)
{
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_target, NULL);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_mpsl_thread, NULL);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_event_thread, NULL);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_armed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_fetch_observed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_allocation_observed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_event_active, 0);
}

static bool sdc_hci_remove_iso_path_trace_receive_target_context(int *busy_out)
{
	struct k_work *target_work =
		(struct k_work *)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_receive_target);
	k_tid_t mpsl_thread =
		(k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_receive_mpsl_thread);

	if (target_work == NULL || mpsl_thread == NULL ||
	    atomic_get(&sdc_hci_remove_iso_path_trace_receive_armed) == 0 ||
	    sdc_hci_remove_iso_path_trace_receive_current_thread_get() != mpsl_thread) {
		return false;
	}

	int busy = sdc_hci_remove_iso_path_trace_receive_busy_get(target_work);

	if (busy_out != NULL) {
		*busy_out = busy;
	}

	return (busy & K_WORK_RUNNING) != 0;
}

static bool sdc_hci_remove_iso_path_trace_receive_msg_type_supported(sdc_hci_msg_type_t msg_type)
{
	return msg_type == SDC_HCI_MSG_TYPE_DATA || msg_type == SDC_HCI_MSG_TYPE_EVT ||
	       msg_type == SDC_HCI_MSG_TYPE_ISO;
}

static bool sdc_hci_remove_iso_path_trace_receive_allocation_claim(int *busy_out)
{
	if (!sdc_hci_remove_iso_path_trace_receive_target_context(busy_out)) {
		return false;
	}

	return atomic_cas(&sdc_hci_remove_iso_path_trace_receive_allocation_observed, 0, 1);
}

static bool sdc_hci_remove_iso_path_trace_receive_event_is_nested(void)
{
	return atomic_get(&sdc_hci_remove_iso_path_trace_receive_event_active) != 0 &&
	       sdc_hci_remove_iso_path_trace_receive_current_thread_get() ==
		       (k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_receive_event_thread);
}

static void sdc_hci_remove_iso_path_trace_receive_arm(struct k_work *work, struct k_work_q *queue)
{
	bool queue_is_mpsl = queue == &mpsl_work_q;
	k_tid_t mpsl_thread =
		queue_is_mpsl ? sdc_hci_remove_iso_path_trace_receive_queue_thread_get(queue)
			      : NULL;

	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_target, work);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_mpsl_thread, mpsl_thread);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_event_thread, NULL);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_fetch_observed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_allocation_observed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_event_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_receive_armed, 1);

	LOG_INF("SDC LE Remove ISO Data Path receive disposition trace armed: queue_is_mpsl=%d",
		queue_is_mpsl ? 1 : 0);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_receive_disposition_armed(queue_is_mpsl);
#endif
}

static void sdc_hci_remove_iso_path_trace_receive_fetch_entry(void)
{
	LOG_INF("SDC LE Remove ISO Data Path receive disposition fetch entry");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_receive_disposition_fetch_entry_observed();
#endif
}

static void sdc_hci_remove_iso_path_trace_receive_fetch_return(int status, bool msg_type_valid,
							       unsigned int msg_type)
{
	if (msg_type_valid) {
		LOG_INF("SDC LE Remove ISO Data Path receive disposition fetch return: status=%d "
			"msg_type=%u",
			status, msg_type);
	} else {
		LOG_INF("SDC LE Remove ISO Data Path receive disposition fetch return: status=%d "
			"msg_type=na",
			status);
	}
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_receive_disposition_fetch_return_observed(
		status, msg_type_valid, msg_type);
#endif
}

static void sdc_hci_remove_iso_path_trace_receive_evt_allocation(unsigned int evt, bool discardable,
								 bool buffer_available,
								 int target_busy)
{
	LOG_INF("SDC LE Remove ISO Data Path receive disposition allocation: kind=evt evt=0x%02x "
		"discardable=%d buffer_available=%d target_busy=0x%x",
		evt, discardable ? 1 : 0, buffer_available ? 1 : 0, (unsigned int)target_busy);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_receive_disposition_evt_observed(
		evt, discardable, buffer_available, target_busy);
#endif
}

static void sdc_hci_remove_iso_path_trace_receive_rx_allocation(unsigned int type,
								bool buffer_available,
								int target_busy)
{
	LOG_INF("SDC LE Remove ISO Data Path receive disposition allocation: kind=rx type=%u "
		"buffer_available=%d target_busy=0x%x",
		type, buffer_available ? 1 : 0, (unsigned int)target_busy);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_receive_disposition_rx_observed(type, buffer_available,
								      target_busy);
#endif
}

#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                       \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST)
static void sdc_hci_remove_iso_path_trace_snapshot_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sdc_hci_remove_iso_path_trace_snapshot_work,
			       sdc_hci_remove_iso_path_trace_snapshot_handler);

__attribute__((weak)) int sdc_hci_remove_iso_path_trace_snapshot_busy_get(const struct k_work *work)
{
	return k_work_busy_get(work);
}

__attribute__((weak)) k_tid_t
sdc_hci_remove_iso_path_trace_snapshot_queue_thread_get(struct k_work_q *queue)
{
	return k_work_queue_thread_get(queue);
}

__attribute__((weak)) const char *
sdc_hci_remove_iso_path_trace_snapshot_state_str(k_tid_t thread, char *buf, size_t buf_size)
{
	return k_thread_state_str(thread, buf, buf_size);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_snapshot_observed(int busy,
									   const char *mpsl_state)
{
	ARG_UNUSED(busy);
	ARG_UNUSED(mpsl_state);
}

static void sdc_hci_remove_iso_path_trace_snapshot_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct k_work *target_work =
		(struct k_work *)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_snapshot_target);
	int busy = target_work == NULL
			   ? 0
			   : sdc_hci_remove_iso_path_trace_snapshot_busy_get(target_work);
	k_tid_t mpsl_thread = sdc_hci_remove_iso_path_trace_snapshot_queue_thread_get(&mpsl_work_q);
	char mpsl_state_buf[SDC_HCI_REMOVE_ISO_PATH_TRACE_MPSL_STATE_MAX];
	const char *mpsl_state = "unavailable";

	if (mpsl_thread != NULL) {
		const char *state = sdc_hci_remove_iso_path_trace_snapshot_state_str(
			mpsl_thread, mpsl_state_buf, sizeof(mpsl_state_buf));

		if (state != NULL && state[0] != '\0') {
			mpsl_state = state;
		} else {
			mpsl_state = "unknown";
		}
	}

	sdc_hci_remove_iso_path_trace_snapshot_observed(busy, mpsl_state);
	LOG_INF("SDC LE Remove ISO Data Path work state snapshot: busy=0x%x mpsl_state=%s",
		(unsigned int)busy, mpsl_state);
}

__attribute__((weak)) int
sdc_hci_remove_iso_path_trace_snapshot_schedule(struct k_work *target_work, k_timeout_t delay)
{
	ARG_UNUSED(target_work);
	return k_work_schedule(&sdc_hci_remove_iso_path_trace_snapshot_work, delay);
}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
void sdc_hci_remove_iso_path_trace_test_run_snapshot(void)
{
	sdc_hci_remove_iso_path_trace_snapshot_handler(
		&sdc_hci_remove_iso_path_trace_snapshot_work.work);
}
#endif
#endif

#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                          \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK)
__attribute__((weak)) int
sdc_hci_remove_iso_path_trace_scheduler_unlock_busy_get(const struct k_work *work)
{
	return k_work_busy_get(work);
}

__attribute__((weak)) k_tid_t
sdc_hci_remove_iso_path_trace_scheduler_unlock_queue_thread_get(struct k_work_q *queue)
{
	return k_work_queue_thread_get(queue);
}

__attribute__((weak)) const char *
sdc_hci_remove_iso_path_trace_scheduler_unlock_state_str(k_tid_t thread, char *buf, size_t buf_size)
{
	return k_thread_state_str(thread, buf, buf_size);
}

__attribute__((weak)) int
sdc_hci_remove_iso_path_trace_scheduler_unlock_thread_priority_get(k_tid_t thread)
{
	return k_thread_priority_get(thread);
}

__attribute__((weak)) k_tid_t
sdc_hci_remove_iso_path_trace_scheduler_unlock_switch_in_thread_get(void)
{
	return k_sched_current_thread_query();
}

void sys_trace_thread_switched_in_user(void)
{
	bool unlock_window_active =
		atomic_get(&sdc_hci_remove_iso_path_trace_scheduler_unlock_window_active) != 0;
	bool yield_window_active =
		atomic_get(&sdc_hci_remove_iso_path_trace_scheduler_yield_window_active) != 0;

	if (!unlock_window_active && !yield_window_active) {
		return;
	}

	k_tid_t switched_in = sdc_hci_remove_iso_path_trace_scheduler_unlock_switch_in_thread_get();
	k_tid_t mpsl_thread =
		(k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread);

	if (switched_in != NULL && switched_in == mpsl_thread) {
		if (unlock_window_active) {
			atomic_inc(&sdc_hci_remove_iso_path_trace_scheduler_unlock_switches);
		}
		if (yield_window_active) {
			atomic_inc(&sdc_hci_remove_iso_path_trace_scheduler_yield_switches);
		}
	}
}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_unlock_armed(bool queue_is_mpsl)
{
	ARG_UNUSED(queue_is_mpsl);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_unlock_state_observed(
	int busy, const char *mpsl_state, bool resumed_current_is_sender,
	unsigned int mpsl_switches_during_unlock, int sender_prio, int resumed_current_prio,
	int mpsl_prio)
{
	ARG_UNUSED(busy);
	ARG_UNUSED(mpsl_state);
	ARG_UNUSED(resumed_current_is_sender);
	ARG_UNUSED(mpsl_switches_during_unlock);
	ARG_UNUSED(sender_prio);
	ARG_UNUSED(resumed_current_prio);
	ARG_UNUSED(mpsl_prio);
}
#endif

__attribute__((weak)) k_tid_t sdc_hci_remove_iso_path_trace_current_thread_get(void)
{
	return k_current_get();
}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_unlock_observed(bool entry)
{
	ARG_UNUSED(entry);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_yield_armed(void)
{
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_yield_observed(bool entry)
{
	ARG_UNUSED(entry);
}

__attribute__((weak)) void sdc_hci_remove_iso_path_trace_scheduler_yield_state_observed(
	int busy, const char *mpsl_state, bool resumed_current_is_sender,
	unsigned int mpsl_switches_during_yield)
{
	ARG_UNUSED(busy);
	ARG_UNUSED(mpsl_state);
	ARG_UNUSED(resumed_current_is_sender);
	ARG_UNUSED(mpsl_switches_during_yield);
}
#endif

static void sdc_hci_remove_iso_path_trace_scheduler_unlock_arm(k_tid_t sender,
							       struct k_work *target_work,
							       struct k_work_q *queue)
{
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_target, target_work);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_sender, sender);
	bool queue_is_mpsl = queue == &mpsl_work_q;
	k_tid_t mpsl_thread =
		queue_is_mpsl
			? sdc_hci_remove_iso_path_trace_scheduler_unlock_queue_thread_get(queue)
			: NULL;
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread, mpsl_thread);

	LOG_INF("SDC LE Remove ISO Data Path scheduler unlock trace armed: queue_is_mpsl=%d",
		queue_is_mpsl ? 1 : 0);
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_unlock_armed(queue_is_mpsl);
#endif
}

static void sdc_hci_remove_iso_path_trace_scheduler_unlock_observe_state(k_tid_t claimed_sender)
{
	struct k_work *target_work =
		(struct k_work *)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_scheduler_target);
	int busy = target_work == NULL
			   ? 0
			   : sdc_hci_remove_iso_path_trace_scheduler_unlock_busy_get(target_work);
	k_tid_t mpsl_thread =
		(k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread);
	char mpsl_state_buf[SDC_HCI_REMOVE_ISO_PATH_TRACE_MPSL_STATE_MAX];
	const char *mpsl_state = "unavailable";

	if (mpsl_thread != NULL) {
		const char *state = sdc_hci_remove_iso_path_trace_scheduler_unlock_state_str(
			mpsl_thread, mpsl_state_buf, sizeof(mpsl_state_buf));

		if (state != NULL && state[0] != '\0') {
			mpsl_state = state;
		} else {
			mpsl_state = "unknown";
		}
	}

	k_tid_t current = sdc_hci_remove_iso_path_trace_current_thread_get();
	bool resumed_current_is_sender = current == claimed_sender;
	int sender_prio =
		sdc_hci_remove_iso_path_trace_scheduler_unlock_thread_priority_get(claimed_sender);
	int resumed_current_prio =
		sdc_hci_remove_iso_path_trace_scheduler_unlock_thread_priority_get(current);
	int mpsl_prio = 0;
	unsigned int mpsl_switches_during_unlock = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_scheduler_unlock_last_switches);

	if (mpsl_thread != NULL) {
		mpsl_prio = sdc_hci_remove_iso_path_trace_scheduler_unlock_thread_priority_get(
			mpsl_thread);
	}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_unlock_state_observed(
		busy, mpsl_state, resumed_current_is_sender, mpsl_switches_during_unlock,
		sender_prio, resumed_current_prio, mpsl_prio);
#endif
	if (mpsl_thread != NULL) {
		LOG_INF("SDC LE Remove ISO Data Path k_sched_unlock state: busy=0x%x mpsl_state=%s "
			"resumed_current_is_sender=%d mpsl_switches_during_unlock=%u "
			"sender_prio=%d resumed_current_prio=%d mpsl_prio=%d",
			(unsigned int)busy, mpsl_state, resumed_current_is_sender ? 1 : 0,
			mpsl_switches_during_unlock, sender_prio, resumed_current_prio, mpsl_prio);
	} else {
		LOG_INF("SDC LE Remove ISO Data Path k_sched_unlock state: busy=0x%x mpsl_state=%s "
			"resumed_current_is_sender=%d mpsl_switches_during_unlock=%u "
			"sender_prio=%d resumed_current_prio=%d mpsl_prio=unavailable",
			(unsigned int)busy, mpsl_state, resumed_current_is_sender ? 1 : 0,
			mpsl_switches_during_unlock, sender_prio, resumed_current_prio);
	}
}

static void sdc_hci_remove_iso_path_trace_scheduler_yield_arm(k_tid_t sender)
{
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_sender, sender);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_window_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_switches, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_last_switches, 0);

	LOG_INF("SDC LE Remove ISO Data Path post-unlock yield trace armed");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_yield_armed();
#endif
}

static void sdc_hci_remove_iso_path_trace_scheduler_yield_observe_state(k_tid_t claimed_sender)
{
	struct k_work *target_work =
		(struct k_work *)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_scheduler_target);
	int busy = target_work == NULL
			   ? 0
			   : sdc_hci_remove_iso_path_trace_scheduler_unlock_busy_get(target_work);
	k_tid_t mpsl_thread =
		(k_tid_t)atomic_ptr_get(&sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread);
	char mpsl_state_buf[SDC_HCI_REMOVE_ISO_PATH_TRACE_MPSL_STATE_MAX];
	const char *mpsl_state = "unavailable";

	if (mpsl_thread != NULL) {
		const char *state = sdc_hci_remove_iso_path_trace_scheduler_unlock_state_str(
			mpsl_thread, mpsl_state_buf, sizeof(mpsl_state_buf));

		if (state != NULL && state[0] != '\0') {
			mpsl_state = state;
		} else {
			mpsl_state = "unknown";
		}
	}

	k_tid_t current = sdc_hci_remove_iso_path_trace_current_thread_get();
	bool resumed_current_is_sender = current == claimed_sender;
	unsigned int mpsl_switches_during_yield = (unsigned int)atomic_get(
		&sdc_hci_remove_iso_path_trace_scheduler_yield_last_switches);

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_yield_state_observed(
		busy, mpsl_state, resumed_current_is_sender, mpsl_switches_during_yield);
#endif
	LOG_INF("SDC LE Remove ISO Data Path post-unlock yield state: busy=0x%x mpsl_state=%s "
		"resumed_current_is_sender=%d mpsl_switches_during_yield=%u",
		(unsigned int)busy, mpsl_state, resumed_current_is_sender ? 1 : 0,
		mpsl_switches_during_yield);
}

#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
static bool sdc_hci_remove_iso_path_trace_test_scheduler_unlock_mode;

void sdc_hci_remove_iso_path_trace_test_set_scheduler_unlock_mode(bool enabled)
{
	sdc_hci_remove_iso_path_trace_test_scheduler_unlock_mode = enabled;
}

void sdc_hci_remove_iso_path_trace_test_reset(void)
{
	atomic_set(&sdc_hci_remove_iso_path_trace_msg_get_pending, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_lock_release_pending, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_work_submit_pending, 0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
	sdc_hci_remove_iso_path_trace_receive_disposition_clear();
#endif
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_sender, NULL);
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                          \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK)
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_target, NULL);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_mpsl_thread, NULL);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_sender, NULL);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_window_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_switches, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_last_switches, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_window_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_switches, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_last_switches, 0);
#endif
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST)
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_snapshot_target, NULL);
#endif
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED)
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_diag_context_clear();
#endif
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed, 0);
#endif
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed, 0);
	for (size_t i = 0; i < ARRAY_SIZE(sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots);
	     i++) {
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_slots[i], NULL);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stages[i],
			   SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_UNCLASSIFIED);
#endif
	}
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_unavailable, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_first_free, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_disable_snapshot_armed, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_allocations, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_outstanding, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_high_water, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_unrefs, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_callbacks_total, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_tracking_error, 0);
#endif
	sdc_hci_remove_iso_path_trace_test_scheduler_unlock_mode = false;
}
#endif

static bool sdc_hci_remove_iso_path_trace_scheduler_unlock_selected(void)
{
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK)
	return true;
#elif defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK)
	return sdc_hci_remove_iso_path_trace_test_scheduler_unlock_mode;
#else
	return false;
#endif
}

static k_tid_t sdc_hci_remove_iso_path_trace_scheduler_unlock_claim(void)
{
	k_tid_t current = sdc_hci_remove_iso_path_trace_current_thread_get();

	if (current != NULL &&
	    atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_scheduler_sender, current, NULL)) {
		return current;
	}

	return NULL;
}

void __real_k_sched_unlock(void);

void __wrap_k_sched_unlock(void)
{
	k_tid_t claimed_sender = sdc_hci_remove_iso_path_trace_scheduler_unlock_claim();
	bool trace_unlock = claimed_sender != NULL;

	if (trace_unlock) {
		LOG_INF("SDC LE Remove ISO Data Path k_sched_unlock entry");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
		sdc_hci_remove_iso_path_trace_scheduler_unlock_observed(true);
#endif
		atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_switches, 0);
		atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_window_active, 1);
	}

	__real_k_sched_unlock();

	if (trace_unlock) {
		atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_window_active, 0);
		atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_unlock_last_switches,
			   atomic_get(&sdc_hci_remove_iso_path_trace_scheduler_unlock_switches));
		sdc_hci_remove_iso_path_trace_scheduler_unlock_observe_state(claimed_sender);
		LOG_INF("SDC LE Remove ISO Data Path k_sched_unlock return");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
		sdc_hci_remove_iso_path_trace_scheduler_unlock_observed(false);
#endif
		sdc_hci_remove_iso_path_trace_scheduler_yield_arm(claimed_sender);
	}
}

void __real_z_impl_k_yield(void);

void __wrap_z_impl_k_yield(void)
{
	k_tid_t current = sdc_hci_remove_iso_path_trace_current_thread_get();
	bool trace_yield = current != NULL &&
			   atomic_ptr_cas(&sdc_hci_remove_iso_path_trace_scheduler_yield_sender,
					  current, NULL);

	if (!trace_yield) {
		__real_z_impl_k_yield();
		return;
	}

	LOG_INF("SDC LE Remove ISO Data Path post-unlock yield entry");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_yield_observed(true);
#endif
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_switches, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_window_active, 1);

	__real_z_impl_k_yield();

	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_window_active, 0);
	atomic_set(&sdc_hci_remove_iso_path_trace_scheduler_yield_last_switches,
		   atomic_get(&sdc_hci_remove_iso_path_trace_scheduler_yield_switches));
	sdc_hci_remove_iso_path_trace_scheduler_yield_observe_state(current);
	LOG_INF("SDC LE Remove ISO Data Path post-unlock yield return");
#ifdef SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST
	sdc_hci_remove_iso_path_trace_scheduler_yield_observed(false);
#endif
}
#endif

uint8_t
__real_sdc_hci_cmd_le_remove_iso_data_path(const sdc_hci_cmd_le_remove_iso_data_path_t *p_params,
					   sdc_hci_cmd_le_remove_iso_data_path_return_t *p_return);

uint8_t
__wrap_sdc_hci_cmd_le_remove_iso_data_path(const sdc_hci_cmd_le_remove_iso_data_path_t *p_params,
					   sdc_hci_cmd_le_remove_iso_data_path_return_t *p_return)
{
	LOG_INF("SDC LE Remove ISO Data Path trace entry");
	uint8_t status = __real_sdc_hci_cmd_le_remove_iso_data_path(p_params, p_return);

	LOG_INF("SDC LE Remove ISO Data Path trace return: status=0x%02x", status);
	return status;
}

int __real_hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out);

void __real_multithreading_lock_release(void);

int __real_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work);

void __wrap_multithreading_lock_release(void)
{
	bool trace_lock_release =
		atomic_cas(&sdc_hci_remove_iso_path_trace_lock_release_pending, 1, 0);

	if (trace_lock_release) {
		LOG_INF("SDC LE Remove ISO Data Path multithreading_lock_release entry");
	}

	__real_multithreading_lock_release();

	if (trace_lock_release) {
		LOG_INF("SDC LE Remove ISO Data Path multithreading_lock_release return");
		atomic_set(&sdc_hci_remove_iso_path_trace_work_submit_pending, 1);
	}
}

int __wrap_k_work_submit_to_queue(struct k_work_q *queue, struct k_work *work)
{
	bool trace_work_submit =
		atomic_cas(&sdc_hci_remove_iso_path_trace_work_submit_pending, 1, 0);

	if (trace_work_submit) {
		LOG_INF("SDC LE Remove ISO Data Path k_work_submit_to_queue entry");
		atomic_set(&sdc_hci_remove_iso_path_trace_msg_get_pending, 1);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
		sdc_hci_remove_iso_path_trace_receive_disposition_clear();
#endif
	}

	int ret = __real_k_work_submit_to_queue(queue, work);

	if (trace_work_submit) {
		LOG_INF("SDC LE Remove ISO Data Path k_work_submit_to_queue return: status=%d",
			ret);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
		if (ret >= 0) {
			sdc_hci_remove_iso_path_trace_receive_arm(work, queue);
		}
#endif
		if (ret < 0) {
			atomic_set(&sdc_hci_remove_iso_path_trace_msg_get_pending, 0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
			sdc_hci_remove_iso_path_trace_receive_disposition_clear();
#endif
		}
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_SCHEDULER_UNLOCK) ||                          \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST_SCHEDULER_UNLOCK)
		else if (sdc_hci_remove_iso_path_trace_scheduler_unlock_selected()) {
			sdc_hci_remove_iso_path_trace_scheduler_unlock_arm(
				sdc_hci_remove_iso_path_trace_current_thread_get(), work, queue);
		}
#endif
		else {
#if defined(CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE_WORK_STATE_SNAPSHOT) ||                       \
	defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_TEST)
			atomic_ptr_set(&sdc_hci_remove_iso_path_trace_snapshot_target, work);
			int snapshot_status =
				sdc_hci_remove_iso_path_trace_snapshot_schedule(work, K_MSEC(250));

			LOG_INF("SDC LE Remove ISO Data Path work state snapshot scheduled: "
				"status=%d",
				snapshot_status);
#endif
		}
	}

	return ret;
}

int __wrap_hci_internal_msg_get(uint8_t *msg_out, sdc_hci_msg_type_t *msg_type_out)
{
	bool trace_msg_get = atomic_cas(&sdc_hci_remove_iso_path_trace_msg_get_pending, 1, 0);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
	bool trace_receive_fetch =
		sdc_hci_remove_iso_path_trace_receive_target_context(NULL) &&
		atomic_cas(&sdc_hci_remove_iso_path_trace_receive_fetch_observed, 0, 1);

	if (trace_receive_fetch) {
		sdc_hci_remove_iso_path_trace_receive_fetch_entry();
	}
#endif

	if (trace_msg_get) {
		LOG_INF("SDC LE Remove ISO Data Path hci_internal_msg_get entry");
	}

	int ret = __real_hci_internal_msg_get(msg_out, msg_type_out);

	if (trace_msg_get) {
		LOG_INF("SDC LE Remove ISO Data Path hci_internal_msg_get return: status=%d", ret);
	}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
	if (trace_receive_fetch) {
		bool msg_type_valid = ret == 0 && msg_type_out != NULL;
		unsigned int msg_type = msg_type_valid ? (unsigned int)*msg_type_out : 0U;

		sdc_hci_remove_iso_path_trace_receive_fetch_return(ret, msg_type_valid, msg_type);
		if (ret != 0 || !msg_type_valid ||
		    !sdc_hci_remove_iso_path_trace_receive_msg_type_supported(*msg_type_out)) {
			sdc_hci_remove_iso_path_trace_receive_disposition_clear();
		}
	}
#endif

	if (trace_msg_get && ret == 0 && msg_out != NULL && msg_type_out != NULL &&
	    *msg_type_out == SDC_HCI_MSG_TYPE_EVT && msg_out[0] == BT_HCI_EVT_CMD_COMPLETE &&
	    msg_out[1] >= 4U && sys_get_le16(&msg_out[3]) == 0x206fU) {
		LOG_INF("SDC LE Remove ISO Data Path completion: status=0x%02x", msg_out[5]);
	}

	return ret;
}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_RECEIVE_DISPOSITION_ENABLED)
struct net_buf *__real_bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout);

struct net_buf *__real_bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout);

struct net_buf *__wrap_bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout)
{
	bool trace_event =
		sdc_hci_remove_iso_path_trace_receive_target_context(NULL) &&
		atomic_get(&sdc_hci_remove_iso_path_trace_receive_armed) != 0 &&
		atomic_get(&sdc_hci_remove_iso_path_trace_receive_allocation_observed) == 0;

	if (trace_event) {
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_event_thread,
			       sdc_hci_remove_iso_path_trace_receive_current_thread_get());
		atomic_set(&sdc_hci_remove_iso_path_trace_receive_event_active, 1);
	}

	struct net_buf *buf = __real_bt_buf_get_evt(evt, discardable, timeout);

	if (trace_event) {
		atomic_set(&sdc_hci_remove_iso_path_trace_receive_event_active, 0);
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_receive_event_thread, NULL);

		int target_busy = 0;
		if (sdc_hci_remove_iso_path_trace_receive_allocation_claim(&target_busy)) {
			sdc_hci_remove_iso_path_trace_receive_evt_allocation(
				evt, discardable, buf != NULL, target_busy);
			sdc_hci_remove_iso_path_trace_receive_disposition_clear();
		}
	}

	return buf;
}

struct net_buf *__wrap_bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout)
{
	bool nested_event = sdc_hci_remove_iso_path_trace_receive_event_is_nested();
	struct net_buf *buf = __real_bt_buf_get_rx(type, timeout);

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED)
	if (type == BT_BUF_ISO_IN && buf != NULL) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_track(buf);
	} else if (type == BT_BUF_ISO_IN && buf == NULL &&
		   atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_armed) != 0 &&
		   sdc_hci_remove_iso_path_trace_receive_target_context(NULL) &&
		   atomic_cas(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_unavailable, 0, 1)) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_snapshot("unavailable");
	}
#endif

	if (!nested_event) {
		int target_busy = 0;
		if (sdc_hci_remove_iso_path_trace_receive_allocation_claim(&target_busy)) {
			sdc_hci_remove_iso_path_trace_receive_rx_allocation(
				(unsigned int)type, buf != NULL, target_busy);
			sdc_hci_remove_iso_path_trace_receive_disposition_clear();
		}
	}

	return buf;
}
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_ENABLED)
void __real_net_buf_unref(struct net_buf *buf);

void __wrap_net_buf_unref(struct net_buf *buf)
{
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_final_release(buf);
	__real_net_buf_unref(buf);
}
#endif

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_DISPOSITION_ENABLED)
struct bt_conn;

void __real_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf, uint8_t flags);

void __wrap_bt_conn_recv(struct bt_conn *conn, struct net_buf *buf, uint8_t flags)
{
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	k_tid_t current = k_current_get();

	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf, buf);
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread, current);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
	atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem, NULL);
#endif
	atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active, 1);
#endif
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
		buf, SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_DISPATCHED);
	__real_bt_conn_recv(conn, buf, flags);
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
	if (sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_context_matches(buf)) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf, SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED);
	}
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_diag_context_clear();
#else
	sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
		buf, SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_HOST_RETURNED);
#endif
}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENABLED)
bool __real_k_work_flush(struct k_work *work, struct k_work_sync *sync);

bool __wrap_k_work_flush(struct k_work *work, struct k_work_sync *sync)
{
	k_tid_t current = k_current_get();
	struct net_buf *buf = (struct net_buf *)atomic_ptr_get(
		&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf);
	bool trace = atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active) != 0 &&
		     buf != NULL &&
		     atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread) ==
			     current;

	if (trace) {
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem,
			       sync == NULL ? NULL : &sync->flusher.sem);
#endif
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf, SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_ENTERED);
	}

	bool result = __real_k_work_flush(work, sync);

	if (trace && atomic_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_active) != 0 &&
	    atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_buf) == buf &&
	    atomic_ptr_get(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_thread) == current) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_RETURNED);
	}
#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
	if (trace) {
		atomic_ptr_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_recv_flush_sem, NULL);
	}
#endif

	return result;
}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENABLED)
int __real_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout);

int __wrap_z_impl_k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	struct net_buf *buf = NULL;
	bool trace = sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sem_context_matches(sem, timeout,
										       &buf);

	if (trace) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_ENTERED);
	}

	int ret = __real_z_impl_k_sem_take(sem, timeout);

	if (trace &&
	    sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sem_context_matches(sem, timeout, &buf)) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_RETURNED);
	}

	return ret;
}

#if defined(SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENABLED)
int __real_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key, _wait_q_t *wait_q,
		       k_timeout_t timeout);

int __wrap_z_pend_curr(struct k_spinlock *lock, k_spinlock_key_t key, _wait_q_t *wait_q,
		       k_timeout_t timeout)
{
	struct net_buf *buf = NULL;
	bool trace = sdc_hci_remove_iso_path_trace_iso_rx_lifetime_pend_context_matches(
		wait_q, timeout, &buf);

	if (trace) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_ENTERED);
#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed, 1);
#endif
	}

	int ret = __real_z_pend_curr(lock, key, wait_q, timeout);

#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
	if (trace) {
		atomic_set(&sdc_hci_remove_iso_path_trace_iso_rx_lifetime_sched_pend_hook_armed, 0);
	}
#endif

	if (trace && sdc_hci_remove_iso_path_trace_iso_rx_lifetime_pend_context_matches(
			     wait_q, timeout, &buf)) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_RETURNED);
	}

	return ret;
}
#endif

#if defined(                                                                                       \
	SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_PEND_SCHED_GIVE_ENABLED)
void __real_z_impl_k_sem_give(struct k_sem *sem);

void __wrap_z_impl_k_sem_give(struct k_sem *sem)
{
	struct net_buf *buf = NULL;
	bool trace = sdc_hci_remove_iso_path_trace_iso_rx_lifetime_give_context_matches(sem, &buf);

	if (trace) {
		sdc_hci_remove_iso_path_trace_iso_rx_lifetime_stage_advance(
			buf,
			SDC_HCI_REMOVE_ISO_PATH_TRACE_ISO_RX_LIFETIME_TX_NOTIFY_FLUSH_SEMAPHORE_GIVE_ENTERED);
	}

	__real_z_impl_k_sem_give(sem);
}
#endif
#endif
#endif
#endif

int __real_hci_internal_cmd_put(uint8_t *cmd_in);

int __wrap_hci_internal_cmd_put(uint8_t *cmd_in)
{
	bool is_target = cmd_in != NULL && sys_get_le16(cmd_in) == 0x206fU;

	if (is_target) {
		LOG_INF("SDC LE Remove ISO Data Path hci_internal_cmd_put entry");
	}

	int ret = __real_hci_internal_cmd_put(cmd_in);

	if (is_target) {
		if (ret == 0) {
			atomic_set(&sdc_hci_remove_iso_path_trace_lock_release_pending, 1);
		}
		LOG_INF("SDC LE Remove ISO Data Path hci_internal_cmd_put return: status=%d", ret);
	}

	return ret;
}
