"""Synthetic encoded-report parser fixtures, not real guest/VM acceptance."""

import copy
import hashlib
import json
import select
import struct
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_host_results import (
    complete_marker_lines,
    decode_marker,
    validate_guest,
    validate_public,
    PUBLIC_CASES,
    validate_capture,
    validate_hold_ready,
    final_result,
    LC3_CONFIG,
    SINK_UUID,
    SOURCE_UUID,
)

STIMULUS = (ROOT / "tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3").read_bytes()
SOURCE = "/org/bluez/hci0/dev_00_AA_01_01_00_01/fd1"
SINK = "/org/bluez/hci1/dev_00_AA_01_00_00_00/fd0"
ADDRESSES = ["00:AA:01:00:00:00", "00:AA:01:01:00:01"]
RUN_ID = "0123456789abcdef0123456789abcdef"
BOOT_ID = "123e4567-e89b-12d3-a456-426614174000"
PEER = SOURCE.rsplit("/", 1)[0]
RECIPROCAL = SINK.rsplit("/", 1)[0]
ADAPTERS = ["/org/bluez/hci0", "/org/bluez/hci1"]


def authored_device(address, adapter, connected=False, bonded=True):
    return {
        "Address": address,
        "Adapter": adapter,
        "Paired": bonded,
        "Bonded": bonded,
        "Connected": connected,
        "ServicesResolved": connected,
    }


def synthetic_public(state):
    source_time = 1000000000
    reciprocal_time = source_time + 200
    start_ns = source_time + 400
    idle_states = {
        PEER: authored_device(ADDRESSES[1], ADAPTERS[0]),
        RECIPROCAL: authored_device(ADDRESSES[0], ADAPTERS[1]),
    }
    events = [
        {
            "adapters": ADAPTERS.copy(),
            "addresses": ADDRESSES.copy(),
        },
        {"initial_devices": copy.deepcopy(idle_states) if state == "retained" else {}},
        {
            "endpoint": "/pb053/source",
            "adapter": ADAPTERS[0],
            "uuid": SOURCE_UUID,
            "supported_uuids": [SOURCE_UUID],
        },
        {
            "endpoint": "/pb053/sink",
            "adapter": ADAPTERS[1],
            "uuid": SINK_UUID,
            "supported_uuids": [SINK_UUID],
        },
        {
            "discovered": PEER,
            "address": ADDRESSES[1],
            "adapter": ADAPTERS[0],
            "discovering_before_stop": True,
            "discovering_after_stop": False,
        },
    ]
    events.extend(
        [
            {
                "callback": "Endpoint.SetConfiguration",
                "path": "/pb053/source",
                "transport": SOURCE,
                "properties": "callback",
            },
            {
                "callback": "Endpoint.SetConfiguration",
                "path": "/pb053/sink",
                "transport": SINK,
                "properties": "callback",
            },
            {
                "paired": PEER,
                "reciprocal": RECIPROCAL,
                "local": authored_device(ADDRESSES[1], ADAPTERS[0], connected=True),
                "remote": authored_device(ADDRESSES[0], ADAPTERS[1], connected=True),
                "operation": "Pair" if state == "fresh" else "Connect",
            },
            {
                "transport": SOURCE,
                "endpoint": "/pb053/source",
                "callback_properties": "callback",
                "transport_properties": {
                    "Codec": 6,
                    "Configuration": LC3_CONFIG,
                    "Device": PEER,
                    "UUID": SOURCE_UUID,
                    "State": "idle",
                },
            },
            {
                "transport": SINK,
                "endpoint": "/pb053/sink",
                "callback_properties": "callback",
                "transport_properties": {
                    "Codec": 6,
                    "Configuration": LC3_CONFIG,
                    "Device": RECIPROCAL,
                    "UUID": SINK_UUID,
                    "State": "idle",
                },
            },
            {"acquired": SOURCE, "read_mtu": 0, "write_mtu": 120},
            {"acquired": SINK, "read_mtu": 120, "write_mtu": 0},
        ]
    )
    for index in range(16):
        frame = STIMULUS[index * 120 : (index + 1) * 120]
        events.append(
            {
                "frame": index,
                "sent": 120,
                "received_len": 120,
                "sha256": hashlib.sha256(frame).hexdigest(),
                "received_hex": frame.hex(),
                "flags": 0,
            }
        )
    events.extend(
        [
            {"released": SOURCE, "role": "source", "ok": True},
            {"closed": SOURCE, "ok": True},
            {
                "revoked": SINK,
                "state": "idle",
                "eof": True,
                "pollhup": True,
                "poll_flags": select.POLLIN | select.POLLHUP,
            },
            {"closed": SINK, "ok": True},
            {"inactive": SOURCE, "ok": True},
            {"inactive": SINK, "ok": True},
            {"disconnect": PEER, "reply": "None", "time_ns": source_time},
            {
                "disconnect_states": copy.deepcopy(idle_states),
                "time_ns": source_time + 100,
            },
            {"disconnect": RECIPROCAL, "reply": "None", "time_ns": reciprocal_time},
            {
                "disconnect_states": copy.deepcopy(idle_states),
                "time_ns": reciprocal_time + 100,
            },
            {
                "disconnect_states": copy.deepcopy(idle_states),
                "time_ns": start_ns + 100,
            },
            {
                "disconnect_states": copy.deepcopy(idle_states),
                "time_ns": start_ns + 500000100,
            },
            {
                "disconnect_observation": {
                    "peers": [PEER, RECIPROCAL],
                    "start_ns": start_ns,
                    "end_ns": start_ns + 500000100,
                    "samples": 2,
                    "all_disconnected": True,
                }
            },
            {"cleanup": "peer disconnect", "ok": True},
        ]
    )
    return {
        "schema_version": 2,
        "state": state,
        "ok": True,
        "cases": {name: True for name in PUBLIC_CASES},
        "events": events,
        "cleanup_errors": [],
        "error": None,
    }


