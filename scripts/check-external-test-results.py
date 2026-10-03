#!/usr/bin/env python3
"""PB-052: fail-closed external report accounting, not execution attestation.

The caller owns trusted inventory selection, actual execution and immutable
evidence retention. This CLI never runs external tests or privileged helpers.
"""

import argparse
from collections import Counter
from datetime import datetime
from decimal import Decimal, InvalidOperation
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

PRODUCERS = {
    "bluez-tester-v1": {
        "name": "bluez",
        "revision": "4dc15be8ee3f7422d447087f1893d215575cb2c8",
    },
    "pytest-xunit2-v1": {"name": "pytest", "version": "8.4.2"},
}
MAX_BYTES = 16 * 1024 * 1024
MAX_CASES = 100000
SGR = re.compile(r"\x1b\[[0-9;]*m")
NUMBER = r"(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?"


class Reject(Exception):
    def __init__(self, code, detail, invalid=False):
        super().__init__(detail)
        self.code, self.detail, self.invalid = code, detail, invalid


def require(condition, code, detail, invalid=False):
    if not condition:
        raise Reject(code, detail, invalid)


def fields(obj, names, label):
    require(
        type(obj) is dict and set(obj) == set(names),
        "schema",
        f"{label}: expected fields {sorted(names)}",
        True,
    )


def text(value, label):
    require(
        type(value) is str
        and bool(value.strip())
        and not any(ord(c) < 32 for c in value),
        "schema",
        f"{label}: nonempty control-free string required",
        True,
    )
    return value


def unique(values, label):
    require(
        len(values) == len(set(values)), "duplicate", f"{label}: duplicate identity"
    )


def digest(value, label):
    require(
        type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value),
        "schema",
        f"{label}: lowercase SHA-256 required",
        True,
    )


def read_input(path, label, inputs):
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    require(
        len(data) <= MAX_BYTES,
        "size-limit",
        f"{label}: exceeds {MAX_BYTES} bytes",
        True,
    )
    inputs[label] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    return data


def pairs(items):
    result = {}
    for key, value in items:
        require(
            key not in result, "duplicate-json-key", f"JSON key repeated: {key}", True
        )
        result[key] = value
    return result


def load_json(data):
    return json.loads(
        data.decode("utf-8"),
        object_pairs_hook=pairs,
        parse_constant=lambda value: (_ for _ in ()).throw(
            Reject("schema", f"Nonfinite JSON number: {value}", True)
        ),
    )


def case_id(value, profile):
    width = 1 if profile == "bluez-tester-v1" else 2
    require(
        type(value) is list and len(value) == width,
        "schema",
        f"Case ID must have {width} exact string component(s)",
        True,
    )
    return tuple(text(component, "case component") for component in value)


def validate_policy(policy):
    fields(
        policy,
        (
            "schema_version",
            "suite",
            "profile",
            "producer",
            "required",
            "exclusions",
            "prerequisites",
            "prerequisite_rationale",
        ),
        "inventory",
    )
    require(
        type(policy["schema_version"]) is int and policy["schema_version"] == 1,
        "schema",
        "Unsupported inventory version",
        True,
    )
    text(policy["suite"], "suite")
    profile = policy["profile"]
    require(
        type(profile) is str and profile in PRODUCERS,
        "profile",
        "Unsupported report profile",
        True,
    )
    require(
        policy["producer"] == PRODUCERS[profile], "producer", "Unreviewed producer pin"
    )
    require(
        type(policy["required"]) is list and 0 < len(policy["required"]) <= MAX_CASES,
        "inventory",
        "Nonempty bounded required case list required",
        True,
    )
    required = [case_id(value, profile) for value in policy["required"]]
    require(
        type(policy["exclusions"]) is list, "schema", "exclusions must be a list", True
    )
    excluded = []
    for entry in policy["exclusions"]:
        fields(entry, ("id", "reason"), "exclusion")
        text(entry["reason"], "exclusion reason")
        excluded.append(case_id(entry["id"], profile))
    unique(required + excluded, "reviewed case universe")
    require(
        len(required) + len(excluded) <= MAX_CASES,
        "size-limit",
        "Case universe too large",
        True,
    )
    require(
        type(policy["prerequisites"]) is list,
        "schema",
        "prerequisites must be a list",
        True,
    )
    prerequisites = [
        text(value, "prerequisite ID") for value in policy["prerequisites"]
    ]
    unique(prerequisites, "prerequisites")
    text(policy["prerequisite_rationale"], "prerequisite rationale")
    return required, excluded, prerequisites


