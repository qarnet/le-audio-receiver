/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable pairing-mode transition owner (P1).  See pairing_mode.h and
 * docs/development/user-pairing-control-plan.md.
 *
 * Sole owner of the NORMAL / BONDING / RESETTING modes, their asynchronous
 * transition phases, LED patterns, supersession, completion, and fatal
 * recovery policy.  All platform side effects go through injected
 * operations; this file contains no Bluetooth, GPIO, or devicetree types.
 *
 * Event serialization
 * -------------------
 * One private work queue owned by the controller.  Public request and
 * notification functions are callable from any thread or work-queue
 * context; they set bits in an atomic pending mask and submit ONE drain
 * work item.  All transitions run in the controller work-queue thread;
 * no user/BT callback ever becomes a transition owner.
 *
 * Event ordering (documented contract):
 *  - The drain handler consumes the whole pending mask per pass and
 *    processes bits in strict priority order: RESET (incl. sync) >
 *    BONDING > CONNECTED > DISCONNECTED > PAIRING_COMPLETE >
 *    SECURITY_CHANGED > PAIRING_FAILED > START.  RESET therefore has
 *    priority over a concurrently pending BONDING.
 *  - A RESET supersedes a mid-flight BONDING transition: it bumps the
 *    transition generation, so delayed BONDING LED/feedback work from the
 *    older generation is rejected when it fires (generation check).
 *  - Notifications carrying a boolean payload (pairing_complete,
 *    security_changed) collapse to the latest value; idempotent duplicate
 *    notifications may collapse, but no required RESET, disconnect,
 *    completion, or fatal event is ever lost (the state machine consumes
 *    every set bit, and a reset request is never dropped once initiated).
 *  - The drain never blocks: a transition that needs a disconnect simply
 *    enters WAIT_DISCONNECT_* and returns; the matching disconnected
 *    notification advances it.
 *  - A disconnected notification in an idle phase (NORMAL/IDLE or
 *    BONDING/IDLE) with a real prior connection restarts advertising once
 *    through the injected advertising_start operation (P5: the main loop
 *    no longer restarts on its own under the full-stack gate).  Stale or
 *    duplicate disconnects with no prior connection are a no-op; the
 *    idle restart never mutates mode/access/LED/generation.
 *  - Synchronous reset waits on a completion event from the CALLER's
 *    thread; the controller never blocks and holds no lock while the
 *    caller waits.
 *  - Work-queue submission failure or an impossible pending-mask state is
 *    fatal (cold reboot exactly once).
 *
 * LED pattern timing
 * ------------------
 * The controller owns logical pattern timing through ops->led_set:
 *  - NORMAL: inactive.
 *  - BONDING: immediately active on entry, then toggles every
 *    USER_PAIRING_BOND_LED_HALF_PERIOD_MS.
 *  - RESETTING: immediately active, then toggles every
 *    USER_PAIRING_RESET_LED_HALF_PERIOD_MS for exactly
 *    RESET_FEEDBACK_MS / HALF_PERIOD half-periods.  Interpretation: the
 *    rapid work performs (FEEDBACK/HALF - 1) toggles (nine at production
 *    defaults: 100..900 ms), which completes five full flashes; the
 *    reset-feedback expiry work at RESET_FEEDBACK_MS is the authoritative
 *    end (it cancels the rapid work and re-arms BONDING).  This keeps
 *    every rapid toggle strictly before the expiry deadline, so no
 *    same-deadline work-queue ordering can corrupt the count.
 */

#include "pairing_mode.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(pairing_mode, LOG_LEVEL_INF);

/* ── Relational timing validation (Kconfig cannot express these) ── */

BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_HOLD_MS > CONFIG_USER_PAIRING_BOND_HOLD_MS,
	     "USER_PAIRING_RESET_HOLD_MS must exceed USER_PAIRING_BOND_HOLD_MS");
BUILD_ASSERT(CONFIG_USER_PAIRING_BOND_HOLD_MS > 0, "USER_PAIRING_BOND_HOLD_MS must be nonzero");
BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_HOLD_MS > 0, "USER_PAIRING_RESET_HOLD_MS must be nonzero");
BUILD_ASSERT(CONFIG_USER_PAIRING_BOND_LED_HALF_PERIOD_MS > 0,
	     "USER_PAIRING_BOND_LED_HALF_PERIOD_MS must be nonzero");
BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS > 0,
	     "USER_PAIRING_RESET_LED_HALF_PERIOD_MS must be nonzero");
BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_FEEDBACK_MS > 0,
	     "USER_PAIRING_RESET_FEEDBACK_MS must be nonzero");
BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_FEEDBACK_MS % CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS ==
		     0,
	     "USER_PAIRING_RESET_FEEDBACK_MS must be divisible by "
	     "USER_PAIRING_RESET_LED_HALF_PERIOD_MS");
BUILD_ASSERT(CONFIG_USER_PAIRING_RESET_FEEDBACK_MS / CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS >=
		     2,
	     "reset feedback must span at least two reset LED half-periods");
BUILD_ASSERT(CONFIG_USER_PAIRING_WORKQ_STACK_SIZE >= 1024,
	     "USER_PAIRING_WORKQ_STACK_SIZE is too small");

/* Rapid pattern toggles: (FEEDBACK/HALF - 1), see module comment. */
#define RESET_RAPID_TOGGLES                                                                        \
	(CONFIG_USER_PAIRING_RESET_FEEDBACK_MS / CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS - 1)

/* ── Pending-event mask bits ─────────────────────────────────────── */

#define EV_START            BIT(0)
#define EV_BONDING          BIT(1)
#define EV_RESET            BIT(2)
#define EV_RESET_SYNC       BIT(3)
#define EV_CONNECTED        BIT(4)
#define EV_DISCONNECTED     BIT(5)
#define EV_PAIRING_COMPLETE BIT(6)
#define EV_PAIRING_FAILED   BIT(7)
#define EV_SECURITY_CHANGED BIT(8)

#define EV_KNOWN_MASK                                                                              \
	(EV_START | EV_BONDING | EV_RESET | EV_RESET_SYNC | EV_CONNECTED | EV_DISCONNECTED |       \
	 EV_PAIRING_COMPLETE | EV_PAIRING_FAILED | EV_SECURITY_CHANGED)

/* ── Controller state ────────────────────────────────────────────── */

/* Read/written ONLY in the controller work-queue thread (the drain and the
 * delayed-work handlers); get_status() copies them under g_status_lock. */
static struct pairing_mode_status g_status;

static struct k_spinlock g_status_lock;

/* Set once by a successful init; immutable afterwards. */
static const struct pairing_mode_ops *g_ops;
static void *g_ctx;

static atomic_t g_initialized;
static atomic_t g_fatal;
static atomic_t g_pending;
static bool g_wq_started;
static bool g_started;

/* Notification payloads (latest value wins, see module comment). */
static atomic_t g_pairing_complete_bonded;
static atomic_t g_security_success;
static atomic_t g_security_bonded;

/* True while the current transition is a RESET (reset completion path
 * signals synchronous waiters).  Written only by the drain (work thread);
 * read by the feedback-expiry handler (same thread). */
static bool g_reset_transition;

/* ── Synchronous reset waiter (single slot, one shell) ───────────── */

#define SYNC_DONE_BIT BIT(0)

static struct k_spinlock g_sync_lock;
static struct k_event *g_sync_waiter;
static bool g_sync_active;

/* ── Work queue and work items ───────────────────────────────────── */

static K_THREAD_STACK_DEFINE(g_pairing_stack, CONFIG_USER_PAIRING_WORKQ_STACK_SIZE);
static struct k_work_q g_pairing_wq;

static struct k_work g_drain_work;

/* Delayed work items carry the generation captured at schedule time so a
 * stale item from an older generation can never modify a newer one. */
struct pairing_dwork {
	struct k_work_delayable dwork;
	uint32_t gen;
	bool rapid; /* blink work: true = rapid reset pattern */
	uint8_t toggles;
};

static struct pairing_dwork g_blink;
static struct pairing_dwork g_feedback;

/* ── Operation failure accounting ────────────────────────────────── */

enum pairing_op {
	OP_ENQUEUE,
	OP_START_SET_ACCESS,
	OP_START_LED,
	OP_START_ADVERTISING,
	OP_BOND_SUSPEND,
	OP_BOND_SET_ACCESS,
	OP_BOND_DISCONNECT,
	OP_BOND_ENTRY_ACCESS,
	OP_BOND_ENTRY_LED,
	OP_BOND_ENTRY_ADVERTISING,
	OP_RESET_SUSPEND,
	OP_RESET_SET_ACCESS,
	OP_RESET_DISCONNECT,
	OP_RESET_DELETE_BONDS,
	OP_RESET_FEEDBACK_LED,
	OP_RESET_ENTRY_ACCESS,
	OP_RESET_ENTRY_LED,
	OP_RESET_ENTRY_ADVERTISING,
	OP_STALE_SUSPEND,
	OP_STALE_DISCONNECT,
	OP_IDLE_RESTART_ADVERTISING,
	OP_CONNECTED_SECURITY,
	OP_COMPLETE_ACCESS,
	OP_COMPLETE_LED,
	OP_BLINK_LED,
};

/* ── Forward declarations ────────────────────────────────────────── */

