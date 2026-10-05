"""Independent, fail-closed PB-051 inventory, trace and execution accounting."""

import hashlib
import json
import math
import os
import re
import stat
import struct
from collections import Counter
from pathlib import Path

POLICY_LIMIT = 1024 * 1024
LOG_LIMIT = 16 * 1024 * 1024
EXECUTION_LIMIT = 2 * 1024 * 1024
IMAGE_LIMIT = 128 * 1024 * 1024
SHA = re.compile(r"[0-9a-f]{64}\Z")
RUN_ID = re.compile(r"[0-9a-f]{32}\Z")
FAMILIES = (
    ("control_frame_validation", 7, 7, 21, 21),
    ("metadata_length_validation", 13, 14, 67, 67),
    ("codec_qos", 7, 7, 27, 27),
    ("lifecycle", 19, 23, 91, 91),
    ("dual", 10, 10, 42, 52),
    ("reconnect", 4, 4, 11, 11),
)
EXTRA_PHASES = {
    "metadata_update_streaming",
    "streaming_config_reject",
    "streaming_enable_reject",
    "disable_from_streaming",
    "release_from_streaming",
}
PROFILE = {
    "sink_ases": 2,
    "source_ases": 0,
    "metadata_capacity": 16,
    "mtu": 65,
    "valid_lc3": {
        "frequency_hz": 48000,
        "duration_us": 10000,
        "octets_per_channel": 120,
    },
    "codec_config": {
        "target_latency": 2,
        "target_phy": 2,
        "codec_id": 6,
        "cid_le": "0000",
        "vid_le": "0000",
        "data": "02010802020105030100000003047800",
    },
    "default_metadata": "03020100",
    "idle_qos_profile": {
        "cig": 0,
        "cis": 0,
        "interval_us": 10000,
        "framing": 0,
        "phy": 2,
        "sdu": 120,
        "rtn": 5,
        "latency_ms": 20,
        "presentation_delay_us": 40000,
    },
}
BUILD_PROFILE = {
    "receiver_experimental": [
        "Experimental symbol BT_LL_SW_SPLIT is enabled.",
        "Experimental symbol BT_CTLR_SET_HOST_FEATURE is enabled.",
        "Experimental symbol BT_CTLR_PERIPHERAL_ISO is enabled.",
    ],
    "client_experimental": [
        "Experimental symbol BT_LL_SW_SPLIT is enabled.",
        "Experimental symbol BT_CTLR_SET_HOST_FEATURE is enabled.",
        "Experimental symbol BT_CTLR_CENTRAL_ISO is enabled.",
    ],
    "native_cmake_notice": "SoC native is not supported by this release.",
    "unlisted_compiler_kconfig_linker_runtime_warnings": "reject",
}


class TraceError(ValueError):
    """Evidence failed an independently anchored check."""


def require(condition, message):
    if not condition:
        raise TraceError(message)


def exact_int(value, low, high, label):
    require(type(value) is int and low <= value <= high, f"invalid {label}")
    return value


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def strict_json(raw, limit):
    require(type(raw) is bytes and len(raw) <= limit, "invalid or oversized JSON")
    try:
        return json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=unique_pairs,
            parse_constant=lambda value: (_ for _ in ()).throw(
                TraceError(f"nonfinite JSON: {value}")
            ),
        )
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise TraceError(f"invalid UTF-8 JSON: {exc}") from exc


def fields(obj, names, label, optional=()):
    require(
        type(obj) is dict and set(names) <= set(obj) <= set(names) | set(optional),
        f"invalid {label} fields",
    )


def same_tree(value, expected):
    if type(value) is not type(expected):
        return False
    if type(expected) is dict:
        return value.keys() == expected.keys() and all(
            same_tree(value[k], v) for k, v in expected.items()
        )
    if type(expected) is list:
        return len(value) == len(expected) and all(
            same_tree(a, b) for a, b in zip(value, expected)
        )
    return value == expected


def _state_map(value, label):
    fields(value, {"A", "B"}, label)
    for key in ("A", "B"):
        exact_int(value[key], 0, 6, f"{label}.{key}")


def _request_descriptor(value, profile):
    require(type(value) is str, "request descriptor must be string")
    if value.startswith("hex:"):
        template = value[4:]
        raw = template.replace("{A}", "01").replace("{B}", "02")
        require(
            re.fullmatch(r"(?:[0-9a-f]{2})+", raw) is not None
            and len(raw) // 2 <= profile["mtu"] - 3,
            "invalid hex request template",
        )
        return int(raw[:2], 16)
    if value.startswith("codec:"):
        require(
            value
            in {
                "codec:base",
                "codec:base;octets_le16=19",
                "codec:base;octets_le16=121",
                "codec:base;first_length=00",
                "codec:base;frequency_width=03010800;cc_len=17",
            },
            "invalid codec descriptor",
        )
        return 1
    if value.startswith("qos:"):
        require(
            re.fullmatch(
                r"qos:(idle|snapshot:[AB](;interval_le24=fe0000|;framing=02|;phy=80)?)",
                value,
            ),
            "invalid QoS descriptor",
        )
        return 2
    if value.startswith("batch:"):
        require(
            value
            in {
                "batch:enable:A=default,B=00",
                "batch:enable:B=00,A=default",
                "batch:qos:A=snapshot,B=snapshot_interval_fe0000",
                "batch:qos:B=snapshot_interval_fe0000,A=snapshot",
                "batch:disable:A,B",
                "batch:disable:B,A",
                "batch:release:A,B",
                "batch:release:B,A",
            },
            "invalid batch descriptor",
        )
        return {"enable": 3, "qos": 2, "disable": 5, "release": 8}[value.split(":")[1]]
    raise TraceError("unknown request descriptor")