def normal_success(record, label):
    require(
        record["termination"] == "exited"
        and type(record["exit_code"]) is int
        and record["exit_code"] == 0,
        "execution",
        f"{label}: normal child exit 0 required",
    )


def validate_run(run, policy, required, excluded, prerequisites, inputs):
    fields(
        run,
        (
            "schema_version",
            "run_id",
            "suite",
            "producer",
            "inventory_sha256",
            "discovered_cases",
            "child",
            "prerequisites",
            "report",
        ),
        "run record",
    )
    require(
        type(run["schema_version"]) is int and run["schema_version"] == 1,
        "schema",
        "Unsupported run record version",
        True,
    )
    text(run["run_id"], "run ID")
    require(
        run["suite"] == policy["suite"] and run["producer"] == policy["producer"],
        "producer",
        "Run suite/producer differs from pinned inventory",
    )
    digest(run["inventory_sha256"], "run inventory digest")
    require(
        run["inventory_sha256"] == inputs["inventory"]["sha256"],
        "inventory-hash",
        "Run record inventory hash mismatch",
    )
    require(
        type(run["discovered_cases"]) is list
        and len(run["discovered_cases"]) <= MAX_CASES,
        "schema",
        "discovered_cases must be a bounded list",
        True,
    )
    discovered = [
        case_id(value, policy["profile"]) for value in run["discovered_cases"]
    ]
    unique(discovered, "discovered cases")
    require(
        set(discovered) == set(required + excluded),
        "inventory-drift",
        "Discovered universe differs from reviewed required/excluded partition",
    )
    child = run["child"]
    fields(
        child,
        ("argv", "cwd", "started_at", "ended_at", "termination", "exit_code"),
        "child",
    )
    require(
        type(child["argv"]) is list and bool(child["argv"]),
        "schema",
        "Nonempty argv required",
        True,
    )
    for argument in child["argv"]:
        text(argument, "argv component")
    require(
        Path(text(child["cwd"], "child cwd")).is_absolute(),
        "schema",
        "Absolute cwd required",
        True,
    )
    start, end = (
        datetime.fromisoformat(text(child[key], key))
        for key in ("started_at", "ended_at")
    )
    require(
        start.tzinfo is not None and end.tzinfo is not None and end >= start,
        "schema",
        "Ordered timezone-aware timestamps required",
        True,
    )
    normal_success(child, "test child")
    if policy["profile"] == "pytest-xunit2-v1":
        command = Path(child["argv"][0]).name
        if command in ("pytest", "py.test"):
            options = child["argv"][1:]
        elif re.fullmatch(r"python(?:3(?:\.[0-9]+)?)?", command) and child["argv"][
            1:3
        ] == ["-m", "pytest"]:
            options = child["argv"][3:]
        else:
            raise Reject("pytest-policy", "Supported explicit pytest launcher required")
        # After --, a token is a path, not an enabled pytest option. An honest
        # run can otherwise XPASS a test in a directory named --runxfail.
        options = options[: options.index("--")] if "--" in options else options
        require(
            "--runxfail" in options,
            "pytest-policy",
            "pytest argv must include --runxfail; JUnit cannot detect non-strict XPASS",
        )
    require(
        type(run["prerequisites"]) is list,
        "schema",
        "Run prerequisites must be a list",
        True,
    )
    ids = []
    for entry in run["prerequisites"]:
        fields(entry, ("id", "termination", "exit_code"), "prerequisite result")
        ids.append(text(entry["id"], "prerequisite ID"))
        normal_success(entry, f"prerequisite {entry['id']}")
    unique(ids, "prerequisite results")
    require(
        set(ids) == set(prerequisites),
        "prerequisites",
        "Required prerequisite set mismatch",
    )
    fields(run["report"], ("bytes", "sha256"), "report identity")
    digest(run["report"]["sha256"], "report digest")
    require(
        type(run["report"]["bytes"]) is int and run["report"] == inputs["report"],
        "report-hash",
        "Report size/hash does not match raw bytes being parsed",
    )


def finite_number(value):
    try:
        result = Decimal(value)
    except InvalidOperation:
        raise Reject("report-format", "Invalid numeric report field") from None
    require(
        result.is_finite() and result >= 0,
        "report-format",
        "Nonnegative finite number required",
    )
    return result


