#!/usr/bin/env python3
"""RH3 matrix fake tests.

Each test injects a row-run callable. No test opens serial devices, executes
subprocesses, flashes firmware, probes hardware, or uses Bluetooth.
"""

import json
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "scripts"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hil_fakes  # noqa: E402
from hil import cli, lifecycle, matrix, rows  # noqa: E402


class FakeRows:
    """Injected public row boundary with outcomes keyed by schedule position."""

    def __init__(self, outcomes=None, cancel_after=None):
        self.outcomes = outcomes or {}
        self.cancel_after = cancel_after
        self.calls = []

    def factory(self, _cancel):
        return self

    def run(
        self,
        fixture_path,
        binding_path,
        output_root,
        run_id,
        junit_path,
        *,
        argv,
        status,
        row,
    ):
        del fixture_path, binding_path, argv, status
        call = {
            "index": len(self.calls) + 1,
            "run_id": run_id,
            "row": row,
        }
        self.calls.append(call)
        outcome, boundary, cleanup = self.outcomes.get(
            call["index"], ("passed", None, [])
        )
        if outcome == "passed" and not cleanup:
            run_dir = os.path.join(output_root, run_id)
            os.makedirs(run_dir)
            with open(
                os.path.join(run_dir, "result.json"), "w", encoding="utf-8"
            ) as fh:
                json.dump({"outcome": "passed"}, fh)
            for name in ("junit.xml", "MANIFEST.md", "SHA256SUMS"):
                with open(os.path.join(run_dir, name), "w", encoding="utf-8") as fh:
                    fh.write(name + "\n")
            with open(junit_path, "w", encoding="utf-8") as fh:
                fh.write("<testsuite/>\n")
        return outcome, boundary, cleanup


def _fixture_binding(directory):
    cfg = os.path.join(directory, "cfg")
    os.makedirs(cfg)
    return hil_fakes.write_fixture_binding(cfg)


def _run_matrix(td, fake, run_id="rh3-matrix-001", cancel=None):
    output_root = os.path.join(td, "out")
    os.makedirs(output_root)
    fixture_path, binding_path = _fixture_binding(td)
    junit_path = os.path.join(output_root, run_id + ".junit.xml")
    deps = matrix.MatrixDeps(
        row_runner_factory=fake.factory,
        cancel=cancel if cancel is not None else lambda: False,
        environment=lambda argv, status: {"argv": list(argv), "status": status},
    )
    coordinator = matrix.MatrixCoordinator(deps)
    result = coordinator.run(
        fixture_path,
        binding_path,
        output_root,
        run_id,
        junit_path,
        argv=["hil-runner.py", "run-rh3-matrix"],
        status=0,
    )
    return result, output_root, junit_path


