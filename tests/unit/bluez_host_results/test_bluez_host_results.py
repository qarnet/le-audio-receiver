"""Synthetic encoded-report parser fixtures, not real guest/VM acceptance."""

import copy
import hashlib
import json
import struct
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bluez_host_results import (
    decode_marker,
    validate_guest,
    validate_public,
    PUBLIC_CASES,
    validate_capture,
)

STIMULUS = (ROOT / "tests/fixtures/lc3/bsim_48k_10ms_120b_l.lc3").read_bytes()
SOURCE = "/org/bluez/hci0/dev_00_AA_01_01_00_01/fd1"
SINK = "/org/bluez/hci1/dev_00_AA_01_00_00_00/fd0"
ADDRESSES = ["00:AA:01:00:00:00", "00:AA:01:01:00:01"]


def synthetic_public(state):
    events = [
        {
            "adapters": ["/org/bluez/hci0", "/org/bluez/hci1"],
            "addresses": ADDRESSES.copy(),
        }
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
                "transport": SOURCE,
                "endpoint": "/pb053/source",
                "callback_properties": "callback",
                "transport_properties": "live",
            },
            {
                "transport": SINK,
                "endpoint": "/pb053/sink",
                "callback_properties": "callback",
                "transport_properties": "live",
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
                "pollhup": False,
                "poll_flags": 1,
            },
            {"closed": SINK, "ok": True},
            {"inactive": SOURCE, "ok": True},
            {"inactive": SINK, "ok": True},
        ]
    )
    return {
        "schema_version": 1,
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
    def test_pinned_corpus_and_valid_fixtures(self):
        self.assertIs(
            validate_public(synthetic_public("fresh"), "fresh", STIMULUS)["ok"], True
        )
        self.assertIs(validate_guest(synthetic_guest(), STIMULUS)["ok"], True)

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
                validate_guest(changed, STIMULUS)

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
                validate_guest(candidate, STIMULUS)

    def test_emitted_final_helper_matches_parser_fixture(self):
        from bluez_host_results import final_result

        fixture = synthetic_guest()
        emitted = final_result(True, "7.1.5", 2, fixture["stages"], None)
        encoded = "PB053_GUEST_RESULT " + json.dumps(emitted)
        self.assertIs(
            validate_guest(
                decode_marker(encoded, "PB053_GUEST_RESULT ", 8 * 1024 * 1024), STIMULUS
            )["ok"],
            True,
        )


if __name__ == "__main__":
    unittest.main()
