#!/usr/bin/env python3
"""Pure NumPy synthetic capture oracle tests. No ALSA or device access."""

import json
import os
import struct
import sys
import tempfile
import unittest

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
sys.path.insert(0, os.path.dirname(__file__))

from hil import capture_analyzer, capture_signal, qualification  # noqa: E402
import capture_fixtures  # noqa: E402


def strict_limits(capability, row):
    """Synthetic-only limits targeted at named deliberate defects."""
    limits = capture_fixtures.synthetic_limits(capability, row)
    limits.update(
        {
            "preamble_correlation": {"min": 0.35},
            "preamble_unique_peak_margin": {"min": 0.01},
            "expected_carrier_power": {"min": 0.02},
            "unexpected_carrier_power": {"max": 0.001},
            "envelope_correlation": {"min": 0.5},
            "lag_samples": {"max": 10000},
            "lag_drift_samples": {"max": 6000},
            "peak_normalized": {"max": 0.95},
            "full_scale_count": {"max": 0},
            "clipping_count": {"max": 0},
            "noise_floor": {"max": 0.2},
            "dc_offset": {"max": 0.1},
            "moving_rms_min": {"min": 0.01},
            "low_energy_window_count": {"max": 0},
            "longest_low_energy_run": {"max": 1},
            "repeat_variation": {"max": 0.3},
        }
    )
    if capability == "stereo":
        for side in ("left", "right"):
            limits.update(
                {
                    side + "_expected_carrier_power": {"min": 0.02},
                    side + "_unexpected_carrier_power": {"max": 0.001},
                    side + "_envelope_correlation": {"min": 0.5},
                    side + "_lag_samples": {"max": 10000},
                    side + "_lag_drift_samples": {"max": 6000},
                    side + "_peak_normalized": {"max": 0.95},
                    side + "_full_scale_count": {"max": 0},
                    side + "_clipping_count": {"max": 0},
                    side + "_noise_floor": {"max": 0.2},
                    side + "_dc_offset": {"max": 0.1},
                    side + "_moving_rms_min": {"min": 0.01},
                    side + "_low_energy_window_count": {"max": 0},
                    side + "_longest_low_energy_run": {"max": 1},
                    side + "_repeat_variation": {"max": 0.3},
                    side + "_continuity": {"max": 0.2},
                }
            )
        limits.update(
            {
                "channel_map_margin": {"min": 0.005},
                "opposite_channel_leakage": {"max": 0.03},
                "duplication_correlation": {"max": 0.2},
                "level_mismatch": {"max": 0.08},
            }
        )
    return limits