class TestRh3Schedule(unittest.TestCase):
    def test_fixed_two_pass_schedule_has_fresh_first_and_healthy_before_follow_on(self):
        schedule = rows.rh3_schedule()
        per_pass = len(rows.RH3_PASS_ROWS)
        self.assertEqual(len(schedule), rows.RH3_PASS_COUNT * per_pass)
        for pass_index in range(1, rows.RH3_PASS_COUNT + 1):
            group = [entry for entry in schedule if entry[0] == pass_index]
            self.assertEqual(
                [entry[1] for entry in group], list(range(1, per_pass + 1))
            )
            self.assertEqual(tuple(entry[2] for entry in group), rows.RH3_PASS_ROWS)
            self.assertTrue(
                all(entry[2].healthy for entry in group[: len(rows.RH3_HEALTHY_ROWS)])
            )
            self.assertEqual(group[-3][2], rows.RH3_RECONNECT_ROW)
            self.assertEqual(group[-2][2], rows.RH3_HANG_ROW)
            self.assertEqual(group[-1][2], rows.RH3_STALL_ROW)

    def test_mandatory_matrix_contains_both_frame_durations(self):
        # Plan revision 2026-09-11 (user decision): the three 7.5 ms rows
        # are reinstated in the mandatory matrix after the RH3-7p5
        # three-stage re-baseline passed every row on the fixed fixture.
        # The 10 ms healthy rows stay first so fresh 10 ms shapes prove
        # the base matrix before 7.5 ms adds its rows.
        profiles = [row.profile for row in rows.RH3_PASS_ROWS]
        self.assertEqual(profiles[:4], ["48_4_1"] * 4)
        self.assertEqual(profiles[4:7], ["48_3_1"] * 3)
        self.assertEqual(
            [row.profile for row in rows.RH3_PASS_ROWS[7:]],
            ["48_4_1"] * 3,
            "follow-on rows stay 10 ms",
        )
        self.assertEqual(len(rows.RH3_PASS_ROWS), 10)

    def test_7p5_rows_are_mandatory_and_selectable(self):
        # The historical diagnostic alias must reference the same RowSpec
        # objects now in the mandatory matrix (evidence docs cite it).
        self.assertEqual(len(rows.RH3_7P5_DIAGNOSTIC_ROWS), 3)
        for row in rows.RH3_7P5_DIAGNOSTIC_ROWS:
            self.assertEqual(row.profile, "48_3_1")
            self.assertIn(row.name, rows.row_names())
            self.assertIs(rows.get_row(row.name), row)
            self.assertIn(
                row, rows.RH3_PASS_ROWS, "7.5 ms row missing from matrix"
            )

    def test_child_ids_are_deterministic_safe_and_unique(self):
        schedule = rows.rh3_schedule()
        first = [matrix.child_run_id("matrix-001", *entry) for entry in schedule]
        second = [matrix.child_run_id("matrix-001", *entry) for entry in schedule]
        self.assertEqual(first, second)
        self.assertEqual(len(first), len(set(first)))
        for child_id in first:
            self.assertLessEqual(len(child_id), 64)
            self.assertNotIn("/", child_id)
            self.assertNotIn(" ", child_id)


