#!/usr/bin/env python3
"""T7 Stage 2: focused tests for scripts/test-coverage.sh argument parsing,
output-dir safety, worktree-dirty rules, and baseline write/enforcement.

Runs the real script (copied into a temporary fixture repo) against fake
west/gcovr/gcov tools so no Zephyr build ever happens.  Stdlib only.
Run directly:

    python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
"""

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
RUNNER_SRC = os.path.join(REPO_ROOT, "scripts", "test-coverage.sh")
INVENTORY_SRC = os.path.join(REPO_ROOT, "scripts", "test_inventory.py")

FAKE_WEST = """#!/usr/bin/env bash
d=""
prev=""
for a in "$@"; do
  if [ "$prev" = "-d" ]; then d="$a"; fi
  prev="$a"
done
[ -n "$d" ] || exit 1
mkdir -p "$d/zephyr"
printf '#!/usr/bin/env bash\\ntouch "$(dirname "$0")/fake.gcda"\\nexit 0\\n' > "$d/zephyr/zephyr.exe"
chmod +x "$d/zephyr/zephyr.exe"
touch "$d/zephyr/fake.gcno"
exit 0
"""

FAKE_GCOV = """#!/usr/bin/env bash
echo "gcov (GCC) 14.3.0"
exit 0
"""

FAKE_GCOVR = """#!/usr/bin/env python3
import json, os, shutil, sys

args = sys.argv[1:]
if args and args[0] == "--version":
    print("gcovr 8.4")
    sys.exit(0)

out_json = out_sum = out_txt = out_html = None
for a in args:
    if a.startswith("--json="):
        out_json = a[len("--json="):]
    elif a.startswith("--json-summary="):
        out_sum = a[len("--json-summary="):]
    elif a.startswith("--txt="):
        out_txt = a[len("--txt="):]
    elif a.startswith("--html-details="):
        out_html = a[len("--html-details="):]

if out_json:
    template = os.environ.get("FAKE_COVERAGE_JSON")
    if template and os.path.exists(template):
        shutil.copy(template, out_json)
    else:
        with open(out_json, "w") as fh:
            json.dump({"gcovr/format_version": "8.4", "files": []}, fh)
if out_sum:
    with open(out_sum, "w") as fh:
        json.dump({"files": []}, fh)
for path in (out_txt, out_html):
    if path:
        with open(path, "w") as fh:
            fh.write("x")
sys.exit(0)
"""

COVERAGE_TEMPLATE = {
    "gcovr/format_version": "8.4",
    "files": [
        {
            "file": "src/foo.c",
            "lines": [{"line_number": 1, "count": 1, "branches": []}],
            "functions": [{"name": "foo", "execution_count": 1}],
        },
        {
            "file": "src/bar.c",
            "lines": [{"line_number": 1, "count": 1, "branches": []}],
            "functions": [{"name": "bar", "execution_count": 1}],
        },
    ],
}

MANIFEST = {
    "entries": [
        {
            "source": "src/foo.c",
            "classification": "direct",
            "stateful": False,
            "suites": [{"name": "fake_suite", "evidence": "direct"}],
            "public_outcomes": [{"api": "foo", "outcome": "0", "witness": "test_foo"}],
            "state_transitions": [],
            "function_exclusions": [],
            "hardware_acceptance": [],
        },
        {
            "source": "src/bar.c",
            "classification": "direct",
            "stateful": False,
            "suites": [{"name": "fake_suite", "evidence": "direct"}],
            "public_outcomes": [{"api": "bar", "outcome": "0", "witness": "test_foo"}],
            "state_transitions": [],
            "function_exclusions": [],
            "hardware_acceptance": [],
        },
    ]
}


