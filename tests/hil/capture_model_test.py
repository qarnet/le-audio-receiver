#!/usr/bin/env python3
"""Host-only strict capture binding and qualification behavior tests."""

import hashlib
import json
import os
import sys
import tempfile
import unittest

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))

from hil import model, qualification  # noqa: E402


def write_json(path, value, canonical=False):
    with open(path, "w", encoding="utf-8") as fh:
        if canonical:
            fh.write(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
        else:
            json.dump(value, fh)


def hash_file(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def fixture_doc(capability="mono"):
    channels = 1 if capability == "mono" else 2
    return {
        "schema_version": 1,
        "fixture_id": "fixture-" + capability,
        "capture_capability": capability,
        "roles": {
            "receiver": {
                "kind": "zephyr_dut",
                "board": "receiver",
                "images": ["cpuapp"],
            },
            "source": {"kind": "zephyr_dut", "board": "source", "images": ["cpuapp"]},
            "capture": {"kind": "alsa_capture", "channels": channels},
        },
    }


def binding_doc(capability, metadata_path):
    channels = 1 if capability == "mono" else 2
    return {
        "schema_version": 1,
        "fixture_id": "fixture-" + capability,
        "roles": {
            "receiver": {
                "probe": {"backend": "nrf-probes", "family": "nrf54l"},
                "serial": {"baud": 115200, "dtr": False, "rts": False, "udev": {}},
            },
            "source": {
                "probe": {
                    "backend": "jlink",
                    "family": "nrf53",
                    "udev": {
                        "ID_VENDOR_ID": "1366",
                        "ID_MODEL_ID": "1015",
                        "ID_SERIAL_SHORT": "source",
                    },
                },
                "serial": {"baud": 115200, "dtr": False, "rts": False, "udev": {}},
            },
            "capture": {
                "backend": "alsa",
                "device": "hw:Capture_1,0",
                "channels": channels,
                "sample_rate": 48000,
                "sample_format": "S16_LE",
                "udev": {
                    "ID_VENDOR_ID": "0d8c",
                    "ID_MODEL_ID": "0014",
                    "ID_SERIAL_SHORT": "capture",
                },
                "mixer": {
                    "control": "Mic",
                    "volume": "12",
                    "capture_switch": "on",
                    "agc_control": "Auto Gain Control",
                    "agc": "off",
                },
                "fixture_metadata": metadata_path,
            },
        },
    }


def metadata_doc(fixture_id):
    return {
        "fixture_id": fixture_id,
        "capture_hardware": "synthetic capture hardware",
        "dac": "synthetic DAC",
        "cable_network_schematic_id": "synthetic-network",
        "measured_attenuation": "synthetic",
        "measured_channel_mismatch": "synthetic",
        "electrical_review_id": "synthetic-review",
        "electrical_review_date": "2026-01-01",
        "usb_path": "synthetic-usb",
        "kernel_version": "synthetic-kernel",
        "alsa_version": "synthetic-alsa",
        "notes": "synthetic test fixture only",
    }


def load_fixture_binding(directory, capability="mono"):
    metadata = os.path.join(directory, "metadata.json")
    write_json(metadata, metadata_doc("fixture-" + capability))
    fixture_path = os.path.join(directory, "fixture.json")
    binding_path = os.path.join(directory, "binding.json")
    write_json(fixture_path, fixture_doc(capability))
    write_json(binding_path, binding_doc(capability, metadata))
    fixture = model.load_logical_fixture(fixture_path)
    binding = model.load_physical_binding(binding_path, fixture)
    return fixture, binding, fixture_path, binding_path


def make_qualification(directory, fixture, binding):
    evidence = []
    for index in range(2):
        wav = os.path.join(directory, "qual-%d.wav" % index)
        result = os.path.join(directory, "qual-%d.json" % index)
        with open(wav, "wb") as fh:
            fh.write(b"synthetic wav %d" % index)
        write_json(result, {"outcome": "passed"}, canonical=True)
        evidence.append(
            {
                "duration_seconds": 130,
                "wav_path": wav,
                "wav_sha256": hash_file(wav),
                "analyzer_result_path": result,
                "analyzer_result_sha256": hash_file(result),
                "timestamp_utc": "2026-01-0%dT00:00:00Z" % (index + 1),
                "outcome": "passed",
            }
        )
    limits = {}
    for name in qualification.metric_names(fixture.capture_capability):
        if name in qualification.EXACT_RANGE_METRICS:
            limits[name] = {"min": 0, "max": 999999999}
        elif name in qualification.MINIMUM_METRICS:
            limits[name] = {"min": -999999999}
        else:
            limits[name] = {"max": 999999999}
    doc = {
        "schema_version": 1,
        "status": "accepted",
        "capability": fixture.capture_capability.value,
        "fixture_id": fixture.fixture_id,
        "capture_identity": qualification.capture_identity(binding.roles["capture"]),
        "fixture_metadata_sha256": binding.roles["capture"].fixture_metadata.sha256,
        "qualification_runs": evidence,
        "limits": limits,
        "accepted_by": "synthetic-test-only",
        "accepted_at_utc": "2026-01-03T00:00:00Z",
    }
    path = os.path.join(directory, "qualification.json")
    write_json(path, doc, canonical=True)
    return path, doc


class TestCaptureModel(unittest.TestCase):
    def test_checked_in_logical_capture_examples_parse(self):
        for capability, name in (
            ("mono", "fixture-mono.json"),
            ("stereo", "fixture-stereo.json"),
        ):
            fixture = model.load_logical_fixture(
                os.path.join(REPO, "tests", "hil", name)
            )
            self.assertEqual(fixture.capture_capability.value, capability)
            self.assertEqual(
                fixture.roles["capture"].channels, 1 if capability == "mono" else 2
            )

    def test_capture_binding_requires_exact_direct_format_and_external_metadata(self):
        with tempfile.TemporaryDirectory() as td:
            fixture, _binding, _f, binding_path = load_fixture_binding(td)
            doc = binding_doc("mono", os.path.join(td, "metadata.json"))
            doc["roles"]["capture"]["device"] = "plughw:CARD,0"
            write_json(binding_path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(binding_path, fixture)
            self.assertIn("direct hw", str(ctx.exception))
            doc["roles"]["capture"]["device"] = "hw:Capture_1,0"
            doc["roles"]["capture"]["fixture_metadata"] = os.path.join(
                REPO, "tests", "hil", "fixture.json"
            )
            write_json(binding_path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(binding_path, fixture)
            self.assertIn("outside repository", str(ctx.exception))
            doc = binding_doc("mono", os.path.join(td, "metadata.json"))
            doc["roles"]["capture"]["udev"]["ID_PATH"] = "/dev/snd/pcmC0D0c"
            del doc["roles"]["capture"]["udev"]["ID_SERIAL_SHORT"]
            write_json(binding_path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(binding_path, fixture)
            self.assertIn("/dev/snd", str(ctx.exception))

    def test_capture_binding_rejects_channel_mixer_and_udev_drift(self):
        with tempfile.TemporaryDirectory() as td:
            fixture, _binding, _f, binding_path = load_fixture_binding(td, "stereo")
            for mutate, fragment in (
                (
                    lambda d: d["roles"]["capture"].__setitem__("channels", 1),
                    "channels",
                ),
                (
                    lambda d: d["roles"]["capture"]["mixer"].__setitem__("agc", "on"),
                    "agc",
                ),
                (lambda d: d["roles"]["capture"].__setitem__("udev", {}), "udev"),
            ):
                doc = binding_doc("stereo", os.path.join(td, "metadata.json"))
                mutate(doc)
                write_json(binding_path, doc)
                with self.assertRaises(model.HilSchemaError) as ctx:
                    model.load_physical_binding(binding_path, fixture)
                self.assertIn(fragment, str(ctx.exception))

    def test_accepted_qualification_rechecks_identity_metadata_and_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            fixture, binding, _f, _b = load_fixture_binding(td)
            path, doc = make_qualification(td, fixture, binding)
            parsed = qualification.load_qualification(path, fixture, binding)
            self.assertEqual(parsed.capability.value, "mono")
            doc["status"] = "pending"
            write_json(path, doc, canonical=True)
            with self.assertRaises(qualification.QualificationError):
                qualification.load_qualification(path, fixture, binding)

    def test_qualification_rejects_missing_metric_and_identity_drift_before_hardware(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            fixture, binding, _f, _b = load_fixture_binding(td)
            path, doc = make_qualification(td, fixture, binding)
            del doc["limits"]["noise_floor"]
            write_json(path, doc, canonical=True)
            with self.assertRaises(qualification.QualificationError) as ctx:
                qualification.load_qualification(path, fixture, binding)
            self.assertIn("missing metric", str(ctx.exception))
            path, doc = make_qualification(td, fixture, binding)
            doc["capture_identity"]["device"] = "hw:Other,0"
            write_json(path, doc, canonical=True)
            with self.assertRaises(qualification.QualificationError) as ctx:
                qualification.load_qualification(path, fixture, binding)
            self.assertIn("capture_identity", str(ctx.exception))


if __name__ == "__main__":
    unittest.main(verbosity=2)
