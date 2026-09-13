"""Synthetic-only PCM/WAV fixtures for HIL capture oracle tests.

No fixture represents electrical hardware or carries reusable production
limits. PCM derives from public source signal constants through production
capture_signal reconstruction.
"""

import json
import os
import struct

import numpy as np

from hil import capture_signal, qualification
from hil.rows import RowSpec


ROW = RowSpec(
    "synthetic.capture.mode_b",
    "fresh",
    "mode_b",
    "48_4_1",
    288,
)


def write_wav(path, samples, sample_rate=48000):
    samples = np.asarray(samples, dtype=np.float64)
    if samples.ndim == 1:
        samples = samples[:, None]
    if samples.shape[1] not in (1, 2):
        raise ValueError("samples need one or two channels")
    pcm = np.clip(np.rint(samples * 32767.0), -32768, 32767).astype("<i2")
    channels = samples.shape[1]
    data = pcm.tobytes()
    fmt = struct.pack(
        "<HHIIHH",
        1,
        channels,
        sample_rate,
        sample_rate * channels * 2,
        channels * 2,
        16,
    )
    body = (
        b"fmt "
        + struct.pack("<I", len(fmt))
        + fmt
        + b"data"
        + struct.pack("<I", len(data))
        + data
    )
    with open(path, "wb") as fh:
        fh.write(b"RIFF" + struct.pack("<I", len(body) + 4) + b"WAVE" + body)


def clean_capture(row=ROW, capability="stereo", gain=0.65, prefix_samples=480):
    signal = capture_signal.output_channels(row, capability) * gain
    prefix = np.zeros((prefix_samples, signal.shape[1]), dtype=np.float64)
    return np.vstack((prefix, signal))


def synthetic_limits(capability, row=ROW):
    """Explicit synthetic-only limits. Never load in production fixture binding."""
    if capability == "mono":
        names = qualification.MONO_LIMIT_METRICS
    elif capability == "stereo":
        names = qualification.STEREO_LIMIT_METRICS
    else:
        raise ValueError("synthetic capability must be mono or stereo")
    # Analyzer tests supply metric behavior, not hardware tuning. Keep every
    # key explicit and broad enough for deterministic generated clean PCM.
    exact = {
        "sample_rate_hz": {"min": 48000, "max": 48000},
        "channel_count": {
            "min": 1 if capability == "mono" else 2,
            "max": 1 if capability == "mono" else 2,
        },
        "sample_width_bits": {"min": 16, "max": 16},
        "frame_count": {"min": 1, "max": 10000000},
        "duration_seconds": {"min": 1, "max": 1000},
        "scored_start_sample": {"min": 0, "max": 1000000},
        "scored_end_sample": {"min": 1, "max": 10000000},
        "scored_duration_seconds": {"min": 0, "max": 1000},
    }
    limits = {}
    for name in names:
        if name in exact:
            limits[name] = exact[name]
        elif name in qualification.MINIMUM_METRICS:
            limits[name] = {"min": -1.0}
        else:
            limits[name] = {"max": 1e9}
    return limits


def fake_qualification(path, capability, limits=None):
    """Build in-memory accepted synthetic qualification for pure analyzer tests."""

    class Binding:
        pass

    value = type("Capability", (), {"value": capability})()
    result = type("Qualification", (), {})()
    result.path = path
    result.sha256 = "synthetic"
    result.capability = value
    result.fixture_id = "synthetic"
    result.accepted_by = "synthetic-test-only"
    result.accepted_at_utc = "2026-01-01T00:00:00Z"
    parsed = {}
    for name, item in (
        limits if limits is not None else synthetic_limits(capability)
    ).items():
        parsed[name] = qualification.MetricLimit(item.get("min"), item.get("max"))
    result.limits = parsed
    return result
