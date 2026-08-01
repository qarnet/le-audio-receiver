/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * BSIM-only scenario-aware strict sink oracle — test-side interface.
 *
 * The public audio_sink.h API is unchanged; these test-only functions
 * configure the oracle and query its records.  Production firmware builds
 * contain no oracle symbols.
 */

#ifndef BSIM_SINK_ORACLE_H
#define BSIM_SINK_ORACLE_H

#include <stdbool.h>
#include <stdint.h>

#define BSIM_SINK_MAX_SEGMENTS 4

/* Scenario ids — mirror the client test ids and the runner. */
enum bsim_sink_scenario {
	BSIM_SCN_MONO_10MS = 1,
	BSIM_SCN_MONO_7P5MS,
	BSIM_SCN_MODEA_10MS,
	BSIM_SCN_MODEA_7P5MS,
	BSIM_SCN_MODEA_REVERSE_START_10MS,
	BSIM_SCN_MODEB_10MS,
	BSIM_SCN_MODEB_7P5MS,
	BSIM_SCN_INVALID_SDU_RESUME_10MS,
	BSIM_SCN_MODEA_FIRST_STOP_10MS,
	BSIM_SCN_RELEASE_WITHOUT_DISABLE_10MS,
	BSIM_SCN_DISCONNECT_STREAMING_10MS,
	BSIM_SCN_RECONNECT_SECOND_STREAM_10MS,
	BSIM_SCN_UNSUPPORTED_SOURCE_DIRECTION,
	BSIM_SCN_NO_FREE_SINK_SLOT,
	BSIM_SCN_INVALID_CODEC_FIELDS,
};

/** Per-segment strict record. */
struct bsim_sink_segment {
	uint32_t pushes;            /* nonzero-energy pushes */
	uint32_t startup_zero;      /* zero-energy pushes before first nonzero */
	uint32_t startup_plc;       /* audio_stats plc at the last startup-zero push */
	uint32_t total_frames;      /* audio_stats total_frames at finalize */
	uint32_t plc_frames;        /* audio_stats plc_frames at finalize */
	uint32_t decode_errors;     /* audio_stats decode_errors at finalize */
	uint32_t malformed_samples; /* pushes with a wrong sample count */
	uint32_t full_hash;         /* ordered FNV-1a over interleaved PCM */
	uint32_t l_hash;            /* ordered FNV-1a over left samples */
	uint32_t r_hash;            /* ordered FNV-1a over right samples */
	int32_t l_energy_min;
	int32_t l_energy_max;
	int32_t r_energy_min;
	int32_t r_energy_max;
	uint32_t configured_samples; /* expected int16 values per push */
	bool finalized;
};

/**
 * Configure the oracle for one scenario.
 *
 * @param scenario  Scenario id.
 * @param dec_calls Decoder invocations per pushed SDU (mono = 1,
 *                  Mode A / Mode B = 2).
 */
void audio_sink_test_begin(enum bsim_sink_scenario scenario, int dec_calls);

/**
 * True when the scenario's end condition is met and all invariants hold.
 * Never true after a fault (faults FAIL immediately instead).
 */
bool audio_sink_test_goal_reached(void);

/**
 * Re-check all per-segment invariants (decode errors, PLC placement,
 * total-frame accounting, channel-hash relations, energy bounds, PACS
 * contexts).  FAILs immediately on the first violation.
 *
 * @return true when all invariants hold.
 */
bool audio_sink_test_validate(void);

/** Number of finalized segments so far. */
int audio_sink_test_segment_count(void);

/** Copy one finalized segment record.  Returns false if not finalized. */
bool audio_sink_test_get_segment(int idx, struct bsim_sink_segment *out);

/** Cumulative pushes-after-stop across all segments (never hidden). */
uint32_t audio_sink_test_after_stop_total(void);

#endif /* BSIM_SINK_ORACLE_H */