static void drain_handler(struct k_work *work);
static void blink_handler(struct k_work *work);
static void feedback_handler(struct k_work *work);
static void fatal_finalize(enum pairing_op op, int err);
static void finish_bonding_entry(enum pairing_op access_op, enum pairing_op led_op,
				 enum pairing_op advertising_op);
static void start_reset_feedback(void);
static int led_set_active(bool active, enum pairing_op op);

/* ── Small state helpers (work-thread context) ───────────────────── */

/* All status writers run on the controller work thread; the lock only
 * protects get_status() readers.  The struct is copied whole under the
 * lock, so single-field writes are never observed partially. */

static void status_write_mode(enum pairing_mode mode)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.mode = mode;
	k_spin_unlock(&g_status_lock, key);
}

static void status_write_phase(enum pairing_mode_phase phase)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.phase = phase;
	k_spin_unlock(&g_status_lock, key);
}

static void status_write_access(enum pairing_access_mode access)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.access_mode = access;
	k_spin_unlock(&g_status_lock, key);
}

static void status_write_connected(bool connected)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.connected = connected;
	k_spin_unlock(&g_status_lock, key);
}

static void status_write_led(bool active)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.led_active = active;
	k_spin_unlock(&g_status_lock, key);
}

static void status_bump_generation(void)
{
	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	g_status.transition_generation++;
	k_spin_unlock(&g_status_lock, key);
}

static uint32_t generation_now(void)
{
	return g_status.transition_generation;
}

/* ── Operation execution (work-thread context) ───────────────────── */

static int op_set_access(enum pairing_access_mode mode)
{
	return g_ops->set_access_mode(mode, g_ctx);
}

static int op_advertising_suspend(void)
{
	return g_ops->advertising_suspend(g_ctx);
}

static int op_advertising_start(void)
{
	return g_ops->advertising_start(g_ctx);
}

static int op_disconnect_peer(bool *pending)
{
	return g_ops->disconnect_peer(pending, g_ctx);
}

static int op_delete_all_bonds(void)
{
	return g_ops->delete_all_bonds(g_ctx);
}

static int op_request_security(void)
{
	return g_ops->request_security(g_ctx);
}

static int op_led_set(bool active)
{
	return g_ops->led_set(active, g_ctx);
}

/* ── Synchronous waiter signaling ────────────────────────────────── */

static void post_sync_done(void)
{
	struct k_event *waiter;

	k_spinlock_key_t key = k_spin_lock(&g_sync_lock);

	waiter = g_sync_active ? g_sync_waiter : NULL;
	k_spin_unlock(&g_sync_lock, key);

	if (waiter != NULL) {
		k_event_post(waiter, SYNC_DONE_BIT);
	}
}

/* ── Event enqueue (public API side, any thread) ─────────────────── */

static int enqueue(atomic_t bit)
{
	int ret;

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}
	if (atomic_get(&g_fatal)) {
		return -ECANCELED;
	}

	atomic_or(&g_pending, bit);
	ret = k_work_submit_to_queue(&g_pairing_wq, &g_drain_work);
	if (ret < 0) {
		/* Impossible state: work-queue submission failure is fatal.
		 * Runs from the caller's thread; the CAS guard keeps the
		 * finalizer single-execution. */
		fatal_finalize(OP_ENQUEUE, -ret);
		return -ECANCELED;
	}

	return 0;
}

/* ── Fatal recovery ──────────────────────────────────────────────── */

static void fatal_finalize(enum pairing_op op, int err)
{
	if (!atomic_cas(&g_fatal, 0, 1)) {
		return; /* already fatal: cold reboot already invoked */
	}

	LOG_ERR("pairing_mode FATAL: op=%d err=%d mode=%d phase=%d "
		"access=%d connected=%d generation=%u",
		(int)op, err, (int)g_status.mode, (int)g_status.phase, (int)g_status.access_mode,
		(int)g_status.connected, (unsigned int)g_status.transition_generation);

	{
		k_spinlock_key_t key = k_spin_lock(&g_status_lock);

		g_status.fatal = true;
		g_status.phase = PAIRING_MODE_PHASE_FATAL;
		k_spin_unlock(&g_status_lock, key);
	}

	/* Attempt to force the LED inactive; the result is best-effort. */
	if (g_ops != NULL && g_ops->led_set != NULL) {
		(void)op_led_set(false);
	}

	/* Cold reboot exactly once.  If the fake reboot returns, g_fatal
	 * stays set: no later event may execute platform operations. */
	if (g_ops != NULL && g_ops->cold_reboot != NULL) {
		g_ops->cold_reboot(g_ctx);
	}
}

/* ── Work-queue handlers ─────────────────────────────────────────── */