def load_inventory(rawbytes, expected_sha):
    require(
        type(expected_sha) is str and SHA.fullmatch(expected_sha),
        "invalid inventory anchor",
    )
    require(sha256(rawbytes) == expected_sha, "inventory digest mismatch")
    policy = strict_json(rawbytes, POLICY_LIMIT)
    fields(
        policy,
        {
            "schema_version",
            "sdk",
            "profile",
            "request_descriptor_grammar",
            "build_diagnostics",
            "client_runtime_diagnostics",
            "totals",
            "exclusions",
            "families",
        },
        "inventory",
    )
    require(
        exact_int(policy["schema_version"], 1, 1, "schema_version") == 1,
        "unsupported schema",
    )
    require(
        policy["sdk"]
        == {
            "ncs": "v3.4.1",
            "zephyr_commit": "33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6",
        },
        "SDK identity drift",
    )
    require(same_tree(policy["profile"], PROFILE), "receiver or codec profile drift")
    require(
        type(policy["request_descriptor_grammar"]) is dict
        and set(policy["request_descriptor_grammar"])
        == {"hex", "codec", "qos", "batch", "response", "generation_delta"}
        and all(
            type(v) is str and v for v in policy["request_descriptor_grammar"].values()
        ),
        "invalid request descriptor grammar",
    )
    require(
        same_tree(policy["build_diagnostics"], BUILD_PROFILE),
        "invalid build diagnostic policy",
    )
    require(
        policy["client_runtime_diagnostics"] == [] and policy["exclusions"] == [],
        "client warnings or exclusions forbidden",
    )
    require(
        type(policy["families"]) is list and len(policy["families"]) == 6,
        "family inventory drift",
    )
    seen = set()
    sums = [0, 0, 0, 0]
    for family, (name, count, phases, exchanges, records) in zip(
        policy["families"], FAMILIES
    ):
        fields(
            family,
            {
                "id",
                "case_count",
                "render_phases",
                "raw_exchanges",
                "response_records",
                "cases",
            },
            "family",
        )
        require(
            family["id"] == name
            and type(family["cases"]) is list
            and len(family["cases"]) == count,
            "family ID/count mismatch",
        )
        for key, expected in (
            ("case_count", count),
            ("render_phases", phases),
            ("raw_exchanges", exchanges),
            ("response_records", records),
        ):
            require(
                exact_int(family[key], 0, 1000, key) == expected, f"{name} total drift"
            )
        actual = [count, 0, 0, 0]
        for case in family["cases"]:
            fields(
                case,
                {
                    "id",
                    "render_phase_delta",
                    "generation_delta",
                    "actions",
                    "diagnostics",
                },
                "case",
                {"batch_order", "cis_order", "pre_disconnect"},
            )
            case_id = case["id"]
            require(
                type(case_id) is str
                and re.fullmatch(r"[a-z][a-z0-9_]*", case_id)
                and case_id not in seen,
                "duplicate or invalid case ID",
            )
            seen.add(case_id)
            require(
                exact_int(case["render_phase_delta"], 1, 2, "render delta")
                == (2 if case_id in EXTRA_PHASES else 1),
                "render delta drift",
            )
            require(
                exact_int(case["generation_delta"], 0, 1, "generation delta")
                == (1 if name == "reconnect" else 0),
                "generation delta drift",
            )
            if name == "dual":
                require(
                    case.get("batch_order") in ("A,B", "B,A")
                    and case.get("cis_order") in ("A,B", "B,A"),
                    "dual ordering missing",
                )
            else:
                require(
                    "batch_order" not in case and "cis_order" not in case,
                    "unexpected dual fields",
                )
            require(
                (name == "reconnect") == ("pre_disconnect" in case),
                "reconnect state obligation missing",
            )
            if name == "reconnect":
                require(
                    case["pre_disconnect"]
                    in {
                        "codec_configured_both",
                        "qos_configured_both",
                        "A_enabling_B_qos",
                        "A_streaming_B_enabling_A_only_CIS_source_accepted_10",
                    },
                    "invalid reconnect pre-state",
                )
            require(
                type(case["actions"]) is list and len(case["actions"]) > 0,
                "empty action sequence",
            )
            actual[1] += case["render_phase_delta"]
            actual[2] += len(case["actions"])
            for action in case["actions"]:
                fields(
                    action,
                    {"request", "response"},
                    "action",
                    {"pre_states", "post_states", "generation_delta"},
                )
                opcode = _request_descriptor(action["request"], policy["profile"])
                response = action["response"]
                fields(response, {"opcode", "records"}, "response")
                require(
                    exact_int(response["opcode"], 0, 255, "opcode") == opcode
                    and type(response["records"]) is list
                    and 1 <= len(response["records"]) <= 2,
                    "invalid response opcode/records",
                )
                actual[3] += len(response["records"])
                for rec in response["records"]:
                    fields(rec, {"ase", "code", "reason"}, "response record")
                    require(rec["ase"] in ("A", "B", "none"), "invalid response ASE")
                    exact_int(rec["code"], 0, 255, "response code")
                    exact_int(rec["reason"], 0, 255, "response reason")
                rejected = any(rec["code"] != 0 for rec in response["records"])
                require(
                    rejected == ("pre_states" in action),
                    "negative pre-state missing/extra",
                )
                if rejected:
                    _state_map(action["pre_states"], "pre_states")
                partial = name == "dual" and action["request"].startswith("batch:")
                require(
                    partial == ("post_states" in action),
                    "dual post-state missing/extra",
                )
                if partial:
                    _state_map(action["post_states"], "post_states")
                require(
                    (name == "reconnect") == ("generation_delta" in action),
                    "action generation delta missing/extra",
                )
                if name == "reconnect":
                    exact_int(
                        action["generation_delta"], 0, 1, "action generation delta"
                    )
            require(type(case["diagnostics"]) is list, "invalid diagnostics")
            diagnostics = set()
            for item in case["diagnostics"]:
                fields(item, {"severity", "module", "text", "count"}, "diagnostic")
                require(
                    item["severity"] in ("wrn", "err")
                    and item["module"] == "bt_ascs"
                    and type(item["text"]) is str
                    and item["text"],
                    "invalid diagnostic template",
                )
                exact_int(item["count"], 1, 100, "diagnostic count")
                key = (item["severity"], item["module"], item["text"])
                require(key not in diagnostics, "duplicate diagnostic template")
                diagnostics.add(key)
        require(
            actual == [count, phases, exchanges, records],
            f"{name} case/action totals drift",
        )
        sums = [a + b for a, b in zip(sums, actual)]
    require(
        type(policy["totals"]) is dict
        and same_tree(
            policy["totals"],
            dict(
                zip(
                    ("cases", "render_phases", "raw_exchanges", "response_records"),
                    sums,
                )
            ),
        )
        and sums == [60, 65, 259, 269],
        "global inventory totals drift",
    )
    return policy


SGR = re.compile(r"\x1b\[[0-9;]*m")
PREFIX = re.compile(r"^d_(0[01]): @(\d{2}):(\d{2}):(\d{2})\.(\d{6}) +(.+)$")
KEY_VALUE = re.compile(r"([a-z][a-z0-9_]*)=([^\s]+)")
KNOWN = {
    "ASCS_PROCEDURE_BEGIN",
    "ASCS_PROCEDURE_END",
    "ASCS_DISCOVERY",
    "ASCS_CCC",
    "ASCS_READ",
    "ASCS_REQUEST",
    "ASCS_CP",
    "ASCS_WRITE",
    "ASCS_PRESERVE",
    "ASCS_DUAL_READ",
    "ASCS_DUAL_QOS",
    "ASCS_METADATA",
    "ASCS_METADATA_CALLBACK",
    "ASCS_DISABLED",
    "ASCS_RECOVERY",
    "ASCS_TX",
    "ASCS_TX_REGISTER",
    "ASCS_TX_AUDIT",
    "ASCS_RENDER",
    "ASCS_CP_CLOSE",
    "ASCS_CLEANUP",
    "ASCS_SUBSCRIBE",
    "ASCS_PRE_DISCONNECT",
    "ASCS_PARTIAL_SOURCE",
    "ASCS_DISCONNECT",
    "ASCS_RELEASED",
    "ASCS_GROUP_DELETE",
    "ASCS_RECONNECT",
    "ASCS_TEARDOWN_OBSERVER",
    "ASCS_CLIENT",
    "ASCS_RECEIVER",
}
WARN = re.compile(r"<(wrn|err)> ([a-zA-Z0-9_]+): (.*)$")


def _number(value, label, maximum=0xFFFFFFFF):
    require(
        type(value) is str and re.fullmatch(r"[0-9]+", value) is not None,
        f"invalid {label}",
    )
    return exact_int(int(value), 0, maximum, label)


def _kv(event, key, maximum=0xFFFFFFFF):
    require(key in event["fields"], f"missing {event['marker']}.{key}")
    return _number(event["fields"][key], f"{event['marker']}.{key}", maximum)


def _hex(value, label, maximum=128):
    require(
        type(value) is str
        and re.fullmatch(r"(?:[0-9a-f]{2})+", value) is not None
        and len(value) <= 2 * maximum,
        f"invalid {label}",
    )
    return bytes.fromhex(value)


def _events(raw, role):
    require(type(raw) is bytes and len(raw) <= LOG_LIMIT, "oversized peer log")
    try:
        text = raw.decode("utf-8")
    except UnicodeError as exc:
        raise TraceError("peer log is not UTF-8") from exc
    clean = SGR.sub("", text)
    require("\x1b" not in clean, "unsupported terminal escape in peer log")
    result = []
    previous = -1
    for index, line in enumerate(clean.splitlines()):
        match = PREFIX.fullmatch(line)
        require(
            match is not None and match[1] == role,
            f"invalid {role} line/prefix {index + 1}",
        )
        hour, minute, second, micro = [int(match[j]) for j in range(2, 6)]
        require(hour < 24 and minute < 60 and second < 60, "invalid BSim time")
        time = ((hour * 60 + minute) * 60 + second) * 1000000 + micro
        require(time >= previous, "nonmonotonic BSim time")
        previous = time
        body = match[6]
        require(
            not re.search(
                r"(?:ERROR:|Assertion failed|TESTCASE FAILED|TESTCASE NOT PASSED|"
                r"FATAL ERROR|__ASSERT\b|Kernel panic)",
                body,
                re.I,
            ),
            "fatal/failed marker in peer log",
        )
        warning = WARN.search(body)
        marker_match = re.search(
            r"\b(ASCS_[A-Z_]+|CASE_BEGIN|CASE_END)\b(?: (.*))?", body
        )
        marker = marker_match[1] if marker_match else None
        if marker:
            require(
                marker in KNOWN or marker in ("CASE_BEGIN", "CASE_END"),
                f"unknown {marker} event",
            )
            require(
                not marker.endswith("_ERROR"), "client ownership/callback error marker"
            )
            words = marker_match[2] or ""
            pairs = KEY_VALUE.findall(words)
            fields_map = unique_pairs(pairs)
            # Recognized event rows must contain only key=value fields.
            require(
                " ".join(f"{key}={value}" for key, value in pairs) == words,
                f"malformed {marker} row",
            )
            require(fields_map, f"empty {marker} row")
        else:
            fields_map = {}
        result.append(
            {
                "time": time,
                "line": index,
                "body": body,
                "marker": marker,
                "fields": fields_map,
                "diagnostic": warning.groups() if warning else None,
            }
        )
    require(result, "empty peer log")
    return result


def _one(events, marker, *, predicate=lambda e: True, label=None):
    matched = [
        event for event in events if event["marker"] == marker and predicate(event)
    ]
    require(len(matched) == 1, f"expected one {label or marker}, got {len(matched)}")
    return matched[0]


def _ase_ids(summary):
    f = summary["fields"]
    require(_kv(summary, "mtu") == 65, "negotiated MTU is not 65")
    handles = [_kv(summary, key, 0xFFFF) for key in ("cp", "ccc")]
    ids = {}
    for index, symbol in enumerate(("A", "B")):
        value = f.get(f"ase{index}", "")
        require(
            re.fullmatch(r"[0-9]+/[0-9]+", value) is not None, "invalid ASE discovery"
        )
        ase_id, handle = [int(part) for part in value.split("/")]
        require(1 <= ase_id <= 255 and 1 <= handle <= 0xFFFF, "invalid ASE ID/handle")
        ids[symbol] = (ase_id, handle)
        handles.append(handle)
    require(
        all(handles)
        and len(set(handles)) == len(handles)
        and ids["A"][0] != ids["B"][0],
        "duplicate dynamic handle or ASE ID",
    )
    return ids


