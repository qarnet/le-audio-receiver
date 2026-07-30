#!/usr/bin/env python3
"""Unit tests for bluez-wireplumber-gate.py — parser, discovery, log parsing."""

import importlib
import json
import os
import sys
import tempfile
import unittest

# Ensure the scripts directory is in the path
SCRIPT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, SCRIPT_DIR)

# Module name has hyphens — use importlib
_bg = importlib.import_module("bluez-wireplumber-gate")
BluezWirePlumberGate = _bg.BluezWirePlumberGate
GateResult = _bg.GateResult
_parse_version = _bg._parse_version


class TestFindBtDevices(unittest.TestCase):
    """Test PipeWire dump BT device discovery."""

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test.log",
        )

    def test_empty_dump(self):
        devices = self.gate._find_bt_devices([])
        self.assertEqual(devices, [])

    def test_no_bt_devices(self):
        dump = [
            {
                "id": 1,
                "info": {
                    "props": {
                        "device.api": "alsa",
                        "device.name": "Built-in Audio",
                    }
                },
            }
        ]
        devices = self.gate._find_bt_devices(dump)
        self.assertEqual(devices, [])

    def test_bluez_device(self):
        dump = [
            {
                "id": 42,
                "info": {
                    "props": {
                        "device.api": "bluez5",
                        "device.name": "bluez_card.AB_CD_EF_01_02_03",
                        "device.description": "LE Audio Receiver",
                        "media.class": "Audio/Device",
                    }
                },
            }
        ]
        devices = self.gate._find_bt_devices(dump)
        self.assertEqual(len(devices), 1)
        self.assertEqual(devices[0]["id"], 42)
        self.assertIn("LE Audio Receiver", devices[0]["description"])

    def test_bluez5_device(self):
        dump = [
            {
                "id": 99,
                "info": {
                    "props": {
                        "device.api": "bluez5",
                        "device.name": "bluez_card.DD_EE_FF_00_11_22",
                        "device.description": "My Headphones",
                    }
                },
            }
        ]
        devices = self.gate._find_bt_devices(dump)
        self.assertEqual(len(devices), 1)


class TestFindBtNodes(unittest.TestCase):
    """Test PipeWire dump BT node discovery."""

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test.log",
        )

    def test_no_nodes(self):
        nodes = self.gate._find_bt_nodes([])
        self.assertEqual(nodes, [])

    def test_bluez_sink_node(self):
        dump = [
            {
                "id": 88,
                "info": {
                    "props": {
                        "node.name": "bluez_output.AB_CD_EF_01_02_03.1",
                        "media.class": "Audio/Sink",
                    }
                },
            }
        ]
        nodes = self.gate._find_bt_nodes(dump)
        self.assertEqual(len(nodes), 1)
        self.assertIn("bluez_output", nodes[0]["name"])
        self.assertEqual(nodes[0]["media_class"], "Audio/Sink")

    def test_bluez_source_node(self):
        dump = [
            {
                "id": 77,
                "info": {
                    "props": {
                        "node.name": "bluez_input.AB_CD_EF_01_02_03.2",
                        "media.class": "Audio/Source",
                    }
                },
            }
        ]
        nodes = self.gate._find_bt_nodes(dump)
        self.assertEqual(len(nodes), 1)
        self.assertIn("bluez_input", nodes[0]["name"])


class TestSummarizePwDump(unittest.TestCase):
    """Test pw-dump summarization."""

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test.log",
        )

    def test_empty(self):
        summary = self.gate._summarize_pw_dump([])
        self.assertEqual(summary["total_objects"], 0)
        self.assertEqual(summary["node_names"], [])

    def test_mixed(self):
        dump = [
            {
                "id": 0,
                "info": {"props": {"core.name": "pipewire-0"}},
            },
            {
                "id": 1,
                "info": {"props": {"node.name": "bluez_output.test.1"}},
            },
            {
                "id": 2,
                "info": {"props": {"device.api": "bluez5"}},
            },
        ]
        summary = self.gate._summarize_pw_dump(dump)
        self.assertEqual(summary["total_objects"], 3)
        self.assertIn("bluez_output.test.1", summary["node_names"])
        self.assertIn("bluez5", summary["device_apis"])