/* BONDING entry shared by the bonding disconnect completion and the reset
 * feedback expiry.  Order is fixed: access BONDING, mode BONDING, LED
 * active + slow pattern, advertising start, phase IDLE. */
static void finish_bonding_entry(enum pairing_op access_op, enum pairing_op led_op,
				 enum pairing_op advertising_op)
{
	int ret;

	ret = op_set_access(PAIRING_ACCESS_BONDING);
	if (ret < 0) {
		fatal_finalize(access_op, -ret);
		return;
	}

	status_write_mode(PAIRING_MODE_BONDING);
	status_write_access(PAIRING_ACCESS_BONDING);

	if (led_set_active(true, led_op)) {
		return; /* fatal already finalized */
	}

	/* Slow blink: immediate active + toggle every bond half-period.
	 * The work self-reschedules; generation guards against staleness. */
	g_blink.gen = generation_now();
	g_blink.rapid = false;
	g_blink.toggles = 0;
	(void)k_work_cancel_delayable(&g_blink.dwork);
	ret = k_work_schedule_for_queue(&g_pairing_wq, &g_blink.dwork,
					K_MSEC(CONFIG_USER_PAIRING_BOND_LED_HALF_PERIOD_MS));
	if (ret < 0) {
		fatal_finalize(led_op, -ret);
		return;
	}

	ret = op_advertising_start();
	if (ret < 0) {
		fatal_finalize(advertising_op, -ret);
		return;
	}

	status_write_phase(PAIRING_MODE_PHASE_IDLE);
}

static int led_set_active(bool active, enum pairing_op op)
{
	int ret = op_led_set(active);

	if (ret < 0) {
		fatal_finalize(op, -ret);
		return -1;
	}

	status_write_led(active);
	return 0;
}

/* Reset feedback entry: delete bonds (exactly once per accepted reset),
 * RESETTING/RESET_FEEDBACK, LED active + rapid pattern, feedback expiry. */
static void start_reset_feedback(void)
{
	int ret;

	ret = op_delete_all_bonds();
	if (ret < 0) {
		fatal_finalize(OP_RESET_DELETE_BONDS, -ret);
		return;
	}

	status_write_mode(PAIRING_MODE_RESETTING);
	status_write_phase(PAIRING_MODE_PHASE_RESET_FEEDBACK);

	if (led_set_active(true, OP_RESET_FEEDBACK_LED)) {
		return; /* fatal already finalized */
	}

	/* Rapid pattern: active at entry, then RESET_RAPID_TOGGLES toggles
	 * at every reset half-period, self-stopping.  The feedback expiry
	 * is the authoritative end (it cancels any remaining rapid work). */
	g_blink.gen = generation_now();
	g_blink.rapid = true;
	g_blink.toggles = 0;
	(void)k_work_cancel_delayable(&g_blink.dwork);
	ret = k_work_schedule_for_queue(&g_pairing_wq, &g_blink.dwork,
					K_MSEC(CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS));
	if (ret < 0) {
		fatal_finalize(OP_RESET_FEEDBACK_LED, -ret);
		return;
	}

	g_feedback.gen = generation_now();
	ret = k_work_schedule_for_queue(&g_pairing_wq, &g_feedback.dwork,
					K_MSEC(CONFIG_USER_PAIRING_RESET_FEEDBACK_MS));
	if (ret < 0) {
		fatal_finalize(OP_RESET_FEEDBACK_LED, -ret);
		return;
	}
}

/* Start sequence: access NORMAL, LED inactive, advertising, NORMAL/IDLE. */
static void do_start(void)
{
	int ret;

	ret = op_set_access(PAIRING_ACCESS_NORMAL);
	if (ret < 0) {
		fatal_finalize(OP_START_SET_ACCESS, -ret);
		return;
	}
	status_write_mode(PAIRING_MODE_NORMAL);
	status_write_access(PAIRING_ACCESS_NORMAL);

	if (led_set_active(false, OP_START_LED)) {
		return; /* fatal already finalized */
	}

	ret = op_advertising_start();
	if (ret < 0) {
		fatal_finalize(OP_START_ADVERTISING, -ret);
		return;
	}

	status_write_phase(PAIRING_MODE_PHASE_IDLE);
	g_started = true;
}
/* BONDING request processing (generation already bumped by the caller). */
static void do_bonding(void)
{
	bool pending = false;
	int ret;

	ret = op_advertising_suspend();
	if (ret < 0) {
		fatal_finalize(OP_BOND_SUSPEND, -ret);
		return;
	}

	ret = op_set_access(PAIRING_ACCESS_SUSPENDED);
	if (ret < 0) {
		fatal_finalize(OP_BOND_SET_ACCESS, -ret);
		return;
	}

	ret = op_disconnect_peer(&pending);
	if (ret < 0) {
		fatal_finalize(OP_BOND_DISCONNECT, -ret);
		return;
	}

	status_write_mode(PAIRING_MODE_BONDING);
	g_reset_transition = false;

	if (pending) {
		status_write_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING);
		return;
	}

	finish_bonding_entry(OP_BOND_ENTRY_ACCESS, OP_BOND_ENTRY_LED, OP_BOND_ENTRY_ADVERTISING);
}

