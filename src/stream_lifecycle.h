/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BAP sink audio-path lifecycle gate.
 *
 * Prevents late ASE callbacks from restarting I2S after teardown.
 * Tracks per-sink started state and one global audio-path gate.
 *
 * Unit-testable: pure logic, no Zephyr deps beyond stdbool/stddef.
 */

#ifndef STREAM_LIFECYCLE_H
#define STREAM_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>

/** Reset all state (call on disconnect / full release). */
void stream_lifecycle_reset(void);

/**
 * Record that sink @p idx is configured with @p chan_count channels.
 * Must be called even when chan_count==1 for a two-ASE stream;
 * the gate decision uses the number of configured sinks, not chan_count.
 */
void stream_lifecycle_sink_configured(size_t idx, int chan_count);

/**
 * Mark sink @p idx as started.
 * @return true only for a closed-to-open transition of the audio-path
 *         gate (Mode B: 1 ASE started; Mode A: both ASEs started).
 *         Duplicate starts while the gate is already open return false
 *         so the caller runs the one-time open work exactly once per
 *         stream lifecycle.
 */
bool stream_lifecycle_sink_started(size_t idx);

/**
 * Clear the per-slot configuration of sink @p idx (release path).
 * Makes the slot reusable: the gate decision no longer counts it as a
 * configured ASE.
 */
void stream_lifecycle_sink_release(size_t idx);

/**
 * Close the audio-path gate.  Idempotent — safe to call multiple times.
 * @return true if the gate was previously open (i.e. this was the
 *         first close).
 */
bool stream_lifecycle_audio_path_close(void);

/**
 * Force-close the audio-path gate (shell stop).  Closes the gate and
 * latches it closed for the current configured slot set: later
 * `stream_lifecycle_sink_started()` calls (duplicates, second-ASE
 * starts, stream-start callbacks) never open it.  `stream_lifecycle_reset()`
 * clears the latch; releasing the LAST configured slot clears it so a
 * later reconfigure/start lifecycle can open (releasing only one Mode A
 * slot does not).
 *
 * @return true if the gate was open before force-close (first-close
 *         observer emission), false if already closed.
 */
bool stream_lifecycle_force_close(void);

/** Query whether the audio-path gate is open. */
bool stream_lifecycle_audio_path_is_open(void);

#endif /* STREAM_LIFECYCLE_H */