def _qos_public(raw, ids, generation, ase):
    require(
        len(raw) == 17 and raw[0] == ids[ase][0] and raw[1] == 2,
        f"wrong public QoS shape for generation {generation} {ase}",
    )
    require(
        int.from_bytes(raw[4:7], "little") == 10000
        and raw[7:9] == b"\x00\x02"
        and int.from_bytes(raw[9:11], "little") == 120
        and raw[11] == 5
        and int.from_bytes(raw[12:14], "little") == 20
        and int.from_bytes(raw[14:17], "little") == 40000,
        "public QoS profile mismatch",
    )


def _request_bytes(descriptor, ids, qos, profile):
    if descriptor.startswith("hex:"):
        return _hex(
            descriptor[4:]
            .replace("{A}", f"{ids['A'][0]:02x}")
            .replace("{B}", f"{ids['B'][0]:02x}"),
            "encoded request",
            62,
        )
    if descriptor.startswith("codec:"):
        config = profile["codec_config"]
        data = bytearray.fromhex(config["data"])
        if descriptor.endswith("octets_le16=19") or descriptor.endswith(
            "octets_le16=121"
        ):
            data[-2:] = (19 if descriptor.endswith("19") else 121).to_bytes(2, "little")
        elif descriptor.endswith("first_length=00"):
            data[0] = 0
        elif "frequency_width=" in descriptor:
            data[0:3] = b"\x03\x01\x08\x00"
        prefix = bytes(
            (
                1,
                1,
                ids["A"][0],
                config["target_latency"],
                config["target_phy"],
                config["codec_id"],
            )
        )
        return (
            prefix
            + bytes.fromhex(config["cid_le"] + config["vid_le"])
            + bytes((len(data),))
            + data
        )
    if descriptor.startswith("qos:"):
        if descriptor == "qos:idle":
            p = profile["idle_qos_profile"]
            payload = (
                bytes((p["cig"], p["cis"]))
                + p["interval_us"].to_bytes(3, "little")
                + bytes((p["framing"], p["phy"]))
                + p["sdu"].to_bytes(2, "little")
                + bytes((p["rtn"],))
                + p["latency_ms"].to_bytes(2, "little")
                + p["presentation_delay_us"].to_bytes(3, "little")
            )
            return bytes((2, 1, ids["A"][0])) + payload
        base, _, edit = descriptor.partition(";")
        ase = base[-1]
        require(ase in qos, f"missing same-step public QoS snapshot for {ase}")
        payload = bytearray(qos[ase][2:])
        if edit == "interval_le24=fe0000":
            payload[2:5] = b"\xfe\x00\x00"
        elif edit == "framing=02":
            payload[5] = 2
        elif edit == "phy=80":
            payload[6] = 0x80
        else:
            require(not edit, "unknown QoS edit")
        return bytes((2, 1, ids[ase][0])) + payload
    if descriptor.startswith("batch:"):
        kind, parts = descriptor[6:].split(":", 1)
        record = bytearray()
        for item in parts.split(","):
            ase = item[0]
            if kind == "enable":
                meta = (
                    bytes.fromhex(profile["default_metadata"])
                    if ase == "A"
                    else b"\x00"
                )
                record.extend((ids[ase][0], len(meta)))
                record.extend(meta)
            elif kind == "qos":
                require(ase in qos, f"missing same-step batch QoS snapshot for {ase}")
                payload = bytearray(qos[ase][2:])
                if ase == "B":
                    payload[2:5] = b"\xfe\x00\x00"
                record.append(ids[ase][0])
                record.extend(payload)
            else:
                record.append(ids[ase][0])
        return (
            bytes(({"enable": 3, "qos": 2, "disable": 5, "release": 8}[kind], 2))
            + record
        )
    raise TraceError("unknown request descriptor")


def _response_bytes(expected, ids):
    records = expected["records"]
    global_error = (
        len(records) == 1
        and records[0]["ase"] == "none"
        and records[0]["code"] in (1, 2)
    )
    result = bytearray((expected["opcode"], 0xFF if global_error else len(records)))
    for rec in records:
        result.extend(
            (
                ids[rec["ase"]][0] if rec["ase"] != "none" else 0,
                rec["code"],
                rec["reason"],
            )
        )
    return bytes(result)


def _case_windows(client, family):
    ordered = []
    pending = None
    for event in client:
        if event["marker"] == "CASE_BEGIN":
            require(pending is None, "nested CASE_BEGIN")
            require(len(ordered) < len(family["cases"]), "extra CASE_BEGIN")
            case = family["cases"][len(ordered)]
            require(
                event["fields"].get("name") == case["id"]
                and _kv(event, "step") == len(ordered) + 1,
                "wrong CASE_BEGIN order",
            )
            pending = (event, case)
        elif event["marker"] == "CASE_END":
            require(pending is not None, "CASE_END without start")
            begin, case = pending
            require(
                event["fields"].get("name") == case["id"]
                and _kv(event, "step") == len(ordered) + 1
                and begin["time"] < event["time"],
                "wrong CASE_END",
            )
            start_gen = _kv(begin, "generation")
            end_gen = _kv(event, "generation")
            require(
                end_gen == start_gen + case["generation_delta"]
                and _kv(begin, "recovery_phase")
                == sum(c["render_phase_delta"] for c in family["cases"][: len(ordered)])
                and _kv(event, "recovery_phase")
                == _kv(begin, "recovery_phase") + case["render_phase_delta"],
                "case generation or recovery phase drift",
            )
            for key in ("preserved", "metadata_observed", "fresh_idle", "rendered"):
                if key in event["fields"]:
                    require(_kv(event, key) == 1, f"unproven CASE_END {key}")
            ordered.append((begin, event, case))
            pending = None
    require(
        pending is None and len(ordered) == len(family["cases"]),
        "incomplete case sequence",
    )
    return ordered


def _family_diagnostics(receiver, client, windows, policy):
    require(
        not any(event["diagnostic"] for event in client), "client runtime diagnostic"
    )
    for event in receiver:
        if not event["diagnostic"]:
            continue
        owners = [
            case
            for begin, end, case in windows
            if begin["time"] <= event["time"] < end["time"]
        ]
        require(len(owners) == 1, "receiver warning/error outside unique case")
    for begin, end, case in windows:
        expected = Counter(
            {
                (entry["severity"], entry["module"], entry["text"]): entry["count"]
                for entry in case["diagnostics"]
            }
        )
        actual = Counter(
            event["diagnostic"]
            for event in receiver
            if event["diagnostic"] and begin["time"] <= event["time"] < end["time"]
        )
        require(
            actual == expected, f"{case['id']}: unexpected/missing runtime diagnostics"
        )


def _discovery(client, family):
    summaries = [
        e
        for e in client
        if e["marker"] == "ASCS_DISCOVERY" and "stage" not in e["fields"]
    ]
    required = 5 if family["id"] == "reconnect" else 1
    require(len(summaries) == required, "missing/duplicate discovery")
    mapping = {}
    for event in summaries:
        gen = _kv(event, "generation")
        require(gen == len(mapping) + 1, "missing or repeated generation")
        mapping[gen] = (event, _ase_ids(event))
    service = [
        e
        for e in client
        if e["marker"] == "ASCS_DISCOVERY" and e["fields"].get("stage") == "service"
    ]
    require(
        len(service) == required
        and [(_kv(e, "generation")) for e in service] == list(mapping),
        "fresh service discovery absent",
    )
    for gen, (summary, _) in mapping.items():
        require(
            any(
                e["marker"] == "ASCS_CCC"
                and e["fields"].get("stage") == "found"
                and _kv(e, "generation") == gen
                and e["line"] < summary["line"]
                for e in client
            ),
            "fresh CP CCC discovery absent",
        )
    return mapping, {gen: e for gen, e in zip(mapping, service)}


def _reads(client, discoveries, starts):
    reads = []
    for event in client:
        if event["marker"] != "ASCS_READ":
            continue
        gen = _kv(event, "generation")
        require(
            gen in discoveries and starts[gen]["line"] < event["line"],
            "read on unknown or stale generation",
        )
        ids = discoveries[gen][1]
        ase = next(
            (
                ase
                for ase in ("A", "B")
                if ids[ase] == (_kv(event, "id", 255), _kv(event, "handle", 0xFFFF))
            ),
            None,
        )
        require(ase is not None, "read uses stale ASE ID/handle")
        raw = _hex(event["fields"].get("raw"), "public ASE", 128)
        state = _kv(event, "state", 6)
        require(
            len(raw) >= 2 and raw[0] == ids[ase][0] and raw[1] == state,
            "public ASE read header mismatch",
        )
        if state in (0, 6):
            require(len(raw) == 2, "Idle/Releasing ASE malformed")
        elif state == 2:
            _qos_public(raw, ids, gen, ase)
        elif state in (3, 4, 5):
            require(
                len(raw) >= 5 and raw[4] <= 16 and len(raw) == 5 + raw[4],
                "Enabling/Streaming ASE metadata shape invalid",
            )
        else:
            require(state == 1 and 2 < len(raw) <= 128, "Codec ASE shape invalid")
        reads.append((event, gen, ase, raw))
    require(reads, "no public ASE reads")
    return reads