def synthetic_guest():
    """Authored parser fixture; never claim it came from guest execution."""
    stages = [
        {"stage": "kernel", "version": "7.1.5"},
        {"stage": "modules"},
        {"stage": "af_alg_ecb_aes", "operation": "success"},
        {"stage": "af_alg_cmac_aes", "operation": "success"},
        {"stage": "started", "process": "dbus", "pid": 11, "argv": ["dbus"]},
        {"stage": "bus_readiness", "reply": "method return: names"},
        {
            "stage": "started",
            "process": "monitor",
            "pid": 12,
            "argv": ["python", "-u", "/opt/pb053/monitor.py"],
        },
        {"stage": "started", "process": "emulator", "pid": 13, "argv": ["btvirt"]},
    ]
    pids = (14, 15, 16)
    for number, (name, state, pid) in enumerate(
        zip(("fresh1", "retained", "fresh2"), ("fresh", "retained", "fresh"), pids)
    ):
        if number:
            for index in (0, 1):
                for operation in ("power", "info"):
                    common = {"phase": name, "index": index, "operation": operation}
                    stages.append(
                        {
                            "stage": "controller_command_start",
                            **common,
                            "wall": 1.0,
                            "monotonic": 1.0,
                            "argv": ["btmgmt"],
                        }
                    )
                    stages.append(
                        {
                            "stage": "controller_command_reply",
                            **common,
                            "wall": 2.0,
                            "monotonic": 2.0,
                            "reply": "powered",
                        }
                    )
            stages.append(
                {
                    "stage": "controller_ready",
                    "phase": name,
                    "controllers": [
                        {"index": i, "address": addr, "powered": True}
                        for i, addr in enumerate(ADDRESSES)
                    ],
                    "indices": [0, 1],
                    "addresses": ADDRESSES.copy(),
                    "powered": True,
                    "wall": 3.0,
                    "monotonic": 3.0,
                }
            )
        stages.extend(
            [
                {
                    "stage": "started",
                    "process": "bluez-" + name,
                    "pid": pid,
                    "argv": ["bluetoothd"],
                },
                {
                    "stage": "readiness",
                    "controllers": 2,
                    "reply": "method return: two adapters",
                },
                {"stage": "bluetooth_iso", "operation": "success"},
                {
                    "stage": "public_" + name,
                    "operation": "success",
                    "child_exit_code": 0,
                    "result": synthetic_public(state),
                },
            ]
        )
        if number < 2:
            stages.append(
                {
                    "stage": "daemon_stopped_" + name,
                    "pid": pid,
                    "returncode": 0,
                    "name_has_owner": False,
                }
            )
            stages.append(
                {"stage": "state_preserved", "hashes": {"bond": "a" * 64}}
                if number == 0
                else {
                    "stage": "state_reset",
                    "hashes": {"bond": "b" * 64},
                    "original_hashes": {"bond": "a" * 64},
                    "backup": "/var/lib/pb053-retained-bluetooth",
                }
            )
    for label, pid in (
        ("bluez-fresh1", 14),
        ("bluez-retained", 15),
        ("bluez-fresh2", 16),
        ("emulator", 13),
        ("monitor", 12),
        ("dbus", 11),
    ):
        stages.append(
            {"stage": "stopped", "process": label, "pid": pid, "returncode": 0}
        )
        if label == "monitor":
            stages.append(
                {
                    "stage": "traffic_capture",
                    "packets": 102,
                    "bytes": 612,
                    "sha256": authored_capture_sha(),
                    "reported_drops": None,
                    "opcode_counts": {
                        "2": 1,
                        "3": 1,
                        "16": 1,
                        "17": 1,
                        "18": 49,
                        "19": 49,
                    },
                }
            )
    return {
        "schema_version": 1,
        "ok": True,
        "kernel": "7.1.5",
        "controllers": 2,
        "stages": stages,
        "error": None,
        "run_id": RUN_ID,
        "boot_id": BOOT_ID,
        "scenario": "normal",
    }


