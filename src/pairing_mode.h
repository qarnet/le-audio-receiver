/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable pairing-mode transition owner (P1 of the user pairing control
 * plan, docs/development/user-pairing-control-plan.md).
 *
 * Sole owner of the NORMAL / BONDING / RESETTING modes, their asynchronous
 * transition phases, LED patterns, supersession, completion, and fatal
 * recovery policy.  All platform side effects go through injected
 * operations (struct pairing_mode_ops); the public header contains no
 * Bluetooth, GPIO, or devicetree types.
 *
 * Serialization: one private work queue owned by the controller.  Public
 * request and notification functions may be called from any thread or
 * work-queue context; they enqueue events into an atomic pending mask and
 * never execute platform operations inline.  Transitions run only in the
 * controller work-queue thread.
 */

#ifndef PAIRING_MODE_H
#define PAIRING_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/* Visible pairing mode (the requested three-mode contract). */
enum pairing_mode {
	PAIRING_MODE_NORMAL = 0,
	PAIRING_MODE_BONDING,
	PAIRING_MODE_RESETTING,
};

/* Internal transition phase. */
enum pairing_mode_phase {
	PAIRING_MODE_PHASE_UNINITIALIZED = 0, /* after init(), before start() */
	PAIRING_MODE_PHASE_IDLE,
	PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_BONDING,
	PAIRING_MODE_PHASE_WAIT_DISCONNECT_FOR_RESET,
	PAIRING_MODE_PHASE_RESET_FEEDBACK,
	PAIRING_MODE_PHASE_FATAL,
};

/* Injected access-policy mode (P4/P5 adapter consumes this). */
enum pairing_access_mode {
	PAIRING_ACCESS_NORMAL = 0,
	PAIRING_ACCESS_BONDING,
	PAIRING_ACCESS_SUSPENDED,
};

/*
 * Injected platform operations.  Every callback except cold_reboot returns
 * an errno; any non-zero return of a mandatory operation is fatal (cold
 * reboot exactly once).  All callbacks are invoked from the controller
 * work-queue thread.
 */
struct pairing_mode_ops {
	/* Set the access-policy mode (NORMAL / BONDING / SUSPENDED). */
	int (*set_access_mode)(enum pairing_access_mode mode, void *ctx);
	/* Stop advertising; idempotent while already suspended. */
	int (*advertising_suspend)(void *ctx);
	/* Start advertising with the current access policy. */
	int (*advertising_start)(void *ctx);
	/*
	 * Request the disconnect of the current peer, if any.  Sets
	 * *pending true when a peer is connected and a disconnect was
	 * requested; false when there is no peer.  Never blocks.
	 */
	int (*disconnect_peer)(bool *pending, void *ctx);
	/* Delete every persisted bond (RESET only, after disconnect). */
	int (*delete_all_bonds)(void *ctx);
	/* Request BT_SECURITY_L2 on the active BONDING connection. */
	int (*request_security)(void *ctx);
	/* Drive the logical user LED (active/inactive). */
	int (*led_set)(bool active, void *ctx);
	/* Cold reboot; must not return (a return is tolerated: the
	 * controller stays FATAL and executes no further operations). */
	void (*cold_reboot)(void *ctx);
};

/* Read-only status snapshot (thread-safe; NULL-safe). */
struct pairing_mode_status {
	enum pairing_mode mode;
	enum pairing_mode_phase phase;
	enum pairing_access_mode access_mode;
	uint32_t transition_generation;
	bool connected;
	bool led_active;
	bool initialized;
	bool fatal;
};

/*
 * Validate and install ops.  Returns 0, or -EINVAL when ops is NULL or any
 * required callback is missing (atomic: no state change, no callback
 * invoked).  A second init returns -EALREADY.
 */
int pairing_mode_init(const struct pairing_mode_ops *ops, void *ctx);

/*
 * Enqueue the boot start sequence: access NORMAL, LED inactive, start
 * advertising, NORMAL/IDLE.  Idempotent.  Returns 0 when enqueued,
 * -EINVAL before init, -ECANCELED after a fatal.
 */
int pairing_mode_start(void);

/*
 * Enqueue a BONDING request.  Bumping the transition generation, suspending
 * advertising, requesting the disconnect of any active peer, then entering
 * BONDING (LED slow pattern + advertising).  Duplicate BONDING while
 * already BONDING and not connected is a no-op.
 */
int pairing_mode_request_bonding(void);

/* Enqueue an asynchronous RESET request (supersedes every BONDING
 * transition; disconnect → delete bonds → rapid LED feedback → BONDING). */
int pairing_mode_request_reset(void);

/*
 * Synchronous RESET: registers the caller as a waiter, enqueues the same
 * transition as pairing_mode_request_reset(), then blocks the CALLER on a
 * completion event until BONDING advertising is active or timeout elapses.
 * No controller lock is held while waiting.  Returns 0 when the reset
 * completed to BONDING, -ETIMEDOUT on timeout (the transition continues in
 * the background), -EBUSY while another synchronous waiter is registered,
 * -EINVAL before init, -ECANCELED after a fatal.  Thread context only.
 */
int pairing_mode_request_reset_sync(k_timeout_t timeout);

/* Connection / pairing notifications (any thread or work-queue context). */
int pairing_mode_notify_connected(void);
int pairing_mode_notify_disconnected(void);
int pairing_mode_notify_pairing_complete(bool bonded);
int pairing_mode_notify_pairing_failed(void);
int pairing_mode_notify_security_changed(bool success, bool bonded);

/*
 * Copy the status snapshot.  NULL is a safe no-op (documented contract).
 */
void pairing_mode_get_status(struct pairing_mode_status *status);

#ifdef PAIRING_MODE_TEST
/* Test-only seam (never compiled into production firmware): reset every
 * file-static module state between tests; the private work queue stays
 * started.  Declared here so the direct test suite can call it. */
void pairing_mode_test_reset(void);
#endif

#endif /* PAIRING_MODE_H */
