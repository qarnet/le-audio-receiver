/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-facing API of the fake I2S driver (fake_i2s.c).
 *
 * Ownership model (mirrors nrfx I2S):
 *   - a successful i2s_write() transfers block ownership to the driver and
 *     queues the block; the caller must never free it;
 *   - a failed i2s_write() never takes ownership; the caller keeps it;
 *   - a successful i2s_trigger(PREPARE) or i2s_trigger(DROP) purges every
 *     queued block back through the mem_slab captured at configure time;
 *   - a failed trigger performs no state change;
 *   - fake_i2s_release()/release_all() emulate DMA completion freeing a
 *     block back to the slab.
 *
 * Difference from physical DMA proof: on hardware the driver frees a block
 * after its DMA transfer completes; here release is explicit test control.
 * The fake additionally detects any pointer submitted twice while still
 * queued (the double-write corruption class) and records it as a violation
 * without taking ownership.
 */

#ifndef FAKE_I2S_H
#define FAKE_I2S_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/i2s.h>

/** Bytes snapshotted per write record (enough to prove silence, audio,
 * repeat, and ASRC/offload output content: patterns repeat per sample). */
#define FAKE_I2S_SNAPSHOT_BYTES 16

/* Record capacities (test sequences are bounded; overflow asserts). */
#define FAKE_I2S_MAX_WRITES   256
#define FAKE_I2S_MAX_TRIGGERS 64
#define FAKE_I2S_MAX_QUEUED   64

struct fake_i2s_write_rec {
	void *ptr;
	size_t size;
	uint8_t snapshot[FAKE_I2S_SNAPSHOT_BYTES];
};

struct fake_i2s_trigger_rec {
	enum i2s_trigger_cmd cmd;
};

struct fake_i2s_cfg_rec {
	bool captured;
	enum i2s_dir dir;
	struct i2s_config cfg; /* deep copy, incl. mem_slab pointer */
};

/* ── reset / injection ───────────────────────────────────────────── */

/** Purge queued blocks (via captured slab), clear all records/counters. */
void fake_i2s_reset(void);

/** Configure result; default 0. */
void fake_i2s_set_configure_ret(int ret);

/** Fail the write with this call index (0-based); -1 = never.  Default -1. */
void fake_i2s_fail_write_at(int call_index);

/** errno used for injected write failures; default -EIO. */
void fake_i2s_set_write_fail_errno(int err);

/** Per-command trigger result; default 0 for every command. */
void fake_i2s_set_trigger_ret(enum i2s_trigger_cmd cmd, int ret);

/* ── observation ─────────────────────────────────────────────────── */

int fake_i2s_configure_calls(void);
int fake_i2s_write_calls(void);
int fake_i2s_trigger_calls(void);
int fake_i2s_queued_count(void);

/** Ordered write records (persist after release; cleared by reset). */
const struct fake_i2s_write_rec *fake_i2s_write_rec(int idx);

/** Ordered trigger records. */
const struct fake_i2s_trigger_rec *fake_i2s_trigger_rec(int idx);

/** Configure capture (NULL if not yet configured). */
const struct fake_i2s_cfg_rec *fake_i2s_cfg_rec(void);

/** Queued pointer by queue position. */
void *fake_i2s_queued_ptr(int idx);

/** true if ptr is currently queued (driver-owned). */
bool fake_i2s_ptr_queued(void *ptr);

/** Double-submit violations (write of a still-queued pointer). */
int fake_i2s_duplicate_write_violations(void);

/* ── release (DMA completion emulation) ──────────────────────────── */

/** Free one queued block back to the captured slab (DMA completion). */
void fake_i2s_release(void *ptr);

/** Free every queued block back to the captured slab. */
void fake_i2s_release_all(void);

#endif /* FAKE_I2S_H */
