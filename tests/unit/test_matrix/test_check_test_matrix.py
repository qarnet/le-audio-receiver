#!/usr/bin/env python3
"""T7 Stage 1: focused tests for scripts/check-test-matrix.py.

Runs the real checker against temporary mini repositories/fixtures.
Stdlib only.  Run directly:

    python3 tests/unit/test_matrix/test_check_test_matrix.py
"""

import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
CHECKER_PATH = os.path.join(REPO_ROOT, "scripts", "check-test-matrix.py")

import importlib.util  # noqa: E402

_spec = importlib.util.spec_from_file_location("check_test_matrix", CHECKER_PATH)
assert _spec is not None and _spec.loader is not None, "checker import failed"
ctm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ctm)


class Fixture:
    """Tiny repo fixture: src/alpha.c + src/beta.c + dongle main + header,
    one direct unit suite (alpha_suite), one structural suite (proto_suite),
    one hardware script."""

    def __init__(self, entries):
        self.root = tempfile.mkdtemp(prefix="t7matrix-")
        self._write(
            "src/alpha.c",
            "int alpha_run(void) { return 0; }\nint alpha_parse(void) { return -1; }\n",
        )
        self._write("src/beta.c", "int beta_init(void) { return 0; }\n")
        self._write(
            "dongle/hci_ipc/src/main.c", "int dongle_main(void) { return 0; }\n"
        )
        self._write("src/flpr_protocol.h", "#ifndef P_H\n#define P_H\n#endif\n")
        self._write(
            "tests/unit/alpha_suite/CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20.0)\n"
            "project(x)\n"
            "target_sources(app PRIVATE src/test_alpha.c)\n",
        )
        self._write(
            "tests/unit/alpha_suite/src/test_alpha.c",
            "void test_alpha_ok(void) {}\nvoid test_alpha_err(void) {}\n",
        )
        self._write(
            "tests/unit/proto_suite/CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20.0)\nproject(x)\n",
        )
        self._write(
            "tests/unit/proto_suite/src/test_proto.c",
            "void test_proto_struct(void) {}\n",
        )
        self._write("scripts/bsim-stage1-run.sh", "#!/bin/sh\n# scenario mono-7.5\n")
        self._write("scripts/hw_probe.sh", "#!/bin/sh\n")
        self._write("docs/evidence.md", "hardware evidence\n")
        self.manifest = self._write(
            "tests/test-matrix.json", json.dumps({"entries": entries}, indent=1)
        )

    def _write(self, rel, text):
        full = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as fh:
            fh.write(text)
        return full

    def run_checker(self, coverage_json=None):
        args = ["--repo-root", self.root, "--manifest", "tests/test-matrix.json"]
        if coverage_json:
            args += ["--coverage-json", coverage_json]
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = ctm.main(args)
        return code, buf.getvalue()

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


def alpha_direct(extra=None):
    entry = {
        "source": "src/alpha.c",
        "classification": "direct",
        "suites": [{"name": "alpha_suite", "evidence": "direct"}],
        "public_outcomes": [
            {"api": "alpha_run", "outcome": "success", "witness": "test_alpha_ok"},
            {"api": "alpha_run", "outcome": "error-class", "witness": "test_alpha_err"},
        ],
        "state_transitions": [],
        "function_exclusions": [],
        "hardware_acceptance": [],
    }
    if extra:
        entry.update(extra)
    return entry


def beta_direct():
    return {
        "source": "src/beta.c",
        "classification": "direct",
        "suites": [{"name": "alpha_suite", "evidence": "direct"}],
        "public_outcomes": [
            {"api": "beta_init", "outcome": "success", "witness": "test_alpha_ok"},
        ],
        "state_transitions": [],
        "function_exclusions": [],
        "hardware_acceptance": [],
    }


def dongle_entry():
    return {
        "source": "dongle/hci_ipc/src/main.c",
        "classification": "hardware-only",
        "reason": "dongle firmware, hardware-only",
        "excluded_from_numeric": True,
        "suites": [{"name": "hw_probe.sh", "evidence": "hardware"}],
        "public_outcomes": [],
        "state_transitions": [],
        "function_exclusions": [],
        "hardware_acceptance": ["hw_probe.sh", "docs/evidence.md"],
    }


def proto_entry():
    return {
        "source": "src/flpr_protocol.h",
        "classification": "header-structural",
        "reason": "shared header, structural proof",
        "excluded_from_numeric": True,
        "suites": [{"name": "proto_suite", "evidence": "structural"}],
        "public_outcomes": [],
        "state_transitions": [],
        "function_exclusions": [],
        "hardware_acceptance": [],
    }


def valid_entries():
    return [alpha_direct(), beta_direct(), dongle_entry(), proto_entry()]


