"""Deterministic public-source signal reconstruction for capture analysis.

This module recreates carrier phase progression and scored envelopes from public
source constants. It deliberately has no LC3 decoder and no private output
hash. Test fixtures use it to generate PCM that exercises analyzer behavior.
"""

import math

import numpy as np

from hil import source_contract


C = source_contract.PUBLIC_CONSTANTS


def frame_samples(profile):
    if profile == "48_3_1":
        return 360
    if profile == "48_4_1":
        return 480
    raise ValueError("unsupported source profile %r" % profile)


def frame_duration_us(profile):
    if profile == "48_3_1":
        return 7500
    if profile == "48_4_1":
        return 10000
    raise ValueError("unsupported source profile %r" % profile)


def tail_samples(profile):
    duration_us = frame_duration_us(profile)
    frames = (C["HIL_SOURCE_TAIL_MIN_US"] + duration_us - 1) // duration_us
    return frames * frame_samples(profile)


def scored_samples(row):
    return row.scored_sdu_count * frame_samples(row.profile)


def total_samples(row):
    return (
        C["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"]
        + scored_samples(row)
        + tail_samples(row.profile)
    )


def xorshift32(value):
    value &= 0xFFFFFFFF
    value ^= (value << 13) & 0xFFFFFFFF
    value ^= value >> 17
    value ^= (value << 5) & 0xFFFFFFFF
    return value & 0xFFFFFFFF


