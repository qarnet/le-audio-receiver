#!/usr/bin/env python3
"""Focused tests for scripts/test-coverage.sh argument parsing,
output-dir safety, worktree-dirty rules, and baseline write/enforcement.

Runs the real script (copied into a temporary fixture repo) against fake
west/gcovr/gcov tools so no Zephyr build ever happens.  Stdlib only.
Run directly:

    python3 tests/unit/test_coverage_runner/test_test_coverage_runner.py
"""

import json
import glob
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
RUNNER_SRC = os.path.join(REPO_ROOT, "scripts", "test-coverage.sh")
INVENTORY_SRC = os.path.join(REPO_ROOT, "scripts", "test_inventory.py")
TEST_ALL_SRC = os.path.join(REPO_ROOT, "scripts", "test-all.sh")

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
log = os.environ.get("FAKE_GCOVR_ARGS_LOG")
if log:
    with open(log, "a") as fh:
        fh.write("\\x00".join(args) + "\\n")
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

# Fake gcovr with the real gcovr-8.4 internal-function filter emulated:
# function entries whose name starts "__" are removed from every produced
# json unless --include-internal-functions is in argv.  Trace inputs
# recorded to FAKE_GCOVR_ARGS_LOG exactly like the basic fake above.
FAKE_GCOVR_FILTERING = """#!/usr/bin/env python3
import json, os, sys

args = sys.argv[1:]
log = os.environ.get("FAKE_GCOVR_ARGS_LOG")
if log:
    with open(log, "a") as fh:
        fh.write("\\x00".join(args) + "\\n")
if args and args[0] == "--version":
    print("gcovr 8.4")
    sys.exit(0)

keep_internal = "--include-internal-functions" in args
out_json = out_sum = None
for a in args:
    if a.startswith("--json="):
        out_json = a[len("--json="):]
    elif a.startswith("--json-summary="):
        out_sum = a[len("--json-summary="):]


def load(path):
    if path and os.path.exists(path):
        with open(path, "rb") as fh:
            return json.load(fh)
    template = os.environ.get("FAKE_COVERAGE_JSON")
    if template and os.path.exists(template):
        with open(template, "rb") as fh:
            return json.load(fh)
    return {"gcovr/format_version": "8.4", "files": []}


def filter_files(data):
    if keep_internal:
        return data["files"]
    files = []
    for f in data.get("files", []):
        f = dict(f)
        f["functions"] = [
            fn
            for fn in f.get("functions", [])
            if not fn.get("name", "").startswith("__")
        ]
        files.append(f)
    return files


if out_json and "--add-tracefile" in args:
    merged = {}
    for a in args:
        if a.endswith(".json") and "trace_" in a:
            for f in filter_files(load(a)):
                entry = dict(f)
                existing = merged.setdefault(f["file"], entry)
                if existing is not entry:
                    continue
    with open(out_json, "w") as fh:
        json.dump({"gcovr/format_version": "8.4", "files": list(merged.values())}, fh)
elif out_json:
    with open(out_json, "w") as fh:
        json.dump({"gcovr/format_version": "8.4", "files": filter_files(load(None))}, fh)
if out_sum:
    if "--add-tracefile" in args:
        merged = {}
        names = {}
        for a in args:
            if a.endswith(".json") and "trace_" in a:
                for f in filter_files(load(a)):
                    prev = names.get(f["file"], {"functions": [], "lines": []})
                    functions = prev["functions"] + [
                        fn
                        for fn in f.get("functions", [])
                        if fn.get("name") not in {
                            e.get("name") for e in prev["functions"]
                        }
                    ]
                    lines = prev["lines"] + [
                        ln
                        for ln in f.get("lines", [])
                        if ln.get("line_number")
                        not in {e.get("line_number") for e in prev["lines"]}
                    ]
                    names[f["file"]] = {
                        "file": f["file"],
                        "functions": functions,
                        "lines": lines,
                    }
        summary_files = []
        for f in names.values():
            summary_files.append(
                {
                    "file": f["file"],
                    "lines": {
                        "covered": sum(1 for l in f["lines"] if l.get("count")),
                        "total": len(f["lines"]),
                    },
                    "functions": {
                        "covered": sum(
                            1 for fn in f["functions"] if fn.get("execution_count")
                        ),
                        "total": len(f["functions"]),
                    },
                }
            )
        summary = {"files": summary_files}
    else:
        summary = {"files": []}
    with open(out_sum, "w") as fh:
        json.dump(summary, fh)
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
    def __init__(
        self,
        dirty=False,
        manifest=None,
        exec_suite=None,
        no_c_suites=False,
        additions=None,
        extra_sources=(),
    ):
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
        if additions is not None:
            with open(
                os.path.join(self.repo, "tests", "coverage-additions.json"), "w"
            ) as fh:
                json.dump(additions, fh)
        os.makedirs(os.path.join(self.repo, "src"))
        with open(os.path.join(self.repo, "src", "foo.c"), "w") as fh:
            fh.write("int foo(void) { return 0; }\n")
        with open(os.path.join(self.repo, "src", "bar.c"), "w") as fh:
            fh.write("int bar(void) { return 0; }\n")
        for name in extra_sources:
            with open(os.path.join(self.repo, "src", name), "w") as fh:
                fh.write("int %s(void) { return 0; }\n" % name[: -len(".c")])

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
    def linked_fixture(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        linked = os.path.join(fx.root, "linked")
        subprocess.run(
            ["git", "-C", fx.repo, "worktree", "add", "--detach", linked, "HEAD"],
            check=True,
            capture_output=True,
        )
        fx.repo = linked
        return fx

    def test_clean_linked_worktree_enforces_baseline(self):
        fx = self.linked_fixture()
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(rc, 0, out)
        rc, out, output = fx.run(
            "--baseline", baseline, output=os.path.join(fx.root, "enforced")
        )
        self.assertEqual(rc, 0, out)
        with open(os.path.join(output, "run-manifest.json")) as stream:
            manifest = json.load(stream)
        self.assertFalse(manifest["dirty"])
        self.assertEqual(
            manifest["source_commit"],
            subprocess.check_output(
                ["git", "-C", fx.repo, "rev-parse", "HEAD"], text=True
            ).strip(),
        )

    def test_dirty_linked_worktree_still_rejected(self):
        fx = self.linked_fixture()
        with open(os.path.join(fx.repo, "owner-edit.txt"), "w") as stream:
            stream.write("Owner change must remain protected\n")
        rc, out, _ = fx.run()
        self.assertNotEqual(rc, 0)
        self.assertIn("worktree is dirty", out)

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


class RunnerOutputPathSafety(unittest.TestCase):
    """Safety fix (P9 documentation-hygiene track): the containment check is
    canonical-path based.  A repo child (even via /tmp, traversal, or a
    symlink alias) must be rejected BEFORE any rm -rf, an ancestor of the
    repo must be rejected, and an external temp output must still work."""

    def _repo_child_fixture(self, child_dir, marker="keep.txt"):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        os.makedirs(child_dir)
        marker_path = os.path.join(child_dir, marker)
        with open(marker_path, "w") as fh:
            fh.write("keep")
        return fx, marker_path

    def test_repo_child_rejected_before_deletion(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        child = os.path.join(fx.repo, "cov-out")
        os.makedirs(child)
        marker = os.path.join(child, "keep.txt")
        with open(marker, "w") as fh:
            fh.write("keep")
        rc, out, _ = fx.run("--report-only", "--clean-output", output=child)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean a path inside the repo root", out)
        self.assertTrue(os.path.exists(marker), "repo child must not be deleted")

    def test_repo_child_with_traversal_rejected(self):
        # /tmp/<root>/other/../repo/cov-out canonically resolves inside the
        # repo even though the raw string is a /tmp child.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        os.makedirs(os.path.join(fx.root, "other"))
        child = os.path.join(fx.root, "other", "..", "repo", "cov-out")
        os.makedirs(child)
        marker = os.path.join(child, "keep.txt")
        with open(marker, "w") as fh:
            fh.write("keep")
        rc, out, _ = fx.run("--report-only", "--clean-output", output=child)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean a path inside the repo root", out)
        self.assertTrue(os.path.exists(marker))

    def test_symlink_alias_of_repo_root_rejected(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        link = os.path.join(fx.root, "repo-link")
        os.symlink(fx.repo, link)
        rc, out, _ = fx.run("--report-only", "--clean-output", output=link)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean the repo root", out)
        self.assertTrue(os.path.exists(os.path.join(fx.repo, "scripts")))

    def test_symlink_alias_of_repo_child_rejected_before_deletion(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        child = os.path.join(fx.repo, "cov-out")
        os.makedirs(child)
        marker = os.path.join(child, "keep.txt")
        with open(marker, "w") as fh:
            fh.write("keep")
        link = os.path.join(fx.root, "child-link")
        os.symlink(child, link)
        rc, out, _ = fx.run("--report-only", "--clean-output", output=link)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean a path inside the repo root", out)
        self.assertTrue(os.path.exists(marker))

    def test_repo_ancestor_rejected(self):
        # fx.root contains the repo — deleting it would take the repo with it.
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        rc, out, _ = fx.run("--report-only", "--clean-output", output=fx.root)
        self.assertNotEqual(0, rc)
        self.assertIn("refusing to clean a path containing the repo root", out)
        self.assertTrue(os.path.exists(os.path.join(fx.repo, "scripts")))

    def test_external_temp_output_works(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        out_dir = os.path.join(fx.root, "external-out")
        rc, out, _ = fx.run("--report-only", "--clean-output", output=out_dir)
        self.assertEqual(0, rc, out)
        self.assertTrue(os.path.exists(os.path.join(out_dir, "run-manifest.json")))


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


class RunnerAdditionsSidecar(unittest.TestCase):
    """Additive independent coverage contract: tests/coverage-additions.json
    pins new sources with exact reference metrics while the frozen baseline
    stays byte-for-byte unchanged.  Real shell, fake tools, sidecar anchored
    to the fixture's own computed baseline hash (never the project
    literal)."""

    def _fixture(self):
        fx = RunnerFixture(
            additions=None,
            extra_sources=("bt_audio_ltv_guard.c",),
        )
        self.addCleanup(fx.cleanup)
        return fx

    def _rewrite_sidecar(self, fx, data):
        with open(os.path.join(fx.repo, "tests", "coverage-additions.json"), "w") as fh:
            json.dump(data, fh)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "sidecar update"], check=True
        )

    def _register_guard_in_manifest(self, fx):
        manifest = json.loads(json.dumps(MANIFEST))
        manifest["entries"].append(
            {
                "source": "src/bt_audio_ltv_guard.c",
                "classification": "direct",
                "stateful": False,
                "suites": [{"name": "fake_suite", "evidence": "direct"}],
                "public_outcomes": [
                    {
                        "api": "__wrap_bt_audio_data_parse",
                        "outcome": "0",
                        "witness": "test_foo",
                    }
                ],
                "state_transitions": [],
                "function_exclusions": [],
                "hardware_acceptance": [],
            }
        )
        with open(os.path.join(fx.repo, "tests", "test-matrix.json"), "w") as fh:
            json.dump(manifest, fh)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "manifest guard"], check=True
        )

    def _anchor(self, fx, baseline_path):
        import hashlib

        with open(baseline_path, "rb") as fh:
            return hashlib.sha256(fh.read()).hexdigest()

    def _coverage_template_with_guard(
        self, fx, *, guard_covered=13, guard_branches=12, guard_functions=1
    ):
        # Extend the template with a guard file whose data comes from the
        # fake gcovr boundary; guard line/branch/function totals 13/12/1.
        template = json.loads(json.dumps(COVERAGE_TEMPLATE))
        guard_lines = [
            {"line_number": i, "count": 1 if i <= guard_covered else 0, "branches": []}
            for i in range(1, 14)
        ]
        guard_lines[12]["branches"] = [
            {"count": guard_branches, "fallthrough": False, "throw": False}
            for _ in range(12)
        ]
        guard_functions = [
            {
                "name": "__wrap_bt_audio_data_parse",
                "execution_count": guard_functions,
            }
        ]
        template["files"].append(
            {
                "file": "src/bt_audio_ltv_guard.c",
                "lines": guard_lines,
                "functions": guard_functions,
            }
        )
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        return template

    def _write_additions(
        self, fx, *, anchor="computed", reference=(13, 13, 12, 12, 1, 1)
    ):
        lines_c, lines_t, br_c, br_t, fn_c, fn_t = reference
        data = {
            "schema_version": 1,
            "frozen_baseline_sha256": anchor,
            "files": {
                "src/bt_audio_ltv_guard.c": {
                    "lines": [lines_c, lines_t],
                    "branches": [br_c, br_t],
                    "functions": [fn_c, fn_t],
                }
            },
        }
        with open(os.path.join(fx.repo, "tests", "coverage-additions.json"), "w") as fh:
            json.dump(data, fh)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "additions sidecar"],
            check=True,
        )
        return data

    def test_valid_frozen_plus_addition_passes(self):
        fx = self._fixture()
        template = self._coverage_template_with_guard(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._register_guard_in_manifest(fx)
        self._write_additions(fx, anchor=anchor)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertEqual(0, rc, out)
        self.assertIn("baseline enforcement PASS", out)
        with open(os.path.join(_out := fx.root, "out", "run-manifest.json")) as fh:
            manifest = json.load(fh)
        self.assertTrue(manifest["coverage_additions_sha256"], "additions recorded")
        numeric = None
        with open(os.path.join(fx.root, "out", "numeric-summary.json"), "rb") as fh:
            numeric = json.load(fh)
        self.assertTrue(numeric.get("addition_sidecar"))
        # Separate frozen vs additive display present in the run log.
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertIn("numeric frozen-population", out)
        self.assertIn("numeric additive-sidecar", out)

    def test_original_regression_not_hidden_by_perfect_addition(self):
        fx = self._fixture()
        template0 = self._coverage_template_with_guard(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template0, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._register_guard_in_manifest(fx)
        self._write_additions(fx, anchor=anchor)
        # Make the original src/foo.c regressed below its frozen ratio
        # (1/2 lines) while the additive guard stays perfect, proving the
        # perfect addition cannot hide a frozen-population regression.
        template = self._coverage_template_with_guard(fx)
        for f in template["files"]:
            if f["file"].endswith("foo.c"):
                f["lines"] = [
                    dict(f["lines"][0], count=1),
                    dict(f["lines"][-1], count=0),
                ]
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("src/foo.c lines below baseline", out)

    def test_addition_uncovered_metric_rejects(self):
        fx = self._fixture()
        template = self._coverage_template_with_guard(
            fx, guard_covered=12, guard_branches=0
        )
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._write_additions(fx, anchor=anchor)
        self._register_guard_in_manifest(fx)
        # Current run: guard present but one line uncovered (+ no
        # branches at all, both below the additive reference).
        template = self._coverage_template_with_guard(
            fx, guard_covered=12, guard_branches=0
        )
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("below additive reference", out)

    def test_zero_or_missing_addition_metrics_reject(self):
        fx = self._fixture()
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._write_additions(fx, anchor=anchor)
        # Current coverage: guard file totally absent from coverage
        # (e.g. filtered), so additive source has no instrumented metric.
        template = json.loads(json.dumps(COVERAGE_TEMPLATE))
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn(
            "additive source missing from current run: src/bt_audio_ltv_guard.c",
            out,
        )

    def test_unknown_third_source_rejects_without_sidecar(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        # Baseline freezes the original population only; a later manifest
        # registration brings a guarded new source into the current
        # population with NO sidecar: the unknown-new-source rule catches
        # it even though its coverage is perfect.
        template = self._coverage_template_with_guard(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        self._register_guard_in_manifest(fx)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("new file in current population not in baseline", out)

    def test_overlapping_sidecar_entry_rejects(self):
        fx = self._fixture()
        template = self._coverage_template_with_guard(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._register_guard_in_manifest(fx)
        data = self._write_additions(fx, anchor=anchor)
        data["files"]["src/foo.c"] = {
            "lines": [1, 1],
            "branches": [1, 1],
            "functions": [1, 1],
        }
        self._rewrite_sidecar(fx, data)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)
        self.assertIn("overlap the frozen baseline population", out)

    def _prepare_valid_flow(self, fx, *, guard_covered=13, guard_branches=12):
        """Baseline first, then guard manifest + sidecar; returns sidecar dict."""
        template = self._coverage_template_with_guard(
            fx, guard_covered=guard_covered, guard_branches=guard_branches
        )
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        self._register_guard_in_manifest(fx)
        self._write_additions(fx, anchor=anchor)
        return baseline

    def _run_and_out(self, fx, baseline):
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        return rc, out

    def test_empty_sidecar_file_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        open(path, "w").close()
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "empty"], check=True
        )
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)
        self.assertIn("file is empty", out)

    def test_sidecar_dangling_symlink_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        os.remove(path)
        os.symlink(os.path.join(fx.root, "does-not-exist.json"), path)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "symlink"], check=True
        )
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)
        self.assertIn("open failed", out)

    def test_sidecar_directory_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        os.remove(path)
        os.mkdir(path)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(["git", "-C", fx.repo, "commit", "-q", "-m", "dir"], check=True)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)
        self.assertIn("not a regular file", out)

    def test_sidecar_nonfinite_constant_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        with open(path, "w") as fh:
            fh.write(
                '{\n  "schema_version": 1,\n  "frozen_baseline_sha256": "x",\n'
                '  "files": {"src/bt_audio_ltv_guard.c": {"lines": [13, 13],'
                ' "branches": [NaN, 12], "functions": [1, 1]}}\n}\n'
            )
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(["git", "-C", fx.repo, "commit", "-q", "-m", "nan"], check=True)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)
        self.assertIn("nonfinite JSON constant", out)

    def test_sidecar_top_level_list_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        with open(path, "w") as fh:
            fh.write("[]\n")
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(["git", "-C", fx.repo, "commit", "-q", "-m", "list"], check=True)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("top-level additions must be an object", out)

    def test_sidecar_metrics_list_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        anchor = self._anchor(fx, baseline)
        with open(path, "w") as fh:
            fh.write(
                '{\n  "schema_version": 1,\n  "frozen_baseline_sha256": "%s",\n'
                '  "files": {"src/bt_audio_ltv_guard.c": ["lines", "branches"]}\n}\n'
                % anchor
            )
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "list2"], check=True
        )
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("metrics must be an object", out)

    def test_sidecar_path_traversal_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        path = os.path.join(fx.repo, "tests", "coverage-additions.json")
        anchor = self._anchor(fx, baseline)
        with open(path, "w") as fh:
            fh.write(
                '{\n  "schema_version": 1,\n  "frozen_baseline_sha256": "%s",\n'
                '  "files": {"src/../src/bt_audio_ltv_guard.c": \n'
                '    {"lines": [13, 13], "branches": [12, 12], "functions": [1, 1]}}\n}\n'
                % anchor
            )
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(["git", "-C", fx.repo, "commit", "-q", "-m", "trav"], check=True)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("unsafe path component", out)

    def test_additive_one_of_thirteen_below_reference_rejects(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        template = self._coverage_template_with_guard(fx, guard_covered=1)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("below additive reference: current 1/13 vs reference 13/13", out)

    def test_additive_bool_current_metrics_reject(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        template = self._coverage_template_with_guard(fx)
        for f in template["files"]:
            if f["file"].endswith("bt_audio_ltv_guard.c"):
                f["lines"] = True
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        # bool type in current metrics cannot be counted; the exact path
        # is a zero-totals record rejected with the additive-minimum
        # error (no skipped record, no pass).
        self.assertNotIn("baseline enforcement PASS", out)
        self.assertIn("total below minimum", out)

    def test_additive_zero_functions_reject(self):
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        baseline = self._prepare_valid_flow(fx)
        template = self._coverage_template_with_guard(fx, guard_functions=0)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out = self._run_and_out(fx, baseline)
        self.assertNotEqual(0, rc)
        self.assertIn("below additive reference: current 0/1 vs reference 1/1", out)

    def test_sidecar_anchor_drift_rejects(self):
        fx = self._fixture()
        self._register_guard_in_manifest(fx)
        self._coverage_template_with_guard(fx)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        self._write_additions(fx, anchor="0" * 64)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("does not match actual baseline SHA", out)

    def test_sidecar_malformed_rejects(self):
        fx = self._fixture()
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        with open(os.path.join(fx.repo, "tests", "coverage-additions.json"), "w") as fh:
            fh.write('{"schema_version": true, "files": {}}')  # scalar bool
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(
            ["git", "-C", fx.repo, "commit", "-q", "-m", "malformed"], check=True
        )
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("invalid additions sidecar", out)

    def test_sidecar_duplicate_key_rejects(self):
        fx = self._fixture()
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        anchor = self._anchor(fx, baseline)
        raw = (
            '{\n  "schema_version": 1,\n'
            '  "frozen_baseline_sha256": "%s",\n'
            '  "files": {},\n'
            '  "files": {}\n}\n' % anchor
        )
        with open(os.path.join(fx.repo, "tests", "coverage-additions.json"), "w") as fh:
            fh.write(raw)
        subprocess.run(["git", "-C", fx.repo, "add", "-A"], check=True)
        subprocess.run(["git", "-C", fx.repo, "commit", "-q", "-m", "dup"], check=True)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("duplicate JSON key", out)

    def test_missing_new_source_sidecar_fails(self):
        # New source is in the current population/run but no sidecar
        # describes it: the strict unknown-new-file rule still rejects.
        fx = RunnerFixture(extra_sources=("bt_audio_ltv_guard.c",))
        self.addCleanup(fx.cleanup)
        template = self._coverage_template_with_guard(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        baseline = os.path.join(fx.root, "baseline.json")
        rc, out, _ = fx.run("--write-baseline", baseline)
        self.assertEqual(0, rc, out)
        self._register_guard_in_manifest(fx)
        with open(fx.coverage_template, "w") as fh:
            json.dump(template, fh)
        rc, out, _ = fx.run("--baseline", baseline, "--clean-output")
        self.assertNotEqual(0, rc)
        self.assertIn("new file in current population not in baseline", out)


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


class RunnerTraceFlags(unittest.TestCase):
    """Per-suite gcovr trace flags: only the ltv_bounds trace (and the
    final merge) carries --include-internal-functions; every other suite
    trace keeps the default filtering.  The fake gcovr emulates the real
    gcovr 8.4 "__"-name filter boundary; recorded argv and surviving
    fixture __wrap data are checked without any real build."""

    def _suites_fixture(self):
        fx = RunnerFixture()
        self.addCleanup(fx.cleanup)
        for suite in ("fake_suite", "ltv_bounds"):
            os.makedirs(os.path.join(fx.repo, "tests", "unit", suite), exist_ok=True)
            with open(
                os.path.join(fx.repo, "tests", "unit", suite, "testcase.yaml"),
                "w",
            ) as fh:
                fh.write(
                    "tests:\n  unit.%s:\n"
                    "    platform_allow: native_sim/native/64\n" % suite
                )
            with open(
                os.path.join(fx.repo, "tests", "unit", suite, "CMakeLists.txt"),
                "w",
            ) as fh:
                fh.write("cmake_minimum_required(VERSION 3.20.0)\nproject(x)\n")
        return fx

    @staticmethod
    def _recorded_invocations(args_log):
        with open(args_log) as fh:
            return [line.split("\x00") for line in fh.read().splitlines() if line]

    def test_ltv_bounds_trace_and_merge_carry_flag_others_not(self):
        with tempfile.TemporaryDirectory(prefix="t7cov-args-") as tempdir:
            fx = self._suites_fixture()
            args_log = os.path.join(tempdir, "gcovr-args.txt")
            fx.env["FAKE_GCOVR_ARGS_LOG"] = args_log
            rc, out, output = fx.run("--report-only")
            self.assertEqual(0, rc, out)
            invocations = self._recorded_invocations(args_log)
            traces = [inv for inv in invocations if "--object-directory" in inv]
            merges = [
                inv for inv in invocations if any("--add-tracefile" == a for a in inv)
            ]
            self.assertEqual(len(merges), 1, invocations)
            by_suite = {}
            for inv in traces:
                json_arg = next(
                    a[len("--json=") :] for a in inv if a.startswith("--json=")
                )
                name = os.path.basename(json_arg)
                self.assertTrue(
                    name.startswith("trace_") and name.endswith(".json"), inv
                )
                by_suite[name] = inv
            self.assertEqual(
                sorted(by_suite),
                sorted(["trace_fake_suite.json", "trace_ltv_bounds.json"]),
            )
            # Exactly the ltv_bounds trace relaxes the internal filter.
            self.assertIn(
                "--include-internal-functions",
                by_suite["trace_ltv_bounds.json"],
            )
            self.assertNotIn(
                "--include-internal-functions",
                by_suite["trace_fake_suite.json"],
            )
            # And only ltv_bounds: plain fixture suites carry no flag.
            self.assertEqual(
                [inv for inv in traces if "--include-internal-functions" in inv],
                [by_suite["trace_ltv_bounds.json"]],
            )
            # Merge carries the literal flag exactly once.
            self.assertEqual(merges[0].count("--include-internal-functions"), 1)

    def test_wrap_line_survives_to_numeric_summary(self):
        with tempfile.TemporaryDirectory(prefix="t7cov-wrap-") as tempdir:
            fx = self._suites_fixture()
            template = json.loads(json.dumps(COVERAGE_TEMPLATE))
            template["files"].append(
                {
                    "file": "src/bt_audio_ltv_guard.c",
                    "lines": [{"line_number": 13, "count": 1, "branches": []}],
                    "functions": [
                        {
                            "name": "__wrap_bt_audio_data_parse",
                            "execution_count": 1,
                        }
                    ],
                }
            )
            with open(fx.coverage_template, "w") as fh:
                json.dump(template, fh)
            # Filtering fake replaces the basic fake: __-functions survive
            # only where the runner actually relaxed the flag.
            path = os.path.join(fx.bin, "gcovr")
            with open(path, "w") as fh:
                fh.write(FAKE_GCOVR_FILTERING)
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)
            args_log = os.path.join(tempdir, "gcovr-args.txt")
            fx.env["FAKE_GCOVR_ARGS_LOG"] = args_log
            rc, out, output = fx.run("--report-only")
            self.assertEqual(0, rc, out)
            with open(os.path.join(output, "coverage-summary.json")) as fh:
                summary = json.load(fh)
            wrapper = [
                f
                for f in summary.get("files", [])
                if f.get("file", "").endswith("bt_audio_ltv_guard.c")
            ]
            self.assertEqual(1, len(wrapper), summary)
            self.assertEqual(
                wrapper[0].get("functions"), {"covered": 1, "total": 1}, wrapper[0]
            )
            self.assertEqual(
                wrapper[0].get("lines"), {"covered": 1, "total": 1}, wrapper[0]
            )
            # Negative control inside the same summary: the filtered
            # fixture function names still appear with nonzero counts.
            plain = [
                f
                for f in summary.get("files", [])
                if f.get("file", "").endswith("foo.c")
            ]
            self.assertEqual(plain[0].get("functions"), {"covered": 1, "total": 1})


class TestAllGateOutputRoot(unittest.TestCase):
    """TEST_OUTPUT_DIR output-root contract for scripts/test-all.sh (PR 11
    CI gate): an invalid root fails fast before any suite, a valid external
    root is accepted and the gate then stops at the next pre-suite
    prerequisite, and the historical mktemp behavior is untouched when
    unset.  Runs the real script with a controlled environment; no Zephyr
    or BabbleSim build ever happens."""

    TEST_ALL = os.path.join(REPO_ROOT, "scripts", "test-all.sh")

    def _run(self, env_extra, args=()):
        env = dict(os.environ)
        env.pop("ZEPHYR_BASE", None)
        env.update(env_extra)
        proc = subprocess.run(
            ["bash", self.TEST_ALL] + list(args),
            capture_output=True,
            text=True,
            env=env,
            timeout=120,
        )
        return proc.returncode, proc.stdout + proc.stderr

    def test_relative_root_rejected_before_env_resolution(self):
        rc, out = self._run({"TEST_OUTPUT_DIR": "relative-dir"})
        self.assertNotEqual(rc, 0)
        self.assertIn("TEST_OUTPUT_DIR must be an absolute path", out)
        self.assertNotIn("ZEPHYR_BASE", out, "must fail before resolve_ncs")

    def test_repo_root_and_inside_repo_rejected(self):
        rc, out = self._run({"TEST_OUTPUT_DIR": REPO_ROOT})
        self.assertNotEqual(rc, 0)
        self.assertIn("TEST_OUTPUT_DIR must not be the repository root", out)
        rc, out = self._run({"TEST_OUTPUT_DIR": os.path.join(REPO_ROOT, "docs")})
        self.assertNotEqual(rc, 0)
        self.assertIn("TEST_OUTPUT_DIR must not be inside the repository", out)
        self.assertNotIn("ZEPHYR_BASE", out, "must fail before resolve_ncs")

    def test_root_and_home_rejected(self):
        for bad in ("/", os.path.expanduser("~")):
            with self.subTest(path=bad):
                rc, out = self._run({"TEST_OUTPUT_DIR": bad})
                self.assertNotEqual(rc, 0)
                self.assertIn("TEST_OUTPUT_DIR", out)
                self.assertNotIn("ZEPHYR_BASE", out, "must fail before resolve_ncs")

    def test_valid_external_root_accepted_then_stops_at_environment(self):
        # Deterministic pre-suite stop: strip nrfutil from PATH and unset
        # ZEPHYR_BASE so resolve_ncs fails right after validation accepts
        # the root and before any suite starts.
        path = [
            p
            for p in os.environ.get("PATH", "").split(os.pathsep)
            if not os.path.isfile(os.path.join(p, "nrfutil"))
        ]
        self.assertFalse(
            any(os.path.isfile(os.path.join(p, "nrfutil")) for p in path),
            "filtered test PATH must not contain nrfutil",
        )
        with tempfile.TemporaryDirectory() as tmp:
            rc, out = self._run({"PATH": os.pathsep.join(path), "TEST_OUTPUT_DIR": tmp})
            self.assertNotEqual(rc, 0)
            self.assertIn("ZEPHYR_BASE must be set", out)
            self.assertIn("TEST_OUTPUT_DIR=%s" % tmp, out)
            self.assertNotIn("TEST_OUTPUT_DIR must", out)
            # Caller-owned root stays present and untouched.
            self.assertTrue(os.path.isdir(tmp))
            self.assertEqual(os.listdir(tmp), [])


class TestAllGatePhaseSelection(unittest.TestCase):
    """Public phase-selection behavior with fake child commands.

    The copied gate runs against a fixture repository. Fake inventory, west,
    coverage, matrix, and BSim commands record only public dispatch order, so
    these tests do not depend on real Zephyr or BabbleSim builds.
    """

    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="testall-phase-")
        self.addCleanup(shutil.rmtree, self.root, True)
        self.repo = os.path.join(self.root, "repo")
        self.scripts = os.path.join(self.repo, "scripts")
        self.bin = os.path.join(self.root, "bin")
        self.home = os.path.join(self.root, "home")
        self.output = os.path.join(self.root, "results")
        self.events_path = os.path.join(self.root, "events.log")
        os.makedirs(self.scripts)
        os.makedirs(self.bin)
        os.makedirs(self.home)
        os.makedirs(self.output)
        self.synthetic_ascs_containers = set()
        self.addCleanup(self._cleanup_synthetic_ascs)

        self._copy_gate()
        self._write_fixture_scripts()
        self._write_fixture_children()

        self.env = dict(os.environ)
        self.env.update(
            {
                "PATH": self.bin + os.pathsep + self.env.get("PATH", ""),
                "HOME": self.home,
                "PHASE_EVENT_LOG": self.events_path,
                "TEST_OUTPUT_DIR": self.output,
                "ZEPHYR_BASE": "/fake/zephyr",
                "FAKE_ASCS_FIXTURE_ROOT": self.root,
            }
        )

    def _write_file(self, path, text, executable=False):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        if executable:
            os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

    def _copy_gate(self):
        destination = os.path.join(self.scripts, "test-all.sh")
        shutil.copy(TEST_ALL_SRC, destination)
        os.chmod(destination, os.stat(destination).st_mode | stat.S_IXUSR)
        self.test_all = destination

    def _write_fixture_scripts(self):
        self._write_file(
            os.path.join(self.scripts, "test_inventory.py"),
            """#!/usr/bin/env python3
