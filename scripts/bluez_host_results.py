"""Strict PB-053 encoded guest/public report validation (stdlib only)."""

import hashlib
import json
import math
import re
import select
import socket
import struct


PUBLIC_CASES = frozenset(
    (
        "state_initialization",
        "endpoint_registration",
        "discovery",
        "pairing",
        "endpoint_configuration",
        "iso_delivery",
    )
)
ACTORS = frozenset(
    ("dbus", "monitor", "emulator", "bluez-fresh1", "bluez-retained", "bluez-fresh2")
)
PHASES = ("fresh1", "retained", "fresh2")
PHASE_STAGES = (
    "public_fresh1",
    "daemon_stopped_fresh1",
    "state_preserved",
    "public_retained",
    "daemon_stopped_retained",
    "state_reset",
    "public_fresh2",
)
STAGE_FIELDS = {
    "kernel": {"version"},
    "modules": set(),
    "af_alg_ecb_aes": {"operation"},
    "af_alg_cmac_aes": {"operation"},
    "bus_readiness": {"reply"},
    "started": {"process", "pid", "argv"},
    "readiness": {"controllers", "reply"},
    "bluetooth_iso": {"operation"},
    "controller_command_start": {
        "phase",
        "index",
        "operation",
        "wall",
        "monotonic",
        "argv",
    },
    "controller_command_reply": {
        "phase",
        "index",
        "operation",
        "wall",
        "monotonic",
        "reply",
    },
    "controller_ready": {
        "phase",
        "controllers",
        "indices",
        "addresses",
        "powered",
        "wall",
        "monotonic",
    },
    "public_fresh1": {"operation", "result", "child_exit_code"},
    "public_retained": {"operation", "result", "child_exit_code"},
    "public_fresh2": {"operation", "result", "child_exit_code"},
    "daemon_stopped_fresh1": {"pid", "returncode", "name_has_owner"},
    "daemon_stopped_retained": {"pid", "returncode", "name_has_owner"},
    "state_preserved": {"hashes"},
    "state_reset": {"hashes", "original_hashes", "backup"},
    "stopped": {"process", "pid", "returncode"},
    "traffic_capture": {
        "packets",
        "bytes",
        "sha256",
        "reported_drops",
        "opcode_counts",
    },
    "failure": {"failed_stage", "error"},
    "cleanup_failure": {"process", "error"},
}
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
ADDRESS = re.compile(r"[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}\Z")
CAPTURE_CAP = 4 * 1024 * 1024
MONITOR_READY = "PB053_MONITOR_READY "
MONITOR_RESULT = "PB053_MONITOR_RESULT "