def _near_read(reads, gen, ase, low, high, *, latest=True):
    matched = [
        (e, raw)
        for e, g, a, raw in reads
        if g == gen and a == ase and low < e["line"] < high
    ]
    require(matched, f"missing public read generation={gen} ASE={ase}")
    return matched[-1 if latest else 0]


def _snapshots(events, marker, transaction, case, gen, *, phase):
    return [
        e
        for e in events
        if e["marker"] == marker
        and e["fields"].get("phase") == phase
        and e["fields"].get("name") == case["id"]
        and (marker == "ASCS_DUAL_READ" or _kv(e, "transaction") == transaction)
        and _kv(e, "generation") == gen
    ]


def _negative_states(
    action, request, cp, following, case_events, reads, gen, ids, case
):
    before = action["pre_states"]
    for ase in ("A", "B"):
        current = _near_read(reads, gen, ase, case_events[0]["line"], request["line"])[
            1
        ]
        require(current[1] == before[ase], f"{case['id']}: wrong {ase} pre-state")
    partial = "post_states" in action
    if partial:
        prior = _snapshots(case_events, "ASCS_DUAL_READ", 0, case, gen, phase="before")
        after = _snapshots(case_events, "ASCS_DUAL_READ", 0, case, gen, phase="after")
        prior = [
            e for e in prior if e["line"] < request["line"] and _kv(e, "index") == 1
        ]
        after = [
            e
            for e in after
            if cp["line"] < e["line"] < following and _kv(e, "index") == 1
        ]
        require(prior and after, f"{case['id']}: missing rejected B public snapshot")
        require(
            _hex(prior[-1]["fields"].get("raw"), "dual before")
            == _hex(after[0]["fields"].get("raw"), "dual after"),
            f"{case['id']}: rejected B changed",
        )
        require(
            _hex(prior[-1]["fields"].get("raw"), "dual before")
            == _near_read(reads, gen, "B", case_events[0]["line"], prior[-1]["line"])[1]
            and _hex(after[0]["fields"].get("raw"), "dual after")
            == _near_read(reads, gen, "B", cp["line"], after[0]["line"])[1],
            f"{case['id']}: rejected B snapshots lack public reads",
        )
        for ase in ("A", "B"):
            observed = _near_read(reads, gen, ase, cp["line"], following, latest=False)[
                1
            ]
            require(
                observed[1] == action["post_states"][ase],
                f"{case['id']}: wrong accepted/rejected {ase} post-state",
            )
        original_a = _near_read(
            reads, gen, "A", case_events[0]["line"], request["line"]
        )[1]
        after_a = _near_read(reads, gen, "A", cp["line"], following, latest=False)[1]
        if request["fields"]["raw"].startswith("02"):
            require(original_a == after_a, "accepted A QoS changed unexpectedly")
        elif request["fields"]["raw"].startswith("03"):
            require(
                original_a[1] == 2
                and after_a[1] == 3
                and after_a[2:4] == original_a[2:4]
                and after_a[4:]
                == bytes((len(bytes.fromhex(PROFILE["default_metadata"])),))
                + bytes.fromhex(PROFILE["default_metadata"]),
                "accepted A metadata/CIG/CIS mismatch",
            )
        elif request["fields"]["raw"].startswith("05"):
            previous_qos = [
                raw
                for e, owner, symbol, raw in reads
                if owner == gen
                and symbol == "A"
                and raw[1] == 2
                and case_events[0]["line"] < e["line"] < request["line"]
            ]
            require(
                original_a[1] == 3 and previous_qos and after_a == previous_qos[-1],
                "accepted A Disable QoS mismatch",
            )
        elif request["fields"]["raw"].startswith("08"):
            require(after_a == bytes((ids["A"][0], 0)), "accepted A Release not Idle")
        return
    for index, ase in enumerate(("A", "B")):
        before_events = [
            e
            for e in _snapshots(
                case_events,
                "ASCS_PRESERVE",
                _kv(request, "transaction"),
                case,
                gen,
                phase="before",
            )
            if _kv(e, "index") == index and e["line"] < request["line"]
        ]
        after_events = [
            e
            for e in _snapshots(
                case_events,
                "ASCS_PRESERVE",
                _kv(request, "transaction"),
                case,
                gen,
                phase="after",
            )
            if _kv(e, "index") == index and cp["line"] < e["line"] < following
        ]
        require(
            len(before_events) == len(after_events) == 1,
            f"{case['id']}: missing full public snapshot {ase}",
        )
        first = _hex(before_events[0]["fields"].get("raw"), "ASE before")
        last = _hex(after_events[0]["fields"].get("raw"), "ASE after")
        require(
            first == last and first[1] == before[ase],
            f"{case['id']}: invalid-input changed {ase} public value",
        )
        require(
            first
            == _near_read(
                reads, gen, ase, case_events[0]["line"], before_events[0]["line"]
            )[1]
            and last
            == _near_read(reads, gen, ase, cp["line"], after_events[0]["line"])[1],
            f"{case['id']}: snapshots not anchored to actual public reads",
        )


def _helper_cp(client, discoveries):
    for event in client:
        if event["marker"] != "ASCS_CP" or event["fields"].get("transaction") != "0":
            continue
        gen = _kv(event, "generation")
        require(gen in discoveries, "helper CP from stale generation")
        ids = discoveries[gen][1]
        raw = _hex(event["fields"].get("raw"), "helper CP", 128)
        require(
            len(raw) >= 5
            and (len(raw) - 2) % 3 == 0
            and (len(raw) - 2) // 3 == raw[1]
            and raw[0] in (1, 2, 3, 5, 8)
            and 1 <= raw[1] <= 2,
            "malformed helper CP",
        )
        seen = set()
        for offset in range(2, len(raw), 3):
            require(
                raw[offset] in (ids["A"][0], ids["B"][0])
                and raw[offset] not in seen
                and raw[offset + 1 : offset + 3] == b"\x00\x00",
                "helper CP cannot satisfy raw rejection",
            )
            seen.add(raw[offset])