/* RESET request processing (generation already bumped when initiating).
 * Returns true when a reset transition is now in progress (initiated or
 * joined) — the caller then drops any BONDING request in the same pass. */
static bool do_reset(void)
{
	bool pending = false;
	int ret;

	/* Join an already-initiated reset: no re-initiation, no extra
	 * generation bump, no second bond deletion. */
	if (g_status.phase == PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET ||
	    g_status.phase == PAIRING_MODE_PHASE_RESET_FEEDBACK) {
		return true;
	}

	status_bump_generation();

	ret = op_advertising_suspend();
	if (ret < 0) {
		fatal_finalize(OP_RESET_SUSPEND, -ret);
		return true;
	}

	ret = op_set_access(PAIRING_ACCESS_SUSPENDED);
	if (ret < 0) {
		fatal_finalize(OP_RESET_SET_ACCESS, -ret);
		return true;
	}

	ret = op_disconnect_peer(&pending);
	if (ret < 0) {
		fatal_finalize(OP_RESET_DISCONNECT, -ret);
		return true;
	}

	status_write_mode(PAIRING_MODE_RESETTING);
	status_write_access(PAIRING_ACCESS_SUSPENDED);
	g_reset_transition = true;

	if (pending) {
		status_write_phase(PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET);
		return true;
	}

	start_reset_feedback();
	return true;
}

/* Completion to NORMAL: access NORMAL, LED inactive; the active connection
 * stays (no suspend, no advertising start, no disconnect). */
static void do_complete_to_normal(void)
{
	int ret;

	(void)k_work_cancel_delayable(&g_blink.dwork);

	ret = op_set_access(PAIRING_ACCESS_NORMAL);
	if (ret < 0) {
		fatal_finalize(OP_COMPLETE_ACCESS, -ret);
		return;
	}

	status_write_mode(PAIRING_MODE_NORMAL);
	status_write_access(PAIRING_ACCESS_NORMAL);

	if (led_set_active(false, OP_COMPLETE_LED)) {
		return; /* fatal already finalized */
	}
}

/* One state-machine step over one pending-mask snapshot.  Returns true
 * when a fatal occurred (the drain must stop). */