def _c_trunc_div(numerator, denominator):
    """C99 signed integer division truncates toward zero, unlike Python //."""
    if numerator < 0:
        return -((-numerator) // denominator)
    return numerator // denominator


def carrier_frequency(channel):
    if channel == "left":
        return C["HIL_SOURCE_LEFT_CARRIER_HZ"]
    if channel == "right":
        return C["HIL_SOURCE_RIGHT_CARRIER_HZ"]
    raise ValueError("unknown semantic channel %r" % channel)


def carrier_phase_step(channel):
    if channel == "left":
        return C["HIL_SOURCE_LEFT_PHASE_STEP"]
    if channel == "right":
        return C["HIL_SOURCE_RIGHT_PHASE_STEP"]
    raise ValueError("unknown semantic channel %r" % channel)


def _prng_mask(channel):
    if channel == "left":
        return C["HIL_SOURCE_LEFT_PRNG_MASK"]
    if channel == "right":
        return C["HIL_SOURCE_RIGHT_PRNG_MASK"]
    raise ValueError("unknown semantic channel %r" % channel)


def preamble_amplitude(channel, samples=None):
    """Return exact integer public preamble amplitude sequence for semantic channel."""
    count = C["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"] if samples is None else samples
    if count < 0 or count > C["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"]:
        raise ValueError("preamble samples out of range")
    segment = C["HIL_SOURCE_PREAMBLE_SEGMENT_SAMPLES"]
    index = np.arange(count, dtype=np.int64) // segment
    on = (
        (index == 1) | (index == 5)
        if channel == "left"
        else (index == 3) | (index == 5)
    )
    return np.where(on, C["HIL_SOURCE_AMPLITUDE_FULL"], 0).astype(np.float64)


def scored_amplitude(channel, seed, samples):
    """Recreate exact public xorshift envelope/ramp amplitude samples."""
    if not isinstance(seed, int) or seed <= 0 or seed > 0xFFFFFFFF:
        raise ValueError("signal seed must be nonzero uint32")
    if samples < 0:
        raise ValueError("scored sample count must be nonnegative")
    out = np.empty(samples, dtype=np.float64)
    state = seed ^ _prng_mask(channel)
    if state == 0:
        state = C["HIL_SOURCE_PRNG_ZERO_REPLACEMENT"]
    state = xorshift32(state)
    old = C["HIL_SOURCE_AMPLITUDE_FULL"]
    target = (
        C["HIL_SOURCE_AMPLITUDE_FULL"] if state & 1 else C["HIL_SOURCE_AMPLITUDE_LOW"]
    )
    block_size = C["HIL_SOURCE_ENVELOPE_BLOCK_SAMPLES"]
    ramp_size = C["HIL_SOURCE_TRANSITION_RAMP_SAMPLES"]
    written = 0
    while written < samples:
        block_count = min(block_size, samples - written)
        ramp_count = min(ramp_size, block_count)
        if ramp_count:
            indices = np.arange(1, ramp_count + 1, dtype=np.int64)
            diff = target - old
            values = np.array(
                [old + _c_trunc_div(diff * int(index), ramp_size) for index in indices],
                dtype=np.float64,
            )
            out[written : written + ramp_count] = values
        if block_count > ramp_count:
            out[written + ramp_count : written + block_count] = target
        written += block_count
        if block_count == block_size:
            state = xorshift32(state)
            old = target
            target = (
                C["HIL_SOURCE_AMPLITUDE_FULL"]
                if state & 1
                else C["HIL_SOURCE_AMPLITUDE_LOW"]
            )
    return out


def semantic_amplitude(channel, row):
    """Return preamble, scored, and tail amplitudes for one semantic channel."""
    scored = scored_samples(row)
    tail = tail_samples(row.profile)
    return np.concatenate(
        (
            preamble_amplitude(channel),
            scored_amplitude(channel, row.signal_seed, scored),
            np.zeros(tail, dtype=np.float64),
        )
    )


def scored_semantic_pcm(channel, row):
    """Render only scored-stage phase-continuous PCM after public preamble."""
    start = C["HIL_SOURCE_PREAMBLE_TOTAL_SAMPLES"]
    count = scored_samples(row)
    phase = (np.arange(count, dtype=np.uint64) + np.uint64(start)) * np.uint64(
        carrier_phase_step(channel)
    )
    radians = phase.astype(np.float64) * (2.0 * math.pi / 4294967296.0)
    amplitude = scored_amplitude(channel, row.signal_seed, count) / float(
        C["HIL_SOURCE_AMPLITUDE_FULL"]
    )
    return np.sin(radians) * amplitude


def semantic_pcm(channel, row):
    """Render normalized phase-continuous public signal for semantic channel."""
    count = total_samples(row)
    phase = np.arange(count, dtype=np.uint64) * np.uint64(carrier_phase_step(channel))
    radians = phase.astype(np.float64) * (2.0 * math.pi / 4294967296.0)
    amplitude = semantic_amplitude(channel, row) / float(C["HIL_SOURCE_AMPLITUDE_FULL"])
    return np.sin(radians) * amplitude


def output_channels(row, capability):
    """Return ideal normalized capture channels for a logical capture capability.

    Mono models documented aggregate capture semantics. Its summing gain is 0.5
    for two active channels so synthetic PCM cannot create a false clipping
    defect; real fixture gain remains qualification-owned.
    """
    left = semantic_pcm("left", row)
    right = semantic_pcm("right", row)
    if row.mode == "mono":
        physical = np.column_stack((left, left))
    elif row.mode in ("mode_a", "mode_b"):
        physical = np.column_stack((left, right))
    else:
        raise ValueError("unsupported source mode %r" % row.mode)
    if capability == "stereo":
        return physical
    if capability == "mono":
        return physical.mean(axis=1, keepdims=True)
    raise ValueError("capture capability must be mono or stereo")


def scored_output_channels(row, capability):
    """Return only public scored interval output for oracle metric alignment."""
    left = scored_semantic_pcm("left", row)
    right = scored_semantic_pcm("right", row)
    if row.mode == "mono":
        physical = np.column_stack((left, left))
    elif row.mode in ("mode_a", "mode_b"):
        physical = np.column_stack((left, right))
    else:
        raise ValueError("unsupported source mode %r" % row.mode)
    if capability == "stereo":
        return physical
    if capability == "mono":
        return physical.mean(axis=1, keepdims=True)
    raise ValueError("capture capability must be mono or stereo")


def expected_semantics(row, capability):
    """Describe expected physical carrier assignment without private output data."""
    if capability == "mono":
        return (("left",) if row.mode == "mono" else ("left", "right"),)
    if capability == "stereo":
        if row.mode == "mono":
            return (("left",), ("left",))
        return (("left",), ("right",))
    raise ValueError("capture capability must be mono or stereo")
