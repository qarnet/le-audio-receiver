/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for flpr_ring_mgr epoch-tagged consumer notification and
 * consume_sem drain logic (Stage 2 stale-notify fix).
 *
 * Tests the core logic without requiring devicetree:
 *   1. Matching-epoch notification → semaphore given.
 *   2. Old-epoch notification after reset → semaphore NOT given,
 *      stale count incremented.
 *   3. Preloaded consume tokens drained by reset.
 *   4. New-epoch notification after reset still wakes.
 *   5. No stale notifications after clean startup (zero counters).
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>

/* Replicate the diagnostic state from flpr_ring_mgr.c (under test). */
static uint32_t test_ring_epoch;
static uint32_t test_stale_notify;
static uint32_t test_sem_drained;
static uint32_t test_sem_gives;

/* Simulated semaphore: counter incremented on give, decremented on take. */
static int fake_sem_count;

static void fake_sem_give(void)
{
	fake_sem_count++;
	test_sem_gives++;
}

static int fake_sem_take_no_wait(void)
{
	if (fake_sem_count > 0) {
		fake_sem_count--;
		return 0; /* success */
	}
	return -1; /* empty */
}

/* ── Logic under test: replication of on_ring_consumer ──────────────
 * This is the exact logic from flpr_ring_mgr.c, replicated for unit
 * testing without DT/hardware dependencies.
 */
static void ring_notify_receive(uint32_t notify_epoch, uint32_t *block_count_out)
{
	if (notify_epoch != test_ring_epoch) {
		test_stale_notify++;
		return; /* stale: do NOT give semaphore */
	}

	/* Matching epoch: update block count, give sem. */
	if (block_count_out) {
		*block_count_out = 0; /* not relevant for wake test */
	}
	fake_sem_give();
}

/* ── Logic under test: replication of consume_sem drain ────────────
 * This is the exact logic from flpr_ring_mgr_reset().
 */
static uint32_t ring_mgr_drain_consume(void)
{
	uint32_t drained = 0;
	while (fake_sem_take_no_wait() == 0) {
		drained++;
	}
	return drained;
}

static void ring_mgr_do_reset(uint32_t new_epoch)
{
	/* 1. Drain stale semaphore tokens BEFORE publishing epoch. */
	uint32_t drained = ring_mgr_drain_consume();
	if (drained > 0) {
		test_sem_drained += drained;
	}

	/* 2. Publish new epoch. */
	test_ring_epoch = new_epoch;
}

/* ── Setup ──────────────────────────────────────────────────────── */

static void test_setup(void *fixture)
{
	(void)fixture;
	test_ring_epoch = 0;
	test_stale_notify = 0;
	test_sem_drained = 0;
	test_sem_gives = 0;
	fake_sem_count = 0;
}

/* ── Test 1: Matching-epoch notification wakes consumer ──────────── */

ZTEST(flpr_ring_mgr, test_matching_epoch_wakes)
{
	/* Setup: ring at epoch 42. */
	ring_mgr_do_reset(42);
	zassert_equal(test_ring_epoch, 42, "epoch set to 42");
	zassert_equal(fake_sem_count, 0, "sem empty after reset");

	/* Send notification with matching epoch. */
	ring_notify_receive(42, NULL);
	zassert_equal(fake_sem_count, 1, "sem given for matching epoch");
	zassert_equal(test_stale_notify, 0, "no stale counted");
	zassert_equal(test_sem_gives, 1, "sem_gives incremented");
}

/* ── Test 2: Old-epoch notification after reset does not wake ────── */

ZTEST(flpr_ring_mgr, test_old_epoch_rejected)
{
	/* Setup: ring at epoch 100. */
	ring_mgr_do_reset(100);
	zassert_equal(test_ring_epoch, 100, "epoch set to 100");
	zassert_equal(fake_sem_count, 0, "sem empty");

	/* Reset to new epoch 200. */
	ring_mgr_do_reset(200);
	zassert_equal(test_ring_epoch, 200, "epoch set to 200");

	/* Stale notification from old epoch 100 arrives. */
	ring_notify_receive(100, NULL);
	zassert_equal(fake_sem_count, 0, "sem NOT given for old epoch");
	zassert_equal(test_stale_notify, 1, "stale_notify incremented");
	zassert_equal(test_sem_gives, 0, "sem_gives unchanged");

	/* Another stale. */
	ring_notify_receive(100, NULL);
	zassert_equal(test_stale_notify, 2, "stale_notify incremented again");
	zassert_equal(fake_sem_count, 0, "sem still empty");
}

/* ── Test 3: Preloaded consume token drained by reset ────────────── */

ZTEST(flpr_ring_mgr, test_preloaded_token_drained)
{
	/* Preload semaphore with 3 tokens (simulating stale notifications). */
	fake_sem_give();
	fake_sem_give();
	fake_sem_give();
	zassert_equal(fake_sem_count, 3, "3 tokens preloaded");

	/* Reset should drain all 3. */
	uint32_t drained = ring_mgr_drain_consume();
	zassert_equal(drained, 3, "3 tokens drained");
	zassert_equal(fake_sem_count, 0, "sem empty after drain");

	/* Second drain should find nothing. */
	drained = ring_mgr_drain_consume();
	zassert_equal(drained, 0, "no tokens on second drain");
}

/* ── Test 4: New-epoch notification after reset still wakes ──────── */

ZTEST(flpr_ring_mgr, test_new_epoch_after_reset_wakes)
{
	/* Setup: ring at epoch 5, preload a stale token. */
	ring_mgr_do_reset(5);
	fake_sem_give(); /* stale token from old epoch */
	zassert_equal(fake_sem_count, 1, "1 stale token preloaded");

	/* Reset to epoch 6 — drains stale token. */
	ring_mgr_do_reset(6);
	zassert_equal(test_ring_epoch, 6, "epoch set to 6");
	zassert_equal(fake_sem_count, 0, "sem drained");
	zassert_equal(test_sem_drained, 1, "sem_drained counter = 1");

	/* Reset sem_gives so we measure ONLY the new epoch notification. */
	test_sem_gives = 0;

	/* New-epoch notification should wake. */
	ring_notify_receive(6, NULL);
	zassert_equal(fake_sem_count, 1, "sem given for new epoch");
	zassert_equal(test_sem_gives, 1, "sem_gives = 1 (fresh)");
	zassert_equal(test_stale_notify, 0, "no stale for new epoch");
}

/* ── Test 5: Zero stale notifications on clean startup ───────────── */

ZTEST(flpr_ring_mgr, test_clean_startup_no_stale)
{
	/* Fresh initialization: epoch 0, no tokens. */
	zassert_equal(test_ring_epoch, 0, "epoch starts at 0");
	zassert_equal(fake_sem_count, 0, "no preloaded tokens");

	/* Reset to epoch 1. */
	ring_mgr_do_reset(1);
	zassert_equal(test_ring_epoch, 1, "epoch = 1");
	zassert_equal(test_sem_drained, 0, "nothing to drain at clean startup");

	/* Matching notifications. */
	ring_notify_receive(1, NULL);
	ring_notify_receive(1, NULL);
	ring_notify_receive(1, NULL);
	zassert_equal(fake_sem_count, 3, "3 sem tokens from 3 matching notifies");
	zassert_equal(test_stale_notify, 0, "zero stale notifications");
	zassert_equal(test_sem_gives, 3, "3 sem_gives");
}

/* ── Test suite ─────────────────────────────────────────────── */

ZTEST_SUITE(flpr_ring_mgr, NULL, NULL, test_setup, NULL, NULL);