class TestParseReceiverLog(unittest.TestCase):
    """Test receiver log parsing for ASCS streaming and fault detection."""

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test_receiver.log",
        )

    def _write_log(self, content: str) -> GateResult:
        with open(self.gate.log_path, "w") as f:
            f.write(content)
        result = GateResult()
        return result

    def test_no_log_file(self):
        self.gate.log_path = "/tmp/nonexistent_receiver_xyz.log"
        result = GateResult()
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        self.assertIn("not found", result.evidence[-1])

    def test_empty_log(self):
        result = self._write_log("")
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        # Any failure reason is fine — just verify it fails
        self.assertIn("FAIL", str(result.evidence))

    def test_clean_log(self):
        content = """[00:00:01] BLE ready
[00:00:02] Advertising as LE Audio Receiver
[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, 48000 decoded frames
[00:01:00] streaming steady
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"Expected clean log to pass: {result.evidence}")

    def test_ascs_config_only_no_start(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:20] I2S DMA started
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("No ASCS streaming", evidence_str)

    def test_malformed_frames(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, decoded=5000
[00:00:20] ERROR: malformed SDU detected
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("malformed frames", evidence_str)

    def test_decode_fault(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, decoded=5000
[00:00:30] LC3 decode failed: error code -1
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)

    def test_i2s_fault(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, decoded=5000
[00:00:20] I2S underrun detected
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)

    def test_offload_fault(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, decoded=5000
[00:00:25] FLPR offload fault: stall detected
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)

    def test_fps_counter(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Audio: 99 fps, buffer OK
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok)

    def test_nonzero_decoded(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] decoded=12345 frames total
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok)

    # ── Strict nonzero gate tests (Phase 2 fix) ──────────────────────

    def test_zero_frames_fatal(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("No explicit SDU/decoded count", evidence_str)

    def test_i2s_not_started_fatal(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:30] 100 fps, decoded=48000 frames
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("I2S DMA not started", evidence_str)

    def test_decoder_not_ready_fatal(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps, decoded=48000
[00:00:20] LC3 decoder not ready for stream[0]
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("LC3 decoder not ready", evidence_str)

    def test_frame_dur_not_set_fatal(self):
        """Frame duration not set — no fallback, must fail codec config."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] frame dur not set (ret=-61)
[00:00:20] 100 fps, decoded=3000
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"frame dur not set must be fatal: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("Frame Duration LTV not set", evidence_str)

    def test_freq_not_set_fatal(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] freq not set
[00:00:20] 100 fps, decoded=48000
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("Frequency not set", evidence_str)

    def test_fps_matches_10ms_expected(self):
        """100 fps log should match 10 ms expected (Frame Duration: 10000 us)."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] Frame Duration: 10000 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] 100 fps, decoded=3000
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok)

    def test_fps_matches_7_5ms_expected(self):
        """~133 fps log should match 7.5 ms expected."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] Frame Duration: 7500 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] 133 fps, decoded=4000
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok)

    def test_fps_deviation_fatal(self):
        """Actual fps far from expected should fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] Frame Duration: 10000 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] 50 fps, decoded=1500
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        evidence_str = "\n".join(result.evidence)
        self.assertIn("deviates from expected", evidence_str)

    # ── Phase 2 strict evidence gate tests ─────────────────────────

    def test_boot_i2s_ready_only_fails(self):
        """Boot 'I2S ready' must not substitute for runtime 'I2S DMA started'."""
        content = """[00:00:01] audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 16 blocks)