class TestCaptureAnalyzer(unittest.TestCase):
    def analyze(self, samples, capability="stereo", limits=None):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "capture.wav")
            capture_fixtures.write_wav(path, samples)
            q = capture_fixtures.fake_qualification(
                os.path.join(td, "qualification.json"),
                capability,
                limits
                if limits is not None
                else strict_limits(capability, capture_fixtures.ROW),
            )
            return capture_analyzer.analyze_capture(
                path, capture_fixtures.ROW, capability, q
            )

    def assert_metric_fail(self, samples, metric, capability="stereo"):
        result = self.analyze(samples, capability)
        self.assertEqual(result["outcome"], "failed")
        self.assertFalse(result["metrics"][metric]["pass"], metric)

    def test_clean_mono_and_stereo_gain_and_bounded_prefix_pass(self):
        stereo = self.analyze(
            capture_fixtures.clean_capture(
                capability="stereo", gain=0.54, prefix_samples=720
            ),
            "stereo",
        )
        mono = self.analyze(
            capture_fixtures.clean_capture(
                capability="mono", gain=0.73, prefix_samples=1440
            ),
            "mono",
        )
        self.assertEqual(stereo["outcome"], "passed")
        self.assertEqual(mono["outcome"], "passed")
        self.assertEqual(stereo["analysis_constants"]["preamble_total_samples"], 69120)

    def test_silence_missing_unexpected_carrier_and_clipping_fail_named_metrics(self):
        clean = capture_fixtures.clean_capture(capability="stereo")
        self.assert_metric_fail(np.zeros_like(clean), "preamble_correlation")
        missing = clean.copy()
        start = 480 + 69120
        missing[start:, 1] = 0
        self.assert_metric_fail(missing, "right_expected_carrier_power")
        mono_row = capture_fixtures.RowSpec(
            "synthetic.capture.mono", "fresh", "mono", "48_4_1", 288
        )
        mono = capture_fixtures.clean_capture(row=mono_row, capability="mono")
        right = capture_signal.semantic_pcm("right", mono_row)
        mono_start = 480 + 69120
        mono[mono_start:, 0] += right[69120 : 69120 + len(mono) - mono_start] * 0.65
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "mono.wav")
            capture_fixtures.write_wav(path, mono)
            q = capture_fixtures.fake_qualification(
                os.path.join(td, "q.json"), "mono", strict_limits("mono", mono_row)
            )
            result = capture_analyzer.analyze_capture(path, mono_row, "mono", q)
        self.assertEqual(result["outcome"], "failed")
        self.assertFalse(result["metrics"]["unexpected_carrier_power"]["pass"])
        clipped = clean.copy()
        clipped[start : start + 1000] = 1.0
        self.assert_metric_fail(clipped, "clipping_count")

    def test_truncation_and_corrupt_wav_fail_before_metrics(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "short.wav")
            capture_fixtures.write_wav(path, np.zeros((100, 2)))
            q = capture_fixtures.fake_qualification(
                os.path.join(td, "q.json"),
                "stereo",
                strict_limits("stereo", capture_fixtures.ROW),
            )
            with self.assertRaises(capture_analyzer.CaptureAnalyzerError) as ctx:
                capture_analyzer.analyze_capture(
                    path, capture_fixtures.ROW, "stereo", q
                )
            self.assertIn("duration shortfall", str(ctx.exception))
            with open(path, "ab") as fh:
                fh.write(b"trailing")
            with self.assertRaises(capture_analyzer.CaptureAnalyzerError):
                capture_analyzer.parse_s16le_wav(path, expected_channels=2)

    def test_dc_offset_clock_lag_brief_long_dropout_and_discontinuity_fail(self):
        clean = capture_fixtures.clean_capture(capability="stereo")
        start = 480 + 69120
        dc = clean.copy()
        dc[start:, 0] += 0.2
        self.assert_metric_fail(dc, "left_dc_offset")
        drift = clean.copy()
        body = drift[start:, 0]
        drift[start:, 0] = np.interp(
            np.linspace(0, len(body) - 1, len(body)),
            np.arange(len(body)),
            body[np.minimum((np.arange(len(body)) * 0.9).astype(int), len(body) - 1)],
        )
        self.assert_metric_fail(drift, "left_lag_drift_samples")
        brief = clean.copy()
        brief[start + 5760 : start + 5760 * 2] = 0
        self.assert_metric_fail(brief, "low_energy_window_count")
        long = clean.copy()
        long[start + 5760 : start + 5760 * 5] = 0
        self.assert_metric_fail(long, "longest_low_energy_run")
        discontinuity = clean.copy()
        discontinuity[start + 10000, 0] = 1.0
        self.assert_metric_fail(discontinuity, "left_continuity")

    def test_stereo_swap_duplication_loss_leakage_and_mismatch_fail_named_metrics(self):
        clean = capture_fixtures.clean_capture(capability="stereo")
        start = 480 + 69120
        swapped = clean.copy()
        swapped[start:] = swapped[start:, ::-1]
        self.assert_metric_fail(swapped, "channel_map_margin")
        duplicate = clean.copy()
        duplicate[start:, 1] = duplicate[start:, 0]
        self.assert_metric_fail(duplicate, "duplication_correlation")
        loss = clean.copy()
        loss[start:, 1] = 0
        self.assert_metric_fail(loss, "right_expected_carrier_power")
        leakage = clean.copy()
        leakage[start:, 0] += leakage[start:, 1] * 0.7
        self.assert_metric_fail(leakage, "opposite_channel_leakage")
        mismatch = clean.copy()
        mismatch[start:, 1] *= 0.1
        self.assert_metric_fail(mismatch, "level_mismatch")

    def test_wav_parser_rejects_corruption_matrix(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "bad.wav")
            capture_fixtures.write_wav(path, np.zeros((8, 1)))
            with open(path, "rb") as fh:
                clean = fh.read()

            def with_riff_size(payload):
                return payload[:4] + struct.pack("<I", len(payload) - 8) + payload[8:]

            duplicate = clean + b"fmt " + (16).to_bytes(4, "little") + clean[20:36]
            duplicate = with_riff_size(duplicate)
            truncated = with_riff_size(clean[:-1])
            extended_fmt = with_riff_size(
                clean[:16] + struct.pack("<I", 18) + clean[20:36] + b"\0\0" + clean[36:]
            )
            compressed = bytearray(clean)
            compressed[20:22] = struct.pack("<H", 3)
            wrong_rate = bytearray(clean)
            wrong_rate[24:28] = struct.pack("<I", 44100)
            wrong_channels = bytearray(clean)
            wrong_channels[22:24] = struct.pack("<H", 3)
            wrong_width = bytearray(clean)
            wrong_width[34:36] = struct.pack("<H", 24)
            cases = (
                (b"NOPE" + clean[4:], "RIFF/WAVE"),
                (clean + b"trailing", "RIFF size mismatch"),
                (truncated, "truncated WAV chunk"),
                (duplicate, "duplicate fmt"),
                (
                    with_riff_size(clean + b"JUNK\0\0\0\0"),
                    "unsupported extra WAV chunk",
                ),
                (extended_fmt, "unsupported extensible"),
                (bytes(compressed), "uncompressed PCM"),
                (bytes(wrong_rate), "48000"),
                (bytes(wrong_channels), "channel count"),
                (bytes(wrong_width), "16-bit"),
            )
            for payload, expected_error in cases:
                with self.subTest(expected_error=expected_error):
                    with open(path, "wb") as fh:
                        fh.write(payload)
                    with self.assertRaises(
                        capture_analyzer.CaptureAnalyzerError
                    ) as ctx:
                        capture_analyzer.parse_s16le_wav(path, expected_channels=1)
                    self.assertIn(expected_error, str(ctx.exception))


if __name__ == "__main__":
    unittest.main(verbosity=2)