def authored_capture_sha():
    """Component parser fixture only; no claimed observed traffic or payload golden."""
    sha = hashlib.sha256()
    for opcode in (2, 3, 16, 17) + (18,) * 49 + (19,) * 49:
        raw = struct.pack("<HHH", opcode, 0, 0)
        sha.update(struct.pack("<I", len(raw)) + raw)
    return sha.hexdigest()


def authored_capture():
    rows = ['PB053_MONITOR_READY {"schema_version":1}']
    for number, opcode in enumerate((2, 3, 16, 17) + (18,) * 49 + (19,) * 49):
        rows.append(
            json.dumps(
                {
                    "packet": number,
                    "wall_ns": number + 1,
                    "monotonic_ns": number + 1,
                    "kernel_timestamp_ns": None,
                    "opcode": opcode,
                    "index": 0,
                    "raw_hex": struct.pack("<HHH", opcode, 0, 0).hex(),
                    "flags": 0,
                    "reported_drops": None,
                }
            )
        )
    rows.append(
        "PB053_MONITOR_RESULT "
        + json.dumps(
            {
                "schema_version": 1,
                "ok": True,
                "packets": 102,
                "bytes": 612,
                "sha256": authored_capture_sha(),
                "error": None,
                "stop_signal": 15,
                "reported_drops": None,
            }
        )
    )
    return "\n".join(rows) + "\n"