import sys

if sys.argv[1:] == ["--twister"]:
    print("twister_alpha")
elif sys.argv[1:] == ["--exec-only"]:
    print("exec_beta")
elif sys.argv[1:] == ["--python"]:
    print("python_gamma\\ttests/unit/python_gamma/test_gamma.py")
else:
    raise SystemExit(2)
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.scripts, "test-coverage.sh"),
            """#!/usr/bin/env bash
set -euo pipefail
out=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output) out="$2"; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$out"
touch "$out/coverage.json"
printf 'coverage:%s\\n' "$out" >> "$PHASE_EVENT_LOG"
[ "${FAKE_FAIL:-}" != "coverage" ]
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.scripts, "check-test-matrix.py"),
            """#!/usr/bin/env python3
import os
import sys

with open(os.environ["PHASE_EVENT_LOG"], "a", encoding="utf-8") as fh:
    fh.write("matrix:%s\\n" % " ".join(sys.argv[1:]))

raise SystemExit(17 if os.environ.get("FAKE_FAIL") == "matrix" else 0)
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.scripts, "bsim-stage1-run.sh"),
            """#!/usr/bin/env bash
set -euo pipefail
printf 'bsim\\n' >> "$PHASE_EVENT_LOG"
[ "${FAKE_FAIL:-}" != "bsim" ]
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.scripts, "ascs-bsim-run.sh"),
            """#!/usr/bin/env bash