def _tx_audits(client, windows, discoveries):
    """Require real TX result retention, guarded busy and explicit retirement."""
    registrations = [e for e in client if e["marker"] == "ASCS_TX_REGISTER"]
    audits = [e for e in client if e["marker"] == "ASCS_TX_AUDIT"]
    used = set()
    results = {}

    def one(events, owner, label, gen, index, low, high, phase=None):
        matches = [
            e
            for e in events
            if e["line"] not in used
            and _kv(e, "generation") == gen
            and _kv(e, "index", 1) == index
            and e["fields"].get("stage") == label
            and low < e["line"] < high
            and (phase is None or _kv(e, "phase") == phase)
        ]
        require(
            len(matches) == 1,
            f"{owner}: expected one {label} generation={gen} ASE={index}",
        )
        used.add(matches[0]["line"])
        return matches[0]

    for begin, end, case in windows:
        part = [e for e in client if begin["line"] < e["line"] < end["line"]]
        for recovery in (e for e in part if e["marker"] == "ASCS_RECOVERY"):
            gen = _kv(recovery, "generation")
            phase = _kv(recovery, "phase")
            for index in (0, 1):
                reg = one(
                    registrations,
                    case["id"],
                    None,
                    gen,
                    index,
                    begin["line"],
                    recovery["line"],
                    phase,
                )
                require(_kv(reg, "ret") == 0, "normal TX registration failed")
                active = one(
                    audits,
                    case["id"],
                    "active",
                    gen,
                    index,
                    reg["line"],
                    recovery["line"],
                    phase,
                )
                require(
                    active["fields"].get("forget_ret") == "-16",
                    "active TX audit was forgotten or guard not exercised",
                )
                tx = [
                    e
                    for e in part
                    if e["marker"] == "ASCS_TX"
                    and e["fields"].get("phase") == str(phase)
                    and e["fields"].get("index") == str(index)
                    and e["fields"].get("unregister") == "0"
                ]
                require(
                    len(tx) == 1 and recovery["line"] < tx[0]["line"],
                    "full TX unregister not tied to rendered phase",
                )
                kept = one(
                    audits,
                    case["id"],
                    "retained",
                    gen,
                    index,
                    tx[0]["line"],
                    end["line"],
                    phase,
                )
                fnv = kept["fields"].get("fnv")
                require(
                    _kv(kept, "ret") == 0
                    and _kv(kept, "sends") == 30
                    and type(fnv) is str
                    and re.fullmatch(r"[0-9a-f]{8}", fnv),
                    "full TX retained count/FNV invalid",
                )
                results[(gen, index)] = (30, fnv, kept["line"])
        if case["id"] == "reconnect_after_partial_streaming":
            gen = _kv(begin, "generation")
            partial = _one(
                part,
                "ASCS_PARTIAL_SOURCE",
                predicate=lambda e: "accepted" in e["fields"],
            )
            unregistered = _one(
                part,
                "ASCS_PARTIAL_SOURCE",
                predicate=lambda e: "unregister" in e["fields"],
            )
            reg = one(
                registrations, case["id"], None, gen, 0, begin["line"], partial["line"]
            )
            require(
                _kv(reg, "ret") == 0
                and _kv(partial, "accepted") == 10
                and _kv(unregistered, "unregister") == 0,
                "partial source registration/counter/drain mismatch",
            )
            kept = one(
                audits, case["id"], "partial", gen, 0, unregistered["line"], end["line"]
            )
            fnv = kept["fields"].get("fnv")
            require(
                _kv(kept, "ret") == 0
                and _kv(kept, "sends") == 10
                and type(fnv) is str
                and re.fullmatch(r"[0-9a-f]{8}", fnv),
                "partial TX retained count/FNV invalid",
            )
            results[(gen, 0)] = (10, fnv, kept["line"])

    for gen in discoveries:
        closed = _one(
            client,
            "ASCS_CLEANUP",
            predicate=lambda e, g=gen: (
                e["fields"].get("generation") == str(g)
                and e["fields"].get("retired") == "1"
            ),
        )
        unsub = _one(
            client,
            "ASCS_CP_CLOSE",
            predicate=lambda e, g=gen: (
                e["fields"].get("generation") == str(g) and "unsubscribe" in e["fields"]
            ),
        )
        for index in (0, 1):
            previous = results.get((gen, index))
            if previous is None:
                unused = one(
                    audits,
                    "retirement",
                    "unused",
                    gen,
                    index,
                    unsub["line"],
                    closed["line"],
                )
                require(
                    unused["fields"].get("result_ret") == "-61"
                    and unused["fields"].get("forget_ret") == "-61",
                    "never-registered stream unexpectedly retained TX",
                )
            else:
                count, fnv, last = previous
                retired = one(
                    audits,
                    "retirement",
                    "retire",
                    gen,
                    index,
                    max(unsub["line"], last),
                    closed["line"],
                )
                require(
                    retired["fields"].get("result_ret") == "0"
                    and _kv(retired, "sends") == count
                    and retired["fields"].get("fnv") == fnv,
                    "retired TX result differs from last retained result",
                )
                forget = one(
                    audits,
                    "retirement",
                    "forget",
                    gen,
                    index,
                    retired["line"],
                    closed["line"],
                )
                require(
                    forget["fields"].get("ret") == "0",
                    "completed TX audit was not explicitly forgotten",
                )
                gone = one(
                    audits,
                    "retirement",
                    "forgotten",
                    gen,
                    index,
                    forget["line"],
                    closed["line"],
                )
                require(
                    gone["fields"].get("result_ret") == "-61",
                    "forgotten TX result still available",
                )
    require(
        len(used) == len(registrations) + len(audits),
        "extra or unowned TX registration/audit marker",
    )


def _procedure_ids(event):
    value = event["fields"].get("ids")
    require(
        type(value) is str and re.fullmatch(r"[0-9]+(?:,[0-9]+)?", value),
        "invalid procedure dynamic ID list",
    )
    ids = tuple(int(part) for part in value.split(","))
    require(len(set(ids)) == len(ids), "reused ASE in procedure ID list")
    return ids


def _procedures(client, discoveries):
    """Require exactly one owned CP notification per complete local procedure."""
    pending = None
    next_id = 1
    raw_owners = {}
    helper_count = 0
    retired = {
        _kv(e, "generation"): e
        for e in client
        if e["marker"] == "ASCS_CLEANUP" and e["fields"].get("retired") == "1"
    }
    for event in client:
        marker = event["marker"]
        if marker == "ASCS_PROCEDURE_BEGIN":
            require(
                pending is None and _kv(event, "procedure") == next_id,
                "missing/nested/noncontiguous procedure",
            )
            next_id += 1
            gen = _kv(event, "generation")
            require(
                gen in discoveries and discoveries[gen][0]["line"] < event["line"],
                "procedure before fresh generation discovery",
            )
            owner = event["fields"].get("owner")
            require(owner in ("raw", "helper"), "procedure owner undeclared")
            opcode = _kv(event, "opcode", 255)
            ids = _procedure_ids(event)
            tx = _kv(event, "transaction")
            known_ids = discoveries[gen][1]
            if owner == "helper":
                if opcode in (1, 3):
                    require(
                        tx == 0 and ids in ((known_ids["A"][0],), (known_ids["B"][0],)),
                        "wrong one-ASE helper procedure",
                    )
                else:
                    require(
                        opcode == 2
                        and tx == 0
                        and ids == (known_ids["A"][0], known_ids["B"][0]),
                        "wrong ordered QoS helper procedure",
                    )
                helper_count += 1
            else:
                require(
                    1 <= tx <= 259
                    and tx not in raw_owners
                    and all(
                        i in (0, known_ids["A"][0], known_ids["B"][0]) for i in ids
                    ),
                    "raw procedure ID or transaction invalid",
                )
            pending = {
                "event": event,
                "gen": gen,
                "owner": owner,
                "opcode": opcode,
                "ids": ids,
                "tx": tx,
                "cp": None,
                "request": None,
                "write": None,
            }
        elif marker == "ASCS_REQUEST":
            require(
                pending is not None
                and pending["owner"] == "raw"
                and _kv(event, "transaction") == pending["tx"]
                and _kv(event, "generation") == pending["gen"]
                and _hex(event["fields"].get("raw"), "procedure request")[0]
                == pending["opcode"]
                and pending["request"] is None,
                "raw request without exact procedure owner",
            )
            pending["request"] = event
        elif marker == "ASCS_WRITE":
            require(
                pending is not None
                and pending["owner"] == "raw"
                and pending["request"] is not None
                and pending["write"] is None
                and _kv(event, "transaction") == pending["tx"]
                and _kv(event, "generation") == pending["gen"],
                "ATT completion without raw procedure owner",
            )
            pending["write"] = event
        elif marker == "ASCS_CP":
            require(
                pending is not None
                and pending["cp"] is None
                and event["fields"].get("owner") == pending["owner"]
                and _kv(event, "procedure") == _kv(pending["event"], "procedure")
                and _kv(event, "generation") == pending["gen"]
                and _kv(event, "transaction") == pending["tx"],
                "unsolicited/duplicate/mismatched CP procedure",
            )
            raw = _hex(event["fields"].get("raw"), "procedure CP response", 128)
            require(
                raw[0] == pending["opcode"]
                and pending["event"]["line"] < event["line"],
                "procedure CP opcode/window mismatch",
            )
            if pending["owner"] == "helper":
                expected = bytes((raw[0], len(pending["ids"]))) + b"".join(
                    bytes((ase, 0, 0)) for ase in pending["ids"]
                )
                require(
                    raw == expected, "helper CP opcode/count/order/code/reason mismatch"
                )
            else:
                require(
                    pending["request"] is not None
                    and pending["request"]["line"] < event["line"]
                    and len(raw) == 2 + len(pending["ids"]) * 3
                    and tuple(raw[offset] for offset in range(2, len(raw), 3))
                    == pending["ids"],
                    "raw CP record not owned by request/ASE list",
                )
            pending["cp"] = event
        elif marker == "ASCS_PROCEDURE_END":
            require(
                pending is not None
                and pending["cp"] is not None
                and _kv(event, "procedure") == _kv(pending["event"], "procedure")
                and _kv(event, "generation") == pending["gen"]
                and event["fields"].get("owner") == pending["owner"]
                and _kv(event, "opcode", 255) == pending["opcode"]
                and _procedure_ids(event) == pending["ids"]
                and _kv(event, "transaction") == pending["tx"]
                and pending["cp"]["line"] < event["line"]
                and (
                    pending["owner"] == "helper"
                    or pending["write"] is not None
                    and pending["write"]["line"] < event["line"]
                ),
                "procedure ended without owned CP and ATT completion",
            )
            closed = retired.get(pending["gen"])
            require(
                closed is not None and event["line"] < closed["line"],
                "procedure leaked across context retirement",
            )
            if pending["owner"] == "raw":
                raw_owners[pending["tx"]] = pending
            pending = None
    require(
        pending is None
        and raw_owners
        and helper_count
        and set(raw_owners) == set(range(1, len(raw_owners) + 1)),
        "incomplete raw/helper procedure population",
    )
    return raw_owners