def bluez_report(raw):
    content = SGR.sub("", raw.decode("utf-8"))
    require(
        "\x1b" not in content, "report-format", "Unsupported terminal escape sequence"
    )
    lines = content.splitlines()
    headers = [index for index, line in enumerate(lines) if line == "Test Summary"]
    require(
        len(headers) == 1,
        "report-format",
        "Exactly one complete BlueZ summary required",
    )
    index = headers[0] + 1
    require(
        index < len(lines) and lines[index] == "------------",
        "report-format",
        "Missing summary separator",
    )
    cases = []
    index += 1
    while index < len(lines) and not lines[index].startswith("Total:"):
        match = re.fullmatch(
            r"(.+?)\s+(Not Run|Passed|Failed|Timed out)\s*(" + NUMBER + r" seconds)?",
            lines[index],
        )
        require(
            match is not None, "report-format", f"Malformed summary row: {lines[index]}"
        )
        assert match is not None
        name, status, duration = match.groups()
        duration = duration or ""
        # The producer pads short labels; widths are minimums, not truncation.
        require(
            name and name == name.strip(), "report-format", "Ambiguous padded case name"
        )
        if status == "Not Run":
            require(
                not duration, "report-format", "Not Run cannot have execution duration"
            )
        else:
            require(
                re.fullmatch(NUMBER + r" seconds", duration),
                "report-format",
                "Missing execution duration",
            )
            finite_number(duration.split()[0])
        cases.append(
            (
                (name,),
                {
                    "Passed": "pass",
                    "Failed": "failure",
                    "Timed out": "timeout",
                    "Not Run": "not-run",
                }[status],
            )
        )
        require(len(cases) <= MAX_CASES, "size-limit", "Too many report cases")
        index += 1
    require(index < len(lines), "report-format", "Missing BlueZ aggregate")
    aggregate = re.fullmatch(
        r"Total: (\d+), Passed: (\d+) \((\d+\.\d)%\), Failed: (\d+), Not Run: (\d+)",
        lines[index],
    )
    require(aggregate is not None, "report-format", "Malformed BlueZ aggregate")
    assert aggregate is not None
    total, passed, percent, failed, not_run = aggregate.groups()
    counts = Counter(status for _, status in cases)
    require(
        (int(total), int(passed), int(failed), int(not_run))
        == (
            len(cases),
            counts["pass"],
            counts["failure"] + counts["timeout"],
            counts["not-run"],
        ),
        "report-counts",
        "BlueZ rows/aggregate disagree",
    )
    # printf uses platform rounding for one decimal: bound half-unit formatting
    # error instead of assuming Decimal's rounding mode. Counts remain exact.
    actual = Decimal(100) * int(passed) / int(total) if int(total) else Decimal(0)
    require(
        abs(Decimal(percent) - actual) <= Decimal("0.05"),
        "report-counts",
        "Pass percentage disagrees",
    )
    require(
        index + 1 < len(lines)
        and re.fullmatch(
            r"Overall execution time: " + NUMBER + r" seconds", lines[index + 1]
        ),
        "report-format",
        "Missing terminal execution time",
    )
    require(
        not any(line.strip() for line in lines[index + 2 :]),
        "report-format",
        "Unexpected trailing report content",
    )
    return cases