class EncodedReportParserFixtures(unittest.TestCase):
    def test_complete_marker_requires_lf_at_every_split(self):
        prefix = b"PB053_HOLD_READY "
        row = prefix + b'{"run_id":"0123456789abcdef0123456789abcdef"}'
        stream = b"boot\n" + row + b"\n"
        for length in range(len(stream)):
            with self.subTest(length=length):
                self.assertEqual(complete_marker_lines(stream[:length], prefix), [])
        self.assertEqual(complete_marker_lines(stream, prefix), [row])
        self.assertEqual(complete_marker_lines(row, prefix), [])
        self.assertEqual(complete_marker_lines(row + b"\r\n", prefix), [row])
        self.assertEqual(
            complete_marker_lines(stream + row + b"\n", prefix), [row, row]
        )

    def test_complete_marker_rejects_bad_inputs_and_oversize(self):
        prefix = b"PB053_HOLD_READY "
        malformed = prefix + b'{"run_id":}'
        rows = complete_marker_lines(malformed + b"\n", prefix)
        self.assertEqual(rows, [malformed])
        with self.assertRaises(json.JSONDecodeError):
            json.loads(rows[0][len(prefix) :])
        for content in (prefix + b"x" * 5, prefix + b"x" * 5 + b"\n"):
            with self.subTest(content=content), self.assertRaises(ValueError):
                complete_marker_lines(content, prefix, len(prefix) + 4)
        for content, marker, cap in (
            ("text", prefix, 4096),
            (b"", b"", 4096),
            (b"", b"PB053_HOLD_READY", 4096),
            (b"", "text", 4096),
            (b"", prefix, 0),
            (b"", prefix, True),
            (b"", prefix, 1.0),
        ):
            with self.subTest(content=content, marker=marker, cap=cap):
                with self.assertRaises(ValueError):
                    complete_marker_lines(content, marker, cap)

    def test_growing_regular_file_has_no_partial_marker(self):
        prefix = b"PB053_HOLD_READY "
        row = prefix + b'{"run_id":"' + RUN_ID.encode() + b'"}'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "serial.log"
            with path.open("ab") as stream:
                stream.write(prefix + b'{"r')
            self.assertEqual(complete_marker_lines(path.read_bytes(), prefix), [])
            with path.open("ab") as stream:
                stream.write(row[len(prefix + b'{"r') :] + b"\n")
            rows = complete_marker_lines(path.read_bytes(), prefix)
            self.assertEqual(rows, [row])
            self.assertEqual(json.loads(rows[0][len(prefix) :])["run_id"], RUN_ID)

    def test_independent_run_boot_scenario_and_hold_marker(self):
        normal = synthetic_guest()
        for key, value in (
            ("run_id", "f" * 32),
            ("boot_id", "not-a-uuid"),
            ("boot_id", BOOT_ID.upper()),
            ("scenario", "hold"),
        ):
            changed = copy.deepcopy(normal)
            changed[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_guest(changed, STIMULUS, RUN_ID, "normal")
        with self.assertRaises(ValueError):
            validate_guest(normal, STIMULUS, "f" * 32, "normal")
        with self.assertRaises(ValueError):
            validate_guest(normal, STIMULUS, RUN_ID, "hold")
        marker = "PB053_HOLD_READY " + json.dumps(
            {
                "run_id": RUN_ID,
                "boot_id": BOOT_ID,
                "actors": {
                    "dbus": 11,
                    "monitor": 12,
                    "emulator": 13,
                    "bluez-fresh1": 14,
                },
            }
        )
        hold = decode_marker(marker, "PB053_HOLD_READY ", 4096)
        self.assertEqual(validate_hold_ready(hold, RUN_ID)["actors"]["emulator"], 13)
        for change in (
            {**hold, "run_id": "f" * 32},
            {**hold, "actors": {**hold["actors"], "monitor": True}},
            {**hold, "boot_id": "invalid"},
        ):
            with self.assertRaises(ValueError):
                validate_hold_ready(change, RUN_ID)
        with self.assertRaises(ValueError):
            validate_guest({**normal, "scenario": "hold"}, STIMULUS, RUN_ID, "hold")

    def test_pinned_corpus_and_valid_fixtures(self):
        self.assertIs(
            validate_public(synthetic_public("fresh"), "fresh", STIMULUS)["ok"], True
        )
        self.assertIs(
            validate_guest(synthetic_guest(), STIMULUS, RUN_ID, "normal")["ok"], True
        )

    def test_marker_cardinality_and_json(self):
        prefix = "PB053_GUEST_RESULT "
        good = prefix + json.dumps(synthetic_guest())
        self.assertEqual(
            decode_marker("boot\n" + good, prefix, 8 * 1024 * 1024)["ok"], True
        )
        for text in (
            good + "\n" + good,
            prefix + '{"ok":1,"ok":1}',
            prefix + '{"ok":NaN}',
            prefix + "[]",
            " " + good,
            prefix + "{}" * 100,
        ):
            with self.subTest(text=text[:64]), self.assertRaises(ValueError):
                decode_marker(text, prefix, 64 if len(text) > 100 else 1024)

    def test_public_negative_controls(self):
        base = synthetic_public("retained")
        for field, value in (
            ("error", "bad"),
            ("cleanup_errors", ["bad"]),
            ("state", "fresh"),
            ("ok", False),
        ):
            changed = copy.deepcopy(base)
            changed[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_public(changed, "retained", STIMULUS)
        false_result = copy.deepcopy(base)
        false_result.update(ok=False, error="actual child failed")
        self.assertIs(
            validate_public(false_result, "retained", STIMULUS, require_success=False)[
                "ok"
            ],
            False,
        )
        for alteration in ("missing", "duplicate", "reordered", "recomputed"):
            changed = copy.deepcopy(base)
            frames = [e for e in changed["events"] if "frame" in e]
            if alteration == "missing":
                changed["events"].remove(frames[3])
            elif alteration == "duplicate":
                changed["events"].insert(
                    changed["events"].index(frames[3]), copy.deepcopy(frames[3])
                )
            elif alteration == "reordered":
                frames[3]["frame"] = 4
            else:
                wrong = bytes.fromhex(frames[3]["received_hex"])[::-1]
                frames[3].update(
                    received_hex=wrong.hex(), sha256=hashlib.sha256(wrong).hexdigest()
                )
            with self.subTest(alteration=alteration), self.assertRaises(ValueError):
                validate_public(changed, "retained", STIMULUS)
        for alteration in (
            "source_close",
            "fake_revocation",
            "unknown_acquire",
            "failed_frame",
        ):
            changed = copy.deepcopy(base)
            events = changed["events"]
            if alteration == "source_close":
                events.remove(next(e for e in events if e.get("closed") == SOURCE))
            elif alteration == "fake_revocation":
                next(e for e in events if "revoked" in e).update(
                    eof=False, pollhup=False
                )
            elif alteration == "unknown_acquire":
                next(e for e in events if e.get("acquired") == SOURCE)["acquired"] = (
                    "/other"
                )
            else:
                events.append({"failed_frame": 7})
            with self.subTest(alteration=alteration), self.assertRaises(ValueError):
                validate_public(changed, "retained", STIMULUS)

    def test_public_numeric_boolean_aliases_and_unhashable_paths(self):
        # Synthetic parser fixtures; only targeted evidence field changes.
        mutations = (
            ("released_ok_int", "released", "ok", 1),
            ("closed_ok_int", "closed", "ok", 1),
            ("inactive_ok_int", "inactive", "ok", 1),
            ("eof_int", "revoked", "eof", 1),
            ("pollhup_int", "revoked", "pollhup", 1),
            ("poll_flags_bool", "revoked", "poll_flags", True),
            ("poll_flags_negative", "revoked", "poll_flags", -1),
            ("negative_frame_flags", "frame", "flags", -1),
            ("negative_source_mtu", "acquired", "read_mtu", -1),
            ("callback_props_int", "callback", "properties", 1),
            ("unhashable_closed", "closed", "closed", [SOURCE]),
            ("unhashable_inactive", "inactive", "inactive", {"path": SOURCE}),
        )
        for name, selector, field, value in mutations:
            changed = synthetic_public("fresh")
            entry = next(item for item in changed["events"] if selector in item)
            entry[field] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_public(changed, "fresh", STIMULUS)

    def test_revocation_requires_actual_hangup_flags(self):
        base = synthetic_public("fresh")
        self.assertTrue(validate_public(base, "fresh", STIMULUS)["ok"])
        for name, replacement in (
            ("zero_flags", {"poll_flags": 0}),
            ("false_hup", {"pollhup": False}),
            ("eof_without_pollin", {"poll_flags": select.POLLHUP}),
            (
                "invalid_fd",
                {"poll_flags": select.POLLIN | select.POLLHUP | select.POLLNVAL},
            ),
        ):
            changed = synthetic_public("fresh")
            next(event for event in changed["events"] if "revoked" in event).update(
                replacement
            )
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_public(changed, "fresh", STIMULUS)
        hup_only = synthetic_public("fresh")
        next(event for event in hup_only["events"] if "revoked" in event).update(
            eof=False, poll_flags=select.POLLHUP
        )
        self.assertTrue(validate_public(hup_only, "fresh", STIMULUS)["ok"])

    def test_public_success_requires_complete_state_event_families(self):
        for key in (
            "adapters",
            "initial_devices",
            "endpoint",
            "discovered",
            "paired",
            "transport",
            "acquired",
            "disconnect",
            "disconnect_states",
            "disconnect_observation",
            "cleanup",
        ):
            with self.subTest(family=key):
                report = synthetic_public("fresh")

                def in_family(event):
                    if key == "endpoint":
                        return "endpoint" in event and "transport" not in event
                    if key == "transport":
                        return "transport" in event and "endpoint" in event
                    return key in event

                report["events"] = [
                    event for event in report["events"] if not in_family(event)
                ]
                self.assertTrue(all(report["cases"].values()))
                with self.assertRaises(ValueError):
                    validate_public(report, "fresh", STIMULUS)
        archived = synthetic_public("fresh")
        archived["schema_version"] = 1
        with self.assertRaisesRegex(ValueError, "Public schema mismatch"):
            validate_public(archived, "fresh", STIMULUS)
        for key in (
            "initial_devices",
            "discovered",
            "paired",
            "disconnect_observation",
        ):
            report = synthetic_public("fresh")
            report["events"].append(
                copy.deepcopy(next(event for event in report["events"] if key in event))
            )
            with self.subTest(duplicate=key), self.assertRaises(ValueError):
                validate_public(report, "fresh", STIMULUS)
        reordered = synthetic_public("fresh")
        discovered = next(e for e in reordered["events"] if "discovered" in e)
        reordered["events"].remove(discovered)
        reordered["events"].insert(1, discovered)
        with self.assertRaises(ValueError):
            validate_public(reordered, "fresh", STIMULUS)

    def test_public_identity_registration_discovery_and_transport_controls(self):
        cases = (
            (
                "adapter_duplicate",
                "adapters",
                lambda e: e["adapters"].__setitem__(1, e["adapters"][0]),
            ),
            (
                "adapter_address",
                "adapters",
                lambda e: e["addresses"].__setitem__(1, e["addresses"][0]),
            ),
            (
                "initial_bond",
                "initial_devices",
                lambda e: e["initial_devices"].update(
                    {PEER: authored_device(ADDRESSES[1], ADAPTERS[0])}
                ),
            ),
            (
                "initial_bool_alias",
                "initial_devices",
                lambda e: e["initial_devices"].update(
                    {
                        PEER: {
                            **authored_device(ADDRESSES[1], ADAPTERS[0], bonded=False),
                            "Paired": 0,
                        }
                    }
                ),
            ),
            (
                "registration_adapter",
                "endpoint",
                lambda e: e.update(adapter=ADAPTERS[1]),
            ),
            (
                "unsupported_uuid",
                "endpoint",
                lambda e: e.update(supported_uuids=[SINK_UUID]),
            ),
            (
                "discovered_path",
                "discovered",
                lambda e: e.update(discovered=RECIPROCAL),
            ),
            (
                "discovering_before",
                "discovered",
                lambda e: e.update(discovering_before_stop=False),
            ),
            (
                "discovering_after",
                "discovered",
                lambda e: e.update(discovering_after_stop=True),
            ),
            ("pair_operation", "paired", lambda e: e.update(operation="Connect")),
            ("pair_remote_bool", "paired", lambda e: e["remote"].update(Connected=1)),
            (
                "pair_wrong_identity",
                "paired",
                lambda e: e["local"].update(Adapter=ADAPTERS[1]),
            ),
            (
                "transport_codec",
                "transport",
                lambda e: e["transport_properties"].update(Codec=7),
            ),
            (
                "transport_configuration",
                "transport",
                lambda e: e["transport_properties"].update(Configuration="00"),
            ),
            (
                "transport_device",
                "transport",
                lambda e: e["transport_properties"].update(Device=RECIPROCAL),
            ),
            (
                "transport_uuid",
                "transport",
                lambda e: e["transport_properties"].update(UUID=SINK_UUID),
            ),
            (
                "transport_state",
                "transport",
                lambda e: e["transport_properties"].update(State="active"),
            ),
        )
        for name, selector, edit in cases:
            report = synthetic_public("fresh")
            event = next(
                e
                for e in report["events"]
                if selector in e
                and (selector != "transport" or "transport_properties" in e)
            )
            edit(event)
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_public(report, "fresh", STIMULUS)
        retained = synthetic_public("retained")
        for change in ("absent", "unbonded", "connected", "unresolved"):
            report = copy.deepcopy(retained)
            initial = next(
                e["initial_devices"] for e in report["events"] if "initial_devices" in e
            )
            if change == "absent":
                initial.pop(PEER)
            elif change == "unbonded":
                initial[PEER]["Bonded"] = False
            elif change == "connected":
                initial[PEER]["Connected"] = True
            else:
                initial[PEER]["ServicesResolved"] = True
            with self.subTest(retained=change), self.assertRaises(ValueError):
                validate_public(report, "retained", STIMULUS)

    def test_reviewed_exact_peer_transport_and_cleanup_bindings(self):
        # Extra Device1 entry is well formed but not opposite controller's address.
        for state, path, address in (
            ("fresh", "/org/bluez/hci0/dev_00_AA_01_00_00_00", ADDRESSES[0]),
            ("retained", "/org/bluez/hci0/dev_00_AA_02_00_00_00", "00:AA:02:00:00:00"),
        ):
            report = synthetic_public(state)
            inventory = next(
                e["initial_devices"] for e in report["events"] if "initial_devices" in e
            )
            inventory[path] = authored_device(address, ADAPTERS[0], bonded=False)
            with (
                self.subTest(state=state),
                self.assertRaisesRegex(ValueError, "Device1 identity/state invalid"),
            ):
                validate_public(report, state, STIMULUS)

        report = synthetic_public("fresh")
        unrelated = "/org/bluez/hci7/dev_00_AA_07_00_00_00"
        remap = {SOURCE: unrelated + "/fd1", SINK: unrelated + "/fd0"}
        for event in report["events"]:
            for key in (
                "transport",
                "acquired",
                "released",
                "closed",
                "revoked",
                "inactive",
            ):
                if key in event and event[key] in remap:
                    event[key] = remap[event[key]]
        with self.assertRaisesRegex(
            ValueError, "Transport path not owned by paired peer"
        ):
            validate_public(report, "fresh", STIMULUS)

        report = synthetic_public("fresh")
        next(e for e in report["events"] if e.get("cleanup") == "peer disconnect")[
            "ok"
        ] = 1
        with self.assertRaisesRegex(
            ValueError, "Peer disconnect cleanup missing/out of order"
        ):
            validate_public(report, "fresh", STIMULUS)

    def test_public_disconnect_window_and_order_controls(self):
        def changed(edit):
            report = synthetic_public("fresh")
            edit(report["events"])
            with self.assertRaises(ValueError):
                validate_public(report, "fresh", STIMULUS)

        for name, edit in (
            (
                "reply_order",
                lambda es: es.insert(
                    es.index(next(e for e in es if e.get("disconnect") == PEER)),
                    es.pop(
                        es.index(
                            next(e for e in es if e.get("disconnect") == RECIPROCAL)
                        )
                    ),
                ),
            ),
            (
                "source_time",
                lambda es: next(e for e in es if e.get("disconnect") == PEER).update(
                    time_ns=True
                ),
            ),
            (
                "state_identity",
                lambda es: next(e for e in es if "disconnect_states" in e)[
                    "disconnect_states"
                ][PEER].update(Address=ADDRESSES[0]),
            ),
            (
                "state_time",
                lambda es: next(e for e in es if "disconnect_states" in e).update(
                    time_ns=1
                ),
            ),
            (
                "late_reconnect",
                lambda es: [e for e in es if "disconnect_states" in e][-1][
                    "disconnect_states"
                ][PEER].update(Connected=True),
            ),
            (
                "missing_observation",
                lambda es: es.remove(
                    next(e for e in es if "disconnect_observation" in e)
                ),
            ),
            (
                "short_window",
                lambda es: next(e for e in es if "disconnect_observation" in e)[
                    "disconnect_observation"
                ].update(start_ns=1000000501),
            ),
            (
                "wrong_sample_count",
                lambda es: next(e for e in es if "disconnect_observation" in e)[
                    "disconnect_observation"
                ].update(samples=3),
            ),
            (
                "wrong_peers",
                lambda es: next(e for e in es if "disconnect_observation" in e)[
                    "disconnect_observation"
                ].update(peers=[RECIPROCAL, PEER]),
            ),
            (
                "missing_cleanup",
                lambda es: es.remove(
                    next(e for e in es if e.get("cleanup") == "peer disconnect")
                ),
            ),
            (
                "cleanup_before_observation",
                lambda es: es.insert(
                    es.index(next(e for e in es if "disconnect_observation" in e)),
                    es.pop(-1),
                ),
            ),
        ):
            with self.subTest(name=name):
                changed(edit)

    def test_guest_public_phase_identity_and_bond_controls(self):
        for name, stage, edit in (
            (
                "retained_adapters",
                "public_retained",
                lambda es: next(e for e in es if "adapters" in e)[
                    "adapters"
                ].__setitem__(0, "/org/bluez/hci8"),
            ),
            (
                "fresh2_address",
                "public_fresh2",
                lambda es: next(e for e in es if "adapters" in e)[
                    "addresses"
                ].__setitem__(0, "00:AA:02:00:00:00"),
            ),
            (
                "retained_peer",
                "public_retained",
                lambda es: next(e for e in es if "paired" in e).update(
                    reciprocal="/org/bluez/hci1/dev_00_AA_01_00_00_02"
                ),
            ),
            (
                "fresh2_bond",
                "public_fresh2",
                lambda es: next(e for e in es if "initial_devices" in e)[
                    "initial_devices"
                ].update({PEER: authored_device(ADDRESSES[1], ADAPTERS[0])}),
            ),
        ):
            report = synthetic_guest()
            events = next(item for item in report["stages"] if item["stage"] == stage)[
                "result"
            ]["events"]
            edit(events)
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_guest(report, STIMULUS, RUN_ID, "normal")

    def test_guest_numeric_boolean_aliases_in_actor_and_controller(self):
        mutations = (
            ("daemon_stopped_pid_bool", "daemon_stopped_fresh1", "pid", True),
            ("controller_index_bool", "controller_ready", "index", True),
            ("controller_power_int", "controller_ready", "powered", 1),
            ("controller_extra_key", "controller_ready", "extra", "ignored"),
            ("controller_address_nonstr", "controller_ready", "address", ["bad"]),
        )
        for name, selector, field, value in mutations:
            changed = synthetic_guest()
            stage = next(
                item for item in changed["stages"] if item["stage"] == selector
            )
            target = (
                stage["controllers"][0] if selector == "controller_ready" else stage
            )
            target[field] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_guest(changed, STIMULUS, RUN_ID, "normal")

    def test_guest_negative_controls(self):
        base = synthetic_guest()
        edits = (
            lambda stages: stages.append(
                {"stage": "stopped", "process": "monitor", "pid": 12, "returncode": 1}
            ),
            lambda stages: stages.append(
                {"stage": "started", "process": "unknown", "pid": 99, "argv": ["bad"]}
            ),
            lambda stages: stages.append("not an object"),
            lambda stages: next(
                e
                for e in stages
                if e.get("stage") == "started" and e.get("process") == "emulator"
            ).update(pid=True),
            lambda stages: next(
                e
                for e in stages
                if e.get("stage") == "stopped" and e.get("process") == "dbus"
            ).update(returncode=True),
            lambda stages: next(
                e
                for e in stages
                if e.get("stage") == "started" and e.get("process") == "monitor"
            ).update(pid=11),
            lambda stages: stages.remove(
                next(e for e in stages if e.get("stage") == "readiness")
            ),
            lambda stages: stages.remove(
                next(e for e in stages if e.get("stage") == "controller_ready")
            ),
            lambda stages: stages.remove(
                next(e for e in stages if e.get("stage") == "bluetooth_iso")
            ),
            lambda stages: stages.insert(
                0,
                stages.pop(
                    next(
                        i
                        for i, e in enumerate(stages)
                        if e.get("stage") == "public_retained"
                    )
                ),
            ),
            lambda stages: next(
                e for e in stages if e.get("stage") == "controller_ready"
            ).update(indices=[True, 1]),
            lambda stages: next(
                e for e in stages if e.get("stage") == "state_preserved"
            )["hashes"].update({"../escape": "a" * 64}),
            lambda stages: next(e for e in stages if e.get("stage") == "state_reset")[
                "hashes"
            ].update({"bond": "wrong"}),
            lambda stages: next(e for e in stages if e.get("stage") == "public_fresh1")[
                "result"
            ].update(error="failed"),
            lambda stages: next(
                e for e in stages if e.get("stage") == "public_fresh1"
            ).update(child_exit_code=1),
            lambda stages: stages.append(
                {
                    "stage": "failure",
                    "failed_stage": "public_retained",
                    "error": "failed",
                }
            ),
        )
        for number, edit in enumerate(edits):
            candidate = copy.deepcopy(base)
            # These fixtures mutate in-memory reports only, never VM/process output.
            try:
                edit(candidate["stages"])
            except AttributeError:
                # Non-dict negative control cannot use dict.get in later lambdas.
                raise AssertionError(f"Broken test edit {number}")
            with self.subTest(number=number), self.assertRaises(ValueError):
                validate_guest(candidate, STIMULUS, RUN_ID, "normal")

    def test_emitted_final_helper_matches_parser_fixture(self):
        fixture = synthetic_guest()
        emitted = final_result(
            True, "7.1.5", 2, fixture["stages"], None, RUN_ID, BOOT_ID, "normal"
        )
        encoded = "PB053_GUEST_RESULT " + json.dumps(emitted)
        self.assertIs(
            validate_guest(
                decode_marker(encoded, "PB053_GUEST_RESULT ", 8 * 1024 * 1024),
                STIMULUS,
                RUN_ID,
                "normal",
            )["ok"],
            True,
        )


if __name__ == "__main__":
    unittest.main()
