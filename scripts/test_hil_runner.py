#!/usr/bin/env python3
"""Public-boundary regression tests for the host-only HIL runner (RH0).

Standalone stdlib unittest program: directly executable by the canonical
Python-child mechanism (scripts/test-all.sh invokes every scripts/test_*.py
with scripts/ on PYTHONPATH).  Tests assert public module/CLI results and
filesystem/process outcomes.  Transactional failure tests may patch narrow
module-local I/O seams (evidence staging, commit, hash, and snapshot
helpers) for deterministic failure injection; there are no helper call-count
or internal data-shape assertions.
"""

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import hil.evidence  # noqa: E402
from hil import cli, lifecycle, model, protocol  # noqa: E402
from hil.evidence import EvidenceError, finalize_evidence  # noqa: E402

FIXTURE_JSON = os.path.join(REPO_ROOT, "tests", "hil", "fixture.json")
BINDING_EXAMPLE = os.path.join(REPO_ROOT, "tests", "hil", "fixture.local.example.json")
FIXTURE_ID = "local-nrf54l15-receiver"


def _write_json(path, obj):
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(obj, fh, indent=2)


def _zephyr_role(name, board):
    return {"kind": "zephyr_dut", "board": board, "images": ["cpuapp", "flpr"]}


def _fixture_dict(**root_overrides):
    doc = {
        "schema_version": 1,
        "fixture_id": FIXTURE_ID,
        "capture_capability": "none",
        "roles": {
            "receiver": _zephyr_role("receiver", "nrf54l15dk/nrf54l15/cpuapp"),
            "source": _zephyr_role("source", "nrf5340dk/nrf5340/cpuapp"),
        },
    }
    doc.update(root_overrides)
    return doc


def _zephyr_binding(
    role,
    baud: object = 115200,
    dtr: object = False,
    rts: object = False,
    udev=None,
    probe_udev=None,
):  # line values are object: schema tests feed invalid types
    u = (
        udev
        if udev is not None
        else {
            "ID_VENDOR_ID": "1234",
            "ID_MODEL_ID": "5678",
            "ID_SERIAL_SHORT": "STABLE_SERIAL",
        }
    )
    if role == "receiver":
        probe = {"backend": "nrf-probes", "family": "nrf54l"}
        if probe_udev is not None:
            probe["udev"] = probe_udev
    else:
        # Source nRF5340DK: onboard Segger J-Link, located by its exact
        # USB identity map (RH2 corrected backend).
        probe = {"backend": "jlink", "family": "nrf53"}
        probe["udev"] = (
            probe_udev
            if probe_udev is not None
            else {
                "ID_VENDOR_ID": "1366",
                "ID_MODEL_ID": "1015",
                "ID_SERIAL_SHORT": "J-LINK-SERIAL",
            }
        )
    return {
        "probe": probe,
        "serial": {"baud": baud, "dtr": dtr, "rts": rts, "udev": u},
    }


def _binding_dict(fixture_id=FIXTURE_ID, roles=None, **root_overrides):
    doc = {
        "schema_version": 1,
        "fixture_id": fixture_id,
        "roles": roles
        if roles is not None
        else {
            "receiver": _zephyr_binding("receiver"),
            "source": _zephyr_binding("source"),
        },
    }
    doc.update(root_overrides)
    return doc


def _run_cli(argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = cli.main(argv)
    return rc, out.getvalue(), err.getvalue()


# ---------- HIL1 record helpers ----------


def _hdr(kind="state", **overrides):
    doc = {
        "protocol_version": 1,
        "kind": kind,
        "firmware_id": "fw1",
        "monotonic_ms": 100,
        "command_id": "c1",
        "run_id": "r1",
        "segment": 0,
        "data": {},
    }
    doc.update(overrides)
    return doc


def _line(**overrides):
    return "HIL1 " + json.dumps(_hdr(**overrides))


def _rec(**overrides):
    return protocol.parse_hil1_line(_line(**overrides))


def _snapshot_tracker(tracker):
    return (
        tracker.firmware_id,
        tracker.segment,
        tracker.state_index,
        tracker.last_monotonic,
        tracker.terminal,
        tracker.aborted,
        tracker.abort_cause,
    )


def _accept_segment(tracker, segment, start_ms, command="c1"):
    states = list(protocol.STATES)
    if segment > 0:
        states = states[states.index("connecting") :]
    for i, state in enumerate(states):
        tracker.accept(
            _rec(
                kind="state",
                segment=segment,
                monotonic_ms=start_ms + i,
                command_id=command,
                data={"state": state},
            )
        )


class TestLogicalFixture(unittest.TestCase):
    def test_checked_in_fixture_parses_capability_none(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        self.assertEqual(fixture.schema_version, 1)
        self.assertEqual(fixture.fixture_id, FIXTURE_ID)
        self.assertEqual(fixture.capture_capability, model.CaptureCapability.NONE)
        self.assertEqual(set(fixture.roles), {"receiver", "source"})
        self.assertEqual(
            fixture.roles["receiver"],
            model.LogicalRole(
                kind="zephyr_dut",
                board="nrf54l15dk/nrf54l15/cpuapp",
                images=("cpuapp", "flpr"),
                channels=None,
            ),
        )
        self.assertEqual(fixture.roles["source"].images, ("cpuapp", "cpunet"))

    def test_unknown_key_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            _write_json(path, _fixture_dict(extra_field=1))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("unknown key", str(ctx.exception))

    def test_missing_key_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            doc = _fixture_dict()
            del doc["capture_capability"]
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("missing key", str(ctx.exception))

    def test_wrong_scalar_types_fail_closed(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            for broken in (
                {"schema_version": "1"},
                {"schema_version": True},
                {"fixture_id": 7},
                {"capture_capability": 42},
                {"capture_capability": "surround"},
                {"roles": []},
            ):
                _write_json(path, _fixture_dict(**broken))
                with self.assertRaises(model.HilSchemaError):
                    model.load_logical_fixture(path)

    def test_invalid_json_and_non_utf8_fail(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("not json")
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)
            with open(path, "wb") as fh:
                fh.write(b"\xff\xfe\x00")
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)

    def test_duplicate_semantic_role_names_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            # Duplicate "roles" key at the object level (hand-crafted text).
            doc = _fixture_dict()
            raw = json.dumps(doc, indent=2)
            dup = raw.replace('"roles": {', '"roles": { "receiver": null,')
            dup = dup.replace(
                '"capture_capability": "none",',
                '"capture_capability": "none", "capture_capability": "none",',
            )
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(dup)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("duplicate key", str(ctx.exception))

    def test_capture_role_capability_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            # none capability with capture role present: forbidden.
            doc = _fixture_dict()
            doc["roles"]["capture"] = {"kind": "alsa_capture", "channels": 1}
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("forbidden", str(ctx.exception))
            # mono capability without capture role: required.
            doc = _fixture_dict(capture_capability="mono")
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("required", str(ctx.exception))
            # mono capability with stereo channels: mismatch.
            doc = _fixture_dict(capture_capability="mono")
            doc["roles"]["capture"] = {"kind": "alsa_capture", "channels": 2}
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("channels", str(ctx.exception))
            # stereo capability with mono channels: mismatch.
            doc = _fixture_dict(capture_capability="stereo")
            doc["roles"]["capture"] = {"kind": "alsa_capture", "channels": 1}
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)
            # capture role with wrong kind.
            doc = _fixture_dict(capture_capability="mono")
            doc["roles"]["capture"] = _zephyr_role("capture", "nrf54l15dk")
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)

    def test_zephyr_role_requirements(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            doc = _fixture_dict()
            doc["roles"]["receiver"] = {
                "kind": "zephyr_dut",
                "board": "",
                "images": ["a"],
            }
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)
            doc = _fixture_dict()
            doc["roles"]["receiver"] = {
                "kind": "zephyr_dut",
                "board": "b",
                "images": [],
            }
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)
            doc = _fixture_dict()
            doc["roles"]["receiver"] = {
                "kind": "zephyr_dut",
                "board": "b",
                "images": ["a", "a"],
            }
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)
            doc = _fixture_dict()
            doc["roles"]["receiver"] = {
                "kind": "zephyr_dut",
                "board": "b",
                "images": ["a", ""],
            }
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError):
                model.load_logical_fixture(path)

    def test_extra_role_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            # none capability: exactly receiver and source.
            doc = _fixture_dict()
            doc["roles"]["extra"] = _zephyr_role("extra", "some-board")
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("extra role", str(ctx.exception))
            # mono capability: exactly receiver, source, and capture.
            doc = _fixture_dict(capture_capability="mono")
            doc["roles"]["capture"] = {"kind": "alsa_capture", "channels": 1}
            doc["roles"]["watcher"] = _zephyr_role("watcher", "other-board")
            _write_json(path, doc)
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_logical_fixture(path)
            self.assertIn("extra role", str(ctx.exception))

    def test_capture_role_board_is_none(self):
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "f.json")
            doc = _fixture_dict(capture_capability="mono")
            doc["roles"]["capture"] = {"kind": "alsa_capture", "channels": 1}
            _write_json(path, doc)
            fixture = model.load_logical_fixture(path)
            capture = fixture.roles["capture"]
            self.assertIsNone(capture.board)
            self.assertEqual(capture.channels, 1)
            self.assertEqual(capture.kind, "alsa_capture")
            self.assertEqual(capture.images, ())

    def test_role_maps_are_immutable(self):
        # Mapping views expose lookup/iteration but no mutation path.
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        self.assertEqual(fixture.roles["receiver"].kind, "zephyr_dut")
        self.assertFalse(hasattr(fixture.roles, "__setitem__"))
        binding = model.load_physical_binding(BINDING_EXAMPLE, fixture)
        self.assertEqual(binding.roles["source"].probe.family, "nrf53")
        self.assertFalse(hasattr(binding.roles, "__setitem__"))