class TestMatrixCoordinator(unittest.TestCase):
    def test_passing_matrix_runs_fixed_two_pass_order_and_aggregate_evidence(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            result, output_root, junit_path = _run_matrix(td, fake)
            self.assertEqual(result, ("passed", None, []))
            expected = [entry[2] for entry in rows.rh3_schedule()]
            self.assertEqual([call["row"] for call in fake.calls], expected)

            matrix_dir = os.path.join(output_root, "rh3-matrix-001")
            for name in (
                "fixture.json",
                "binding.json",
                "environment.json",
                "schedule.json",
                "children.jsonl",
                "result.json",
                "junit.xml",
                "MANIFEST.md",
                "SHA256SUMS",
            ):
                self.assertTrue(os.path.isfile(os.path.join(matrix_dir, name)), name)
            with open(
                os.path.join(matrix_dir, "schedule.json"), encoding="utf-8"
            ) as fh:
                schedule = json.load(fh)
            self.assertEqual(schedule["pass_count"], 2)
            self.assertEqual(len(schedule["children"]), len(rows.rh3_schedule()))
            with open(
                os.path.join(matrix_dir, "children.jsonl"), encoding="utf-8"
            ) as fh:
                children = [json.loads(line) for line in fh if line.strip()]
            self.assertEqual(len(children), len(rows.rh3_schedule()))
            self.assertEqual(
                len({child["child_run_id"] for child in children}), len(children)
            )
            with open(os.path.join(matrix_dir, "result.json"), encoding="utf-8") as fh:
                aggregate = json.load(fh)
            self.assertEqual(aggregate["outcome"], "passed")
            self.assertEqual(
                aggregate["attempted_child_count"], len(rows.rh3_schedule())
            )
            self.assertNotIn("ACCEPTED", json.dumps(aggregate))
            self.assertTrue(os.path.isfile(junit_path))
            tree = ET.parse(junit_path)
            tests = tree.getroot().findall("testcase")
            self.assertEqual(len(tests), len(rows.rh3_schedule()))
            self.assertEqual(tree.getroot().attrib["tests"], str(len(tests)))
            self.assertEqual(tree.getroot().attrib["errors"], "0")
            self.assertNotIn(
                "matrix.orchestration", [test.attrib["name"] for test in tests]
            )
            self.assertEqual(
                tests[0].attrib["name"], "pass1.%s" % rows.RH3_PASS_ROWS[0].name
            )
            self.assertEqual(
                tests[-1].attrib["name"], "pass2.%s" % rows.RH3_PASS_ROWS[-1].name
            )
            with open(os.path.join(matrix_dir, "SHA256SUMS"), encoding="utf-8") as fh:
                sums = fh.read()
            self.assertIn("  children.jsonl", sums)
            self.assertIn("  junit.xml", sums)
            self.assertIn("  result.json", sums)

    def test_healthy_failure_stops_before_reconnect_fault_and_pass_two(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows({2: ("failed", "receiver tail", [])})
            result, output_root, junit_path = _run_matrix(td, fake)
            self.assertEqual(result[0], "failed")
            self.assertEqual(len(fake.calls), 2)
            self.assertEqual(fake.calls[-1]["row"], rows.RH3_PASS_ROWS[1])
            self.assertNotIn(
                rows.RH3_RECONNECT_ROW, [call["row"] for call in fake.calls]
            )
            self.assertFalse(
                any(call["row"] == rows.RH3_PASS_ROWS[0] for call in fake.calls[2:])
            )
            root = ET.parse(junit_path).getroot()
            tests = root.findall("testcase")
            self.assertEqual(root.attrib["tests"], str(len(tests)))
            self.assertEqual(root.attrib["errors"], "0")
            self.assertNotIn(
                "matrix.orchestration", [test.attrib["name"] for test in tests]
            )
            self.assertIsNotNone(tests[1].find("failure"))
            self.assertIsNotNone(tests[2].find("skipped"))
            skipped = tests[2].find("skipped")
            assert skipped is not None
            self.assertIn("pass1 row2", skipped.attrib["message"])
            with open(
                os.path.join(output_root, "rh3-matrix-001", "result.json"),
                encoding="utf-8",
            ) as fh:
                aggregate = json.load(fh)
            self.assertEqual(aggregate["attempted_child_count"], 2)
            self.assertEqual(aggregate["failed_child_count"], 1)

    def test_cancelled_child_stops_further_execution_and_marks_unattempted_skipped(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows({3: ("cancelled", "cancelled during row", [])})
            result, _output_root, junit_path = _run_matrix(td, fake)
            self.assertEqual(result[0], "cancelled")
            self.assertEqual(len(fake.calls), 3)
            root = ET.parse(junit_path).getroot()
            tests = root.findall("testcase")
            self.assertEqual(root.attrib["tests"], str(len(tests)))
            self.assertEqual(root.attrib["errors"], "0")
            self.assertNotIn(
                "matrix.orchestration", [test.attrib["name"] for test in tests]
            )
            self.assertIsNotNone(tests[2].find("skipped"))
            cancelled = tests[2].find("skipped")
            unattempted = tests[3].find("skipped")
            assert cancelled is not None
            assert unattempted is not None
            self.assertIn("cancelled during row", cancelled.attrib["message"])
            self.assertIn("not attempted", unattempted.attrib["message"])

    def test_child_cleanup_failure_fails_matrix_and_stops(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows({1: ("passed", None, [("close source", "boom")])})
            result, output_root, _junit_path = _run_matrix(td, fake)
            self.assertEqual(result[0], "failed")
            self.assertEqual(len(fake.calls), 1)
            with open(
                os.path.join(output_root, "rh3-matrix-001", "result.json"),
                encoding="utf-8",
            ) as fh:
                aggregate = json.load(fh)
            self.assertEqual(aggregate["cleanup_failures"][0]["name"], "close source")

    def test_pass_without_child_evidence_fails_matrix(self):
        class MissingEvidenceRows(FakeRows):
            def run(self, *args, **kwargs):
                call = {"index": len(self.calls) + 1, "row": kwargs["row"]}
                self.calls.append(call)
                return "passed", None, []

        with tempfile.TemporaryDirectory() as td:
            fake = MissingEvidenceRows()
            result, _output_root, _junit_path = _run_matrix(td, fake)
            self.assertEqual(result[0], "failed")
            self.assertEqual(len(fake.calls), 1)
            self.assertIn("passed child evidence", result[1])

    def test_matrix_finalization_failure_preserves_attempted_child_records(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            original = matrix.finalize_evidence

            def fail_finalize(*args, **kwargs):
                del args, kwargs
                raise matrix.EvidenceError("injected finalization failure")

            matrix.finalize_evidence = fail_finalize
            try:
                result, output_root, junit_path = _run_matrix(td, fake)
            finally:
                matrix.finalize_evidence = original
            self.assertEqual(result[0], "failed")
            matrix_dir = os.path.join(output_root, "rh3-matrix-001")
            with open(
                os.path.join(matrix_dir, "children.jsonl"), encoding="utf-8"
            ) as fh:
                records = [json.loads(line) for line in fh if line.strip()]
            self.assertEqual(len(records), len(rows.rh3_schedule()))
            self.assertTrue(
                os.path.isfile(
                    os.path.join(records[0]["child_evidence_path"], "result.json")
                )
            )
            self.assertTrue(os.path.isfile(os.path.join(matrix_dir, "result.json")))
            root = ET.parse(junit_path).getroot()
            tests = root.findall("testcase")
            self.assertEqual(root.attrib["tests"], str(len(tests)))
            self.assertEqual(root.attrib["errors"], "1")
            self.assertEqual(len(tests), len(rows.rh3_schedule()) + 1)
            self.assertEqual(tests[-1].attrib["name"], "matrix.orchestration")
            self.assertEqual(tests[-1].attrib["classname"], "hil.rh3")
            error = tests[-1].find("error")
            self.assertIsNotNone(error)
            assert error is not None
            self.assertIn("evidence finalization", error.attrib["message"])

    def test_validation_rejects_existing_roots_and_external_junit_before_child(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            output_root = os.path.join(td, "out")
            os.makedirs(output_root)
            fixture_path, binding_path = _fixture_binding(td)
            existing = os.path.join(output_root, "existing")
            os.makedirs(existing)
            coordinator = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory,
                    environment=lambda argv, status: {},
                )
            )
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                coordinator.run(
                    fixture_path,
                    binding_path,
                    output_root,
                    "existing",
                    os.path.join(output_root, "existing.junit.xml"),
                )
            self.assertIn("already exists", str(ctx.exception))
            self.assertEqual(fake.calls, [])

    def test_precreated_child_root_rejected_before_matrix_work(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            output_root = os.path.join(td, "out")
            os.makedirs(output_root)
            fixture_path, binding_path = _fixture_binding(td)
            run_id = "child-root-check"
            os.makedirs(os.path.join(output_root, matrix._children_root_id(run_id)))
            coordinator = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory,
                    environment=lambda argv, status: {},
                )
            )
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                coordinator.run(
                    fixture_path,
                    binding_path,
                    output_root,
                    run_id,
                    os.path.join(output_root, run_id + ".junit.xml"),
                )
            self.assertIn("child output root already exists", str(ctx.exception))
            self.assertFalse(os.path.exists(os.path.join(output_root, run_id)))
            self.assertEqual(fake.calls, [])

            existing_junit = os.path.join(output_root, "taken.junit.xml")
            with open(existing_junit, "w", encoding="utf-8") as fh:
                fh.write("keep")
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                coordinator.run(
                    fixture_path,
                    binding_path,
                    output_root,
                    "new-run",
                    existing_junit,
                )
            self.assertIn("external JUnit path already exists", str(ctx.exception))
            self.assertEqual(fake.calls, [])

    def test_invalid_matrix_run_id_and_output_root_reject_before_child(self):
        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            output_root = os.path.join(td, "out")
            os.makedirs(output_root)
            fixture_path, binding_path = _fixture_binding(td)
            coordinator = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory,
                    environment=lambda argv, status: {},
                )
            )
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                coordinator.run(
                    fixture_path,
                    binding_path,
                    output_root,
                    "bad/id",
                    os.path.join(output_root, "bad.junit.xml"),
                )
            self.assertIn("run id", str(ctx.exception))
            with self.assertRaises(lifecycle.HilLifecycleError) as ctx:
                coordinator.run(
                    fixture_path,
                    binding_path,
                    "relative-output-root",
                    "valid-id",
                    os.path.join(output_root, "valid.junit.xml"),
                )
            self.assertIn("absolute", str(ctx.exception))
            self.assertEqual(fake.calls, [])

    def test_schedule_is_retained_when_matrix_setup_fails_before_child(self):
        class BrokenEnvironment:
            def __call__(self, argv, status):
                del argv, status
                raise OSError("environment capture failed")

        with tempfile.TemporaryDirectory() as td:
            fake = FakeRows()
            output_root = os.path.join(td, "out")
            os.makedirs(output_root)
            fixture_path, binding_path = _fixture_binding(td)
            run_id = "setup-failure"
            coordinator = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory,
                    environment=BrokenEnvironment(),
                )
            )
            result = coordinator.run(
                fixture_path,
                binding_path,
                output_root,
                run_id,
                os.path.join(output_root, run_id + ".junit.xml"),
            )
            self.assertEqual(result[0], "failed")
            self.assertEqual(fake.calls, [])
            with open(
                os.path.join(output_root, run_id, "schedule.json"), encoding="utf-8"
            ) as fh:
                schedule = json.load(fh)
            self.assertEqual(len(schedule["children"]), len(rows.rh3_schedule()))
            root = ET.parse(os.path.join(output_root, run_id + ".junit.xml")).getroot()
            tests = root.findall("testcase")
            self.assertEqual(root.attrib["tests"], str(len(tests)))
            self.assertEqual(root.attrib["errors"], "1")
            self.assertEqual(len(tests), len(rows.rh3_schedule()) + 1)
            self.assertEqual(tests[-1].attrib["name"], "matrix.orchestration")
            self.assertEqual(tests[-1].attrib["classname"], "hil.rh3")
            error = tests[-1].find("error")
            self.assertIsNotNone(error)
            assert error is not None
            self.assertIn("matrix setup", error.attrib["message"])


