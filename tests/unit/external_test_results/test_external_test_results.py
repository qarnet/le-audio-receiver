#!/usr/bin/env python3
"""Real accounting CLI traffic and real pytest emitter controls; no Bluetooth claim."""

import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "scripts/check-external-test-results.py"
PRODUCERS = {
    "bluez-tester-v1": {
        "name": "bluez",
        "revision": "4dc15be8ee3f7422d447087f1893d215575cb2c8",
    },
    "pytest-xunit2-v1": {"name": "pytest", "version": "8.4.2"},
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bluez(rows):
    lines = [
        "Authored progress before summary",
        "\x1b[1;39mTest Summary\x1b[0m",
        "------------",
    ]
    counts = {
        state: sum(status == state for _, status in rows)
        for state in ("Passed", "Failed", "Timed out", "Not Run")
    }
    for name, state in rows:
        duration = "" if state == "Not Run" else f"{0.125:8.3f} seconds"
        lines.append(f"{name:<52} \x1b[0;32m{state:<10}\x1b[0m{duration}")
    percent = 100 * counts["Passed"] / len(rows) if rows else 0
    lines.append(
        f"Total: {len(rows)}, Passed: {counts['Passed']} ({percent:.1f}%), "
        f"Failed: {counts['Failed'] + counts['Timed out']}, Not Run: {counts['Not Run']}"
    )
    lines.append("Overall execution time: 1.25e+02 seconds")
    return ("\n".join(lines) + "\n").encode()


def junit(rows):
    root = ET.Element("testsuites", name="pytest tests")
    suite = ET.SubElement(
        root,
        "testsuite",
        name="pytest",
        tests=str(len(rows)),
        failures=str(sum(state == "failure" for _, state in rows)),
        errors=str(sum(state == "error" for _, state in rows)),
        skipped=str(sum(state == "skipped" for _, state in rows)),
        time="0.1",
    )
    for identity, state in rows:
        case = ET.SubElement(
            suite, "testcase", classname=identity[0], name=identity[1], time="0.01"
        )
        if state != "pass":
            ET.SubElement(case, state)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


class AccountingCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="external-results-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.serial = 0

    def policy(self, profile, identities):
        return {
            "schema_version": 1,
            "suite": "authored-parser-fixture",
            "profile": profile,
            "producer": PRODUCERS[profile],
            "required": identities,
            "exclusions": [],
            "prerequisites": ["isolated-environment"],
            "prerequisite_rationale": "Synthetic parser fixture, not external Bluetooth execution",
        }

    def invoke(
        self,
        policy,
        report,
        modify=None,
        anchor=None,
        inventory_bytes=None,
        record_bytes=None,
        child=None,
    ):
        self.serial += 1
        run_dir = self.directory / str(self.serial)
        run_dir.mkdir()
        data = inventory_bytes or json.dumps(policy, sort_keys=True).encode()
        record = {
            "schema_version": 1,
            "run_id": f"authored-{self.serial}",
            "suite": policy["suite"],
            "producer": policy["producer"],
            "inventory_sha256": sha(data),
            "discovered_cases": policy["required"]
            + [e["id"] for e in policy["exclusions"]],
            "child": child
            or {
                "argv": ["pytest", "--runxfail"]
                if policy["profile"].startswith("pytest")
                else ["authored-bluez-tester"],
                "cwd": str(run_dir),
                "started_at": "2026-10-03T01:00:00+00:00",
                "ended_at": "2026-10-03T01:00:01+00:00",
                "termination": "exited",
                "exit_code": 0,
            },
            "prerequisites": [
                {"id": "isolated-environment", "termination": "exited", "exit_code": 0}
            ],
            "report": {"bytes": len(report), "sha256": sha(report)},
        }
        if modify:
            modify(record)
        for name, content in (
            ("inventory.json", data),
            ("run.json", record_bytes or json.dumps(record).encode()),
            ("report", report),
        ):
            (run_dir / name).write_bytes(content)
        proc = subprocess.run(
            [
                sys.executable,
                str(CLI),
                "--inventory",
                str(run_dir / "inventory.json"),
                "--expected-inventory-sha256",
                anchor or sha(data),
                "--run-record",
                str(run_dir / "run.json"),
                "--report",
                str(run_dir / "report"),
            ],
            capture_output=True,
            timeout=15,
        )
        (run_dir / "stdout.json").write_bytes(proc.stdout)
        (run_dir / "stderr").write_bytes(proc.stderr)
        result = json.loads(proc.stdout)
        self.assertEqual(result["inputs"]["inventory"]["sha256"], sha(data))
        self.assertEqual(proc.returncode == 0, result["accepted"])
        return proc.returncode, result

    def rejected(self, policy, report, **kwargs):
        code, result = self.invoke(policy, report, **kwargs)
        self.assertNotEqual(code, 0, result)
        self.assertTrue(result["errors"])
        return result

    def test_complete_profiles_with_exact_identities_and_exclusions(self):
        names = ["simple", "Passed inside case name", "long-case-" + "x" * 80]
        policy = self.policy("bluez-tester-v1", [[name] for name in names])
        policy["exclusions"] = [
            {"id": ["unselected"], "reason": "Outside reviewed selected scope"}
        ]
        code, result = self.invoke(policy, bluez([(name, "Passed") for name in names]))
        self.assertEqual(code, 0, result)
        self.assertEqual(
            result["counts"], {"required": 3, "passed": 3, "excluded_unselected": 1}
        )
        self.assertEqual(result["excluded"], policy["exclusions"])
        ids = [
            ["tests.test_wire.Class", "test_connect[param & <name>]"],
            ["tests.test_wire", "test_reset"],
        ]
        policy = self.policy("pytest-xunit2-v1", ids)
        code, result = self.invoke(
            policy, junit([(identity, "pass") for identity in ids])
        )
        self.assertEqual(code, 0, result)
        self.assertEqual([entry["id"] for entry in result["cases"]], ids)

    def test_no_execution_missing_duplicate_and_unreviewed_cases(self):
        policy = self.policy("bluez-tester-v1", [["one"], ["two"]])
        for rows in (
            [],
            [("one", "Passed")],
            [("one", "Passed"), ("one", "Passed")],
            [("one", "Passed"), ("extra", "Passed")],
        ):
            with self.subTest(rows=rows):
                self.rejected(policy, bluez(rows))
        policy = self.policy("pytest-xunit2-v1", [["module", "one"], ["module", "two"]])
        for rows in (
            [],
            [(["module", "one"], "pass")],
            [(["module", "one"], "pass"), (["module", "one"], "pass")],
            [(["module", "one"], "pass"), (["module", "extra"], "pass")],
        ):
            with self.subTest(rows=rows):
                self.rejected(policy, junit(rows))

    def test_reported_failure_skip_timeout_and_exclusion_cannot_pass(self):
        policy = self.policy("bluez-tester-v1", [["one"]])
        for state in ("Failed", "Timed out", "Not Run"):
            self.rejected(policy, bluez([("one", state)]))
        policy["exclusions"] = [{"id": ["excluded"], "reason": "Unselected"}]
        for state in ("Passed", "Not Run"):
            self.rejected(policy, bluez([("one", "Passed"), ("excluded", state)]))
        policy = self.policy("pytest-xunit2-v1", [["module", "one"]])
        for state in ("failure", "error", "skipped"):
            self.rejected(policy, junit([(["module", "one"], state)]))

    def test_count_and_format_corruption(self):
        policy = self.policy("bluez-tester-v1", [["one"]])
        valid = bluez([("one", "Passed")])
        for corrupted in (
            valid.replace(b"Total: 1", b"Total: 2"),
            valid.replace(b"100.0%", b"99.0%"),
            valid.replace(b"Passed", b"Success", 1),
            valid[: valid.index(b"Overall")],
            valid + b"Test Summary\n------------\n",
            valid + b"unexplained tail\n",
            valid.replace(b"0.125 seconds", b"nan seconds"),
        ):
            self.assertNotEqual(corrupted, valid, "Control must change encoded report")
            self.rejected(policy, corrupted)
        policy = self.policy("pytest-xunit2-v1", [["module", "one"]])
        valid = junit([(["module", "one"], "pass")])
        for corrupted in (
            valid.replace(b'tests="1"', b'tests="2"'),
            valid[:-15],
            valid.replace(b'time="0.1"', b'time="NaN"'),
            valid.replace(
                b" /></testsuite>", b"><failure/><skipped/></testcase></testsuite>"
            ),
        ):
            self.assertNotEqual(corrupted, valid, "Control must change encoded report")
            self.rejected(policy, corrupted)

    def test_execution_prerequisite_hash_and_collection_binding(self):
        policy = self.policy("bluez-tester-v1", [["one"]])
        report = bluez([("one", "Passed")])
        mutations = [
            lambda r: r["child"].update(exit_code=1),
            lambda r: r["child"].update(exit_code=True),
            lambda r: r["child"].update(termination="timeout", exit_code=None),
            lambda r: r["child"].update(termination="cancelled", exit_code=None),
            lambda r: r["child"].update(termination="signal", exit_code=-9),
            lambda r: r["child"].update(termination="spawn-failed", exit_code=None),
            lambda r: r["child"].update(ended_at="2026-10-02T01:00:00+00:00"),
            lambda r: r["prerequisites"].clear(),
            lambda r: r["prerequisites"][0].update(exit_code=1),
            lambda r: r["prerequisites"][0].update(termination="skipped"),
            lambda r: r["prerequisites"].append(copy.deepcopy(r["prerequisites"][0])),
            lambda r: r["discovered_cases"].append(["unknown"]),
            lambda r: r.update(inventory_sha256="0" * 64),
            lambda r: r["report"].update(sha256="0" * 64),
            lambda r: r["report"].update(bytes=len(report) + 1),
        ]
        for index, mutation in enumerate(mutations):
            with self.subTest(control=index):
                self.rejected(policy, report, modify=mutation)
        self.rejected(policy, report, anchor="0" * 64)
        narrowed = self.policy("bluez-tester-v1", [["one"]])
        reviewed = self.policy("bluez-tester-v1", [["one"], ["two"]])
        self.rejected(
            narrowed, report, anchor=sha(json.dumps(reviewed, sort_keys=True).encode())
        )

    def test_schema_xml_entities_nesting_and_ambiguous_outcomes(self):
        policy = self.policy("bluez-tester-v1", [["one"]])
        report = bluez([("one", "Passed")])
        for change in (
            lambda p: p.update(schema_version=2),
            lambda p: p.update(required=[]),
            lambda p: p.update(profile="unknown"),
            lambda p: p.update(extra="unknown"),
            lambda p: p.update(required=[["one"], ["one"]]),
            lambda p: p.update(exclusions=[{"id": ["one"], "reason": "not a waiver"}]),
        ):
            changed = copy.deepcopy(policy)
            change(changed)
            self.rejected(changed, report)
        duplicate = (
            json.dumps(policy)
            .replace('"schema_version": 1', '"schema_version": 1, "schema_version": 1')
            .encode()
        )
        self.rejected(policy, report, inventory_bytes=duplicate)
        policy = self.policy("pytest-xunit2-v1", [["module", "one"]])
        report = junit([(["module", "one"], "pass")])
        self.rejected(
            policy, report, modify=lambda r: r["child"].update(argv=["pytest"])
        )
        root = ET.fromstring(report)
        node = root[0][0]
        ET.SubElement(node, "failure")
        ET.SubElement(node, "skipped")
        self.rejected(policy, ET.tostring(root))
        boundary = report.index(b"?>") + 2
        entity_report = (
            report[:boundary]
            + b'<!DOCTYPE testsuites [<!ENTITY label "one">]>'
            + report[boundary:].replace(b'name="one"', b'name="&label;"')
        )
        self.assertEqual(ET.fromstring(entity_report)[0][0].get("name"), "one")
        result = self.rejected(policy, entity_report)
        self.assertEqual(result["errors"][0]["code"], "unsafe-xml")
        root = ET.fromstring(report)
        ET.SubElement(root[0], "testsuite")
        result = self.rejected(policy, ET.tostring(root))
        self.assertIn("Unexpected suite child", result["errors"][0]["detail"])
        root = ET.fromstring(report)
        diagnostic = ET.SubElement(root[0][0], "system-out")
        ET.SubElement(diagnostic, "failure")
        result = self.rejected(policy, ET.tostring(root))
        self.assertIn("Nested unexpected diagnostic", result["errors"][0]["detail"])
        self.rejected(policy, b"\xff")

    def test_real_pytest_emission_and_unhappy_lifecycle(self):
        import pytest

        self.assertEqual(
            pytest.__version__,
            "8.4.2",
            "Pinned producer profile must be reviewed on version change",
        )
        bodies = {
            "pass": "def test_case():\n    assert True\n",
            "fail": "def test_case():\n    assert False\n",
            "skip": "import pytest\n@pytest.mark.skip(reason='fixture')\ndef test_case():\n    assert True\n",
            "xfail": "import pytest\n@pytest.mark.xfail\ndef test_case():\n    assert False\n",
            "teardown": "import pytest\n@pytest.fixture\ndef bad():\n    yield\n    raise RuntimeError('teardown')\ndef test_case(bad):\n    pass\n",
            "double": "import pytest\n@pytest.fixture\ndef bad():\n    yield\n    raise RuntimeError('teardown')\ndef test_case(bad):\n    assert False\n",
            "collection": "raise RuntimeError('collection')\n",
            "empty": "# no tests\n",
        }
        for name, body in bodies.items():
            with self.subTest(emission=name):
                directory = self.directory / ("pytest-" + name)
                directory.mkdir()
                (directory / "test_emission.py").write_text(body)
                argv = [
                    sys.executable,
                    "-m",
                    "pytest",
                    "--runxfail",
                    "-o",
                    "junit_family=xunit2",
                    "--junitxml=report.xml",
                    "test_emission.py",
                ]
                env = dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
                proc = subprocess.run(
                    argv, cwd=directory, env=env, capture_output=True, timeout=30
                )
                (directory / "stdout").write_bytes(proc.stdout)
                (directory / "stderr").write_bytes(proc.stderr)
                report = (directory / "report.xml").read_bytes()
                policy = self.policy(
                    "pytest-xunit2-v1", [["test_emission", "test_case"]]
                )
                child = {
                    "argv": argv,
                    "cwd": str(directory),
                    "termination": "exited",
                    "exit_code": proc.returncode,
                    "started_at": "2026-10-03T01:00:00+00:00",
                    "ended_at": "2026-10-03T01:00:01+00:00",
                }
                code, result = self.invoke(policy, report, child=child)
                self.assertEqual(code == 0, name == "pass", result)
                if name != "pass":
                    self.rejected(policy, report, child=dict(child, exit_code=0))

    def test_runxfail_path_cannot_masquerade_as_enabled_option(self):
        directory = self.directory / "positional"
        directory.mkdir()
        source = directory / "--runxfail"
        source.mkdir()
        (source / "test_emission.py").write_text(
            "import pytest\n@pytest.mark.xfail\ndef test_case():\n    assert True\n"
        )
        argv = [
            sys.executable,
            "-m",
            "pytest",
            "-o",
            "junit_family=xunit2",
            "--junitxml=report.xml",
            "--",
            "--runxfail",
        ]
        env = dict(os.environ, PYTEST_DISABLE_PLUGIN_AUTOLOAD="1")
        proc = subprocess.run(
            argv, cwd=directory, env=env, capture_output=True, timeout=30
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn(b"xpassed", proc.stdout)
        report = (directory / "report.xml").read_bytes()
        policy = self.policy(
            "pytest-xunit2-v1", [["--runxfail.test_emission", "test_case"]]
        )
        child = {
            "argv": argv,
            "cwd": str(directory),
            "termination": "exited",
            "exit_code": 0,
            "started_at": "2026-10-03T01:00:00+00:00",
            "ended_at": "2026-10-03T01:00:01+00:00",
        }
        result = self.rejected(policy, report, child=child)
        self.assertEqual(result["errors"][0]["code"], "pytest-policy")
        argv.insert(3, "--runxfail")
        proc = subprocess.run(
            argv, cwd=directory, env=env, capture_output=True, timeout=30
        )
        self.assertEqual(proc.returncode, 0)
        self.assertNotIn(b"xpassed", proc.stdout)
        report = (directory / "report.xml").read_bytes()
        child["argv"] = argv
        code, result = self.invoke(policy, report, child=child)
        self.assertEqual(code, 0, result)


if __name__ == "__main__":
    unittest.main()