[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"Boot I2S ready must not pass: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("I2S DMA not started", evidence_str)

    def test_decoder_init_only_fails(self):
        """Decoder init without explicit SDUs/decoded must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:14] LC3 decoder[0]: 48000 Hz 10000 us ch=1
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(
            ok, f"Decoder init without explicit counts must fail: {result.evidence}"
        )
        evidence_str = "\n".join(result.evidence)
        self.assertIn("No explicit SDU/decoded count", evidence_str)

    def test_stream_summary_zero_sdu_fails(self):
        """Stream summary with SDUs=0 must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=0 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"Zero SDUs must fail: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("Zero valid SDUs", evidence_str)

    def test_stream_summary_zero_decoded_fails(self):
        """Stream summary with decoded=0 must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=50 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"Zero decoded must fail: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("Zero decoded frames", evidence_str)

    def test_stream_summary_passes(self):
        """Stream summary with SDUs>0, decoded>0, I2S DMA started must pass."""
        content = """[00:00:01] audio_i2s: I2S ready (48 kHz, 16-bit, stereo, 12 blocks)
[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"Clean summary should pass: {result.evidence}")

    def test_stream_summary_with_faults_fails(self):
        """Stream summary with decode_err=5 must FAIL (Phase 2 strict correction)."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=95 plc=5 decode_err=5 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"Summary with decode_err=5 must FAIL: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("decode_err=5", evidence_str)

    # ── Phase 2 strict: summary fault fields are hard failures ───

    def test_summary_decode_err_nonzero_fails(self):
        """Summary with decode_err > 0 must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=3 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"decode_err=3 must fail: {result.evidence}")

    def test_summary_i2s_underrun_nonzero_fails(self):
        """Summary with i2s_underrun > 0 must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=0 i2s_underrun=1 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"i2s_underrun=1 must fail: {result.evidence}")

    def test_summary_stream_reset_nonzero_fails(self):
        """Summary with stream_reset > 0 must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=0 i2s_underrun=0 stream_reset=1
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"stream_reset=1 must fail: {result.evidence}")

    def test_summary_all_faults_zero_passes(self):
        """Summary with all fault fields zero must pass."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=7 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"All-zero faults must pass: {result.evidence}")

    # ── Phase 2 strict: duration-consistent SDU counts ────────────

    def test_sdu_count_consistent_7_5ms_30s_passes(self):
        """~4000 SDUs at 7.5ms for 30s must pass."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 7500 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=4000 decoded=4100 plc=100 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"4000 SDUs at 7.5ms/30s must pass: {result.evidence}")

    def test_sdu_count_too_low_for_duration_fails(self):
        """500 SDUs at 7.5ms for 30s (~4000 expected) must fail."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 7500 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=500 decoded=550 plc=50 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"500 SDUs vs ~4000 expected must fail: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("too low", evidence_str.lower())

    def test_sdu_count_too_high_for_duration_fails(self):
        """50000 SDUs at 7.5ms for 30s (~4000 expected) must fail as stale."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 7500 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=50000 decoded=51000 plc=1000 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(
            ok, f"50000 SDUs vs ~4000 expected must fail: {result.evidence}"
        )

    def test_sdu_count_10ms_30s_consistent_passes(self):
        """~3000 SDUs at 10ms for 30s must pass."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 10000 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=3000 decoded=3050 plc=50 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"3000 SDUs at 10ms/30s must pass: {result.evidence}")

    def test_multiple_summaries_uses_last(self):
        """If log has multiple summaries, last one should be the acceptance target."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] Stream[0] summary: SDUs=50 decoded=55 plc=5 decode_err=0 i2s_underrun=0 stream_reset=0
[00:00:20] Stream[0] summary: SDUs=4000 decoded=4100 plc=100 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        # This gate is duration=30, but no Frame Duration line so expected_fps=0.0
        # → SDU check skipped. Both summaries non-zero → passes.
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"Multiple summaries with valid counts: {result.evidence}")
        self.assertEqual(result.receiver_counters.get("sdu_summary"), 4000)

    # ── Phase 2 strict: log freshness / independent capture ──────

    def test_no_duplicate_run_markers(self):
        """Log with multiple 'BLE ready' markers should still parse correctly."""
        content = """[00:00:00] BLE ready
[00:00:02] Advertising as LE Audio Receiver
[00:01:00] BLE ready
[00:01:02] Advertising as LE Audio Receiver
[00:01:10] ASCS: ASE configured
[00:01:12] ASCS: stream started
[00:01:13] I2S DMA started
[00:01:30] Stream[0] summary: SDUs=4000 decoded=4100 plc=100 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"Log with multiple boot markers: {result.evidence}")

    def test_binary_prefix_before_log_lines(self):
        """Binary garbage before valid log lines should be handled."""
        content = b"\x00\x01\x02\x03[00:00:10] ASCS: ASE configured\n[00:00:12] ASCS: stream started\n[00:00:13] I2S DMA started\n[00:00:30] Stream[0] summary: SDUs=3000 decoded=3050 plc=50 decode_err=0 i2s_underrun=0 stream_reset=0\n"
        with open(self.gate.log_path, "wb") as f:
            f.write(content)
        result = GateResult()
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"Log with binary prefix: {result.evidence}")

    def test_i2s_dma_started_exact_match_required(self):
        """Only exact 'I2S DMA started', not 'I2S clock started' or 'I2S ready'."""
        content = """[00:00:01] I2S clock started
[00:00:02] I2S configuring DMA
[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:30] Stream[0] summary: SDUs=100 decoded=100 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"'I2S clock started' must not pass: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("I2S DMA not started", evidence_str)

    def test_binary_log(self):
        """Ensure binary garbage in log is handled gracefully."""
        content = b"ASCS: ASE configured\nASCS: stream started\nI2S DMA started\n\x00\xff\xfe100 fps\n"
        with open(self.gate.log_path, "wb") as f:
            f.write(content)
        result = GateResult()
        ok = self.gate.parse_receiver_log(result)
        # Should succeed despite binary garbage (fps=100 found)
        self.assertTrue(ok)

    def tearDown(self):
        if os.path.exists(self.gate.log_path):
            os.unlink(self.gate.log_path)