class RunnerFixture:
    def __init__(self, dirty=False, manifest=None, exec_suite=None, no_c_suites=False):
        self.root = tempfile.mkdtemp(prefix="t7covrun-")
        self.repo = os.path.join(self.root, "repo")
        os.makedirs(os.path.join(self.repo, "scripts"))
        shutil.copy(RUNNER_SRC, os.path.join(self.repo, "scripts", "test-coverage.sh"))
        os.chmod(
            os.path.join(self.repo, "scripts", "test-coverage.sh"),
            os.stat(os.path.join(self.repo, "scripts", "test-coverage.sh")).st_mode
            | stat.S_IXUSR,
        )
        # The runner discovers suites through the shared inventory module;
        # the fixture repo must carry it too.
        shutil.copy(
            INVENTORY_SRC, os.path.join(self.repo, "scripts", "test_inventory.py")
        )
        if not no_c_suites:
            os.makedirs(os.path.join(self.repo, "tests", "unit", "fake_suite"))
            with open(
                os.path.join(self.repo, "tests", "unit", "fake_suite", "testcase.yaml"),
                "w",
            ) as fh:
                fh.write(
                    "tests:\n  unit.fake_suite:\n    platform_allow: native_sim/native/64\n"
                )
            with open(
                os.path.join(
                    self.repo, "tests", "unit", "fake_suite", "CMakeLists.txt"
                ),
                "w",
            ) as fh:
                fh.write("cmake_minimum_required(VERSION 3.20.0)\nproject(x)\n")
            if exec_suite:
                # A CMakeLists-only dir is an exec-only C suite (no
                # testcase.yaml) — the empty-category side of the fixture.
                os.makedirs(os.path.join(self.repo, "tests", "unit", exec_suite))
                with open(
                    os.path.join(
                        self.repo, "tests", "unit", exec_suite, "CMakeLists.txt"
                    ),
                    "w",
                ) as fh:
                    fh.write("cmake_minimum_required(VERSION 3.20.0)\nproject(x)\n")
        os.makedirs(os.path.join(self.repo, "tests"), exist_ok=True)
        with open(os.path.join(self.repo, "tests", "test-matrix.json"), "w") as fh:
            json.dump(manifest or MANIFEST, fh)
        os.makedirs(os.path.join(self.repo, "src"))
        with open(os.path.join(self.repo, "src", "foo.c"), "w") as fh:
            fh.write("int foo(void) { return 0; }\n")
        with open(os.path.join(self.repo, "src", "bar.c"), "w") as fh:
            fh.write("int bar(void) { return 0; }\n")

        # git repo with an initial commit (clean state)
        env = dict(os.environ)
        subprocess.run(["git", "init", "-q", self.repo], check=True)
        subprocess.run(
            ["git", "-C", self.repo, "config", "user.email", "t@t"], check=True
        )
        subprocess.run(["git", "-C", self.repo, "config", "user.name", "t"], check=True)
        subprocess.run(["git", "-C", self.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", self.repo, "commit", "-q", "-m", "fixture"], check=True
        )
        if dirty:
            with open(os.path.join(self.repo, "untracked.txt"), "w") as fh:
                fh.write("dirty\n")

        # fake tools
        self.bin = os.path.join(self.root, "bin")
        os.makedirs(self.bin)
        for name, body in (
            ("west", FAKE_WEST),
            ("gcov", FAKE_GCOV),
            ("gcovr", FAKE_GCOVR),
        ):
            path = os.path.join(self.bin, name)
            with open(path, "w") as fh:
                fh.write(body)
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

        self.coverage_template = os.path.join(self.root, "coverage.json")
        with open(self.coverage_template, "w") as fh:
            json.dump(COVERAGE_TEMPLATE, fh)

        self.env = dict(os.environ)
        self.env["PATH"] = self.bin + os.pathsep + self.env.get("PATH", "")
        self.env["ZEPHYR_BASE"] = "/fake/zephyr"
        self.env["NIX_HARDENING_ENABLE"] = ""
        self.env["FAKE_COVERAGE_JSON"] = self.coverage_template

    def run(self, *args, output=None):
        script = os.path.join(self.repo, "scripts", "test-coverage.sh")
        out = output or os.path.join(self.root, "out")
        cmd = [script] + list(args) + ["--output", out]
        proc = subprocess.run(
            cmd, capture_output=True, text=True, env=self.env, timeout=120
        )
        return proc.returncode, proc.stdout + proc.stderr, out

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)


