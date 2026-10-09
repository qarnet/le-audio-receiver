#!/usr/bin/env python3
"""Own PB-053 isolated pytest lane and reconcile native JUnit with PB-052."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

from bluez_guest_limits import read_bounded
from bluez_host_guest import (
    QEMU,
    REPO,
    exclusive,
    read_manifest,
    reject_constant,
    unique_pairs,
)
from bluez_host_process import run_owned
from bluez_host_descendants import DescendantScope


INVENTORY = REPO / "tests/host_bluez/inventory.json"
INVENTORY_SHA = "1de02a5083330284600ac740ab21a1f35ff76e5411377beb029ea251d324f276"
ACCOUNTANT = REPO / "scripts/check-external-test-results.py"
HOST = REPO / "scripts/bluez_host_guest.py"
CASE = "tests/host_bluez/test_host_lane.py"
CAP = 16 * 1024 * 1024


def bounded(path, cap=CAP):
    return read_bounded(path, cap)


def strict_json(raw):
    return json.loads(
        raw, object_pairs_hook=unique_pairs, parse_constant=reject_constant
    )


def validate_collection(raw, required):
    observed = strict_json(raw)
    if (
        not isinstance(observed, dict)
        or set(observed) != {"pytest_version", "nodeids", "discovered_cases"}
        or observed["pytest_version"] != "8.4.2"
    ):
        raise ValueError("Actual pytest collection schema/version invalid")
    nodeids, cases = observed["nodeids"], observed["discovered_cases"]

    def valid(value):
        return (
            isinstance(value, str)
            and bool(value.strip())
            and not any(ord(char) < 32 for char in value)
        )

    if (
        not isinstance(nodeids, list)
        or not isinstance(cases, list)
        or not all(valid(nodeid) for nodeid in nodeids)
        or any(
            not isinstance(case, list)
            or len(case) != 2
            or not all(valid(component) for component in case)
            for case in cases
        )
        or len(nodeids) != len(set(nodeids))
        or len(cases) != len(nodeids)
        or len(cases) != len({tuple(case) for case in cases})
        or {tuple(case) for case in cases} != {tuple(case) for case in required}
    ):
        raise ValueError("Actual pytest collection differs from reviewed universe")
    return observed


def intentional_rejection(checked, expected):
    process = checked["process"]
    return (
        process["returncode"] == 1
        and process["timed_out"] is False
        and process["cancelled_signal"] is None
        and process["log_limit_exceeded"] is False
        and process["descendant_cleanup_required"] is False
        and process["cleanup_errors"] == []
        and process["error"] is None
        and checked["accepted"] is False
        and [item["code"] for item in checked["verdict"]["errors"]] == [expected]
    )


def identity(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def timestamp(value):
    return datetime.fromtimestamp(value, timezone.utc).isoformat()


def execution(process):
    return {
        "termination": "exited" if process["ok"] else "failed",
        "exit_code": process["returncode"],
    }


def kvm_check():
    """Fixed read-only prerequisite child; no QEMU launch or host mutation."""
    result = subprocess.run(
        [str(QEMU), "--version"],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
    )
    if result.returncode != 0 or not result.stdout.startswith(
        "QEMU emulator version 11.0.2"
    ):
        raise ValueError("QEMU 11.0.2 version mismatch")
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        raise ValueError("Current user lacks /dev/kvm read/write")
    print(result.stdout.strip())


def reconcile(output, run_path, report_path, label):
    argv = [
        sys.executable,
        str(ACCOUNTANT),
        "--inventory",
        str(INVENTORY),
        "--expected-inventory-sha256",
        INVENTORY_SHA,
        "--run-record",
        str(run_path),
        "--report",
        str(report_path),
    ]
    result = run_owned(argv, output / (label + ".log"), 30, cwd=REPO)
    content = bounded(output / (label + ".log"))
    verdict = strict_json(content)
    return {
        "process": result,
        "log": str(output / (label + ".log")),
        "log_identity": identity(content),
        "verdict": verdict,
        "accepted": result["ok"] and verdict["accepted"] is True,
    }


def controls(output, run_record, raw):
    """Copy only accepted evidence; each changed input must be refused."""
    results = {}
    for label in ("missing", "skipped", "child-exit", "collection-shrink"):
        directory = output / ("control-" + label)
        directory.mkdir()
        copied = strict_json(json.dumps(run_record))
        report = raw
        if label in ("missing", "skipped"):
            root = ET.fromstring(raw)
            suite = root[0]
            case = next(node for node in suite if node.tag == "testcase")
            if label == "missing":
                suite.remove(case)
                suite.set(
                    "tests",
                    str(len([node for node in suite if node.tag == "testcase"])),
                )
            else:
                ET.SubElement(case, "skipped", message="control")
                suite.set("skipped", "1")
            report = ET.tostring(root, encoding="utf-8", xml_declaration=True)
            copied["report"] = identity(report)
        elif label == "child-exit":
            copied["child"]["exit_code"] = 1
        else:
            copied["discovered_cases"] = copied["discovered_cases"][:-1]
        report_path = directory / "report.xml"
        report_path.write_bytes(report)
        path = directory / "run.json"
        write_json(path, copied)
        checked = reconcile(directory, path, report_path, "accountant")
        results[label] = checked
        expected = {
            "missing": "case-coverage",
            "skipped": "case-outcome",
            "child-exit": "execution",
            "collection-shrink": "inventory-drift",
        }[label]
        if not intentional_rejection(checked, expected):
            raise ValueError(f"Negative control not rejected: {label}")
    return results


def _run_body(prepared, manifest_sha256, output, scope):
    prepared = Path(prepared)
    if prepared.is_symlink():
        raise ValueError("Prepared root must not be symlink")
    prepared = prepared.resolve()
    output = exclusive(output, protected=(prepared,))
    if not prepared.is_dir() or prepared.is_symlink():
        raise ValueError("Prepared root missing or symlink")
    policy_bytes = bounded(INVENTORY)
    if identity(policy_bytes)["sha256"] != INVENTORY_SHA:
        raise ValueError("Reviewed inventory digest mismatch")
    policy = strict_json(policy_bytes)
    if policy["prerequisites"] != ["prepared-runtime", "kvm-qemu", "pinned-producer"]:
        raise ValueError("Reviewed prerequisite IDs changed")
    output.mkdir()
    record = {
        "schema_version": 1,
        "run_id": uuid.uuid4().hex,
        "scope": "isolated Linux/BlueZ host regression; no physical/codec acceptance",
        "prepared": str(prepared),
        "manifest_sha256": manifest_sha256,
        "inventory": identity(policy_bytes),
        "processes": {},
        "ok": False,
    }
    scope.lane_record = record
    scope.lane_output = output
    try:
        prereqs = [
            (
                "prepared-runtime",
                [
                    sys.executable,
                    str(HOST),
                    "check",
                    "--prepared",
                    str(prepared),
                    "--manifest-sha256",
                    manifest_sha256,
                ],
            ),
            ("kvm-qemu", [sys.executable, str(Path(__file__).resolve()), "_kvm_check"]),
            (
                "pinned-producer",
                [
                    sys.executable,
                    "-c",
                    "import pytest; assert pytest.__version__ == '8.4.2', pytest.__version__; print(pytest.__version__)",
                ],
            ),
        ]
        for name, argv in prereqs:
            result = run_owned(argv, output / (name + ".log"), 30, cwd=REPO)
            record["processes"][name] = {
                **result,
                "log_path": str(output / (name + ".log")),
            }
            if not result["ok"]:
                raise ValueError(f"Prerequisite failed: {name}")
        prepared_record = read_manifest(prepared / "manifest.json", manifest_sha256)
        record["runtime"] = {
            "version": prepared_record["version"],
            "artifacts": prepared_record["artifacts"],
            "source_hashes": prepared_record["source_hashes"],
            "qemu": prepared_record["qemu"],
            "producer": policy["producer"],
            "prerequisite_logs": {
                name: identity(bounded(output / (name + ".log"))) for name, _ in prereqs
            },
        }
        env = dict(os.environ)
        env.update(
            PYTEST_DISABLE_PLUGIN_AUTOLOAD="1",
            PB053_SUITE_ROOT=str(output),
            PB053_PREPARED_ROOT=str(prepared),
            PB053_MANIFEST_SHA256=manifest_sha256,
        )
        argv = [
            sys.executable,
            "-m",
            "pytest",
            "--runxfail",
            "-q",
            "--junitxml=" + str(output / "report.xml"),
            "-o",
            "junit_family=xunit2",
            CASE,
        ]
        child = run_owned(
            argv,
            output / "pytest.log",
            1500,
            max_log_bytes=8 * 1024 * 1024,
            env=env,
            cwd=REPO,
        )
        record["processes"]["pytest"] = {
            **child,
            "log_path": str(output / "pytest.log"),
        }
        if not child["ok"]:
            raise ValueError("Owned pytest did not exit normally with status 0")
        collection = bounded(output / "collection.json")
        report = bounded(output / "report.xml")
        observed = validate_collection(collection, policy["required"])
        cases = observed["discovered_cases"]
        record["collection"] = {
            "path": str(output / "collection.json"),
            **identity(collection),
        }
        record["report"] = {"path": str(output / "report.xml"), **identity(report)}
        run_record = {
            "schema_version": 1,
            "run_id": record["run_id"],
            "suite": policy["suite"],
            "producer": policy["producer"],
            "inventory_sha256": INVENTORY_SHA,
            "discovered_cases": cases,
            "child": {
                "argv": argv,
                "cwd": str(REPO),
                "started_at": timestamp(child["start_time"]),
                "ended_at": timestamp(child["end_time"]),
                **execution(child),
            },
            "prerequisites": [
                {"id": name, **execution(record["processes"][name])}
                for name, _ in prereqs
            ],
            "report": identity(report),
        }
        path = output / "run.json"
        write_json(path, run_record)
        checked = reconcile(output, path, output / "report.xml", "accountant")
        record["accountant"] = checked
        if not checked["accepted"]:
            raise ValueError("PB-052 accounting rejected actual report")
        record["controls"] = controls(output, run_record, report)
        record["ok"] = True
    except BaseException as exc:
        record["error"] = f"{type(exc).__name__}: {exc}"
        if not isinstance(exc, Exception):
            raise
    finally:
        for process in record["processes"].values():
            if process.get("cancelled_signal") is not None:
                scope.record["cancelled_signal"] = process["cancelled_signal"]
        for name in ("collection.json", "report.xml", "pytest.log"):
            path = output / name
            if path.is_file():
                try:
                    record.setdefault("retained", {})[name] = {
                        "path": str(path),
                        **identity(bounded(path)),
                    }
                except (OSError, ValueError) as exc:
                    record["ok"] = False
                    record["error"] = f"Evidence retention failed: {name}: {exc}"
    if not record["ok"]:
        raise ValueError(record["error"])
    return record


def run(prepared, manifest_sha256, output):
    scope = DescendantScope()
    try:
        with scope:
            return _run_body(prepared, manifest_sha256, output, scope)
    except BaseException as exc:
        if scope.lane_record is not None:
            scope.lane_record["ok"] = False
            if not scope.lane_record.get("error"):
                scope.lane_record["error"] = f"{type(exc).__name__}: {exc}"
        raise
    finally:
        if scope.lane_record is not None:
            record = scope.lane_record
            record["descendant_scope"] = scope.record
            if not scope.record["ok"]:
                record["ok"] = False
                if not record.get("error"):
                    record["error"] = "Descendant scope failed"
            assert scope.lane_output is not None
            write_json(scope.lane_output / "suite-record.json", record)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepared", required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = run(args.prepared, args.manifest_sha256, args.output)
        print(json.dumps({"ok": result["ok"], "run_id": result["run_id"]}))
        return 0
    except (ValueError, OSError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"PB053 lane: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    if sys.argv[1:] == ["_kvm_check"]:
        try:
            kvm_check()
        except (ValueError, OSError, subprocess.SubprocessError) as exc:
            print(f"PB053 KVM: {exc}", file=sys.stderr)
            sys.exit(1)
    else:
        sys.exit(main())