static bool step(atomic_t ev)
{
	bool bonded;
	bool success;

	/* Unknown bits are an impossible state: fatal. */
	if ((ev & ~EV_KNOWN_MASK) != 0) {
		fatal_finalize(OP_START_SET_ACCESS, -EIO);
		return true;
	}

	if (!g_started) {
		if ((ev & EV_START) != 0) {
			do_start();
		}
		/* All other events before start are discarded. */
		return atomic_get(&g_fatal);
	}

	/* 1. RESET (highest priority; supersedes every BONDING transition). */
	if ((ev & (EV_RESET | EV_RESET_SYNC)) != 0) {
		if (do_reset()) {
			/* A reset is now in progress (or fatal): every other
			 * bit in this pass is superseded or stale. */
			return atomic_get(&g_fatal);
		}
	}

	/* 2. BONDING (dropped while a reset transition is in progress).
	 * The duplicate no-op is detected BEFORE the generation bump so a
	 * redundant request cannot invalidate the running slow-blink work. */
	if ((ev & EV_BONDING) != 0) {
		if (g_status.phase == PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET ||
		    g_status.phase == PAIRING_MODE_PHASE_RESET_FEEDBACK) {
			/* RESET supersedes: the request is discarded. */
		} else if (g_status.mode == PAIRING_MODE_BONDING &&
			   g_status.phase == PAIRING_MODE_PHASE_IDLE && !g_status.connected) {
			/* Duplicate BONDING while already BONDING and not
			 * connected: no-op (no generation bump). */
		} else {
			status_bump_generation();
			do_bonding();
			if (atomic_get(&g_fatal)) {
				return true;
			}
		}
	}

	/* 3. CONNECTED. */
	if ((ev & EV_CONNECTED) != 0) {
		status_write_connected(true);

		if (g_status.mode == PAIRING_MODE_BONDING &&
		    g_status.phase == PAIRING_MODE_PHASE_IDLE) {
			int ret = op_request_security();

			if (ret < 0) {
				fatal_finalize(OP_CONNECTED_SECURITY, -ret);
				return true;
			}
		} else if (g_status.mode == PAIRING_MODE_NORMAL &&
			   g_status.phase == PAIRING_MODE_PHASE_IDLE) {
			/* NORMAL: no operation. */
		} else {
			/* Suspended/wait/reset phase: the connection is
			 * stale.  Suspend and disconnect again; never allow
			 * it to advance the wrong transition. */
			bool pending = false;
			int ret = op_advertising_suspend();

			if (ret < 0) {
				fatal_finalize(OP_STALE_SUSPEND, -ret);
				return true;
			}
			ret = op_disconnect_peer(&pending);
			if (ret < 0) {
				fatal_finalize(OP_STALE_DISCONNECT, -ret);
				return true;
			}
		}
	}

	/* 4. DISCONNECTED. */
	if ((ev & EV_DISCONNECTED) != 0) {
		bool was_connected = g_status.connected;

		status_write_connected(false);

		if (g_status.phase == PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING) {
			finish_bonding_entry(OP_BOND_ENTRY_ACCESS, OP_BOND_ENTRY_LED,
					     OP_BOND_ENTRY_ADVERTISING);
			if (atomic_get(&g_fatal)) {
				return true;
			}
		} else if (g_status.phase == PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET) {
			start_reset_feedback();
			if (atomic_get(&g_fatal)) {
				return true;
			}
		} else if (!was_connected) {
			/* Duplicate/stale disconnect with no prior connection:
			 * no-op.  P4 forwards exactly one matching disconnect
			 * after teardown; a second must never restart
			 * advertising (P5 ownership: main loop no longer
			 * restarts on its own). */
		} else if (g_status.phase == PAIRING_MODE_PHASE_IDLE &&
			   (g_status.mode == PAIRING_MODE_NORMAL ||
			    g_status.mode == PAIRING_MODE_BONDING)) {
			/* P5 idle disconnect restart: NORMAL/IDLE and
			 * BONDING/IDLE with a real prior connection call the
			 * injected advertising start exactly once, restoring
			 * BONDED_ONLY (NORMAL) or OPEN (BONDING) advertising
			 * after the peer dropped.  No mode, access, LED, or
			 * generation mutation during the idle restart; a
			 * restart failure is fatal through the dedicated
			 * operation context. */
			int ret = op_advertising_start();

			if (ret < 0) {
				fatal_finalize(OP_IDLE_RESTART_ADVERTISING, -ret);
				return true;
			}
		}
	}

	/* 5. PAIRING_COMPLETE (only bonded completion in BONDING/IDLE
	 * completes NORMAL; the active connection stays). */
	if ((ev & EV_PAIRING_COMPLETE) != 0) {
		bonded = atomic_get(&g_pairing_complete_bonded);

		if (bonded && g_status.mode == PAIRING_MODE_BONDING &&
		    g_status.phase == PAIRING_MODE_PHASE_IDLE) {
			do_complete_to_normal();
			if (atomic_get(&g_fatal)) {
				return true;
			}
		}
	}

	/* 6. SECURITY_CHANGED (success + bonded in BONDING/IDLE completes
	 * NORMAL; everything else stays BONDING). */
	if ((ev & EV_SECURITY_CHANGED) != 0) {
		success = atomic_get(&g_security_success);
		bonded = atomic_get(&g_security_bonded);

		if (success && bonded && g_status.mode == PAIRING_MODE_BONDING &&
		    g_status.phase == PAIRING_MODE_PHASE_IDLE) {
			do_complete_to_normal();
			if (atomic_get(&g_fatal)) {
				return true;
			}
		}
	}

	/* 7. PAIRING_FAILED: remains BONDING, never fatal. */
	if ((ev & EV_PAIRING_FAILED) != 0) {
		/* no-op: remote pairing failure is a normal outcome */
	}

	/* 8. START (idempotent after the first start). */
	if ((ev & EV_START) != 0 && !g_started) {
		do_start();
		if (atomic_get(&g_fatal)) {
			return true;
		}
	}

	return atomic_get(&g_fatal);
}

static void drain_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (atomic_get(&g_fatal)) {
		return;
	}

	for (;;) {
		atomic_t ev = atomic_set(&g_pending, 0);

		if (ev == 0) {
			break;
		}
		if (step(ev)) {
			break; /* fatal: no further operations */
		}
	}

	/* Events that arrived during processing are picked up by a fresh
	 * submission; the controller never spins here. */
	if (atomic_get(&g_pending) != 0 && !atomic_get(&g_fatal)) {
		int ret = k_work_submit_to_queue(&g_pairing_wq, &g_drain_work);

		if (ret < 0) {
			fatal_finalize(OP_ENQUEUE, -ret);
		}
	}
}