class RunnerArgs(unittest.TestCase):
    def _fx(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        return fx

    def test_unknown_flag_fails(self):
        fx = self._fx()
        rc, out, _ = fx.run("--report-only", "--bogus-flag")
        self.assertNotEqual(0, rc)
        self.assertIn("unknown option: --bogus-flag", out)

    def test_default_mode_enforces_committed_baseline(self):
        fx = self._fx()
        # No mode flag → default enforcement against the committed
        # tests/coverage-baseline.json, which this fixture does not have.
        rc, out, _ = fx.run()
        self.assertNotEqual(0, rc)
        self.assertIn("baseline not found", out)

    def test_missing_output_fails(self):
        fx = self._fx()
        script = os.path.join(fx.repo, "scripts", "test-coverage.sh")
        proc = subprocess.run(
            [script, "--report-only"],
            capture_output=True,
            text=True,
            env=fx.env,
            timeout=60,
        )
        self.assertNotEqual(0, proc.returncode)
        self.assertIn("--output DIR is required", proc.stdout + proc.stderr)


class RunnerDirtyWorktree(unittest.TestCase):
    def test_report_only_allows_dirty(self):
        fx = RunnerFixture(dirty=True)
        self.addCleanup(fx.cleanup)
        rc, out, out_dir = fx.run("--report-only")
        self.assertEqual(0, rc, out)
        with open(os.path.join(out_dir, "run-manifest.json")) as fh:
            manifest = json.load(fh)
        self.assertTrue(manifest["dirty"], "dirty recorded")

    def test_enforcement_rejects_dirty(self):
        fx = RunnerFixture(dirty=True)
        self.addCleanup(fx.cleanup)
        rc, out, _ = fx.run()
        self.assertNotEqual(0, rc)
        self.assertIn("worktree is dirty", out)

    def test_write_baseline_rejects_dirty(self):
        fx = RunnerFixture(dirty=True)
        self.addCleanup(fx.cleanup)
        rc, out, _ = fx.run("--write-baseline", os.path.join(fx.root, "b.json"))
        self.assertNotEqual(0, rc)
        self.assertIn("worktree is dirty", out)


class RunnerCleanOutput(unittest.TestCase):
    def test_nonempty_output_requires_clean_flag(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        out_dir = os.path.join(fx.root, "prefilled")
        os.makedirs(out_dir)
        with open(os.path.join(out_dir, "stale.txt"), "w") as fh:
            fh.write("x")
        rc, out, _ = fx.run("--report-only", output=out_dir)
        self.assertNotEqual(0, rc)
        self.assertIn("output directory not empty", out)
        self.assertTrue(os.path.exists(os.path.join(out_dir, "stale.txt")))

    def test_clean_output_removes_and_proceeds(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        out_dir = os.path.join(fx.root, "prefilled")
        os.makedirs(out_dir)
        with open(os.path.join(out_dir, "stale.txt"), "w") as fh:
            fh.write("x")
        rc, out, _ = fx.run("--report-only", "--clean-output", output=out_dir)
        self.assertEqual(0, rc, out)
        self.assertFalse(os.path.exists(os.path.join(out_dir, "stale.txt")))

    def test_clean_output_refuses_unsafe_paths(self):
        for unsafe in (
            "/",
            "/etc",
            "/usr",
            os.path.expanduser("~"),
        ):
            fx = RunnerFixture()
            self.addCleanup(fx.cleanup)
            rc, out, _ = fx.run("--report-only", "--clean-output", output=unsafe)
            self.assertNotEqual(0, rc, "path %s must be refused" % unsafe)
            self.assertIn("refusing to clean", out)

    def test_clean_output_refuses_repo_root_and_cwd(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        rc, out, _ = fx.run("--report-only", "--clean-output", output=fx.repo)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean", out)

    def test_clean_output_refuses_relative_path(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        rc, out, _ = fx.run(
            "--report-only", "--clean-output", output="some-relative-dir"
        )
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean path outside the allowed output tree", out)


class RunnerBaseline(unittest.TestCase):
    def test_write_and_enforce_baseline(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = os.path.join(fx.root, "baseline.json")

        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        self.assertTrue(os.path.exists(baseline))
        with open(baseline) as fh:
            bl = json.load(fh)
        self.assertEqual(bl["schema_version"], 1)
        self.assertIn("src/foo.c", bl["population"])
        self.assertIn("src/foo.c", bl["files"])
        self.assertEqual(bl["totals"]["lines"], [2, 2])

        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertEqual(0, rc, out)
        self.assertIn("baseline enforcement PASS", out)

        rc, out, _ = fx.run("--report-only", output=os.path.join(fx.root, "out2"))
        self.assertEqual(0, rc, out)

    def test_baseline_enforcement_fails_on_regression(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = os.path.join(fx.root, "baseline.json")

        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)

        # Inflate the baseline totals so current (2/2) is below it (3/2).
        with open(baseline) as fh:
            bl = json.load(fh)
        bl["totals"]["lines"] = [3, 2]
        with open(baseline, "w") as fh:
            json.dump(bl, fh)

        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("overall lines below baseline", out)

    def test_population_drift_fails(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = os.path.join(fx.root, "baseline.json")

        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)

        # Manifest loses src/bar.c → current population missing a baseline file.
        manifest = dict(MANIFEST)
        manifest["entries"] = [
            e for e in manifest["entries"] if e["source"] != "src/bar.c"
        ]
        with open(os.path.join(fx.repo, "tests", "test-matrix.json"), "w") as fh:
            json.dump(manifest, fh)

        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "manifest update"], check=True
        )

        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("baseline file missing from current population: src/bar.c", out)


class RunnerToolVersionEnforcement(unittest.TestCase):
    """Baseline gcovr/gcov version enforcement (R0): recorded versions are
    compared in baseline mode, absent fields stay accepted, --write-baseline
    records current versions, and --report-only never enforces versions."""

    def _write_baseline(self, fx, mutate=None):
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        self.assertTrue(os.path.exists(baseline))
        with open(baseline) as fh:
            bl = json.load(fh)
        # --write-baseline records the fake current tool versions.
        self.assertEqual(bl["gcovr_version"], "gcovr 8.4")
        self.assertEqual(bl["gcov_version"], "gcov (GCC) 14.3.0")
        if mutate is not None:
            mutate(bl)
            with open(baseline, "w") as fh:
                json.dump(bl, fh)
        return baseline

    def test_matching_versions_pass(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(fx)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertEqual(0, rc, out)
        self.assertIn("baseline enforcement PASS", out)

    def test_gcovr_version_mismatch_fails_with_diagnostic(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(
            fx, mutate=lambda bl: bl.__setitem__("gcovr_version", "gcovr 8.3")
        )
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn(
            "error: gcovr version mismatch: current 'gcovr 8.4' vs "
            "baseline 'gcovr 8.3' (refresh intentionally with "
            "--write-baseline %s)" % baseline,
            out,
        )

    def test_gcov_version_mismatch_fails_with_diagnostic(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(
            fx, mutate=lambda bl: bl.__setitem__("gcov_version", "gcov (GCC) 14.2.0")
        )
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn(
            "error: gcov version mismatch: current 'gcov (GCC) 14.3.0' vs "
            "baseline 'gcov (GCC) 14.2.0' (refresh intentionally with "
            "--write-baseline %s)" % baseline,
            out,
        )

    def test_absent_version_fields_remain_accepted(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(
            fx,
            mutate=lambda bl: (
                bl.pop("gcovr_version", None),
                bl.pop("gcov_version", None),
            ),
        )
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertEqual(0, rc, out)
        self.assertIn("baseline enforcement PASS", out)

    def test_present_null_version_fails(self):
        # A baseline that stores a null version must fail in baseline mode:
        # only entirely-omitted legacy fields are accepted, never a present
        # null/non-string/empty value.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(
            fx, mutate=lambda bl: bl.__setitem__("gcovr_version", None)
        )
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn(
            "error: gcovr baseline version invalid: None (expected non-empty "
            "string; refresh intentionally with --write-baseline %s)" % baseline,
            out,
        )

    def test_present_empty_or_nonstring_version_fails(self):
        # A present gcovr_version that is an empty string or a non-string
        # (e.g. integer) must fail baseline mode with the invalid-version
        # diagnostic naming the actual value, the expected non-empty
        # string, and the intentional --write-baseline refresh instruction.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(fx)
        with open(baseline) as fh:
            bl = json.load(fh)
        for bad in ("", 42):
            bl["gcovr_version"] = bad
            with open(baseline, "w") as fh:
                json.dump(bl, fh)
            rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
            self.assertNotEqual(0, rc, "present gcovr_version %r must fail" % (bad,))
            self.assertIn(
                "error: gcovr baseline version invalid: %r (expected non-empty "
                "string; refresh intentionally with --write-baseline %s)"
                % (bad, baseline),
                out,
            )

    def test_report_only_does_not_enforce_versions(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        baseline = self._write_baseline(
            fx, mutate=lambda bl: bl.__setitem__("gcovr_version", "gcovr 0.0")
        )
        rc, out, _ = fx.run("--report-only", output=os.path.join(fx.root, "out2"))
        self.assertEqual(0, rc, out)


class RunnerInventoryDiscovery(unittest.TestCase):
    """R3: suite discovery comes from the shared scripts/test_inventory.py
    module (not copied shell lists); empty categories are valid, but at
    least one C suite is still required."""

    def test_exec_only_suite_discovered_via_inventory(self):
        fx = RunnerFixture(exec_suite="exec_suite")
        self.addCleanup(fx.cleanup)
        rc, out, out_dir = fx.run("--report-only")
        self.assertEqual(0, rc, out)
        with open(os.path.join(out_dir, "run-manifest.json")) as fh:
            manifest = json.load(fh)
        names = {s["name"]: s["kind"] for s in manifest["suites"]}
        self.assertEqual(names, {"fake_suite": "twister", "exec_suite": "exec-only"})

    def test_empty_category_is_valid_but_no_c_suites_fails(self):
        # fake_suite (twister) present, exec category empty: valid.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        rc, out, out_dir = fx.run("--report-only")
        self.assertEqual(0, rc, out)
        with open(os.path.join(out_dir, "run-manifest.json")) as fh:
            manifest = json.load(fh)
        self.assertEqual([s["name"] for s in manifest["suites"]], ["fake_suite"])

        # No C suites at all: the runner must refuse loudly.
        fx2 = RunnerFixture(no_c_suites=True)
        self.addCleanup(fx2.cleanup)
        rc2, out2, _ = fx2.run("--report-only")
        self.assertNotEqual(0, rc2)
        self.assertIn("no suites discovered", out2)

    def test_missing_inventory_module_fails_clearly(self):
        # If the shared inventory module is absent the runner must fail
        # with a clear message instead of silently building nothing.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        os.remove(os.path.join(fx.repo, "scripts", "test_inventory.py"))
        rc, out, _ = fx.run("--report-only")
        self.assertNotEqual(0, rc)
        self.assertIn("test_inventory.py", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