def junit_report(raw):
    content = raw.decode("utf-8")
    require(
        "<!DOCTYPE" not in content.upper() and "<!ENTITY" not in content.upper(),
        "unsafe-xml",
        "DTD/entity declarations are forbidden",
    )
    root = ET.fromstring(content)
    require(
        root.attrib == {"name": "pytest tests"},
        "report-format",
        "Native pytest wrapper attributes required; parent aggregates unsupported",
    )
    require(
        root.tag == "testsuites" and len(root) == 1 and root[0].tag == "testsuite",
        "report-format",
        "Native pytest single-suite wrapper required; nested/merged reports unsupported",
    )
    suite = root[0]
    require(
        set(suite.attrib)
        <= {
            "name",
            "tests",
            "failures",
            "errors",
            "skipped",
            "time",
            "timestamp",
            "hostname",
        },
        "report-format",
        "Unsupported pytest suite attributes",
    )
    cases = []
    for node in suite:
        if node.tag == "properties":
            require(
                all(child.tag == "property" and not len(child) for child in node),
                "report-format",
                "Malformed suite properties",
            )
            continue
        require(
            node.tag == "testcase",
            "report-format",
            "Unexpected suite child, possibly nested cases",
        )
        require(
            set(node.attrib) == {"classname", "name", "time"},
            "report-format",
            "Native xunit2 testcase attributes required",
        )
        name = (
            text(node.get("classname"), "testcase classname"),
            text(node.get("name"), "testcase name"),
        )
        finite_number(node.get("time", ""))
        outcomes = [
            child.tag for child in node if child.tag in ("failure", "error", "skipped")
        ]
        require(
            len(outcomes) <= 1,
            "report-format",
            "Repeated/conflicting testcase outcomes",
        )
        for child in node:
            require(
                child.tag
                in (
                    "failure",
                    "error",
                    "skipped",
                    "system-out",
                    "system-err",
                    "properties",
                ),
                "report-format",
                "Unexpected testcase child",
            )
            require(
                not list(child.iter("testcase")) and not list(child.iter("testsuite")),
                "report-format",
                "Nested hidden execution cases",
            )
            require(
                (
                    child.tag == "properties"
                    and all(
                        entry.tag == "property" and not len(entry) for entry in child
                    )
                )
                or (child.tag != "properties" and not len(child)),
                "report-format",
                "Nested unexpected diagnostic elements",
            )
        cases.append((name, outcomes[0] if outcomes else "pass"))
        require(len(cases) <= MAX_CASES, "size-limit", "Too many report cases")
    totals = []
    for key in ("tests", "failures", "errors", "skipped"):
        value = suite.get(key, "")
        require(
            re.fullmatch(r"[0-9]+", value),
            "report-counts",
            f"Missing/malformed pytest {key} count",
        )
        totals.append(int(value))
    counts = Counter(status for _, status in cases)
    require(
        totals == [len(cases), counts["failure"], counts["error"], counts["skipped"]],
        "report-counts",
        "pytest summary differs from rows (phase-expanded failures cannot be accepted)",
    )
    finite_number(suite.get("time", ""))
    return cases


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", required=True)
    parser.add_argument("--expected-inventory-sha256", required=True)
    parser.add_argument("--run-record", required=True)
    parser.add_argument("--report", required=True)
    args = parser.parse_args(argv)
    result = {
        "schema_version": 1,
        "accepted": False,
        "inputs": {},
        "cases": [],
        "errors": [],
    }
    status = 0
    try:
        inventory = read_input(args.inventory, "inventory", result["inputs"])
        digest(args.expected_inventory_sha256, "trusted inventory digest")
        require(
            result["inputs"]["inventory"]["sha256"] == args.expected_inventory_sha256,
            "inventory-anchor",
            "Inventory differs from independently trusted policy digest",
        )
        record = read_input(args.run_record, "run_record", result["inputs"])
        report = read_input(args.report, "report", result["inputs"])
        policy, run = load_json(inventory), load_json(record)
        required, excluded, prerequisites = validate_policy(policy)
        result["suite"], result["profile"], result["run_id"] = (
            policy["suite"],
            policy["profile"],
            run.get("run_id") if type(run) is dict else None,
        )
        result["excluded"] = policy["exclusions"]
        validate_run(run, policy, required, excluded, prerequisites, result["inputs"])
        cases = (
            bluez_report(report)
            if policy["profile"] == "bluez-tester-v1"
            else junit_report(report)
        )
        result["cases"] = [
            {"id": list(identity), "outcome": outcome} for identity, outcome in cases
        ]
        require(cases, "zero-execution", "No execution cases in report")
        ids = [identity for identity, _ in cases]
        unique(ids, "report cases")
        result["missing"] = [
            list(identity) for identity in sorted(set(required) - set(ids))
        ]
        result["unexpected"] = [
            list(identity) for identity in sorted(set(ids) - set(required))
        ]
        require(
            not result["missing"] and not result["unexpected"],
            "case-coverage",
            "Required cases missing or unselected/excluded/unreviewed cases reported",
        )
        require(
            all(outcome == "pass" for _, outcome in cases),
            "case-outcome",
            "Every selected required case must pass; skips/Not Run/failure/error/timeout are rejected",
        )
        result["accepted"] = True
        result["counts"] = {
            "required": len(required),
            "passed": len(cases),
            "excluded_unselected": len(excluded),
        }
    except Reject as exc:
        status = 2 if exc.invalid else 1
        result["errors"].append({"code": exc.code, "detail": exc.detail})
    except (
        OSError,
        UnicodeError,
        ValueError,
        TypeError,
        RecursionError,
        ET.ParseError,
    ) as exc:
        status = 2
        result["errors"].append({"code": "input", "detail": str(exc)})
    print(json.dumps(result, sort_keys=True, allow_nan=False))
    return status


if __name__ == "__main__":
    sys.exit(main())