static void blink_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct pairing_dwork *pw = CONTAINER_OF(dwork, struct pairing_dwork, dwork);
	bool next_active;

	if (atomic_get(&g_fatal)) {
		return;
	}

	/* Stale work from an older transition generation: no-op. */
	if (pw->gen != generation_now()) {
		return;
	}

	if (pw->rapid) {
		if (g_status.phase != PAIRING_MODE_PHASE_RESET_FEEDBACK) {
			return; /* stale phase */
		}

		next_active = !g_status.led_active;
		if (led_set_active(next_active, OP_BLINK_LED)) {
			return; /* fatal already finalized */
		}
		pw->toggles++;

		if (pw->toggles < RESET_RAPID_TOGGLES) {
			if (k_work_schedule_for_queue(
				    &g_pairing_wq, &pw->dwork,
				    K_MSEC(CONFIG_USER_PAIRING_RESET_LED_HALF_PERIOD_MS)) < 0) {
				fatal_finalize(OP_BLINK_LED, -EIO);
			}
		}
	} else {
		if (g_status.mode != PAIRING_MODE_BONDING ||
		    g_status.phase != PAIRING_MODE_PHASE_IDLE) {
			return; /* stale phase (e.g. completed or superseded) */
		}

		next_active = !g_status.led_active;
		if (led_set_active(next_active, OP_BLINK_LED)) {
			return; /* fatal already finalized */
		}

		if (k_work_schedule_for_queue(&g_pairing_wq, &pw->dwork,
					      K_MSEC(CONFIG_USER_PAIRING_BOND_LED_HALF_PERIOD_MS)) <
		    0) {
			fatal_finalize(OP_BLINK_LED, -EIO);
		}
	}
}

static void feedback_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
	struct pairing_dwork *pw = CONTAINER_OF(dwork, struct pairing_dwork, dwork);

	if (atomic_get(&g_fatal)) {
		return;
	}

	/* Stale reset feedback from an older generation: no-op. */
	if (pw->gen != generation_now()) {
		return;
	}
	if (g_status.phase != PAIRING_MODE_PHASE_RESET_FEEDBACK) {
		return;
	}

	/* Reset the rapid pattern before the BONDING pattern. */
	(void)k_work_cancel_delayable(&g_blink.dwork);

	finish_bonding_entry(OP_RESET_ENTRY_ACCESS, OP_RESET_ENTRY_LED, OP_RESET_ENTRY_ADVERTISING);
	if (atomic_get(&g_fatal)) {
		return;
	}

	/* Complete every synchronous reset waiter only after BONDING
	 * advertising starts. */
	post_sync_done();
}

/* ── Public API ──────────────────────────────────────────────────── */

int pairing_mode_init(const struct pairing_mode_ops *ops, void *ctx)
{
	if (ops == NULL || ops->set_access_mode == NULL || ops->advertising_suspend == NULL ||
	    ops->advertising_start == NULL || ops->disconnect_peer == NULL ||
	    ops->delete_all_bonds == NULL || ops->request_security == NULL ||
	    ops->led_set == NULL || ops->cold_reboot == NULL) {
		return -EINVAL;
	}

	if (atomic_get(&g_initialized)) {
		return -EALREADY;
	}

	g_ops = ops;
	g_ctx = ctx;

	memset(&g_status, 0, sizeof(g_status));
	g_status.mode = PAIRING_MODE_NORMAL;
	g_status.phase = PAIRING_MODE_PHASE_UNINITIALIZED;
	g_status.access_mode = PAIRING_ACCESS_NORMAL;
	g_status.initialized = true;
	g_started = false;
	g_reset_transition = false;
	atomic_set(&g_pending, 0);

	k_work_init(&g_drain_work, drain_handler);
	k_work_init_delayable(&g_blink.dwork, blink_handler);
	k_work_init_delayable(&g_feedback.dwork, feedback_handler);

	if (!g_wq_started) {
		k_work_queue_start(&g_pairing_wq, g_pairing_stack,
				   K_THREAD_STACK_SIZEOF(g_pairing_stack),
				   CONFIG_USER_PAIRING_WORKQ_PRIORITY, NULL);
		g_wq_started = true;
	}

	atomic_set(&g_initialized, 1);
	return 0;
}

int pairing_mode_start(void)
{
	return enqueue(EV_START);
}

int pairing_mode_request_bonding(void)
{
	return enqueue(EV_BONDING);
}

int pairing_mode_request_reset(void)
{
	return enqueue(EV_RESET);
}