def _metadata_echo(client, reads, discoveries, transactions):
    accepted = []
    for request, cp, expected, ids, following, case_start, case_id in transactions:
        raw = _hex(request["fields"].get("raw"), "metadata request")
        if raw[0] not in (3, 7) or not any(
            rec["code"] == 0 and rec["ase"] in ("A", "B") for rec in expected["records"]
        ):
            continue
        offset = 2
        parsed = {}
        for _ in range(raw[1]):
            require(offset + 2 <= len(raw), "metadata outer length invalid")
            ase_id, size = raw[offset : offset + 2]
            offset += 2
            require(
                ase_id not in parsed and offset + size <= len(raw),
                "metadata duplicate ID or truncation",
            )
            parsed[ase_id] = raw[offset : offset + size]
            offset += size
        require(offset == len(raw), "metadata trailing bytes")
        for rec in expected["records"]:
            if rec["code"] == 0 and rec["ase"] in ("A", "B"):
                ase = rec["ase"]
                gen = _kv(request, "generation")
                require(
                    ids[ase][0] in parsed,
                    "successful metadata record absent from request",
                )
                meta = parsed[ids[ase][0]]
                expected_state = (
                    3
                    if raw[0] == 3
                    or case_id == "metadata_unknown_enable_update_enabling"
                    else 4
                )
                require(
                    raw[0] == 3
                    or case_id
                    in (
                        "metadata_unknown_enable_update_enabling",
                        "metadata_update_streaming",
                    ),
                    "unexpected successful metadata Update case",
                )
                public_event, public = _near_read(
                    reads, gen, ase, cp["line"], following, latest=False
                )
                qos = [
                    value
                    for e, owner, symbol, value in reads
                    if owner == gen
                    and symbol == ase
                    and value[1] == 2
                    and case_start < e["line"] < public_event["line"]
                ]
                require(
                    qos
                    and public[1] == expected_state
                    and public[2:4] == qos[-1][2:4]
                    and public[4:] == bytes((len(meta),)) + meta,
                    f"{case_id}: successful metadata not in fresh public ASE read",
                )
                accepted.append((cp["line"], following, gen, ase, public_event, public))
    for event in client:
        if event["marker"] != "ASCS_METADATA":
            continue
        gen = _kv(event, "generation")
        require(gen in discoveries, "metadata echoed on stale generation")
        index = _kv(event, "index", 1)
        ase = ("A", "B")[index]
        raw = _hex(event["fields"].get("raw"), "public metadata")
        matching = [
            entry
            for entry in accepted
            if entry[0] < entry[4]["line"] < event["line"] < entry[1]
            and entry[2] == gen
            and entry[3] == ase
        ]
        require(len(matching) == 1, "metadata echo without matching public read")
        public = matching[0][5]
        require(
            raw
            == public
            == _near_read(reads, gen, ase, matching[0][0], event["line"])[1]
            and raw[1] in (3, 4)
            and raw[0] == discoveries[gen][1][ase][0]
            and _kv(event, "state") == raw[1],
            "public metadata echo mismatch",
        )
        require(
            _kv(event, "cig") == raw[2] and _kv(event, "cis") == raw[3],
            "metadata CIG/CIS not tied to public QoS",
        )


def _reconnect(case_events, case, begin, discoveries, reads):
    old_gen = _kv(begin, "generation")
    new_gen = old_gen + 1
    states = {
        "codec_configured_both": (1, 1),
        "qos_configured_both": (2, 2),
        "A_enabling_B_qos": (3, 2),
        "A_streaming_B_enabling_A_only_CIS_source_accepted_10": (4, 3),
    }[case["pre_disconnect"]]
    disconnected = _one(
        case_events, "ASCS_DISCONNECT", predicate=lambda e: "request_ret" in e["fields"]
    )
    observed = _one(
        case_events, "ASCS_DISCONNECT", predicate=lambda e: "observed" in e["fields"]
    )
    require(
        _kv(disconnected, "generation") == old_gen
        and _kv(disconnected, "request_ret") == 0
        and _kv(observed, "generation") == old_gen
        and _kv(observed, "observed") == _kv(observed, "default_conn_null") == 1
        and disconnected["line"] < observed["line"],
        "unowned ACL disconnect",
    )
    for index, ase in enumerate(("A", "B")):
        prior = _one(
            case_events,
            "ASCS_PRE_DISCONNECT",
            predicate=lambda e, n=index: e["fields"].get("index") == str(n),
        )
        require(
            _kv(prior, "step") == _kv(begin, "step")
            and prior["fields"].get("name") == case["id"]
            and _kv(prior, "generation") == old_gen
            and prior["line"] < disconnected["line"],
            "missing previous-generation state snapshot",
        )
        raw = _hex(prior["fields"].get("raw"), "pre-disconnect public state")
        require(
            raw == _near_read(reads, old_gen, ase, begin["line"], prior["line"])[1]
            and raw[1] == states[index] == _kv(prior, "state"),
            "wrong public pre-disconnect state",
        )
        release = _one(
            case_events,
            "ASCS_RELEASED",
            predicate=lambda e, n=index: (
                e["fields"].get("stage") == "acl" and e["fields"].get("index") == str(n)
            ),
        )
        require(
            _kv(release, "generation") == old_gen
            and _kv(release, "mask") == (1 if index == 0 else 3),
            "old stream released barrier mismatch",
        )
    barrier = _one(
        case_events,
        "ASCS_RELEASED",
        predicate=lambda e: e["fields"].get("barrier") == "both",
    )
    deleted = _one(case_events, "ASCS_GROUP_DELETE")
    reconnect = _one(case_events, "ASCS_RECONNECT")
    require(
        _kv(barrier, "generation") == old_gen
        and _kv(barrier, "mask") == 3
        and _kv(deleted, "generation") == old_gen
        and _kv(deleted, "ret") == 0
        and observed["line"] < barrier["line"] < deleted["line"] < reconnect["line"],
        "stream release/group ownership incomplete",
    )
    fresh, ids = discoveries[new_gen]
    require(
        _kv(reconnect, "old_generation") == old_gen
        and _kv(reconnect, "new_generation") == new_gen
        and _kv(reconnect, "mtu") == 65
        and reconnect["fields"].get("idle") == "both"
        and fresh["line"] < reconnect["line"]
        and deleted["line"] < fresh["line"]
        and _kv(reconnect, "cp") == _kv(fresh, "cp")
        and _kv(reconnect, "ccc") == _kv(fresh, "ccc")
        and reconnect["fields"].get("ase0") == fresh["fields"].get("ase0")
        and reconnect["fields"].get("ase1") == fresh["fields"].get("ase1"),
        "new generation not publicly rediscovered",
    )
    for ase in ("A", "B"):
        idle = _near_read(reads, new_gen, ase, fresh["line"], reconnect["line"])[1]
        require(idle == bytes((ids[ase][0], 0)), "fresh ASE not Idle")
    if case["id"] == "reconnect_after_partial_streaming":
        partial = _one(
            case_events,
            "ASCS_PARTIAL_SOURCE",
            predicate=lambda e: "accepted" in e["fields"],
        )
        unregister = _one(
            case_events,
            "ASCS_PARTIAL_SOURCE",
            predicate=lambda e: "unregister" in e["fields"],
        )
        require(
            _kv(partial, "generation") == old_gen
            and _kv(partial, "index") == 0
            and _kv(partial, "accepted") == _kv(partial, "expected") == 10
            and partial["fields"].get("peer_delivery") == "unproved"
            and _kv(unregister, "unregister") == 0
            and unregister["line"] < disconnected["line"],
            "partial source acceptance/cleanup mismatch",
        )


def _closure(client, discoveries, final):
    for gen, (discovery, _) in discoveries.items():
        null = _one(
            client,
            "ASCS_CP_CLOSE",
            predicate=lambda e, g=gen: (
                e["fields"].get("generation") == str(g)
                and e["fields"].get("notify") == "NULL"
            ),
        )
        unsub = _one(
            client,
            "ASCS_CP_CLOSE",
            predicate=lambda e, g=gen: (
                e["fields"].get("generation") == str(g) and "unsubscribe" in e["fields"]
            ),
        )
        cleaned = _one(
            client,
            "ASCS_CLEANUP",
            predicate=lambda e, g=gen: e["fields"].get("generation") == str(g),
        )
        require(
            _kv(null, "closing") == 1
            and _kv(unsub, "unsubscribe") == 0
            and _kv(cleaned, "retired") == _kv(cleaned, "cp_closed") == 1
            and discovery["line"] < null["line"] <= unsub["line"] < cleaned["line"]
            and cleaned["line"] < final["line"],
            "CP/connection cleanup incomplete",
        )