def validate_capture(text):
    require(
        isinstance(text, str) and len(text.encode("utf-8")) <= CAPTURE_CAP,
        "Monitor capture oversized",
    )
    lines = text.splitlines(keepends=True)
    require(text.endswith("\n") and len(lines) >= 2, "Monitor capture incomplete")

    def decode(line, prefix):
        require(line.startswith(prefix) and line.endswith("\n"), "Monitor row invalid")
        try:
            return json.loads(
                line[len(prefix) :],
                object_pairs_hook=pairs_unique,
                parse_constant=reject_constant,
            )
        except (ValueError, TypeError, RecursionError) as exc:
            raise ValueError(f"Invalid monitor JSON: {exc}") from exc

    ready = decode(lines[0], MONITOR_READY)
    exact_keys(ready, ("schema_version",), "Monitor readiness")
    require(
        type(ready["schema_version"]) is int and ready["schema_version"] == 1,
        "Monitor readiness invalid",
    )
    summary = decode(lines[-1], MONITOR_RESULT)
    exact_keys(
        summary,
        (
            "schema_version",
            "ok",
            "packets",
            "bytes",
            "sha256",
            "error",
            "stop_signal",
            "reported_drops",
        ),
        "Monitor result",
    )
    require(
        type(summary["schema_version"]) is int
        and summary["schema_version"] == 1
        and summary["ok"] is True
        and summary["error"] is None
        and type(summary["stop_signal"]) is int
        and summary["stop_signal"] in (2, 15)
        and type(summary["packets"]) is int
        and summary["packets"] >= 0
        and type(summary["bytes"]) is int
        and summary["bytes"] >= 0
        and isinstance(summary["sha256"], str)
        and HEX64.fullmatch(summary["sha256"])
        and (
            summary["reported_drops"] is None
            or type(summary["reported_drops"]) is int
            and summary["reported_drops"] == 0
        ),
        "Monitor terminal failure",
    )
    sha = hashlib.sha256()
    total = 0
    opcodes = {}
    seen_drops = False
    previous_monotonic = None
    for number, line in enumerate(lines[1:-1]):
        require(
            not line.startswith((MONITOR_READY, MONITOR_RESULT)), "Extra monitor marker"
        )
        row = decode(line, "")
        exact_keys(
            row,
            (
                "packet",
                "wall_ns",
                "monotonic_ns",
                "kernel_timestamp_ns",
                "opcode",
                "index",
                "raw_hex",
                "flags",
                "reported_drops",
            ),
            "Monitor packet",
        )
        require(
            type(row["packet"]) is int
            and row["packet"] == number
            and all(
                type(row[key]) is int and row[key] >= 0
                for key in ("wall_ns", "monotonic_ns", "opcode", "index", "flags")
            )
            and (
                row["kernel_timestamp_ns"] is None
                or type(row["kernel_timestamp_ns"]) is int
                and row["kernel_timestamp_ns"] >= 0
            )
            and (
                row["reported_drops"] is None
                or type(row["reported_drops"]) is int
                and row["reported_drops"] == 0
            )
            and not row["flags"] & (socket.MSG_TRUNC | socket.MSG_CTRUNC)
            and isinstance(row["raw_hex"], str)
            and bool(re.fullmatch(r"[0-9a-f]+", row["raw_hex"]))
            and len(row["raw_hex"]) % 2 == 0,
            "Monitor packet metadata invalid",
        )
        require(
            previous_monotonic is None or row["monotonic_ns"] >= previous_monotonic,
            "Monitor observation time regressed",
        )
        previous_monotonic = row["monotonic_ns"]
        seen_drops |= row["reported_drops"] is not None
        raw = bytes.fromhex(row["raw_hex"])
        require(6 <= len(raw) <= 65541, "Monitor raw size invalid")
        opcode, index, length = struct.unpack_from("<HHH", raw)
        require(
            opcode == row["opcode"]
            and index == row["index"]
            and length == len(raw) - 6,
            "Monitor header mismatch",
        )
        sha.update(struct.pack("<I", len(raw)))
        sha.update(raw)
        total += len(raw)
        opcodes[opcode] = opcodes.get(opcode, 0) + 1
    require(
        len(lines) - 2 == summary["packets"]
        and total == summary["bytes"]
        and sha.hexdigest() == summary["sha256"]
        and (
            summary["reported_drops"] == 0
            if seen_drops
            else summary["reported_drops"] is None
        ),
        "Monitor digest/count/drop mismatch",
    )
    return {**summary, "opcode_counts": opcodes}


def final_result(ok, kernel, controllers, stages, error):
    return {
        "schema_version": 1,
        "ok": ok,
        "kernel": kernel,
        "controllers": controllers,
        "stages": stages,
        "error": error,
    }


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def pairs_unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"Invalid JSON constant: {value}")


def decode_marker(text, prefix, max_bytes):
    require(
        isinstance(text, str)
        and isinstance(prefix, str)
        and prefix
        and type(max_bytes) is int
        and max_bytes > 0,
        "Invalid marker inputs",
    )
    lines = [
        line[len(prefix) :] for line in text.splitlines() if line.startswith(prefix)
    ]
    require(len(lines) == 1, f"Expected one {prefix} result, found {len(lines)}")
    require(len(lines[0].encode("utf-8")) <= max_bytes, "Marker payload too large")
    try:
        result = json.loads(
            lines[0], object_pairs_hook=pairs_unique, parse_constant=reject_constant
        )
    except (UnicodeError, ValueError, RecursionError, TypeError) as exc:
        raise ValueError(f"Malformed marker: {exc}") from exc
    require(isinstance(result, dict), "Marker must contain object")
    return result


