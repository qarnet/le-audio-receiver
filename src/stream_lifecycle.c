/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BAP sink audio-path lifecycle gate — implementation.
 */

#include "stream_lifecycle.h"

#define MAX_SINK_ASE 2

static bool sink_started[MAX_SINK_ASE];
static int sink_chan_count[MAX_SINK_ASE];
static bool audio_path_open;
static bool force_closed; /* R1: forced-close latch (shell stop) */

void stream_lifecycle_reset(void)
{
	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		sink_started[i] = false;
		sink_chan_count[i] = 0;
	}
	audio_path_open = false;
	force_closed = false;
}

void stream_lifecycle_sink_configured(size_t idx, int chan_count)
{
	if (idx < MAX_SINK_ASE) {
		sink_chan_count[idx] = chan_count;
		sink_started[idx] = false;
	}
}

bool stream_lifecycle_sink_started(size_t idx)
{
	if (idx >= MAX_SINK_ASE) {
		return false;
	}
	if (force_closed) {
		/* R1: a forced (shell) close latches until the current
		 * configured slot set is released or a full reset; later
		 * duplicate/second-ASE start requests and stream-start
		 * callbacks can never reopen this lifecycle. */
		return false;
	}
	sink_started[idx] = true;

	/* Count total configured ASEs and how many are started. */
	int total_ase = 0;
	int started = 0;

	for (int i = 0; i < MAX_SINK_ASE; i++) {
		if (sink_chan_count[i] > 0) {
			total_ase++;
			if (sink_started[i]) {
				started++;
			}
		}
	}

	bool should_open = false;

	if (total_ase == 1) {
		/* Mode B / single ASE — open when it starts. */
		should_open = (started >= 1);
	} else if (total_ase >= 2) {
		/* Mode A / two ASEs — open only when both start. */
		should_open = (started >= 2);
	}

	/* Report a closed-to-open transition only: a duplicate start
	 * while the gate is already open must not repeat the one-time
	 * open work in the caller (perf reset, offload start, observer
	 * event).  See LIFE-003.
	 */
	bool opened = should_open && !audio_path_open;

	if (opened) {
		audio_path_open = true;
	}

	return opened;
}

void stream_lifecycle_sink_release(size_t idx)
{
	if (idx < MAX_SINK_ASE) {
		sink_chan_count[idx] = 0;
		sink_started[idx] = false;
	}

	/* R1: releasing the LAST configured slot clears the forced-close
	 * latch so a later reconfigure/start lifecycle can open.  Releasing
	 * only one Mode A slot (another remains configured) does not. */
	bool any_configured = false;

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (sink_chan_count[i] > 0) {
			any_configured = true;
			break;
		}
	}
	if (!any_configured) {
		force_closed = false;
	}
}

bool stream_lifecycle_audio_path_close(void)
{
	bool was_open = audio_path_open;

	audio_path_open = false;
	return was_open;
}

bool stream_lifecycle_force_close(void)
{
	/* R1: close the gate AND latch it closed for the current configured
	 * slot set.  Returns whether the gate was open before force-close
	 * so the caller can emit the first-close observer event exactly
	 * once.
	 *
	 * Repair: with NO slot configured there is no lifecycle to latch —
	 * a future first configure/start must be able to open normally, so
	 * the force latch is only set when at least one slot is currently
	 * configured.  stream_lifecycle_reset() and releasing the last
	 * configured slot remain the release boundaries for a latched set. */
	bool was_open = audio_path_open;

	audio_path_open = false;

	bool any_configured = false;

	for (size_t i = 0; i < MAX_SINK_ASE; i++) {
		if (sink_chan_count[i] > 0) {
			any_configured = true;
			break;
		}
	}
	if (any_configured) {
		force_closed = true;
	}
	return was_open;
}

bool stream_lifecycle_audio_path_is_open(void)
{
	return audio_path_open;
}
