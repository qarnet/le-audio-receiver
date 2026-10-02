"""Process-boundary contract for the BabbleSim dependency compiler policy."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[3]
WRAPPER = REPO / "scripts/bsim-component-cc.py"
PREPARE = REPO / "scripts/prepare-bsim-sources.sh"
AUDITED = {
    "libUtilv1/src/bs_oswrap.c": "-Wno-unused-result",
    "libPhyComv1/src/bs_pc_base.c": "-Wno-unused-result",
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless.c": "-Wno-unused-result",
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_stateless_wo_callbacks.c": "-Wno-unused-result",
    "ext_2G4_libPhyComv1/src/bs_pc_2G4_utils.c": "-Wno-maybe-uninitialized",
}


class TestComponentCompiler(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "components"
        self.root.mkdir()
        self.compiler = Path(self.tmp.name) / "fake compiler"
        self.compiler.write_text(
            "#!%s\n" % sys.executable
            + "import json, os, sys\n"
            + "from pathlib import Path\n"
            + "Path(os.environ['ARGV_LOG']).write_text(json.dumps(sys.argv[1:]))\n"
            + "if '-o' in sys.argv:\n"
            + "    Path(sys.argv[sys.argv.index('-o') + 1]).write_text('artifact')\n"
            + "sys.exit(int(os.environ.get('FAKE_RC', '0')))\n",
            encoding="utf-8",
        )
        self.compiler.chmod(0o755)
        self.log = Path(self.tmp.name) / "argv.json"
        self.artifact = Path(self.tmp.name) / "output file.o"
        zephyr = os.environ.get("ZEPHYR_BASE")
        if not zephyr:
            self.skipTest("ZEPHYR_BASE required for pinned BabbleSim source bytes")
        self.sdk = Path(zephyr).resolve().parent / "tools/bsim/components"
        for name in AUDITED:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.sdk / name, target)

    def run_cc(self, source, *, rc=0, extra=()):
        args = [
            "-Wall",
            "-pedantic",
            *extra,
            "-c",
            str(source),
            "-o",
            str(self.artifact),
        ]
        return subprocess.run(
            [sys.executable, str(WRAPPER), str(self.compiler), str(self.root), *args],
            capture_output=True,
            text=True,
            env={**os.environ, "ARGV_LOG": str(self.log), "FAKE_RC": str(rc)},
        ), args

    def test_pinned_sources_get_only_their_audited_diagnostic_class(self):
        for name, exception in AUDITED.items():
            with self.subTest(name=name):
                result, args = self.run_cc(
                    self.root / name, extra=("-DSTRING=has spaces",)
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    json.loads(self.log.read_text()), args + ["-Werror", exception]
                )
                self.assertTrue(self.artifact.is_file())

    def test_changed_audited_source_fails_before_compiler_and_artifact(self):
        source = self.root / next(iter(AUDITED))
        source.write_bytes(source.read_bytes() + b"\n")
        result, _ = self.run_cc(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("re-audit required", result.stderr)
        self.assertFalse(self.log.exists())
        self.assertFalse(self.artifact.exists())

    def test_same_basename_outside_audited_path_stays_strict(self):
        source = self.root / "other/src/bs_oswrap.c"
        source.parent.mkdir(parents=True)
        shutil.copyfile(self.sdk / "libUtilv1/src/bs_oswrap.c", source)
        result, args = self.run_cc(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.log.read_text()), args + ["-Werror"])

    def test_source_preflight_rejects_symlink_even_with_pinned_bytes(self):
        name = next(iter(AUDITED))
        source = self.root / name
        source.unlink()
        source.symlink_to(self.sdk / name)
        result = subprocess.run(
            [sys.executable, str(WRAPPER), "--check-sources", str(self.root)],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must not be a symlink", result.stderr)

    def test_unknown_source_and_link_get_werror_and_preserve_status(self):
        source = self.root / "new source.c"
        source.write_text("int foo;\n")
        result, args = self.run_cc(source, rc=42, extra=("-DVALUE=has spaces", "-m32"))
        self.assertEqual(result.returncode, 42)
        self.assertEqual(json.loads(self.log.read_text()), args + ["-Werror"])
        self.log.unlink()
        link_args = ["-m32", "-o", str(self.artifact), "lib with spaces.a"]
        result = subprocess.run(
            [
                sys.executable,
                str(WRAPPER),
                str(self.compiler),
                str(self.root),
                *link_args,
            ],
            capture_output=True,
            text=True,
            env={**os.environ, "ARGV_LOG": str(self.log), "FAKE_RC": "13"},
        )
        self.assertEqual(result.returncode, 13)
        self.assertEqual(json.loads(self.log.read_text()), link_args + ["-Werror"])


class TestSourcePreparation(unittest.TestCase):
    """Real west/Git, offline pinned remote, and the real source-dependent suite."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.workspace = self.base / "SDK with spaces"
        self.manifest = self.workspace / "nrf"
        self.manifest.mkdir(parents=True)
        self.zephyr = self.workspace / "zephyr"
        self.zephyr.mkdir()
        self.remote = self.base / "upstream"
        self.remote.mkdir()
        sdk = Path(os.environ["ZEPHYR_BASE"]).resolve().parent / "tools/bsim/components"
        for name in AUDITED:
            target = self.remote / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(sdk / name, target)
        self.git("init", "-q", cwd=self.remote)
        revision = self.commit_remote()
        self.write_manifest(revision)
        self.git("init", "-q", cwd=self.manifest)
        self.git("add", ".", cwd=self.manifest)
        self.git("commit", "-qm", "Pinned test manifest", cwd=self.manifest)
        # Input is an already bootstrapped SDK/cache with west metadata but
        # absent optional projects, not a test of sdk-manager or west init.
        west_config = self.workspace / ".west"
        west_config.mkdir()
        (west_config / "config").write_text("[manifest]\npath = nrf\nfile = west.yml\n")
        self.components = self.workspace / "tools/bsim/components"

    def run_tool(self, *args, cwd):
        result = subprocess.run(
            args,
            cwd=cwd,
            capture_output=True,
            text=True,
            env={**os.environ, "ZEPHYR_BASE": str(self.zephyr)},
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def git(self, *args, cwd):
        return self.run_tool(
            "git",
            "-c",
            "user.name=Fixture",
            "-c",
            "user.email=fixture@example.invalid",
            "-c",
            "commit.gpgsign=false",
            *args,
            cwd=cwd,
        )

    def commit_remote(self):
        self.git("add", "-A", cwd=self.remote)
        self.git("commit", "-qm", "Pinned source fixture", cwd=self.remote)
        return self.git("rev-parse", "HEAD", cwd=self.remote).stdout.strip()

    def write_manifest(self, revision, *, remote=None):
        self.manifest.joinpath("west.yml").write_text(
            "manifest:\n"
            "  self:\n    path: nrf\n"
            "  group-filter: [-babblesim]\n"
            "  projects:\n"
            "    - name: audited_components\n"
            f"      url: {remote or self.remote.as_uri()}\n"
            "      path: tools/bsim/components\n"
            f"      revision: {revision}\n"
            "      groups: [babblesim]\n"
        )

    def prepare(self, cache_hit="false", zephyr=None):
        return subprocess.run(
            ["bash", str(PREPARE)],
            cwd=REPO,
            capture_output=True,
            text=True,
            env={
                **os.environ,
                "ZEPHYR_BASE": str(zephyr or self.zephyr),
                "CACHE_HIT": cache_hit,
            },
        )

    def policy_suite(self):
        return subprocess.run(
            [
                sys.executable,
                "-m",
                "unittest",
                "tests.unit.bsim_components.test_bsim_components.TestComponentCompiler",
            ],
            cwd=REPO,
            capture_output=True,
            text=True,
            env={**os.environ, "ZEPHYR_BASE": str(self.zephyr)},
        )

    def assert_policy_suite_passes(self):
        result = self.policy_suite()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Ran 5 tests", result.stderr)
        self.assertIn("OK", result.stderr)

    def test_cold_workspace_populates_sources_and_real_policy_suite_passes(self):
        self.assertFalse(self.components.exists())
        before = self.policy_suite()
        self.assertNotEqual(before.returncode, 0)
        self.assertIn("FileNotFoundError", before.stderr)
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_policy_suite_passes()

    def test_cache_hit_with_missing_sources_and_repeated_preparation(self):
        # SDK metadata exists but optional projects are absent, as in a partial
        # SDK cache. CACHE_HIT must never bypass source population/readiness.
        self.assertFalse(self.components.exists())
        for _ in range(2):
            result = self.prepare(cache_hit="true")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assert_policy_suite_passes()

    def test_pinned_checkout_with_changed_source_is_rejected(self):
        source = self.remote / next(iter(AUDITED))
        source.write_bytes(source.read_bytes() + b"\n")
        self.write_manifest(self.commit_remote())
        result = self.prepare(cache_hit="true")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("re-audit required", result.stderr)

    def test_pinned_checkout_missing_a_required_source_is_rejected(self):
        (self.remote / next(iter(AUDITED))).unlink()
        self.write_manifest(self.commit_remote())
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("re-audit required", result.stderr)

    def test_wrong_workspace_parent_fails_before_population(self):
        other = self.workspace / "other/zephyr"
        other.mkdir(parents=True)
        result = self.prepare(zephyr=other)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("workspace does not match", result.stderr)
        self.assertFalse(self.components.exists())

    def test_failed_fetch_does_not_report_ready(self):
        revision = self.git("rev-parse", "HEAD", cwd=self.remote).stdout.strip()
        self.write_manifest(revision, remote=(self.base / "missing-remote").as_uri())
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("sources verified", result.stdout)


if __name__ == "__main__":
    unittest.main()