class TestPhysicalBinding(unittest.TestCase):
    def test_checked_in_example_binding_cross_validates(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        binding = model.load_physical_binding(BINDING_EXAMPLE, fixture)
        self.assertEqual(binding.schema_version, 1)
        self.assertEqual(binding.fixture_id, FIXTURE_ID)
        self.assertEqual(set(binding.roles), {"receiver", "source"})
        self.assertEqual(binding.roles["receiver"].probe.family, "nrf54l")
        self.assertEqual(binding.roles["source"].probe.family, "nrf53")

    def test_valid_temporary_binding_cross_validates(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")
            _write_json(path, _binding_dict())
            binding = model.load_physical_binding(path, fixture)
            self.assertEqual(binding.roles["receiver"].serial.baud, 115200)
            self.assertFalse(binding.roles["receiver"].serial.dtr)
            self.assertFalse(binding.roles["receiver"].serial.rts)

    def test_serial_line_states_are_explicit_booleans(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")
            roles = {
                "receiver": _zephyr_binding("receiver", dtr=True, rts=False),
                "source": _zephyr_binding("source", dtr=True, rts=True),
            }
            _write_json(path, _binding_dict(roles=roles))
            binding = model.load_physical_binding(path, fixture)
            self.assertTrue(binding.roles["receiver"].serial.dtr)
            self.assertFalse(binding.roles["receiver"].serial.rts)
            self.assertTrue(binding.roles["source"].serial.dtr)
            self.assertTrue(binding.roles["source"].serial.rts)

            roles = {
                "receiver": _zephyr_binding("receiver", dtr="true"),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("dtr", str(ctx.exception))

            missing = _zephyr_binding("receiver")
            del missing["serial"]["rts"]
            _write_json(
                path,
                _binding_dict(
                    roles={"receiver": missing, "source": _zephyr_binding("source")}
                ),
            )
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("rts", str(ctx.exception))

    def test_fixture_id_and_role_set_mismatch(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")
            _write_json(path, _binding_dict(fixture_id="other-fixture"))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("does not match", str(ctx.exception))
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": _zephyr_binding("source"),
                "capture": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("role set mismatch", str(ctx.exception))

    def test_probe_and_serial_failures(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")

            # Static probe serial is not allowed.
            roles = {
                "receiver": {
                    "probe": {
                        "backend": "nrf-probes",
                        "family": "nrf54l",
                        "serial": "1234567890",
                    },
                    "serial": _zephyr_binding("receiver")["serial"],
                },
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("probe serial", str(ctx.exception))

            # Wrong probe backend: receiver must be nrf-probes, source must
            # be jlink (RH2 physical correction).
            roles = {
                "receiver": {
                    "probe": {"backend": "jlink", "family": "nrf54l"},
                    "serial": _zephyr_binding("receiver")["serial"],
                },
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("probe backend", str(ctx.exception))
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": {
                    "probe": {"backend": "nrf-probes", "family": "nrf53"},
                    "serial": _zephyr_binding("source")["serial"],
                },
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("probe backend", str(ctx.exception))

            # Wrong probe family.
            roles = {
                "receiver": {
                    "probe": {"backend": "nrf-probes", "family": "nrf53"},
                    "serial": _zephyr_binding("receiver")["serial"],
                },
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("family mismatch", str(ctx.exception))

            # A jlink source probe requires its udev map.
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": {
                    "probe": {"backend": "jlink", "family": "nrf53"},
                    "serial": _zephyr_binding("source")["serial"],
                },
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("probe udev is required", str(ctx.exception))

            # Empty or incomplete probe udev map fails closed.
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": _zephyr_binding("source", probe_udev={}),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("nonempty", str(ctx.exception))
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": _zephyr_binding(
                    "source", probe_udev={"ID_MODEL_ID": "2", "ID_SERIAL_SHORT": "S"}
                ),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("ID_VENDOR_ID", str(ctx.exception))

            # DEVNAME is forbidden in probe udev too.
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": _zephyr_binding(
                    "source",
                    probe_udev={
                        "ID_VENDOR_ID": "1",
                        "ID_MODEL_ID": "2",
                        "DEVNAME": "/dev/bus/usb/001/002",
                        "ID_SERIAL_SHORT": "S",
                    },
                ),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("DEVNAME", str(ctx.exception))

            # Volatile /dev/tty* values are rejected in serial udev.
            roles = {
                "receiver": _zephyr_binding(
                    "receiver",
                    udev={
                        "ID_VENDOR_ID": "1",
                        "ID_MODEL_ID": "2",
                        "ID_PATH": "/dev/ttyUSB0",
                    },
                ),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("tty path", str(ctx.exception))

            # DEVNAME key is forbidden in serial udev.
            roles = {
                "receiver": _zephyr_binding(
                    "receiver",
                    udev={
                        "ID_VENDOR_ID": "1",
                        "ID_MODEL_ID": "2",
                        "DEVNAME": "/dev/ttyACM0",
                        "ID_SERIAL_SHORT": "S",
                    },
                ),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("DEVNAME", str(ctx.exception))

            # Insufficient serial udev identity: no ID_SERIAL_SHORT/ID_PATH.
            roles = {
                "receiver": _zephyr_binding(
                    "receiver", udev={"ID_VENDOR_ID": "1", "ID_MODEL_ID": "2"}
                ),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("ID_SERIAL_SHORT or ID_PATH", str(ctx.exception))

            # Missing required serial udev keys.
            roles = {
                "receiver": _zephyr_binding(
                    "receiver", udev={"ID_MODEL_ID": "2", "ID_SERIAL_SHORT": "S"}
                ),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("ID_VENDOR_ID", str(ctx.exception))

            # Invalid baud.
            roles = {
                "receiver": _zephyr_binding("receiver", baud=0),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, fixture)
            self.assertIn("baud", str(ctx.exception))
            roles = {
                "receiver": _zephyr_binding("receiver", baud="115200"),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError):
                model.load_physical_binding(path, fixture)
            roles = {
                "receiver": _zephyr_binding("receiver", baud=True),
                "source": _zephyr_binding("source"),
            }
            _write_json(path, _binding_dict(roles=roles))
            with self.assertRaises(model.HilSchemaError):
                model.load_physical_binding(path, fixture)

    def test_empty_serial_udev_and_probe_tty_path_rules(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")
            # Empty serial udev is allowed for both roles (identity is then
            # correlated to the resolved probe at discovery time).
            doc = {
                "schema_version": 1,
                "fixture_id": FIXTURE_ID,
                "roles": {
                    "receiver": {
                        "probe": {"backend": "nrf-probes", "family": "nrf54l"},
                        "serial": {
                            "baud": 115200,
                            "dtr": False,
                            "rts": False,
                            "udev": {},
                        },
                    },
                    "source": {
                        "probe": {
                            "backend": "jlink",
                            "family": "nrf53",
                            "udev": {
                                "ID_VENDOR_ID": "1366",
                                "ID_MODEL_ID": "1015",
                                "ID_SERIAL_SHORT": "J-LINK-SERIAL",
                            },
                        },
                        "serial": {
                            "baud": 115200,
                            "dtr": False,
                            "rts": False,
                            "udev": {},
                        },
                    },
                },
            }
            _write_json(path, doc)
            binding = model.load_physical_binding(path, fixture)
            self.assertEqual(len(binding.roles["receiver"].serial.udev.values), 0)
            self.assertEqual(len(binding.roles["source"].serial.udev.values), 0)
            self.assertIsNone(binding.roles["receiver"].probe.udev)
            self.assertEqual(
                binding.roles["source"].probe.udev.get("ID_SERIAL_SHORT"),
                "J-LINK-SERIAL",
            )
            # The tty-path check is irrelevant for probe udev: a USB device
            # identity value is accepted even when it looks like a tty path.
            roles = {
                "receiver": _zephyr_binding("receiver"),
                "source": _zephyr_binding(
                    "source",
                    probe_udev={
                        "ID_VENDOR_ID": "1",
                        "ID_MODEL_ID": "2",
                        "ID_SERIAL_SHORT": "/dev/ttyACM0",
                    },
                ),
            }
            _write_json(path, _binding_dict(roles=roles))
            binding = model.load_physical_binding(path, fixture)
            self.assertEqual(
                binding.roles["source"].probe.udev.get("ID_SERIAL_SHORT"),
                "/dev/ttyACM0",
            )

    def test_capture_capability_requires_capture_binding(self):
        fixture = model.load_logical_fixture(FIXTURE_JSON)
        # A mono logical fixture is schema-valid with a capture role...
        mono = dict(fixture.roles)
        mono["capture"] = model.LogicalRole(
            kind="alsa_capture", board=None, images=(), channels=1
        )
        mono_fixture = model.LogicalFixture(
            schema_version=1,
            fixture_id=FIXTURE_ID,
            capture_capability=model.CaptureCapability.MONO,
            roles=model.MappingProxyType(mono),
        )
        with tempfile.TemporaryDirectory() as td:
            path = os.path.join(td, "b.json")
            _write_json(path, _binding_dict())
            # ...and a capture-capable fixture must carry capture binding data.
            with self.assertRaises(model.HilSchemaError) as ctx:
                model.load_physical_binding(path, mono_fixture)
            self.assertIn("role set mismatch", str(ctx.exception))


class TestProtocolParser(unittest.TestCase):
    def test_unprefixed_returns_none(self):
        self.assertIsNone(protocol.parse_hil1_line("some source log line"))
        self.assertIsNone(protocol.parse_hil1_line(b"binary log line"))
        self.assertIsNone(protocol.parse_hil1_line(""))
        self.assertIsNone(protocol.parse_hil1_line("HIL1X not a record"))

    def test_malformed_prefixed_line_fails(self):
        for bad in (
            "HIL1 not json",
            "HIL1 {}",
            "HIL1 []",
            "HIL1 ",
            b"HIL1 \xff\xfe",
            "HIL1 " + json.dumps({"kind": "ack", "kind": "state"}),
        ):
            with self.assertRaises(protocol.HilProtocolError):
                protocol.parse_hil1_line(bad)

    def test_field_validation(self):
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            _rec(kind="bogus")
        self.assertIn("kind", str(ctx.exception))
        with self.assertRaises(protocol.HilProtocolError):
            _rec(protocol_version=2)
        with self.assertRaises(protocol.HilProtocolError):
            _rec(protocol_version=True)
        with self.assertRaises(protocol.HilProtocolError):
            _rec(monotonic_ms=-1)
        with self.assertRaises(protocol.HilProtocolError):
            _rec(monotonic_ms=True)
        with self.assertRaises(protocol.HilProtocolError):
            _rec(segment=-1)
        with self.assertRaises(protocol.HilProtocolError):
            _rec(firmware_id="")
        with self.assertRaises(protocol.HilProtocolError):
            _rec(run_id="")
        with self.assertRaises(protocol.HilProtocolError):
            _rec(command_id="")
        with self.assertRaises(protocol.HilProtocolError):
            _rec(data=[])
        with self.assertRaises(protocol.HilProtocolError):
            _rec(kind="state", data={"state": "exploded"})
        with self.assertRaises(protocol.HilProtocolError):
            _rec(kind="state", data={})
        with self.assertRaises(protocol.HilProtocolError):
            _rec(kind="terminal", data={"verdict": "maybe"})
        with self.assertRaises(protocol.HilProtocolError):
            _rec(kind="terminal", data={})
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            _rec(extra_key=1)
        self.assertIn("unknown key", str(ctx.exception))
        missing_run = json.dumps({k: v for k, v in _hdr().items() if k != "run_id"})
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            protocol.parse_hil1_line("HIL1 " + missing_run)
        self.assertIn("missing key", str(ctx.exception))

    def test_valid_single_segment_and_terminal(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="ack", monotonic_ms=1))
        _accept_segment(tracker, 0, start_ms=10)
        tracker.accept(
            _rec(kind="terminal", monotonic_ms=100, data={"verdict": "pass"})
        )
        terminal = tracker.terminal
        self.assertIsNotNone(terminal)
        assert terminal is not None  # type guard for static analysis
        self.assertEqual(terminal.data["verdict"], "pass")

    def test_valid_reconnect_sequence(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        tracker.accept(_rec(kind="ack", monotonic_ms=60))
        _accept_segment(tracker, 1, start_ms=70)
        tracker.accept(
            _rec(kind="terminal", segment=1, monotonic_ms=200, data={"verdict": "fail"})
        )
        terminal = tracker.terminal
        self.assertIsNotNone(terminal)
        assert terminal is not None  # type guard for static analysis
        self.assertEqual(tracker.segment, 1)
        self.assertEqual(terminal.data["verdict"], "fail")

    def test_run_firmware_command_mismatch(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", run_id="other"))
        self.assertIn("run id mismatch", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="ack", firmware_id="fw1"))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", firmware_id="fw2"))
        self.assertIn("firmware id mismatch", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", command_id="other"))
        self.assertIn("command id mismatch", str(ctx.exception))

    def test_time_and_segment_regression(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="ack", monotonic_ms=50))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", monotonic_ms=49))
        self.assertIn("monotonic", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        tracker.accept(
            _rec(
                kind="state", segment=1, monotonic_ms=100, data={"state": "connecting"}
            )
        )
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", segment=0, monotonic_ms=101))
        self.assertIn("segment regression", str(ctx.exception))

    def test_state_repeat_regression_skip(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="state", data={"state": "idle"}))
        self.assertIn("state repeat", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        tracker.accept(_rec(kind="state", data={"state": "configured"}))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="state", data={"state": "idle"}))
        self.assertIn("state regression", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="state", data={"state": "streaming"}))
        self.assertIn("state skip", str(ctx.exception))
        # First state must be idle.
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="state", data={"state": "connecting"}))
        self.assertIn("first state must be idle", str(ctx.exception))

    def test_segment_skip_and_early_new_segment(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(
                _rec(
                    kind="state",
                    segment=2,
                    monotonic_ms=100,
                    data={"state": "connecting"},
                )
            )
        self.assertIn("segment skip", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        tracker.accept(_rec(kind="state", data={"state": "configured"}))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(
                _rec(
                    kind="state",
                    segment=1,
                    monotonic_ms=110,
                    data={"state": "connecting"},
                )
            )
        self.assertIn("teardown", str(ctx.exception))
        # Segment must start at 0.
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(
                _rec(kind="state", segment=1, monotonic_ms=10, data={"state": "idle"})
            )
        self.assertIn("segment must start at 0", str(ctx.exception))

    def test_duplicate_and_post_terminal_rejection(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        tracker.accept(
            _rec(kind="terminal", monotonic_ms=100, data={"verdict": "pass"})
        )
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(
                _rec(kind="terminal", monotonic_ms=101, data={"verdict": "pass"})
            )
        self.assertIn("duplicate terminal", str(ctx.exception))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", monotonic_ms=102))
        self.assertIn("post-terminal", str(ctx.exception))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="state", monotonic_ms=103, data={"state": "idle"}))
        self.assertIn("post-terminal", str(ctx.exception))

    def test_terminal_before_teardown(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="terminal", data={"verdict": "pass"}))
        self.assertIn("terminal before teardown", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(_rec(kind="terminal", data={"verdict": "pass"}))

    def test_status_does_not_advance_state(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        tracker.accept(_rec(kind="state", data={"state": "configured"}))
        # In-run status with a different command ID passes and changes nothing.
        tracker.accept(
            _rec(kind="status", command_id="s-query", monotonic_ms=105, data={"seq": 1})
        )
        # State still advances exactly one step after the status record.
        tracker.accept(
            _rec(kind="state", monotonic_ms=120, data={"state": "connecting"})
        )
        tracker.accept(_rec(kind="state", monotonic_ms=130, data={"state": "secured"}))
        tracker.accept(
            _rec(
                kind="status", command_id="s-query-2", monotonic_ms=135, data={"seq": 2}
            )
        )
        tracker.accept(
            _rec(kind="state", monotonic_ms=140, data={"state": "discovered"})
        )
        tracker.accept(_rec(kind="state", monotonic_ms=150, data={"state": "qos"}))
        tracker.accept(
            _rec(kind="state", monotonic_ms=160, data={"state": "streaming"})
        )
        tracker.accept(
            _rec(kind="state", monotonic_ms=170, data={"state": "scored_complete"})
        )
        tracker.accept(_rec(kind="state", monotonic_ms=180, data={"state": "teardown"}))
        # Post-terminal status with a new command ID still passes.
        tracker.accept(
            _rec(kind="terminal", monotonic_ms=200, data={"verdict": "pass"})
        )
        tracker.accept(_rec(kind="status", command_id="s-query-3", monotonic_ms=205))
        terminal = tracker.terminal
        self.assertIsNotNone(terminal)
        assert terminal is not None  # type guard for static analysis
        self.assertEqual(terminal.data["verdict"], "pass")

    def test_pre_state_nonzero_segment_rejected(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="ack", segment=1, monotonic_ms=10))
        self.assertIn("segment", str(ctx.exception))
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(
                _rec(kind="status", segment=2, command_id="s2", monotonic_ms=10)
            )
        self.assertIn("segment", str(ctx.exception))

    def test_future_segment_ack_status_terminal_rejected(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        for rec in (
            _rec(kind="ack", segment=1, monotonic_ms=100),
            _rec(kind="status", segment=1, command_id="s2", monotonic_ms=100),
            _rec(
                kind="terminal", segment=1, monotonic_ms=100, data={"verdict": "pass"}
            ),
        ):
            with self.assertRaises(protocol.HilProtocolError) as ctx:
                tracker.accept(rec)
            self.assertIn("segment skip", str(ctx.exception))

    def test_same_command_status_rejected(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        with self.assertRaises(protocol.HilProtocolError) as ctx:
            tracker.accept(_rec(kind="status", command_id="c1", monotonic_ms=150))
        self.assertIn("different command id", str(ctx.exception))

    def test_rejected_records_leave_tracker_unchanged(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        tracker.accept(
            _rec(kind="terminal", monotonic_ms=200, data={"verdict": "pass"})
        )
        snap = _snapshot_tracker(tracker)
        for bad in (
            _rec(kind="ack", run_id="other", monotonic_ms=300),
            _rec(kind="ack", firmware_id="fw2", monotonic_ms=300),
            _rec(kind="ack", command_id="other", monotonic_ms=300),
            _rec(kind="status", command_id="c1", monotonic_ms=300),
            _rec(kind="ack", monotonic_ms=150),  # time regression
            _rec(kind="ack", segment=1, monotonic_ms=300),
            _rec(kind="status", segment=1, command_id="s2", monotonic_ms=300),
            _rec(
                kind="terminal", segment=1, monotonic_ms=300, data={"verdict": "pass"}
            ),
            _rec(
                kind="terminal", segment=0, monotonic_ms=300, data={"verdict": "pass"}
            ),  # duplicate terminal
            _rec(
                kind="state", monotonic_ms=300, data={"state": "idle"}
            ),  # post-terminal
        ):
            with self.assertRaises(protocol.HilProtocolError):
                tracker.accept(bad)
            self.assertEqual(_snapshot_tracker(tracker), snap)

    def test_rejected_state_and_terminal_leave_tracker_unchanged(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, start_ms=10)
        snap = _snapshot_tracker(tracker)
        # New segment must start at connecting.
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(kind="state", segment=1, monotonic_ms=300, data={"state": "idle"})
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)
        # Mid-segment terminal is rejected without advancing state.
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        snap = _snapshot_tracker(tracker)
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(kind="terminal", monotonic_ms=300, data={"verdict": "pass"})
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)


class TestCleanupStack(unittest.TestCase):
    def test_lifo_order_and_idempotence(self):
        stack = lifecycle.CleanupStack()
        order = []
        stack.register("a", lambda: order.append("a"))
        stack.register("b", lambda: order.append("b"))
        stack.register("c", lambda: order.append("c"))
        stack.close()
        self.assertEqual(order, ["c", "b", "a"])
        stack.close()  # idempotent, no rerun
        self.assertEqual(order, ["c", "b", "a"])

    def test_every_callback_runs_despite_failure(self):
        stack = lifecycle.CleanupStack()
        order = []

        def boom():
            order.append("boom")
            raise ValueError("cb boom")

        # Older successful callback registered first; failing newer callback
        # registered second, so LIFO runs the failure first and must still
        # continue to the older callback afterward.
        stack.register("ok", lambda: order.append("ok"))
        stack.register("boom", boom)
        with self.assertRaises(lifecycle.CleanupFailure) as ctx:
            stack.close()
        self.assertEqual(order, ["boom", "ok"])
        self.assertEqual([name for name, _ in ctx.exception.failures], ["boom"])
        self.assertIsInstance(ctx.exception.failures[0][1], ValueError)

    def test_register_after_close_rejected(self):
        stack = lifecycle.CleanupStack()
        stack.close()
        with self.assertRaises(RuntimeError):
            stack.register("late", lambda: None)

    def test_body_failure_still_cleans_up(self):
        stack = lifecycle.CleanupStack()
        called = []
        stack.register("x", lambda: called.append(1))
        with self.assertRaises(ValueError):
            with stack:
                raise ValueError("body")
        self.assertEqual(called, [1])

    def test_setup_failure_still_cleans_up(self):
        stack = lifecycle.CleanupStack()
        called = []
        stack.register("setup", lambda: called.append(1))
        with self.assertRaises(RuntimeError):
            with stack:
                raise RuntimeError("setup")
        self.assertEqual(called, [1])

    def test_keyboard_interrupt_still_cleans_up(self):
        stack = lifecycle.CleanupStack()
        called = []
        stack.register("k", lambda: called.append(1))
        with self.assertRaises(KeyboardInterrupt):
            with stack:
                raise KeyboardInterrupt()
        self.assertEqual(called, [1])

    def test_cleanup_failure_does_not_hide_body_failure(self):
        stack = lifecycle.CleanupStack()
        stack.register("boom", lambda: (_ for _ in ()).throw(ValueError("cb boom")))
        with self.assertRaises(BaseExceptionGroup) as ctx:
            with stack:
                raise ValueError("body")
        kinds = {type(child).__name__ for child in ctx.exception.exceptions}
        self.assertEqual(kinds, {"ValueError", "CleanupFailure"})

    def test_cleanup_failure_alone_propagates(self):
        stack = lifecycle.CleanupStack()
        stack.register("boom", lambda: (_ for _ in ()).throw(ValueError("cb boom")))
        with self.assertRaises(lifecycle.CleanupFailure):
            with stack:
                pass

    def test_callback_keyboard_interrupt_does_not_skip_cleanup(self):
        stack = lifecycle.CleanupStack()
        order = []

        def ki():
            order.append("ki")
            raise KeyboardInterrupt()

        # Failing newer callback runs first under LIFO; the older successful
        # callback must still execute afterward.
        stack.register("ok", lambda: order.append("ok"))
        stack.register("ki", ki)
        with self.assertRaises(lifecycle.CleanupFailure) as ctx:
            stack.close()
        self.assertEqual(order, ["ki", "ok"])
        self.assertEqual([name for name, _ in ctx.exception.failures], ["ki"])
        self.assertIsInstance(ctx.exception.failures[0][1], KeyboardInterrupt)

    def test_body_keyboard_interrupt_and_cleanup_failure_expose_both(self):
        stack = lifecycle.CleanupStack()
        stack.register("boom", lambda: (_ for _ in ()).throw(ValueError("cb boom")))
        with self.assertRaises(BaseExceptionGroup) as ctx:
            with stack:
                raise KeyboardInterrupt()
        kinds = {type(child).__name__ for child in ctx.exception.exceptions}
        self.assertEqual(kinds, {"KeyboardInterrupt", "CleanupFailure"})


class TestFixtureLock(unittest.TestCase):
    def test_exclusion_and_normal_release(self):
        with tempfile.TemporaryDirectory() as td:
            stack = lifecycle.CleanupStack()
            lock = lifecycle.FixtureLock.acquire(td, "fixture-x", stack)
            self.assertTrue(os.path.isfile(lock.path))
            with self.assertRaises(lifecycle.FixtureBusy):
                lifecycle.FixtureLock.acquire(td, "fixture-x")
            stack.close()
            self.assertFalse(os.path.exists(lock.path))
            stack.close()  # repeated close idempotent
            # Lock is reusable after release.
            lifecycle.FixtureLock.acquire(td, "fixture-x").release()

    def test_lock_content(self):
        with tempfile.TemporaryDirectory() as td:
            lock = lifecycle.FixtureLock.acquire(td, "fixture-x")
            with open(lock.path, "r", encoding="utf-8") as fh:
                info = json.load(fh)
            self.assertEqual(info["fixture_id"], "fixture-x")
            self.assertEqual(info["token"], lock.token)
            self.assertEqual(info["pid"], os.getpid())
            lock.release()

    def test_foreign_token_not_removed(self):
        with tempfile.TemporaryDirectory() as td:
            stack = lifecycle.CleanupStack()
            lock = lifecycle.FixtureLock.acquire(td, "fixture-x", stack)
            with open(lock.path, "w", encoding="utf-8") as fh:
                fh.write(json.dumps({"token": "foreign-token"}))
            with self.assertRaises(lifecycle.LockReleaseError) as ctx:
                lock.release()
            self.assertIn("token changed", str(ctx.exception))
            self.assertTrue(os.path.isfile(lock.path))

    def test_malformed_lock_not_removed(self):
        with tempfile.TemporaryDirectory() as td:
            stack = lifecycle.CleanupStack()
            lock = lifecycle.FixtureLock.acquire(td, "fixture-x", stack)
            with open(lock.path, "w", encoding="utf-8") as fh:
                fh.write("not json")
            with self.assertRaises(lifecycle.LockReleaseError) as ctx:
                lock.release()
            self.assertIn("malformed", str(ctx.exception))
            self.assertTrue(os.path.isfile(lock.path))

    def test_setup_failure_releases_lock(self):
        with tempfile.TemporaryDirectory() as td:
            lock_path = None
            with self.assertRaises(ValueError):
                with lifecycle.CleanupStack() as stack:
                    lock = lifecycle.FixtureLock.acquire(td, "fixture-x", stack)
                    assert lock is not None
                    lock_path = lock.path
                    self.assertTrue(os.path.isfile(lock_path))
                    raise ValueError("setup failed")
            assert lock_path is not None
            self.assertFalse(os.path.exists(lock_path))
            # Reusable after the failed setup.
            lifecycle.FixtureLock.acquire(td, "fixture-x").release()

    def test_lock_dir_symlink_escape_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            outside = os.path.join(td, "outside")
            os.makedirs(outside)
            os.symlink(outside, os.path.join(td, ".locks"))
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                lifecycle.FixtureLock.acquire(td, "fixture-x")
            self.assertIn("symlink", str(ctx.exception))
            self.assertEqual(os.listdir(outside), [])

    def test_lock_dir_non_directory_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            with open(os.path.join(td, ".locks"), "w", encoding="utf-8") as fh:
                fh.write("not a directory")
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                lifecycle.FixtureLock.acquire(td, "fixture-x")
            self.assertIn("not a directory", str(ctx.exception))


class TestRunDirectorySafety(unittest.TestCase):
    def test_unsafe_output_roots(self):
        for bad in (
            "/",
            os.path.expanduser("~"),
            REPO_ROOT,
            os.path.join(REPO_ROOT, "docs"),
            os.path.dirname(REPO_ROOT),
            "relative/path",
        ):
            with self.assertRaises(lifecycle.HilLifecycleError):
                lifecycle.validate_output_root(bad)

    def test_missing_output_root(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                lifecycle.validate_output_root(os.path.join(td, "missing"))
            self.assertIn("does not exist", str(ctx.exception))

    def test_symlinked_output_root_into_repo(self):
        with tempfile.TemporaryDirectory() as td:
            link = os.path.join(td, "link")
            os.symlink(REPO_ROOT, link)
            with self.assertRaises(lifecycle.HilLifecycleError):
                lifecycle.validate_output_root(link)

    def test_run_id_pattern(self):
        for good in ("a", "A9", "run-1.2_3"):
            lifecycle.validate_run_id(good)
        for bad in ("", "-x", "_x", "a" * 65, "a b", "a/b", "a\nb"):
            with self.assertRaises(lifecycle.HilLifecycleError):
                lifecycle.validate_run_id(bad)

    def test_existing_run_dir_fails_without_deleting(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = os.path.join(td, "run1")
            os.makedirs(run_dir)
            with open(os.path.join(run_dir, "keep.txt"), "w") as fh:
                fh.write("keep")
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                lifecycle.create_run_dir(td, "run1")
            self.assertIn("already exists", str(ctx.exception))
            self.assertTrue(os.path.isfile(os.path.join(run_dir, "keep.txt")))

    def test_symlink_escape_fails_without_touching_target(self):
        with tempfile.TemporaryDirectory() as td:
            outside = os.path.join(td, "outside")
            os.makedirs(outside)
            with open(os.path.join(outside, "keep.txt"), "w") as fh:
                fh.write("keep")
            os.symlink(outside, os.path.join(td, "run2"))
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                lifecycle.create_run_dir(td, "run2")
            self.assertIn("symlink", str(ctx.exception))
            self.assertTrue(os.path.isfile(os.path.join(outside, "keep.txt")))


class TestEvidence(unittest.TestCase):
    def _prepare_run_dir(self, td, name="run1"):
        run_dir = os.path.join(td, name)
        os.makedirs(run_dir)
        with open(os.path.join(run_dir, "b.txt"), "w", encoding="utf-8") as fh:
            fh.write("bbb")
        with open(os.path.join(run_dir, "a.txt"), "w", encoding="utf-8") as fh:
            fh.write("aaa")
        return run_dir

    def test_hashes_and_manifest_deterministic_sorted(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            finalize_evidence(
                run_dir,
                fixture_id=FIXTURE_ID,
                run_id="r1",
                capture_capability="none",
                outcome="prepared",
            )
            with open(os.path.join(run_dir, "SHA256SUMS"), "rb") as fh:
                first = fh.read()
            with open(os.path.join(run_dir, "MANIFEST.md"), "rb") as fh:
                first_manifest = fh.read()
            finalize_evidence(
                run_dir,
                fixture_id=FIXTURE_ID,
                run_id="r1",
                capture_capability="none",
                outcome="prepared",
            )
            with open(os.path.join(run_dir, "SHA256SUMS"), "rb") as fh:
                second = fh.read()
            with open(os.path.join(run_dir, "MANIFEST.md"), "rb") as fh:
                second_manifest = fh.read()
            self.assertEqual(first, second)
            self.assertEqual(first_manifest, second_manifest)
            lines = first.decode("utf-8").strip().splitlines()
            self.assertEqual(
                [line.split("  ")[1] for line in lines], ["a.txt", "b.txt"]
            )
            # Correct SHA-256 for a.txt.
            expected = (
                "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0"
            )
            self.assertEqual(lines[0].split("  ")[0], expected)
            manifest = first_manifest.decode("utf-8")
            self.assertIn("fixture_id: %s" % FIXTURE_ID, manifest)
            self.assertIn("run_id: r1", manifest)
            self.assertIn("capture_capability: none", manifest)
            self.assertIn("outcome: prepared", manifest)
            # SHA256SUMS and MANIFEST.md are excluded from their own hashes.
            for name in ("SHA256SUMS", "MANIFEST.md"):
                for line in lines:
                    self.assertFalse(line.endswith("  " + name))

    def test_symlink_and_non_regular_evidence_fail(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            os.symlink("/etc/hostname", os.path.join(run_dir, "evil"))
            with self.assertRaises(EvidenceError) as ctx:
                finalize_evidence(
                    run_dir,
                    fixture_id=FIXTURE_ID,
                    run_id="r1",
                    capture_capability="none",
                    outcome="prepared",
                )
            self.assertIn("symlink", str(ctx.exception))
            self.assertFalse(os.path.exists(os.path.join(run_dir, "SHA256SUMS")))

        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            os.makedirs(os.path.join(run_dir, "subdir"))
            with self.assertRaises(EvidenceError) as ctx:
                finalize_evidence(
                    run_dir,
                    fixture_id=FIXTURE_ID,
                    run_id="r1",
                    capture_capability="none",
                    outcome="prepared",
                )
            self.assertIn("non-regular", str(ctx.exception))

    def test_failed_finalization_preserves_prior_files(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            prior_sums = os.path.join(run_dir, "SHA256SUMS")
            prior_manifest = os.path.join(run_dir, "MANIFEST.md")
            with open(prior_sums, "w", encoding="utf-8") as fh:
                fh.write("old-sums")
            with open(prior_manifest, "w", encoding="utf-8") as fh:
                fh.write("old-manifest")
            os.symlink("/etc/hostname", os.path.join(run_dir, "evil"))
            with self.assertRaises(EvidenceError):
                finalize_evidence(
                    run_dir,
                    fixture_id=FIXTURE_ID,
                    run_id="r1",
                    capture_capability="none",
                    outcome="prepared",
                )
            with open(prior_sums, "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "old-sums")
            with open(prior_manifest, "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "old-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")

    def _write_prior_metadata(self, run_dir):
        with open(os.path.join(run_dir, "SHA256SUMS"), "w", encoding="utf-8") as fh:
            fh.write("prior-sums")
        with open(os.path.join(run_dir, "MANIFEST.md"), "w", encoding="utf-8") as fh:
            fh.write("prior-manifest")

    def _assert_no_temp_files(
        self, run_dir, expected=("a.txt", "b.txt", "SHA256SUMS", "MANIFEST.md")
    ):
        # Exact retained-name comparison: staged names look like
        # .SHA256SUMS.tmpXXXX, so any extra entry (hidden staging/backup
        # file) fails this assertion.
        self.assertEqual(sorted(os.listdir(run_dir)), sorted(expected))

    def test_staging_failure_preserves_prior_metadata(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            real_stage = hil.evidence._stage_metadata

            def fail_second_stage(path, payload):
                # First staging call (SHA256SUMS) executes for real so its
                # staged temporary exists; the second call fails, proving
                # the first staged file is removed again.
                if os.path.basename(path) == "SHA256SUMS":
                    return real_stage(path, payload)
                raise OSError("injected second staging failure")

            with mock.patch(
                "hil.evidence._stage_metadata", side_effect=fail_second_stage
            ):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_first_replacement_failure_preserves_prior_metadata(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            with mock.patch(
                "hil.evidence._commit_metadata",
                side_effect=OSError("injected first replacement failure"),
            ):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_second_replacement_failure_rolls_back_both(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            real_commit = hil.evidence._commit_metadata

            def fail_second(tmp, path):
                if path.endswith("MANIFEST.md"):
                    raise OSError("injected second replacement failure")
                return real_commit(tmp, path)

            with mock.patch("hil.evidence._commit_metadata", side_effect=fail_second):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            # Both metadata files restored to their exact prior bytes.
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_second_replacement_failure_without_prior_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            with open(os.path.join(run_dir, "SHA256SUMS"), "w", encoding="utf-8") as fh:
                fh.write("prior-sums")
            # No prior MANIFEST.md exists.
            real_commit = hil.evidence._commit_metadata

            def fail_second(tmp, path):
                if path.endswith("MANIFEST.md"):
                    raise OSError("injected second replacement failure")
                return real_commit(tmp, path)

            with mock.patch("hil.evidence._commit_metadata", side_effect=fail_second):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                manifest = fh.read()
            # Best-effort failed manifest is allowed when no prior manifest
            # existed; it labels the failed finalization.
            self.assertIn("outcome: failed", manifest)
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_second_stage_keyboard_interrupt(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            real_stage = hil.evidence._stage_metadata

            def ki_on_second_stage(path, payload):
                if os.path.basename(path) == "SHA256SUMS":
                    return real_stage(path, payload)
                raise KeyboardInterrupt()

            with mock.patch(
                "hil.evidence._stage_metadata", side_effect=ki_on_second_stage
            ):
                with self.assertRaises(KeyboardInterrupt):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            # Nothing was committed: prior metadata and payload remain exact
            # and the first staged temporary was removed.
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_second_replacement_keyboard_interrupt(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            real_commit = hil.evidence._commit_metadata

            def ki_on_second_commit(tmp, path):
                if os.path.basename(path) == "SHA256SUMS":
                    return real_commit(tmp, path)
                raise KeyboardInterrupt()

            with mock.patch(
                "hil.evidence._commit_metadata", side_effect=ki_on_second_commit
            ):
                with self.assertRaises(KeyboardInterrupt):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            # SHA256SUMS was replaced first, then interrupted: both metadata
            # files are rolled back to exact prior bytes, payload retained,
            # no temporary remains, and KeyboardInterrupt propagated.
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_hashing_io_failure_raises_evidence_error(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            with mock.patch(
                "hil.evidence._sha256_file",
                side_effect=OSError("injected hash failure"),
            ):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            # Hash stage precedes any metadata write: payload retained,
            # no metadata or staging files created.
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir, ("a.txt", "b.txt"))

    def test_snapshot_io_failure_raises_evidence_error(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            self._write_prior_metadata(run_dir)
            # Payload enumeration succeeds first; the snapshot seam then
            # raises an ordinary OSError, proving the snapshot-boundary
            # conversion to EvidenceError.
            with mock.patch(
                "hil.evidence._snapshot_bytes",
                side_effect=OSError("injected snapshot failure"),
            ):
                with self.assertRaises(EvidenceError):
                    finalize_evidence(
                        run_dir,
                        fixture_id=FIXTURE_ID,
                        run_id="r1",
                        capture_capability="none",
                        outcome="prepared",
                    )
            # Snapshot precedes staging/commit: prior metadata and payload
            # remain exact and no staging files were created.
            with open(os.path.join(run_dir, "SHA256SUMS"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "prior-sums")
            with open(
                os.path.join(run_dir, "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                self.assertEqual(fh.read(), "prior-manifest")
            with open(os.path.join(run_dir, "a.txt"), "r", encoding="utf-8") as fh:
                self.assertEqual(fh.read(), "aaa")
            self._assert_no_temp_files(run_dir)

    def test_unknown_outcome_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            run_dir = self._prepare_run_dir(td)
            with self.assertRaises(EvidenceError):
                finalize_evidence(
                    run_dir,
                    fixture_id=FIXTURE_ID,
                    run_id="r1",
                    capture_capability="none",
                    outcome="accepted",
                )


class TestCli(unittest.TestCase):
    def test_validate_success_shape(self):
        rc, out, err = _run_cli(
            ["validate", "--fixture", FIXTURE_JSON, "--binding", BINDING_EXAMPLE]
        )
        self.assertEqual(rc, 0)
        self.assertEqual(err, "")
        self.assertEqual(
            json.loads(out),
            {"fixture_id": FIXTURE_ID, "capture_capability": "none"},
        )
        self.assertEqual(out.rstrip("\n").count("\n"), 0)

    def test_prepare_success_shape(self):
        with tempfile.TemporaryDirectory() as td:
            rc, out, err = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    FIXTURE_JSON,
                    "--binding",
                    BINDING_EXAMPLE,
                    "--output-root",
                    td,
                    "--run-id",
                    "rh0-0001",
                ]
            )
            self.assertEqual(rc, 0)
            self.assertEqual(err, "")
            result = json.loads(out)
            self.assertEqual(result["outcome"], "prepared")
            self.assertEqual(result["run_directory"], os.path.join(td, "rh0-0001"))
            for name in (
                "fixture.json",
                "binding.json",
                "prepared.json",
                "SHA256SUMS",
                "MANIFEST.md",
            ):
                self.assertTrue(
                    os.path.isfile(os.path.join(td, "rh0-0001", name)), name
                )
            # Exact logical/binding bytes are copied.
            with open(FIXTURE_JSON, "rb") as fh:
                fixture_bytes = fh.read()
            with open(os.path.join(td, "rh0-0001", "fixture.json"), "rb") as fh:
                self.assertEqual(fh.read(), fixture_bytes)
            with open(
                os.path.join(td, "rh0-0001", "prepared.json"), "r", encoding="utf-8"
            ) as fh:
                prepared = json.load(fh)
            self.assertEqual(prepared["run_id"], "rh0-0001")
            self.assertEqual(prepared["outcome"], "prepared")
            self.assertEqual(prepared["capture_capability"], "none")
            # Lock released after successful prepare.
            self.assertEqual(os.listdir(os.path.join(td, ".locks")), [])
            # Evidence finalized with the expected file set.
            with open(
                os.path.join(td, "rh0-0001", "MANIFEST.md"), "r", encoding="utf-8"
            ) as fh:
                manifest = fh.read()
            self.assertIn("outcome: prepared", manifest)
            self.assertIn("  fixture.json", manifest)
            self.assertIn("  binding.json", manifest)
            self.assertIn("  prepared.json", manifest)

    def test_cli_failures_exit_2(self):
        with tempfile.TemporaryDirectory() as td:
            bad_fixture = os.path.join(td, "bad.json")
            with open(bad_fixture, "w", encoding="utf-8") as fh:
                fh.write("not json")
            rc, out, err = _run_cli(
                ["validate", "--fixture", bad_fixture, "--binding", BINDING_EXAMPLE]
            )
            self.assertEqual(rc, 2)
            self.assertEqual(out, "")
            self.assertTrue(err.startswith("hil-runner: error: "), err)
            self.assertEqual(err.count("\n"), 1)

            # Unsafe output root.
            rc, out, err = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    FIXTURE_JSON,
                    "--binding",
                    BINDING_EXAMPLE,
                    "--output-root",
                    REPO_ROOT,
                    "--run-id",
                    "rh0-0001",
                ]
            )
            self.assertEqual(rc, 2)
            self.assertEqual(out, "")
            self.assertTrue(err.startswith("hil-runner: error: "), err)

            # Duplicate run directory.
            ok = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    FIXTURE_JSON,
                    "--binding",
                    BINDING_EXAMPLE,
                    "--output-root",
                    td,
                    "--run-id",
                    "dup-run",
                ]
            )
            self.assertEqual(ok[0], 0)
            rc, out, err = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    FIXTURE_JSON,
                    "--binding",
                    BINDING_EXAMPLE,
                    "--output-root",
                    td,
                    "--run-id",
                    "dup-run",
                ]
            )
            self.assertEqual(rc, 2)
            self.assertIn("already exists", err)

            # Lock conflict: another holder owns the fixture.
            held = lifecycle.FixtureLock.acquire(td, FIXTURE_ID)
            rc, out, err = _run_cli(
                [
                    "prepare",
                    "--fixture",
                    FIXTURE_JSON,
                    "--binding",
                    BINDING_EXAMPLE,
                    "--output-root",
                    td,
                    "--run-id",
                    "locked-run",
                ]
            )
            self.assertEqual(rc, 2)
            self.assertIn("busy", err)
            held.release()

            # Missing required CLI arguments.
            rc, out, err = _run_cli(["validate", "--fixture", FIXTURE_JSON])
            self.assertEqual(rc, 2)
            self.assertTrue(err.startswith("hil-runner: error: "), err)

    def test_wrapper_executable_validate(self):
        proc = subprocess.run(
            [
                sys.executable,
                os.path.join(SCRIPT_DIR, "hil-runner.py"),
                "validate",
                "--fixture",
                FIXTURE_JSON,
                "--binding",
                BINDING_EXAMPLE,
            ],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(
            json.loads(proc.stdout),
            {"fixture_id": FIXTURE_ID, "capture_capability": "none"},
        )
        self.assertEqual(proc.stderr, "")


class TestHil1RecordCrossContract(unittest.TestCase):
    """RH1A cross-contract check: the exact HIL1 envelope bytes emitted by
    the source-core record formatter (hil_source_record.c) must parse
    through the RH0 wire parser and tracker."""

    FIRMWARE = "le-audio-hil-source-rh1"

    def _line(
        self,
        kind,
        data,
        monotonic_ms=10,
        command_id="cmd-0001",
        run_id="run-0001",
        segment=0,
    ):
        return (
            'HIL1 {"protocol_version":1,"kind":"%s","firmware_id":"%s",'
            '"monotonic_ms":%d,"command_id":"%s","run_id":"%s","segment":%d,'
            '"data":%s}'
            % (kind, self.FIRMWARE, monotonic_ms, command_id, run_id, segment, data)
        )

    def test_exact_envelope_records_parse_and_track(self):
        tracker = protocol.HilRunTracker("run-0001", "cmd-0001")
        records = [
            self._line("ack", '{"command":"start","accepted":true}'),
            self._line("state", '{"state":"idle"}'),
            self._line("state", '{"state":"configured"}'),
            self._line("state", '{"state":"connecting"}'),
            self._line("state", '{"state":"secured"}'),
            self._line("state", '{"state":"discovered"}'),
            self._line("state", '{"state":"qos"}'),
            self._line("state", '{"state":"streaming"}'),
            self._line("state", '{"state":"scored_complete"}'),
            self._line("state", '{"state":"teardown"}'),
            self._line("terminal", '{"verdict":"pass"}'),
        ]
        for raw in records:
            record = protocol.parse_hil1_line(raw)
            self.assertIsNotNone(record)
            self.assertEqual(record.firmware_id, self.FIRMWARE)
            tracker.accept(record)
        self.assertIsNotNone(tracker.terminal)
        self.assertEqual(tracker.terminal.data["verdict"], "pass")

    def test_status_response_shape_does_not_advance_state(self):
        tracker = protocol.HilRunTracker("run-0001", "cmd-0001")
        tracker.accept(
            protocol.parse_hil1_line(self._line("state", '{"state":"idle"}'))
        )
        before = (tracker.segment, tracker.state_index)
        status = protocol.parse_hil1_line(
            self._line(
                "status",
                '{"command":"status","ok":true,"error":"ok"}',
                command_id="cmd-0009",
                monotonic_ms=12,
            )
        )
        tracker.accept(status)
        self.assertEqual((tracker.segment, tracker.state_index), before)
        self.assertEqual(status.data["command"], "status")
        self.assertEqual(status.data["ok"], True)
        self.assertEqual(status.data["error"], "ok")


class TestHilSourceSineLutGenerator(unittest.TestCase):
    """RH1A generator contract: --write and --check are mutually
    exclusive (usage error, no file change), and bare/--check succeed."""

    GENERATOR = os.path.join(SCRIPT_DIR, "generate_hil_source_sine_lut.py")
    LUT_INC = os.path.join(
        REPO_ROOT, "hil", "source", "core", "hil_source_sine_lut.inc"
    )

    def test_write_check_mutually_exclusive(self):
        with open(self.LUT_INC, "r", encoding="utf-8") as fh:
            before = fh.read()
        proc = subprocess.run(
            [sys.executable, self.GENERATOR, "--write", "--check"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 2)
        self.assertIn("not allowed with", proc.stderr)
        with open(self.LUT_INC, "r", encoding="utf-8") as fh:
            after = fh.read()
        self.assertEqual(after, before, "file must not change on usage error")

    def test_bare_and_check_success(self):
        for argv in ([], ["--check"]):
            proc = subprocess.run(
                [sys.executable, self.GENERATOR] + argv,
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, "rc for %r" % (argv,))
            self.assertEqual(proc.stderr, "", "stderr for %r" % (argv,))


class TestHilAbortTransition(unittest.TestCase):
    """RH0 bounded abort-to-teardown: legal only from an accepted
    pre-teardown state of the current segment, terminal for the whole
    run, and fail-closed on every rejected shape."""

    def test_abort_from_each_pre_teardown_state_then_fail(self):
        for idx in range(len(protocol.STATES) - 1):
            tracker = protocol.HilRunTracker("r1", "c1")
            tracker.accept(_rec(kind="state", data={"state": "idle"}))
            for st in protocol.STATES[1 : idx + 1]:
                tracker.accept(_rec(kind="state", data={"state": st}))
            tracker.accept(
                _rec(
                    kind="state",
                    monotonic_ms=200,
                    data={"state": "teardown", "cause": "stop"},
                )
            )
            self.assertTrue(tracker.aborted, "aborted from %s" % protocol.STATES[idx])
            self.assertEqual(tracker.abort_cause, "stop")
            self.assertEqual(tracker.state_index, len(protocol.STATES) - 1)
            tracker.accept(
                _rec(kind="terminal", monotonic_ms=201, data={"verdict": "fail"})
            )
            self.assertIsNotNone(tracker.terminal)

    def test_abort_in_reconnect_segment(self):
        tracker = protocol.HilRunTracker("r1", "c1")
        _accept_segment(tracker, 0, 100)
        tracker.accept(
            _rec(
                kind="state", segment=1, monotonic_ms=200, data={"state": "connecting"}
            )
        )
        tracker.accept(
            _rec(
                kind="state",
                segment=1,
                monotonic_ms=201,
                data={"state": "teardown", "cause": "error"},
            )
        )
        self.assertTrue(tracker.aborted)
        self.assertEqual(tracker.abort_cause, "error")
        tracker.accept(
            _rec(kind="terminal", segment=1, monotonic_ms=202, data={"verdict": "fail"})
        )
        self.assertIsNotNone(tracker.terminal)

    def test_abort_parse_shape_rejections(self):
        for data in [
            {"state": "streaming", "cause": "stop"},  # wrong target state
            {"state": "teardown", "cause": None},  # missing cause
            {"state": "teardown", "cause": "bogus"},  # unknown cause
            {"state": "teardown", "cause": "stop", "x": 1},  # extra key
            {"state": "idle", "x": 1},  # ordinary extra key
        ]:
            with self.assertRaises(protocol.HilProtocolError):
                _rec(kind="state", data=data)

    def test_abort_tracker_rejections_leave_snapshot(self):
        # First-record abort.
        tracker = protocol.HilRunTracker("r1", "c1")
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(kind="state", data={"state": "teardown", "cause": "stop"})
            )

        # Repeat abort.
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        tracker.accept(
            _rec(
                kind="state",
                monotonic_ms=200,
                data={"state": "teardown", "cause": "stop"},
            )
        )
        snap = _snapshot_tracker(tracker)
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(
                    kind="state",
                    monotonic_ms=201,
                    data={"state": "teardown", "cause": "timeout"},
                )
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)

        # Pass terminal after abort is rejected; fail is accepted once.
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(kind="terminal", monotonic_ms=202, data={"verdict": "pass"})
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)
        tracker.accept(
            _rec(kind="terminal", monotonic_ms=203, data={"verdict": "fail"})
        )
        self.assertIsNotNone(tracker.terminal)

        # Reconnect after abort.
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        tracker.accept(
            _rec(
                kind="state",
                monotonic_ms=200,
                data={"state": "teardown", "cause": "stop"},
            )
        )
        snap = _snapshot_tracker(tracker)
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(
                    kind="state",
                    segment=1,
                    monotonic_ms=201,
                    data={"state": "connecting"},
                )
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)

        # Wrong command id on the abort record.
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        snap = _snapshot_tracker(tracker)
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(
                    kind="state",
                    monotonic_ms=200,
                    command_id="other",
                    data={"state": "teardown", "cause": "stop"},
                )
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)

        # Wrong segment on the abort record (skip).
        tracker = protocol.HilRunTracker("r1", "c1")
        tracker.accept(_rec(kind="state", data={"state": "idle"}))
        snap = _snapshot_tracker(tracker)
        with self.assertRaises(protocol.HilProtocolError):
            tracker.accept(
                _rec(
                    kind="state",
                    segment=1,
                    monotonic_ms=200,
                    data={"state": "teardown", "cause": "stop"},
                )
            )
        self.assertEqual(_snapshot_tracker(tracker), snap)

    def test_generated_rh1a_abort_records_parse_and_track(self):
        firmware = "le-audio-hil-source-rh1"
        for cause in ("stop", "timeout", "error"):
            tracker = protocol.HilRunTracker("run-0001", "cmd-0001")
            tracker.accept(
                protocol.parse_hil1_line(
                    'HIL1 {"protocol_version":1,"kind":"state","firmware_id":"%s",'
                    '"monotonic_ms":10,"command_id":"cmd-0001","run_id":"run-0001",'
                    '"segment":0,"data":{"state":"idle"}}' % firmware
                )
            )
            record = protocol.parse_hil1_line(
                'HIL1 {"protocol_version":1,"kind":"state","firmware_id":"%s",'
                '"monotonic_ms":20,"command_id":"cmd-0001","run_id":"run-0001",'
                '"segment":0,"data":{"state":"teardown","cause":"%s"}}'
                % (firmware, cause)
            )
            tracker.accept(record)
            self.assertTrue(tracker.aborted)
            self.assertEqual(tracker.abort_cause, cause)
            tracker.accept(
                protocol.parse_hil1_line(
                    'HIL1 {"protocol_version":1,"kind":"terminal","firmware_id":"%s",'
                    '"monotonic_ms":30,"command_id":"cmd-0001","run_id":"run-0001",'
                    '"segment":0,"data":{"verdict":"fail"}}' % firmware
                )
            )
            self.assertIsNotNone(tracker.terminal)


if __name__ == "__main__":
    unittest.main()