def exact_keys(value, keys, name):
    require(
        isinstance(value, dict) and set(value) == set(keys), f"{name} fields invalid"
    )


def positive_int(value):
    return type(value) is int and value > 0


def timestamp(value):
    return type(value) in (int, float) and math.isfinite(value) and value >= 0


def payload(stimulus):
    require(
        isinstance(stimulus, bytes)
        and len(stimulus) == 15360
        and hashlib.sha256(stimulus).hexdigest()
        == "c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be",
        "Pinned transport stimulus mismatch",
    )
    return stimulus


def ordered_event(events, key):
    return [(i, item) for i, item in enumerate(events) if key in item]


def validate_public(result, expected_state, stimulus, require_success=True):
    exact_keys(
        result,
        ("schema_version", "state", "ok", "cases", "events", "cleanup_errors", "error"),
        "Public",
    )
    require(
        type(result["schema_version"]) is int and result["schema_version"] == 1,
        "Public schema mismatch",
    )
    require(
        expected_state in ("fresh", "retained") and result["state"] == expected_state,
        "Public state mismatch",
    )
    require(type(result["ok"]) is bool, "Public ok must be boolean")
    cases = result["cases"]
    require(
        isinstance(cases, dict)
        and set(cases) == PUBLIC_CASES
        and all(type(value) is bool for value in cases.values()),
        "Public cases invalid",
    )
    events = result["events"]
    require(
        isinstance(events, list)
        and len(events) <= 4096
        and all(isinstance(event, dict) for event in events),
        "Public events invalid",
    )
    require(
        isinstance(result["cleanup_errors"], list)
        and all(isinstance(error, str) for error in result["cleanup_errors"])
        and (result["error"] is None or isinstance(result["error"], str)),
        "Public failure evidence invalid",
    )
    if not result["ok"]:
        require(not require_success, "Public result failed")
        require(
            not all(cases.values())
            or bool(result["cleanup_errors"])
            or result["error"] is not None,
            "Public failure lacks failed-case/cleanup/error evidence",
        )
        return result
    require(
        all(cases.values())
        and result["cleanup_errors"] == []
        and result["error"] is None,
        "Contradictory successful public result",
    )
    stimulus = payload(stimulus)
    require(
        not any("failed_frame" in item or "unexpected_iso" in item for item in events),
        "Public transport failure event",
    )
    frames = ordered_event(events, "frame")
    require(len(frames) == 16, "Expected exactly 16 transport frames")
    for number, (_, item) in enumerate(frames):
        exact_keys(
            item,
            ("frame", "sent", "received_len", "sha256", "received_hex", "flags"),
            "Frame",
        )
        expected = stimulus[number * 120 : (number + 1) * 120]
        require(
            type(item["frame"]) is int
            and item["frame"] == number
            and type(item["sent"]) is int
            and item["sent"] == 120
            and type(item["received_len"]) is int
            and item["received_len"] == 120
            and type(item["flags"]) is int
            and item["flags"] >= 0
            and not item["flags"] & socket.MSG_TRUNC
            and item["received_hex"] == expected.hex()
            and item["sha256"] == hashlib.sha256(expected).hexdigest(),
            f"Pinned transport frame {number} mismatch",
        )
    transports = [
        (i, item)
        for i, item in enumerate(events)
        if "transport" in item and "endpoint" in item
    ]
    require(
        len(transports) == 2
        and all(
            isinstance(item.get("endpoint"), str)
            and isinstance(item.get("transport"), str)
            for _, item in transports
        )
        and {item.get("endpoint") for _, item in transports}
        == {"/pb053/source", "/pb053/sink"},
        "Two callback-derived transports required",
    )
    for _, item in transports:
        exact_keys(
            item,
            ("transport", "endpoint", "callback_properties", "transport_properties"),
            "Transport",
        )
    mapping = {item["endpoint"]: item["transport"] for _, item in transports}
    source, sink = mapping["/pb053/source"], mapping["/pb053/sink"]
    callbacks = [
        (i, item)
        for i, item in enumerate(events)
        if item.get("callback") == "Endpoint.SetConfiguration"
    ]
    require(
        len(callbacks) == 2
        and all(
            isinstance(item.get("path"), str) and isinstance(item.get("transport"), str)
            for _, item in callbacks
        )
        and {item.get("path"): item.get("transport") for _, item in callbacks}
        == mapping
        and all(
            any(
                cidx < tidx and callback.get("path") == transport["endpoint"]
                for cidx, callback in callbacks
            )
            for tidx, transport in transports
        ),
        "Transport not backed by SetConfiguration callbacks",
    )
    for _, item in callbacks:
        exact_keys(
            item,
            ("callback", "path", "transport", "properties"),
            "SetConfiguration callback",
        )
        require(
            isinstance(item["properties"], str),
            "SetConfiguration properties must be string",
        )
    require(
        all(
            isinstance(path, str) and path.startswith("/org/bluez/")
            for path in (source, sink)
        )
        and source != sink
        and all(
            isinstance(item.get("callback_properties"), str)
            and isinstance(item.get("transport_properties"), str)
            for _, item in transports
        ),
        "Transport callback evidence invalid",
    )
    acquired = ordered_event(events, "acquired")
    require(
        len(acquired) == 2
        and all(isinstance(item["acquired"], str) for _, item in acquired)
        and {item["acquired"] for _, item in acquired} == {source, sink},
        "Acquired transport mismatch",
    )
    for _, item in acquired:
        exact_keys(item, ("acquired", "read_mtu", "write_mtu"), "Acquisition")
        require(
            type(item.get("read_mtu")) is int
            and item["read_mtu"] >= 0
            and type(item.get("write_mtu")) is int
            and item["write_mtu"] >= 0,
            "Transport MTU invalid",
        )
    require(
        next(item for _, item in acquired if item["acquired"] == source)["write_mtu"]
        >= 120
        and next(item for _, item in acquired if item["acquired"] == sink)["read_mtu"]
        >= 120
        and max(i for i, _ in acquired) < frames[0][0],
        "Transport acquisition incomplete",
    )
    release, closed, revoked, inactive = (
        ordered_event(events, key)
        for key in ("released", "closed", "revoked", "inactive")
    )
    require(
        len(release) == 1
        and release[0][1] == {"released": source, "role": "source", "ok": True},
        "Source lease release missing/duplicated",
    )
    require(
        type(release[0][1]["ok"]) is bool and release[0][1]["ok"] is True,
        "Source release success flag invalid",
    )
    require(
        len(closed) == 2
        and all(isinstance(item.get("closed"), str) for _, item in closed)
        and {item.get("closed") for _, item in closed} == {source, sink}
        and all(
            type(item.get("ok")) is bool and item["ok"] is True for _, item in closed
        )
        and all(item == {"closed": item["closed"], "ok": True} for _, item in closed),
        "Both lease closes required",
    )
    require(
        len(revoked) == 1
        and revoked[0][1].get("revoked") == sink
        and revoked[0][1].get("state") == "idle"
        and type(revoked[0][1].get("eof")) is bool
        and type(revoked[0][1].get("pollhup")) is bool
        and type(revoked[0][1].get("poll_flags")) is int
        and revoked[0][1]["poll_flags"] >= 0
        and (
            revoked[0][1].get("eof") is True
            or revoked[0][1].get("pollhup") is True
            and bool(revoked[0][1]["poll_flags"] & select.POLLHUP)
        ),
        "Sink revocation not observed",
    )
    if len(revoked) == 1:
        exact_keys(
            revoked[0][1],
            ("revoked", "state", "eof", "pollhup", "poll_flags"),
            "Revocation",
        )
    require(
        len(inactive) == 2
        and all(isinstance(item.get("inactive"), str) for _, item in inactive)
        and {item.get("inactive") for _, item in inactive} == {source, sink}
        and all(
            type(item.get("ok")) is bool and item["ok"] is True for _, item in inactive
        )
        and all(
            item == {"inactive": item["inactive"], "ok": True} for _, item in inactive
        ),
        "Both transports must become inactive",
    )
    source_close = next(i for i, item in closed if item["closed"] == source)
    sink_close = next(i for i, item in closed if item["closed"] == sink)
    require(
        frames[-1][0]
        < release[0][0]
        < source_close
        < revoked[0][0]
        < sink_close
        < min(i for i, _ in inactive),
        "Lease cleanup order invalid",
    )
    return result