class TestMatrixCli(unittest.TestCase):
    def test_matrix_command_has_fixed_arguments_only_and_direct_run_default_remains_rh2(
        self,
    ):
        parser = cli.build_parser()
        matrix_args = parser.parse_args(
            [
                "run-rh3-matrix",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--output-root",
                "/tmp/out",
                "--run-id",
                "matrix-1",
                "--junit",
                "/tmp/out/matrix-1.junit.xml",
            ]
        )
        self.assertIs(matrix_args.func, cli.cmd_run_rh3_matrix)
        with self.assertRaises(cli.HilCliError):
            parser.parse_args(
                [
                    "run-rh3-matrix",
                    "--fixture",
                    "f",
                    "--binding",
                    "b",
                    "--output-root",
                    "/tmp/out",
                    "--run-id",
                    "matrix-1",
                    "--junit",
                    "/tmp/out/matrix-1.junit.xml",
                    "--row",
                    rows.RH2_ROW.name,
                ]
            )
        direct = parser.parse_args(
            [
                "run",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--output-root",
                "/tmp/out",
                "--run-id",
                "row-1",
                "--junit",
                "/tmp/out/row-1.junit.xml",
            ]
        )
        self.assertEqual(cli._selected_row(direct.row), rows.RH2_ROW)

    def test_matrix_cli_maps_outcomes_to_process_status(self):
        from unittest import mock

        for outcome, expected_status in (
            ("passed", 0),
            ("failed", 1),
            ("cancelled", 130),
        ):
            calls = []

            class FakeCoordinator:
                def run(self, *args, **kwargs):
                    calls.append((args, kwargs))
                    return outcome, "boundary", []

            class Args:
                fixture = "f"
                binding = "b"
                output_root = "/tmp/out"
                run_id = "matrix-1"
                junit = "/tmp/out/matrix.junit.xml"

            with mock.patch.object(cli.matrix, "MatrixDeps", return_value=object()):
                with mock.patch.object(
                    cli.matrix, "MatrixCoordinator", return_value=FakeCoordinator()
                ):
                    with mock.patch.object(cli.signal, "signal"):
                        self.assertEqual(
                            cli.cmd_run_rh3_matrix(Args()), expected_status
                        )
            self.assertEqual(len(calls), 1)
            self.assertEqual(
                calls[0][0][:5],
                ("f", "b", "/tmp/out", "matrix-1", "/tmp/out/matrix.junit.xml"),
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