int pairing_mode_request_reset_sync(k_timeout_t timeout)
{
	struct k_event ev;
	int ret;
	uint32_t got;

	if (!atomic_get(&g_initialized)) {
		return -EINVAL;
	}

	{
		k_spinlock_key_t key = k_spin_lock(&g_sync_lock);

		if (g_sync_active) {
			k_spin_unlock(&g_sync_lock, key);
			return -EBUSY;
		}
		if (atomic_get(&g_fatal)) {
			k_spin_unlock(&g_sync_lock, key);
			return -ECANCELED;
		}
		k_event_init(&ev);
		g_sync_waiter = &ev;
		g_sync_active = true;
		k_spin_unlock(&g_sync_lock, key);
	}

	atomic_or(&g_pending, EV_RESET_SYNC);
	ret = k_work_submit_to_queue(&g_pairing_wq, &g_drain_work);
	if (ret < 0) {
		k_spinlock_key_t key = k_spin_lock(&g_sync_lock);

		g_sync_active = false;
		g_sync_waiter = NULL;
		k_spin_unlock(&g_sync_lock, key);
		fatal_finalize(OP_ENQUEUE, -ret);
		return -ECANCELED;
	}

	/* Block the CALLER (thread context only); no controller lock is
	 * held while waiting.  Timeout leaves the transition running. */
	got = k_event_wait(&ev, SYNC_DONE_BIT, false, timeout);

	{
		k_spinlock_key_t key = k_spin_lock(&g_sync_lock);

		g_sync_active = false;
		g_sync_waiter = NULL;
		k_spin_unlock(&g_sync_lock, key);
	}

	return (got & SYNC_DONE_BIT) != 0 ? 0 : -ETIMEDOUT;
}

int pairing_mode_notify_connected(void)
{
	return enqueue(EV_CONNECTED);
}

int pairing_mode_notify_disconnected(void)
{
	return enqueue(EV_DISCONNECTED);
}

int pairing_mode_notify_pairing_complete(bool bonded)
{
	atomic_set(&g_pairing_complete_bonded, bonded);
	return enqueue(EV_PAIRING_COMPLETE);
}

int pairing_mode_notify_pairing_failed(void)
{
	return enqueue(EV_PAIRING_FAILED);
}

int pairing_mode_notify_security_changed(bool success, bool bonded)
{
	atomic_set(&g_security_success, success);
	atomic_set(&g_security_bonded, bonded);
	return enqueue(EV_SECURITY_CHANGED);
}

void pairing_mode_get_status(struct pairing_mode_status *status)
{
	if (status == NULL) {
		return; /* documented NULL-safe contract */
	}

	k_spinlock_key_t key = k_spin_lock(&g_status_lock);

	*status = g_status;
	k_spin_unlock(&g_status_lock, key);
}

#ifdef PAIRING_MODE_TEST
/* GCOVR_EXCL_START — test seams, absent from production builds */
/*
 * Test seams (tests/unit/pairing_mode): a single function resets every
 * file-static module state between tests without pretending to release
 * hardware, and cancels all queued work synchronously so no stale event
 * can leak into the next test.  The work queue itself stays started
 * (k_work_queue_start may only run once); the next pairing_mode_init()
 * re-arms the work items.  None of this enters production firmware.
 */
static struct k_work_sync g_test_work_sync;

void pairing_mode_test_reset(void)
{
	(void)k_work_cancel_sync(&g_drain_work, &g_test_work_sync);
	(void)k_work_cancel_delayable_sync(&g_blink.dwork, &g_test_work_sync);
	(void)k_work_cancel_delayable_sync(&g_feedback.dwork, &g_test_work_sync);

	{
		k_spinlock_key_t key = k_spin_lock(&g_sync_lock);

		g_sync_active = false;
		g_sync_waiter = NULL;
		k_spin_unlock(&g_sync_lock, key);
	}

	{
		k_spinlock_key_t key = k_spin_lock(&g_status_lock);

		memset(&g_status, 0, sizeof(g_status));
		g_status.mode = PAIRING_MODE_NORMAL;
		g_status.phase = PAIRING_MODE_PHASE_UNINITIALIZED;
		g_status.access_mode = PAIRING_ACCESS_NORMAL;
		k_spin_unlock(&g_status_lock, key);
	}

	atomic_set(&g_pending, 0);
	atomic_set(&g_initialized, 0);
	atomic_set(&g_fatal, 0);
	g_ops = NULL;
	g_ctx = NULL;
	g_started = false;
	g_reset_transition = false;
	g_blink.gen = 0;
	g_blink.rapid = false;
	g_blink.toggles = 0;
	g_feedback.gen = 0;
}
/* GCOVR_EXCL_STOP */
#endif /* PAIRING_MODE_TEST */