def write_coverage(root, files):
    path = os.path.join(root, "cov.json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"files": files}, fh)
    return path


class CheckTestMatrixValid(unittest.TestCase):
    def test_valid_manifest_passes(self):
        fx = Fixture(valid_entries())
        try:
            code, out = fx.run_checker()
            self.assertEqual(0, code, out)
            self.assertIn("0 error(s)", out)
        finally:
            fx.cleanup()


class CheckTestMatrixInventory(unittest.TestCase):
    def test_missing_entry(self):
        entries = valid_entries()
        entries = [e for e in entries if e["source"] != "src/beta.c"]
        fx = Fixture(entries)
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: missing inventory entry: src/beta.c", out)
        finally:
            fx.cleanup()

    def test_duplicate_entry(self):
        entries = valid_entries() + [alpha_direct()]
        fx = Fixture(entries)
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: duplicate manifest entry: src/alpha.c", out)
        finally:
            fx.cleanup()

    def test_stale_entry(self):
        entries = valid_entries() + [
            {
                "source": "src/gone.c",
                "classification": "direct",
                "suites": [{"name": "alpha_suite", "evidence": "direct"}],
                "public_outcomes": [],
                "state_transitions": [],
                "function_exclusions": [],
                "hardware_acceptance": [],
            }
        ]
        fx = Fixture(entries)
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: stale manifest entry: src/gone.c", out)
        finally:
            fx.cleanup()


class CheckTestMatrixSchema(unittest.TestCase):
    def test_bad_classification(self):
        fx = Fixture(valid_entries())
        fx.manifest = fx._write(
            "tests/test-matrix.json",
            json.dumps(
                {
                    "entries": [
                        {**alpha_direct(), "classification": "bogus"},
                        beta_direct(),
                        dongle_entry(),
                        proto_entry(),
                    ]
                }
            ),
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: bad classification: src/alpha.c: 'bogus'", out)
        finally:
            fx.cleanup()

    def test_bad_evidence(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "suites": [{"name": "alpha_suite", "evidence": "model"}],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: bad evidence: src/alpha.c: alpha_suite: 'model'", out)
        finally:
            fx.cleanup()

    def test_nonexistent_suite(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "suites": [{"name": "no_such_suite", "evidence": "direct"}],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: unresolvable suite: src/alpha.c: no_such_suite", out)
        finally:
            fx.cleanup()

    def test_nonexistent_acceptance_command(self):
        fx = Fixture(
            [
                alpha_direct(),
                beta_direct(),
                {
                    **dongle_entry(),
                    "hardware_acceptance": ["no-such-command.sh"],
                },
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: unresolvable acceptance: dongle/hci_ipc/src/main.c: no-such-command.sh",
                out,
            )
        finally:
            fx.cleanup()

    def test_direct_without_direct_suite(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "suites": [{"name": "bsim:stage1", "evidence": "integration"}],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: direct source without direct suite: src/alpha.c", out)
        finally:
            fx.cleanup()

    def test_hardware_without_acceptance(self):
        fx = Fixture(
            [
                alpha_direct(),
                beta_direct(),
                {**dongle_entry(), "hardware_acceptance": []},
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: missing acceptance: dongle/hci_ipc/src/main.c (classification hardware-only)",
                out,
            )
        finally:
            fx.cleanup()

    def test_historical_counted_numerically(self):
        hist = {
            "source": "src/alpha.c",
            "classification": "historical-retired",
            "suites": [{"name": "alpha_suite", "evidence": "historical"}],
            "public_outcomes": [],
            "state_transitions": [],
            "function_exclusions": [],
            "hardware_acceptance": [],
        }
        fx = Fixture([hist, beta_direct(), dongle_entry(), proto_entry()])
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: historical source not excluded from numeric: src/alpha.c", out
            )
        finally:
            fx.cleanup()

    def test_header_structural_without_structural_suite(self):
        fx = Fixture(
            [
                alpha_direct(),
                beta_direct(),
                dongle_entry(),
                {
                    **proto_entry(),
                    "suites": [{"name": "alpha_suite", "evidence": "direct"}],
                },
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: header-structural without structural suite: src/flpr_protocol.h",
                out,
            )
        finally:
            fx.cleanup()


class CheckTestMatrixWitnessesAndApis(unittest.TestCase):
    def test_invented_api(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "public_outcomes": [
                        {
                            "api": "alpha_missing",
                            "outcome": "success",
                            "witness": "test_alpha_ok",
                        }
                    ],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn("error: invented API: src/alpha.c: 'alpha_missing'", out)
        finally:
            fx.cleanup()

    def test_invented_witness(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "public_outcomes": [
                        {
                            "api": "alpha_run",
                            "outcome": "success",
                            "witness": "test_no_such_test",
                        }
                    ],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: invented witness: src/alpha.c: 'test_no_such_test'", out
            )
        finally:
            fx.cleanup()

    def test_witness_without_test_source(self):
        fx = Fixture(
            [
                {
                    "source": "src/alpha.c",
                    "classification": "delegated-glue",
                    "reason": "glue only",
                    "excluded_from_numeric": True,
                    "suites": [{"name": "fw-build-5340", "evidence": "build"}],
                    "public_outcomes": [
                        {
                            "api": "alpha_run",
                            "outcome": "success",
                            "witness": "some_name",
                        }
                    ],
                    "state_transitions": [],
                    "function_exclusions": [],
                    "hardware_acceptance": ["fw-build-5340"],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: witness without test source: src/alpha.c: 'some_name'", out
            )
        finally:
            fx.cleanup()

    def test_empty_outcome_placeholder(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "public_outcomes": [
                        {"api": "alpha_run", "outcome": "", "witness": "test_alpha_ok"}
                    ],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            code, out = fx.run_checker()
            self.assertNotEqual(0, code)
            self.assertIn(
                "error: empty outcome placeholder: src/alpha.c: outcome 0 (outcome)",
                out,
            )
        finally:
            fx.cleanup()


class CheckTestMatrixCoverage(unittest.TestCase):
    def _entries(self):
        return valid_entries()

    def test_absent_coverage_source(self):
        fx = Fixture(self._entries())
        try:
            cov = write_coverage(
                fx.root,
                [
                    {
                        "file": "src/alpha.c",
                        "lines": [],
                        "functions": [{"name": "alpha_run", "execution_count": 1}],
                    }
                ],
            )
            code, out = fx.run_checker(cov)
            self.assertNotEqual(0, code)
            self.assertIn("error: absent from coverage: src/beta.c", out)
        finally:
            fx.cleanup()

    def test_zero_hit_function(self):
        fx = Fixture(self._entries())
        try:
            cov = write_coverage(
                fx.root,
                [
                    {
                        "file": "src/alpha.c",
                        "lines": [],
                        "functions": [
                            {"name": "alpha_run", "execution_count": 1},
                            {"name": "alpha_parse", "execution_count": 0},
                        ],
                    },
                    {
                        "file": "src/beta.c",
                        "lines": [],
                        "functions": [{"name": "beta_init", "execution_count": 1}],
                    },
                ],
            )
            code, out = fx.run_checker(cov)
            self.assertNotEqual(0, code)
            self.assertIn("error: zero-hit function: src/alpha.c: alpha_parse", out)
        finally:
            fx.cleanup()

    def test_zero_hit_function_excluded_with_reason(self):
        fx = Fixture(
            [
                {
                    **alpha_direct(),
                    "function_exclusions": [
                        {
                            "function": "alpha_parse",
                            "reason": "hardware-only path",
                            "evidence": "docs/evidence.md",
                        }
                    ],
                },
                beta_direct(),
                dongle_entry(),
                proto_entry(),
            ]
        )
        try:
            cov = write_coverage(
                fx.root,
                [
                    {
                        "file": "src/alpha.c",
                        "lines": [],
                        "functions": [
                            {"name": "alpha_run", "execution_count": 1},
                            {"name": "alpha_parse", "execution_count": 0},
                        ],
                    },
                    {
                        "file": "src/beta.c",
                        "lines": [],
                        "functions": [{"name": "beta_init", "execution_count": 1}],
                    },
                ],
            )
            code, out = fx.run_checker(cov)
            self.assertEqual(0, code, out)
        finally:
            fx.cleanup()

    def test_duplicate_variant_any_hit_satisfies(self):
        fx = Fixture(self._entries())
        try:
            cov = write_coverage(
                fx.root,
                [
                    {
                        "file": "src/alpha.c",
                        "lines": [],
                        "functions": [
                            {"name": "alpha_run", "execution_count": 2},
                            {"name": "alpha_parse", "execution_count": 0},
                            {"name": "alpha_parse", "execution_count": 3},
                        ],
                    },
                    {
                        "file": "src/beta.c",
                        "lines": [],
                        "functions": [{"name": "beta_init", "execution_count": 1}],
                    },
                ],
            )
            code, out = fx.run_checker(cov)
            self.assertEqual(0, code, out)
            self.assertIn(
                "note: zero-hit variant remains visible: src/alpha.c: alpha_parse", out
            )
        finally:
            fx.cleanup()


class CheckTestMatrixDeterminism(unittest.TestCase):
    def test_deterministic_multiple_error_output(self):
        fx = Fixture(
            [
                {**alpha_direct(), "classification": "bogus"},
                beta_direct(),
                {**dongle_entry(), "hardware_acceptance": ["no-such.sh"]},
                proto_entry(),
            ]
        )
        try:
            code1, out1 = fx.run_checker()
            code2, out2 = fx.run_checker()
            self.assertNotEqual(0, code1)
            self.assertEqual(code1, code2)
            self.assertEqual(out1, out2)
            for needle in (
                "error: bad classification: src/alpha.c",
                "error: unresolvable acceptance: dongle/hci_ipc/src/main.c: no-such.sh",
            ):
                self.assertIn(needle, out1)
            self.assertIn("check-test-matrix: 2 error(s), 0 note(s)", out1)
        finally:
            fx.cleanup()

    def test_missing_manifest_fatal(self):
        fx = Fixture(valid_entries())
        try:
            os.unlink(fx.manifest)
            code, out = fx.run_checker()
            self.assertEqual(2, code)
            self.assertIn("FATAL: manifest not found", out)
        finally:
            fx.cleanup()


if __name__ == "__main__":
    unittest.main(verbosity=2)