def check_family_logs(policy, family, client_bytes, receiver_bytes):
    """Independently check encoded peer traces; this proves no OS execution."""
    require(type(policy) is dict and type(family) is str, "invalid trace arguments")
    matches = [f for f in policy.get("families", []) if f.get("id") == family]
    require(len(matches) == 1, "undeclared family")
    spec = matches[0]
    client = _events(client_bytes, "01")
    receiver = _events(receiver_bytes, "00")
    windows = _case_windows(client, spec)
    discoveries, starts = _discovery(client, spec)
    reads = _reads(client, discoveries, starts)
    _family_diagnostics(receiver, client, windows, policy)
    _helper_cp(client, discoveries)
    raw_owners = _procedures(client, discoveries)
    final = _one(client, "ASCS_CLIENT")
    received = _one(receiver, "ASCS_RECEIVER")
    require(
        final["fields"].get("case") == family
        and _kv(final, "cases") == spec["case_count"]
        and _kv(final, "assertions") == spec["raw_exchanges"]
        and _kv(final, "phases") == _kv(received, "phases") == spec["render_phases"]
        and windows[-1][1]["line"] < final["line"]
        and "INFO: ASCS_CLIENT " in final["body"]
        and "INFO: ASCS_RECEIVER " in received["body"],
        "peer summaries not proven",
    )
    _closure(client, discoveries, final)
    _tx_audits(client, windows, discoveries)
    raw_requests = [e for e in client if e["marker"] == "ASCS_REQUEST"]
    cp_events = [
        e
        for e in client
        if e["marker"] == "ASCS_CP" and e["fields"].get("transaction") != "0"
    ]
    writes = [e for e in client if e["marker"] == "ASCS_WRITE"]
    count = spec["raw_exchanges"]
    require(
        len(raw_requests) == len(cp_events) == len(writes) == len(raw_owners) == count
        and [_kv(e, "transaction") for e in raw_requests] == list(range(1, count + 1)),
        "missing/extra/noncontiguous raw transactions",
    )
    cp_by_tx = {}
    write_by_tx = {}
    for event, dest in [(e, cp_by_tx) for e in cp_events] + [
        (e, write_by_tx) for e in writes
    ]:
        tx = _kv(event, "transaction")
        require(tx not in dest, "duplicate CP/ATT transaction")
        dest[tx] = event
    transactions = []
    phase_seen = set()
    for begin, end, case in windows:
        part = [e for e in client if begin["line"] <= e["line"] <= end["line"]]
        requests = [e for e in part if e["marker"] == "ASCS_REQUEST"]
        require(
            len(requests) == len(case["actions"]),
            f"{case['id']}: missing/extra request",
        )
        for key, expected in (
            ("assertions", len(requests)),
            ("exchanges", len(requests)),
            ("records", sum(len(a["response"]["records"]) for a in case["actions"])),
        ):
            if key in end["fields"]:
                require(_kv(end, key) == expected, f"unproved CASE_END {key}")
        last_negative = -1
        for i, (action, req) in enumerate(zip(case["actions"], requests)):
            tx = _kv(req, "transaction")
            cp, write = cp_by_tx.get(tx), write_by_tx.get(tx)
            require(cp is not None and write is not None, "missing CP/ATT completion")
            require(
                raw_owners[tx]["request"] is req
                and raw_owners[tx]["cp"] is cp
                and raw_owners[tx]["write"] is write,
                "policy action lacks one complete owned raw procedure",
            )
            gen = _kv(begin, "generation") + action.get("generation_delta", 0)
            require(
                gen in discoveries
                and _kv(req, "generation") == gen
                and _kv(cp, "generation") == _kv(write, "generation") == gen,
                "stale CP/ATT generation",
            )
            following = (
                requests[i + 1]["line"] if i + 1 < len(requests) else end["line"]
            )
            require(
                begin["line"] < req["line"] < cp["line"] < following
                and req["line"] < write["line"] < following
                and _kv(write, "att_error") == _kv(write, "ret") == 0,
                "CP/ATT missing, late or failed",
            )
            ids = discoveries[gen][1]
            qos = {}
            for ase in ("A", "B"):
                prior = [
                    raw
                    for e, owner, symbol, raw in reads
                    if owner == gen
                    and symbol == ase
                    and begin["line"] < e["line"] < req["line"]
                    and raw[1] == 2
                ]
                if prior:
                    qos[ase] = prior[-1]
            expected_raw = _request_bytes(
                action["request"], ids, qos, policy["profile"]
            )
            require(
                len(expected_raw) <= 62
                and _hex(req["fields"].get("raw"), "raw request", 62) == expected_raw,
                f"{case['id']}: wrong encoded request",
            )
            named_release = case["id"].startswith("release_from_") and i == (
                1 if case["id"] == "release_from_enabling" else 0
            )
            label = (
                "legal-release"
                if action["request"].startswith("hex:0801")
                and action["response"]["records"][0]["code"] == 0
                and not named_release
                else case["id"]
            )
            require(req["fields"].get("name") == label, "wrong request case name")
            require(
                _hex(cp["fields"].get("raw"), "CP response")
                == _response_bytes(action["response"], ids),
                f"{case['id']}: wrong ordered CP response",
            )
            if "pre_states" in action:
                _negative_states(
                    action, req, cp, following, part, reads, gen, ids, case
                )
                last_negative = cp["time"]
            transactions.append(
                (req, cp, action["response"], ids, following, begin["line"], case["id"])
            )
        phases = [e for e in part if e["marker"] == "ASCS_RECOVERY"]
        require(
            len(phases) == case["render_phase_delta"],
            f"{case['id']}: missing/extra recovery",
        )
        for i, event in enumerate(phases):
            number = _kv(event, "phase")
            require(
                number == _kv(begin, "recovery_phase") + i + 1
                and number not in phase_seen
                and _kv(event, "rendered") > 0
                and _kv(event, "generation") == _kv(end, "generation"),
                "invalid or reused recovery phase",
            )
            phase_seen.add(number)
            observed = [
                e
                for e in receiver
                if e["marker"] == "ASCS_RENDER"
                and _kv(e, "phase") == number
                and begin["time"] < e["time"] < end["time"]
            ]
            require(
                len(observed) == 1
                and _kv(observed[0], "pushes") == _kv(event, "rendered")
                and observed[0]["time"] <= event["time"],
                "missing actual receiver-rendered output",
            )
            for index in (0, 1):
                audits = [
                    e
                    for e in part
                    if e["marker"] == "ASCS_TX"
                    and e["fields"].get("phase") == str(number)
                    and e["fields"].get("index") == str(index)
                ]
                require(
                    len(audits) == 2
                    and sum("sends" in e["fields"] for e in audits) == 1
                    and sum("unregister" in e["fields"] for e in audits) == 1,
                    "missing/duplicate TX audit",
                )
                sent = next(e for e in audits if "sends" in e["fields"])
                unreg = next(e for e in audits if "unregister" in e["fields"])
                require(
                    _kv(sent, "sends") == _kv(sent, "expected") == 30
                    and _kv(unreg, "unregister") == 0
                    and _kv(sent, "generation")
                    == _kv(unreg, "generation")
                    == _kv(end, "generation")
                    and event["line"] < sent["line"] < unreg["line"] < end["line"],
                    "wrong stream send or unregister",
                )
        require(
            last_negative < phases[-1]["time"], "no post-rejection rendered recovery"
        )
        if case["render_phase_delta"] == 2:
            require(
                phases[0]["time"] < last_negative,
                "missing pre-rejection initial streaming render",
            )
        # Final A/B releases must clean up the just-rendered recovery phase.
        require(
            len(requests) >= 2
            and all(req["line"] > phases[-1]["line"] for req in requests[-2:])
            and [action["request"] for action in case["actions"][-2:]]
            == ["hex:0801{A}", "hex:0801{B}"],
            f"{case['id']}: missing post-render release for both streams",
        )
        if family == "reconnect":
            _reconnect(part, case, begin, discoveries, reads)
        for ase in ("A", "B"):
            gen = _kv(end, "generation")
            idle = _near_read(reads, gen, ase, phases[-1]["line"], end["line"])[1]
            require(
                idle == bytes((discoveries[gen][1][ase][0], 0)),
                "final public ASE not Idle",
            )
    require(
        len(transactions) == count
        and phase_seen == set(range(1, spec["render_phases"] + 1))
        and len([e for e in receiver if e["marker"] == "ASCS_RENDER"])
        == len(phase_seen),
        "incomplete family trace population",
    )
    _metadata_echo(client, reads, discoveries, transactions)
    return {
        "ok": True,
        "family": family,
        "cases": spec["case_count"],
        "render_phases": len(phase_seen),
        "raw_exchanges": len(transactions),
        "response_records": sum(
            len(a["response"]["records"]) for c in spec["cases"] for a in c["actions"]
        ),
        "client_sha256": sha256(client_bytes),
        "receiver_sha256": sha256(receiver_bytes),
        "claim": "trace-consistency-only; OS execution not established",
    }


def _snapshot_with_identity(path, limit):
    require(
        type(path) is str and Path(path).is_absolute(), "snapshot path must be absolute"
    )
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            info = os.fstat(fd)
            require(
                stat.S_ISREG(info.st_mode) and info.st_size <= limit,
                "snapshot not a bounded regular file",
            )
            with os.fdopen(fd, "rb") as stream:
                fd = None
                data = stream.read(limit + 1)
            require(len(data) <= limit, "snapshot exceeds limit")
            return data, (info.st_dev, info.st_ino)
        finally:
            if fd is not None:
                os.close(fd)
    except (OSError, ValueError) as exc:
        raise TraceError(f"unsafe or oversized snapshot {path}: {exc}") from exc