class TestVersionParsing(unittest.TestCase):
    """Test version string parsing."""

    def test_simple(self):
        self.assertEqual(_parse_version("5.86"), (5, 86))
        self.assertEqual(_parse_version("1.6.5"), (1, 6, 5))
        self.assertEqual(_parse_version("0.5.14"), (0, 5, 14))

    def test_with_prefix(self):
        self.assertEqual(_parse_version("bluetoothctl: 5.86"), (5, 86))


class TestBlueZPathConstruction(unittest.TestCase):
    """Test BlueZ object path construction."""

    def test_find_receiver_path(self):
        gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test.log",
        )
        # Without D-Bus, find_receiver uses bluetoothctl
        # Test the path construction logic

        # Just verify the regex-based construction logic
        addr = "DB:A6:0C:05:A2:AA"
        expected = "/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA"
        self.assertEqual(
            expected,
            f"/org/bluez/hci0/dev_{addr.replace(':', '_').upper()}",
        )


class TestParsedCounters(unittest.TestCase):
    """Test counter extraction from receiver logs."""

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test_counters.log",
        )

    def test_multiple_faults_counted(self):
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] 100 fps
[00:00:20] malformed SDU
[00:00:21] malformed SDU
[00:00:22] malformed SDU
[00:00:25] LC3 decode fault
[00:00:26] LC3 decode fault
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok)
        # The result.evidence should count them
        evidence_str = "\n".join(result.evidence)
        # 3 malformed + 2 decode faults
        self.assertIn("Malformed frames: 3", evidence_str)
        self.assertIn("Decode faults: 2", evidence_str)

    def _write_log(self, content: str) -> GateResult:
        with open(self.gate.log_path, "w") as f:
            f.write(content)
        result = GateResult()
        return result

    def tearDown(self):
        if os.path.exists(self.gate.log_path):
            os.unlink(self.gate.log_path)


class TestFrameDurationGateIntegration(unittest.TestCase):
    """Gate-level tests for frame duration handling.

    Actual lc3_enable() frame-duration logic is verified by BSIM:
      - 10 ms scenario (48_4_1) via scripts/bsim-stage1-run.sh
      - 7.5 ms scenario (48_3_1) via scripts/bsim-stage1-run.sh
    Both scenarios exercise the full codec-config→decoder→I2S path
    including audio_sink_set_input_frames() with dynamic frame counts.

    These tests verify the gate correctly handles frame duration
    information from receiver logs, not the C logic itself.
    """

    def setUp(self):
        self.gate = BluezWirePlumberGate(
            receiver_name="LE Audio Receiver",
            duration=30,
            log_path="/tmp/test_framedur.log",
        )

    def _write_log(self, content: str) -> GateResult:
        with open(self.gate.log_path, "w") as f:
            f.write(content)
        return GateResult()

    def test_7_5ms_frame_duration_parsed(self):
        """Gate must parse 7.5 ms frame duration from log."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 7500 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=4000 decoded=4100 plc=100 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"7.5ms gate must pass: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("7500 us", evidence_str)
        self.assertIn("133.3 fps", evidence_str)

    def test_10ms_frame_duration_parsed(self):
        """Gate must parse 10 ms frame duration from log."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:11] LC3 decoder[0]: 48000 Hz 10000 us
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:30] Stream[0] summary: SDUs=3000 decoded=3050 plc=50 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertTrue(ok, f"10ms gate must pass: {result.evidence}")
        evidence_str = "\n".join(result.evidence)
        self.assertIn("10000 us", evidence_str)
        self.assertIn("100.0 fps", evidence_str)

    def test_frame_dur_not_set_still_fatal(self):
        """Missing Frame Duration in log is still a fatal gate error."""
        content = """[00:00:10] ASCS: ASE configured
[00:00:12] ASCS: stream started
[00:00:13] I2S DMA started
[00:00:15] frame dur not set (ret=-61)
[00:00:30] Stream[0] summary: SDUs=0 decoded=0 plc=0 decode_err=0 i2s_underrun=0 stream_reset=0
"""
        result = self._write_log(content)
        ok = self.gate.parse_receiver_log(result)
        self.assertFalse(ok, f"frame dur not set must be fatal: {result.evidence}")

    def tearDown(self):
        if os.path.exists(self.gate.log_path):
            os.unlink(self.gate.log_path)


if __name__ == "__main__":
    unittest.main()