set -euo pipefail
: "${ASCS_OUTPUT_ROOT:?new ASCS child output required}"
case "$ASCS_OUTPUT_ROOT" in /tmp/le-audio-ascs.*/run) ;; *) exit 3 ;; esac
[ ! -e "$ASCS_OUTPUT_ROOT" ] && [ ! -L "$ASCS_OUTPUT_ROOT" ] || exit 3
mkdir "$ASCS_OUTPUT_ROOT"
printf '%s\\n' "$FAKE_ASCS_FIXTURE_ROOT" > "$ASCS_OUTPUT_ROOT/marker"
printf 'ascs\\n' >> "$PHASE_EVENT_LOG"
[ "${FAKE_FAIL:-}" != "ascs" ]
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.bin, "west"),
            """#!/usr/bin/env bash
set -euo pipefail
case "$*" in
  *twister_alpha*) kind=twister ;;
  *exec_beta*) kind=exec ;;
  *) exit 2 ;;
esac
printf '%s\\n' "$kind" >> "$PHASE_EVENT_LOG"
[ "${FAKE_FAIL:-}" != "$kind" ]
""",
            executable=True,
        )
        self._write_file(
            os.path.join(self.bin, "nrfutil"),
            """#!/usr/bin/env bash
printf 'nrfutil\\n' >> "$PHASE_EVENT_LOG"
exit 1
""",
            executable=True,
        )

    def _write_fixture_children(self):
        self._write_file(
            os.path.join(self.repo, "tests", "unit", "python_gamma", "test_gamma.py"),
            """import os

with open(os.environ["PHASE_EVENT_LOG"], "a", encoding="utf-8") as fh:
    fh.write("python\\n")

if os.environ.get("FAKE_FAIL") == "python":
    raise SystemExit(17)
""",
        )

    def _events(self):
        if not os.path.exists(self.events_path):
            return []
        with open(self.events_path, "r", encoding="utf-8") as fh:
            return fh.read().splitlines()

    def _cleanup_synthetic_ascs(self):
        for container in self.synthetic_ascs_containers:
            marker = os.path.join(container, "run", "marker")
            if os.path.isfile(marker):
                with open(marker, encoding="utf-8") as stream:
                    if stream.read().strip() == self.root:
                        shutil.rmtree(container)

    def _links(self):
        return sorted(glob.glob(os.path.join(self.output, "ascs-*")))

    def _run(self, args=(), fail=None, resolve_environment=False):
        with open(self.events_path, "w", encoding="utf-8"):
            pass
        env = dict(self.env)
        if fail is not None:
            env["FAKE_FAIL"] = fail
        if resolve_environment:
            env.pop("ZEPHYR_BASE", None)
        proc = subprocess.run(
            ["bash", self.test_all] + list(args),
            cwd=self.repo,
            capture_output=True,
            text=True,
            env=env,
            timeout=120,
        )
        for container in re.findall(
            r"ASCS evidence container: (/tmp/le-audio-ascs\.[^\s]+) ", proc.stdout
        ):
            if os.path.isfile(os.path.join(container, "run", "marker")):
                self.synthetic_ascs_containers.add(container)
        return proc.returncode, proc.stdout + proc.stderr

    def test_invalid_phase_forms_fail_before_environment_resolution(self):
        cases = (
            (("--phase",), "--phase requires one of"),
            (("--phase", "unknown"), "invalid phase: unknown"),
            (
                ("--phase", "unit", "--phase", "bsim"),
                "--phase may be specified only once",
            ),
            (("unexpected",), "unexpected positional argument: unexpected"),
            (("--unknown",), "unknown option: --unknown"),
        )
        for args, error in cases:
            with self.subTest(args=args):
                rc, out = self._run(args, resolve_environment=True)
                self.assertNotEqual(rc, 0)
                self.assertIn("Usage:", out)
                self.assertIn(error, out)
                self.assertNotIn("ZEPHYR_BASE", out)
                self.assertEqual(self._events(), [], "invalid arguments ran a child")

    def test_omitted_phase_and_explicit_all_have_same_dispatch_order(self):
        expected = [
            "twister",
            "exec",
            "python",
            "coverage:%s" % os.path.join(self.output, "coverage"),
            "matrix:--repo-root %s --coverage-json %s"
            % (self.repo, os.path.join(self.output, "coverage", "coverage.json")),
            "bsim",
            "ascs",
        ]
        rc, out = self._run()
        self.assertEqual(rc, 0, out)
        self.assertEqual(self._events(), expected)
        self.assertIn("Gate complete: 7 PASS / 0 FAIL / 7 TOTAL", out)
        first_link = self._links()
        self.assertEqual(len(first_link), 1)
        self.assertTrue(os.path.islink(first_link[0]))
        first_target = os.path.realpath(first_link[0])
        self.assertEqual(
            first_target,
            os.path.join(next(iter(self.synthetic_ascs_containers)), "run"),
        )
        self.assertTrue(os.path.isfile(os.path.join(first_link[0], "marker")))

        rc, out = self._run(("--phase", "all"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self._events(), expected)
        self.assertIn("Gate complete: 7 PASS / 0 FAIL / 7 TOTAL", out)
        self.assertEqual(len(self._links()), 2)
        self.assertTrue(os.path.islink(first_link[0]))
        self.assertEqual(os.path.realpath(first_link[0]), first_target)
        self.assertTrue(os.path.isfile(os.path.join(first_link[0], "marker")))

    def test_unit_phase_runs_only_unit_children(self):
        rc, out = self._run(("--phase", "unit"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self._events(), ["twister", "exec", "python"])
        self.assertIn("Gate complete: 3 PASS / 0 FAIL / 3 TOTAL", out)

    def test_coverage_phase_runs_coverage_then_matrix_with_coverage_output(self):
        rc, out = self._run(("--phase", "coverage"))
        coverage_dir = os.path.join(self.output, "coverage")
        self.assertEqual(rc, 0, out)
        self.assertEqual(
            self._events(),
            [
                "coverage:%s" % coverage_dir,
                "matrix:--repo-root %s --coverage-json %s"
                % (self.repo, os.path.join(coverage_dir, "coverage.json")),
            ],
        )
        self.assertIn("Gate complete: 2 PASS / 0 FAIL / 2 TOTAL", out)

    def test_bsim_phase_runs_both_mandatory_children(self):
        rc, out = self._run(("--phase", "bsim"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(self._events(), ["bsim", "ascs"])
        self.assertIn("Gate complete: 2 PASS / 0 FAIL / 2 TOTAL", out)

    def test_ascs_failure_retains_link_and_fails_aggregate_after_stage1(self):
        rc, out = self._run(("--phase", "bsim"), fail="ascs")
        self.assertNotEqual(rc, 0)
        self.assertEqual(self._events(), ["bsim", "ascs"])
        self.assertIn("Gate complete: 1 PASS / 1 FAIL / 2 TOTAL", out)
        links = self._links()
        self.assertEqual(len(links), 1)
        self.assertTrue(os.path.islink(links[0]))
        self.assertEqual(
            os.path.realpath(links[0]),
            os.path.join(next(iter(self.synthetic_ascs_containers)), "run"),
        )
        self.assertTrue(os.path.isfile(os.path.join(links[0], "marker")))

    def test_stage1_failure_still_runs_mandatory_ascs(self):
        rc, out = self._run(("--phase", "bsim"), fail="bsim")
        self.assertNotEqual(rc, 0)
        self.assertEqual(self._events(), ["bsim", "ascs"])
        self.assertIn("Gate complete: 1 PASS / 1 FAIL / 2 TOTAL", out)
        self.assertEqual(len(self._links()), 1)
        self.assertTrue(os.path.isfile(os.path.join(self._links()[0], "marker")))

    def test_bsim_without_test_output_dir_keeps_external_ascs_container(self):
        env = dict(self.env)
        env.pop("TEST_OUTPUT_DIR")
        with open(self.events_path, "w", encoding="utf-8"):
            pass
        proc = subprocess.run(
            ["bash", self.test_all, "--phase", "bsim"],
            cwd=self.repo,
            env=env,
            capture_output=True,
            text=True,
            timeout=120,
        )
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertEqual(self._events(), ["bsim", "ascs"])
        candidates = re.findall(
            r"ASCS evidence container: (/tmp/le-audio-ascs\.[^\s]+) ", proc.stdout
        )
        self.assertEqual(len(candidates), 1)
        container = candidates[0]
        with open(os.path.join(container, "run", "marker"), encoding="utf-8") as stream:
            self.assertEqual(stream.read().strip(), self.root)
        self.synthetic_ascs_containers.add(container)

    def test_selected_phase_continues_after_child_failure_and_fails(self):
        rc, out = self._run(("--phase", "unit"), fail="twister")
        self.assertNotEqual(rc, 0)
        self.assertEqual(self._events(), ["twister", "exec", "python"])
        self.assertIn("Gate complete: 2 PASS / 1 FAIL / 3 TOTAL", out)
        self.assertIn("FAIL", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