def hashes(value):
    require(
        isinstance(value, dict) and 0 < len(value) <= 1024,
        "State hashes missing/oversized",
    )
    for name, digest in value.items():
        require(
            isinstance(name, str)
            and bool(name)
            and not name.startswith("/")
            and all(part not in ("", ".", "..") for part in name.split("/"))
            and isinstance(digest, str)
            and HEX64.fullmatch(digest),
            "State hash/path invalid",
        )


def validate_guest(result, stimulus):
    exact_keys(
        result,
        ("schema_version", "ok", "kernel", "controllers", "stages", "error"),
        "Guest",
    )
    require(
        type(result["schema_version"]) is int
        and result["schema_version"] == 1
        and result["ok"] is True
        and result["kernel"] == "7.1.5"
        and type(result["controllers"]) is int
        and result["controllers"] == 2
        and result["error"] is None,
        "Guest success/profile mismatch",
    )
    stages = result["stages"]
    require(
        isinstance(stages, list) and 0 < len(stages) <= 4096,
        "Guest stages missing/oversized",
    )
    for stage in stages:
        require(
            isinstance(stage, dict)
            and isinstance(stage.get("stage"), str)
            and stage["stage"] in STAGE_FIELDS,
            "Unknown/non-object guest stage",
        )
        exact_keys(stage, {"stage"} | STAGE_FIELDS[stage["stage"]], "Guest stage")
        require(
            stage["stage"] not in ("failure", "cleanup_failure"),
            "Guest contains failure",
        )

    def one(label):
        matches = [
            (i, stage) for i, stage in enumerate(stages) if stage["stage"] == label
        ]
        require(len(matches) == 1, f"Expected exactly one {label}")
        return matches[0]

    require(
        one("kernel")[1]["version"] == "7.1.5"
        and one("modules")[0] < one("bus_readiness")[0]
        and isinstance(one("bus_readiness")[1]["reply"], str)
        and one("bus_readiness")[1]["reply"].startswith("method return"),
        "Guest kernel/bus evidence invalid",
    )
    for label in ("af_alg_ecb_aes", "af_alg_cmac_aes"):
        require(one(label)[1]["operation"] == "success", "Guest AF_ALG unavailable")
    starts, stops = {}, {}
    for i, stage in enumerate(stages):
        if stage["stage"] not in ("started", "stopped"):
            continue
        actor = stage["process"]
        require(
            isinstance(actor, str) and actor in ACTORS and positive_int(stage["pid"]),
            "Guest actor/PID invalid",
        )
        if stage["stage"] == "started":
            require(
                actor not in starts
                and isinstance(stage["argv"], list)
                and stage["argv"]
                and all(isinstance(arg, str) and bool(arg) for arg in stage["argv"]),
                "Duplicate/invalid guest start",
            )
            starts[actor] = (i, stage)
        else:
            require(
                actor not in stops
                and actor in starts
                and starts[actor][0] < i
                and stage["pid"] == starts[actor][1]["pid"]
                and type(stage["returncode"]) is int
                and stage["returncode"] == 0,
                "Invalid/extra guest stop",
            )
            stops[actor] = (i, stage)
    require(
        set(starts) == ACTORS
        and set(stops) == ACTORS
        and len({stage["pid"] for _, stage in starts.values()}) == 6,
        "Guest actor ownership incomplete",
    )
    require(
        starts["dbus"][0]
        < one("bus_readiness")[0]
        < starts["monitor"][0]
        < starts["emulator"][0]
        < starts["bluez-fresh1"][0]
        and stops["emulator"][0] < stops["monitor"][0] < stops["dbus"][0],
        "Guest private bus/monitor lifetime invalid",
    )
    capture_at, capture = one("traffic_capture")
    counts = capture["opcode_counts"]
    require(
        stops["monitor"][0] < capture_at < stops["dbus"][0]
        and type(capture["packets"]) is int
        and capture["packets"] >= 0
        and type(capture["bytes"]) is int
        and capture["bytes"] >= 0
        and isinstance(capture["sha256"], str)
        and HEX64.fullmatch(capture["sha256"])
        and (
            capture["reported_drops"] is None
            or type(capture["reported_drops"]) is int
            and capture["reported_drops"] == 0
        )
        and isinstance(counts, dict)
        and all(
            isinstance(k, str)
            and k.isdecimal()
            and str(int(k)) == k
            and type(v) is int
            and v > 0
            for k, v in counts.items()
        )
        and sum(counts.values()) == capture["packets"]
        and counts.get("18", 0) >= 48
        and counts.get("19", 0) >= 48
        and counts.get("2", 0) > 0
        and counts.get("3", 0) > 0
        and counts.get("16", 0) > 0
        and counts.get("17", 0) > 0,
        "Guest traffic capture incomplete",
    )
    phase = [
        (i, stage) for i, stage in enumerate(stages) if stage["stage"] in PHASE_STAGES
    ]
    require(
        [item["stage"] for _, item in phase] == list(PHASE_STAGES),
        "Guest lifecycle missing/duplicate/out of order",
    )
    require(
        phase[-1][0] < stops["emulator"][0],
        "Guest controller emulator stopped before final public phase",
    )
    require(
        phase[2][0] < starts["bluez-retained"][0]
        and phase[5][0] < starts["bluez-fresh2"][0],
        "Next daemon started before state checkpoint",
    )
    require(
        sum(s["stage"] == "readiness" for s in stages) == 3
        and sum(s["stage"] == "bluetooth_iso" for s in stages) == 3,
        "Extra/missing adapter or ISO readiness",
    )
    public = (phase[0], phase[3], phase[6])
    for (position, item), name, state in zip(
        public, PHASES, ("fresh", "retained", "fresh")
    ):
        actor = "bluez-" + name
        require(
            starts[actor][0] < position < stops[actor][0]
            and item["operation"] == "success"
            and type(item["child_exit_code"]) is int
            and item["child_exit_code"] == 0,
            "Guest public child provenance invalid",
        )
        validate_public(item["result"], state, stimulus)
        prior = starts[actor][0]
        require(
            len(
                [
                    s
                    for s in stages[prior:position]
                    if s["stage"] == "readiness"
                    and type(s["controllers"]) is int
                    and s["controllers"] == 2
                    and isinstance(s["reply"], str)
                    and s["reply"].startswith("method return")
                ]
            )
            == 1
            and len(
                [
                    s
                    for s in stages[prior:position]
                    if s["stage"] == "bluetooth_iso" and s["operation"] == "success"
                ]
            )
            == 1,
            "Per-phase adapter/ISO readiness missing",
        )
    for item, actor in ((phase[1], "bluez-fresh1"), (phase[4], "bluez-retained")):
        position, stage = item
        require(
            starts[actor][0]
            < position
            < starts["bluez-retained" if actor.endswith("fresh1") else "bluez-fresh2"][
                0
            ]
            and positive_int(stage["pid"])
            and stage["pid"] == starts[actor][1]["pid"]
            and type(stage["returncode"]) is int
            and stage["returncode"] == 0
            and stage["name_has_owner"] is False,
            "Daemon disappearance not verified",
        )
    preserved, reset = phase[2][1], phase[5][1]
    hashes(preserved["hashes"])
    hashes(reset["hashes"])
    hashes(reset["original_hashes"])
    require(
        reset["backup"] == "/var/lib/pb053-retained-bluetooth"
        and reset["original_hashes"] == preserved["hashes"],
        "Guest state history invalid",
    )
    fresh_events = public[0][1]["result"]["events"]
    identity = [e for e in fresh_events if "adapters" in e]
    require(
        len(identity) == 1
        and isinstance(identity[0].get("addresses"), list)
        and len(identity[0]["addresses"]) == 2
        and all(
            isinstance(addr, str) and ADDRESS.fullmatch(addr)
            for addr in identity[0]["addresses"]
        ),
        "Fresh controller identity missing",
    )
    ready_events = [
        (i, s) for i, s in enumerate(stages) if s["stage"] == "controller_ready"
    ]
    require(
        [s["phase"] for _, s in ready_events] == ["retained", "fresh2"],
        "Controller readiness phase mismatch",
    )
    commands = [
        (i, s)
        for i, s in enumerate(stages)
        if s["stage"] in ("controller_command_start", "controller_command_reply")
    ]
    require(len(commands) == 16, "Controller command accounting incomplete")
    for phase_name, (ready_pos, ready), actor in zip(
        ("retained", "fresh2"), ready_events, ("bluez-retained", "bluez-fresh2")
    ):
        checkpoint = phase[2][0] if phase_name == "retained" else phase[5][0]
        require(
            checkpoint < ready_pos < starts[actor][0]
            and ready["powered"] is True
            and timestamp(ready["wall"])
            and timestamp(ready["monotonic"])
            and isinstance(ready["indices"], list)
            and len(ready["indices"]) == 2
            and all(type(index) is int and index >= 0 for index in ready["indices"])
            and len(set(ready["indices"])) == 2
            and ready["addresses"] == identity[0]["addresses"]
            and len(set(ready["addresses"])) == 2
            and isinstance(ready["controllers"], list)
            and len(ready["controllers"]) == 2
            and all(
                isinstance(item, dict)
                and set(item) == {"index", "address", "powered"}
                and type(item["index"]) is int
                and isinstance(item["address"], str)
                and item["powered"] is True
                for item in ready["controllers"]
            )
            and ready["controllers"]
            == [
                {"index": index, "address": addr, "powered": True}
                for index, addr in zip(ready["indices"], ready["addresses"])
            ],
            "Controller readiness identity invalid",
        )
        phase_commands = [(i, s) for i, s in commands if s.get("phase") == phase_name]
        require(
            len(phase_commands) == 8
            and checkpoint < min(i for i, _ in phase_commands)
            and max(i for i, _ in phase_commands) < ready_pos,
            "Controller commands missing/out of phase",
        )
        for offset, (index, operation) in enumerate(
            (index, operation)
            for index in ready["indices"]
            for operation in ("power", "info")
        ):
            (begin_at, begin), (end_at, end) = phase_commands[
                2 * offset : 2 * offset + 2
            ]
            require(
                begin["stage"] == "controller_command_start"
                and end["stage"] == "controller_command_reply"
                and begin_at < end_at
                and type(begin["index"]) is int
                and type(end["index"]) is int
                and begin["index"] == end["index"] == index
                and begin["operation"] == end["operation"] == operation
                and timestamp(begin["wall"])
                and timestamp(end["wall"])
                and timestamp(begin["monotonic"])
                and timestamp(end["monotonic"])
                and begin["wall"] <= end["wall"] <= ready["wall"]
                and begin["monotonic"] <= end["monotonic"] <= ready["monotonic"]
                and isinstance(begin["argv"], list)
                and begin["argv"]
                and all(isinstance(arg, str) and bool(arg) for arg in begin["argv"])
                and isinstance(end["reply"], str)
                and bool(end["reply"]),
                "Controller command evidence invalid",
            )
    require(
        all(s.get("phase") in ("retained", "fresh2") for _, s in commands),
        "Unexpected controller command phase",
    )
    return result