def _regular_snapshot(path, limit):
    return _snapshot_with_identity(path, limit)[0]


def _identity(entry, raw, label, *, image=False):
    fields(entry, {"path", "bytes", "sha256"}, label)
    require(
        type(entry["path"]) is str and Path(entry["path"]).is_absolute(),
        f"{label} path not absolute",
    )
    require(
        exact_int(
            entry["bytes"],
            1 if image else 0,
            IMAGE_LIMIT if image else LOG_LIMIT,
            f"{label}.bytes",
        )
        == len(raw)
        and type(entry["sha256"]) is str
        and SHA.fullmatch(entry["sha256"])
        and entry["sha256"] == sha256(raw),
        f"{label} bytes/hash mismatch",
    )


def image_abi(raw, role):
    """Checked peer ABI: encrypted CPU peers are 32-bit; PHY is 64-bit."""
    require(role in ("receiver", "client", "phy"), "unknown image role")
    phy = role == "phy"
    require(
        len(raw) >= (64 if phy else 52)
        and raw[:4] == b"\x7fELF"
        and raw[4:6] == (b"\x02\x01" if phy else b"\x01\x01")
        and struct.unpack_from("<H", raw, 18)[0] == (62 if phy else 3),
        f"{role} image is not {'ELF64 x86_64' if phy else 'ELF32 i386'}",
    )


def check_execution_record(raw, policy, family, run_id, client_path, receiver_path):
    """Check caller-owned execution identity separately from pure trace bytes."""
    require(
        type(run_id) is str and RUN_ID.fullmatch(run_id),
        "invalid independently supplied run ID",
    )
    record = strict_json(raw, EXECUTION_LIMIT)
    fields(
        record,
        {
            "schema_version",
            "run_id",
            "family",
            "images",
            "participants",
            "logs",
            "scope",
        },
        "execution record",
    )
    require(
        exact_int(record["schema_version"], 1, 1, "execution schema") == 1
        and record["run_id"] == run_id
        and record["family"] == family
        and any(item["id"] == family for item in policy["families"]),
        "execution family/run ID mismatch",
    )
    for key in ("images", "participants", "logs"):
        require(
            type(record[key]) is dict
            and set(record[key]) == {"receiver", "client", "phy"},
            f"missing/extra execution {key}",
        )
    require(
        type(client_path) is str
        and type(receiver_path) is str
        and Path(client_path).is_absolute()
        and Path(receiver_path).is_absolute()
        and record["logs"]["client"]["path"] == client_path
        and record["logs"]["receiver"]["path"] == receiver_path,
        "CLI peer log paths do not match execution record",
    )
    contents = {}
    images = {}
    pids = set()
    file_ids = set()
    intervals = []
    session = f"ascs_{run_id}_{family}"
    for role in ("receiver", "client", "phy"):
        image = record["images"][role]
        fields(image, {"path", "bytes", "sha256"}, f"{role} image")
        binary, image_id = _snapshot_with_identity(image["path"], IMAGE_LIMIT)
        require(image_id not in file_ids, "image/log snapshots share a file")
        file_ids.add(image_id)
        _identity(image, binary, f"{role} image", image=True)
        image_abi(binary, role)
        images[role] = image["sha256"]
        log = record["logs"][role]
        fields(log, {"path", "bytes", "sha256"}, f"{role} log")
        contents[role], log_id = _snapshot_with_identity(log["path"], LOG_LIMIT)
        require(log_id not in file_ids, "image/log snapshots share a file")
        file_ids.add(log_id)
        _identity(log, contents[role], f"{role} log")
        if role == "phy":
            require(
                not re.search(
                    rb"(?i)(?:\b(?:warning|error|fatal|failed)\b|<\s*(?:wrn|err)\s*>)",
                    contents[role],
                ),
                "PHY log contains warning/error/fatal marker",
            )
        participant = record["participants"][role]
        fields(
            participant,
            {
                "schema_version",
                "argv",
                "pid",
                "start_time",
                "end_time",
                "returncode",
                "ok",
                "timed_out",
                "cancelled_signal",
                "log_limit_exceeded",
                "bytes_logged",
                "log_sha256",
                "cleanup_errors",
                "error",
                "descendant_cleanup_required",
            },
            f"{role} process",
        )
        require(
            exact_int(participant["schema_version"], 1, 1, "owner schema") == 1,
            "invalid owned process schema",
        )
        pid = exact_int(participant["pid"], 1, 2147483647, "owned process pid")
        require(pid not in pids, "duplicate owned process PID")
        pids.add(pid)
        require(
            type(participant["start_time"]) in (int, float)
            and type(participant["end_time"]) in (int, float)
            and math.isfinite(participant["start_time"])
            and math.isfinite(participant["end_time"])
            and participant["start_time"] > 0
            and participant["end_time"] > participant["start_time"],
            "invalid owned process timestamps",
        )
        intervals.append((participant["start_time"], participant["end_time"]))
        args = [image["path"], "-v=2", f"-s={session}"]
        if role == "phy":
            # Exact installed-PHY argv including -nodump: p2G4_main.c opens
            # default dump files unless the flag disables them.
            args += ["-D=2", "-nodump", "-sim_length=250e6"]
        else:
            args += [
                f"-d={0 if role == 'receiver' else 1}",
                f"-testid={family}",
                "-RealEncryption=1",
                f"-rs={23 if role == 'receiver' else 28}",
            ]
        require(
            participant["argv"] == args
            and type(participant["returncode"]) is int
            and participant["returncode"] == 0
            and participant["ok"] is True
            and participant["timed_out"] is False
            and participant["cancelled_signal"] is None
            and participant["log_limit_exceeded"] is False
            and participant["descendant_cleanup_required"] is False
            and participant["cleanup_errors"] == []
            and participant["error"] is None
            and type(participant["bytes_logged"]) is int
            and participant["bytes_logged"] == log["bytes"]
            and participant["log_sha256"] == log["sha256"],
            f"{role} process not owned/complete/exact argv",
        )
    scope = record["scope"]
    fields(
        scope,
        {
            "owner_pid",
            "saved_flag",
            "restored_flag",
            "adopted",
            "actions",
            "errors",
            "unexpected_live_descendants",
            "cancelled_signal",
            "ok",
        },
        "descendant scope",
    )
    owner_pid = exact_int(scope["owner_pid"], 1, 2147483647, "scope PID")
    require(
        owner_pid not in pids
        and max(start for start, _ in intervals) < min(end for _, end in intervals)
        and type(scope["saved_flag"]) is int
        and scope["saved_flag"] in (0, 1)
        and type(scope["restored_flag"]) is int
        and scope["saved_flag"] == scope["restored_flag"]
        and type(scope["adopted"]) is list
        and type(scope["actions"]) is list
        and scope["errors"] == []
        and scope["unexpected_live_descendants"] is False
        and scope["cancelled_signal"] is None
        and scope["ok"] is True,
        "descendant scope did not close",
    )
    adopted = set()
    for entry in scope["adopted"]:
        fields(entry, {"pid", "ppid", "start_ticks"}, "adopted descendant")
        child = exact_int(entry["pid"], 1, 2147483647, "adopted PID")
        parent = exact_int(entry["ppid"], 1, 2147483647, "adopted PPID")
        birth = exact_int(
            entry["start_ticks"], 1, 0x7FFFFFFFFFFFFFFF, "adopted start ticks"
        )
        require(
            parent == owner_pid
            and child != owner_pid
            and (child, birth) not in adopted,
            "invalid adopted descendant identity",
        )
        adopted.add((child, birth))
    reaped = set()
    for action in scope["actions"]:
        fields(action, {"pid", "start_ticks", "wait_status"}, "normal descendant reap")
        child = exact_int(action["pid"], 1, 2147483647, "reaped PID")
        birth = exact_int(
            action["start_ticks"], 1, 0x7FFFFFFFFFFFFFFF, "reaped start ticks"
        )
        status = exact_int(action["wait_status"], 0, 0x7FFFFFFF, "wait status")
        require(
            (child, birth) in adopted
            and (child, birth) not in reaped
            and os.WIFEXITED(status)
            and os.WEXITSTATUS(status) == 0,
            "unexpected descendant cleanup or nonzero reap",
        )
        reaped.add((child, birth))
    require(adopted == reaped, "unreaped adopted descendant")
    require(
        not any(
            record["logs"][a]["path"] == record["logs"][b]["path"]
            for a, b in (("client", "receiver"), ("client", "phy"), ("receiver", "phy"))
        ),
        "shared peer log paths",
    )
    all_paths = [
        record[key][role]["path"]
        for key in ("images", "logs")
        for role in ("receiver", "client", "phy")
    ]
    require(len(all_paths) == len(set(all_paths)), "image/log snapshots share a path")
    return {
        "record_sha256": sha256(raw),
        "images": images,
        "logs": {role: record["logs"][role]["sha256"] for role in contents},
        "client": contents["client"],
        "receiver": contents["receiver"],
        "run_id": run_id,
    }
