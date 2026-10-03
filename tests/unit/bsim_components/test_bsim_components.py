"""Process-boundary contract for the BabbleSim dependency compiler policy."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import tarfile
import time
import unittest


REPO = Path(__file__).resolve().parents[3]
WRAPPER = REPO / "scripts/bsim-component-cc.py"
PREPARE = REPO / "scripts/prepare-bsim-sources.sh"
BUILD = REPO / "scripts/build-bsim-components.sh"
RUNTIME = REPO / "scripts/check-bsim-runtime.py"
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


class TestRuntimeClosure(unittest.TestCase):
    """Build real pinned components in an isolated, initially empty output."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.workspace = Path(cls.tmp.name) / "sdk"
        cls.zephyr = cls.workspace / "zephyr"
        cls.zephyr.mkdir(parents=True)
        cls.root = cls.workspace / "tools/bsim"
        components = cls.root / "components"
        sdk = Path(os.environ["ZEPHYR_BASE"]).resolve().parent / "tools/bsim"
        for name in (
            "common",
            "libUtilv1",
            "libPhyComv1",
            "libRandv2",
            "ext_2G4_libPhyComv1",
            "ext_2G4_phy_v1",
            "ext_2G4_channel_NtNcable",
            "ext_2G4_modem_magic",
            "ext_libCryptov1",
        ):
            shutil.copytree(
                sdk / "components" / name,
                components / name,
                ignore=shutil.ignore_patterns(".git", "*.o", "*.a", "*.so", "*.d"),
            )
        shutil.copyfile(sdk / "Makefile", cls.root / "Makefile")
        cls.env = {
            **os.environ,
            "ZEPHYR_BASE": str(cls.zephyr),
            "BSIM_OUT_PATH": str(cls.root),
            "BSIM_COMPONENTS_PATH": str(components),
            "NIX_HARDENING_ENABLE": "",
        }
        before = subprocess.run(
            [sys.executable, str(RUNTIME), "--root", str(cls.root)],
            capture_output=True,
            text=True,
        )
        if before.returncode == 0:
            raise AssertionError("empty runtime must not be ready")
        build = subprocess.run(
            ["bash", str(BUILD), "--force"],
            env=cls.env,
            capture_output=True,
            text=True,
            timeout=300,
        )
        if build.returncode != 0:
            raise AssertionError(build.stdout + build.stderr)

    def runtime(self):
        return subprocess.run(
            [sys.executable, str(RUNTIME), "--root", str(self.root), "--timeout", "3"],
            capture_output=True,
            text=True,
            timeout=8,
        )

    def test_cold_build_initializes_actual_default_models(self):
        result = self.runtime()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("models initialized", result.stdout)

    def test_missing_either_default_plugin_fails_before_peer_launch(self):
        for name in (
            "lib_2G4Channel_NtNcable.so",
            "lib_2G4Modem_Magic.so",
            "libCryptov1.so",
        ):
            with self.subTest(plugin=name):
                path = self.root / "lib" / name
                data = path.read_bytes()
                try:
                    path.unlink()
                    result = self.runtime()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(name, result.stderr)
                    caller = subprocess.run(
                        [
                            "bash",
                            "-e",
                            "-c",
                            'source "$1"; printf "peers-would-start\\n"',
                            "runtime-test",
                            str(REPO / "scripts/bsim-env.sh"),
                        ],
                        env=self.env,
                        capture_output=True,
                        text=True,
                        timeout=8,
                    )
                    self.assertNotEqual(caller.returncode, 0)
                    self.assertNotIn("peers-would-start", caller.stdout)
                finally:
                    path.write_bytes(data)
        self.assertEqual(self.runtime().returncode, 0)

    def test_nonempty_invalid_plugin_is_rejected_by_actual_phy_loader(self):
        path = self.root / "lib/lib_2G4Modem_Magic.so"
        data = path.read_bytes()
        try:
            path.write_bytes(b"not an ELF shared library")
            result = self.runtime()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("PHY model initialization failed", result.stderr)
        finally:
            path.write_bytes(data)
        self.assertEqual(self.runtime().returncode, 0)

    def test_crypto_wrong_elf_class_and_missing_symbols_fail_before_peers(self):
        library = self.root / "lib/libCryptov1.so"
        original = library.read_bytes()
        dummy = self.workspace / "dummy_crypto.c"
        dummy.write_text("int unrelated_symbol(void) { return 0; }\n")
        try:
            for architecture, diagnostic in (
                ("-m64", "ELF ABI mismatch"),
                ("-m32", "blecrypt_aes_128"),
            ):
                with self.subTest(architecture=architecture):
                    compile_result = subprocess.run(
                        [
                            "gcc",
                            architecture,
                            "-shared",
                            "-fPIC",
                            "-Werror",
                            str(dummy),
                            "-o",
                            str(library),
                        ],
                        capture_output=True,
                        text=True,
                        env=self.env,
                    )
                    self.assertEqual(
                        compile_result.returncode, 0, compile_result.stderr
                    )
                    result = self.runtime()
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(diagnostic, result.stderr)
        finally:
            library.write_bytes(original)
        self.assertEqual(self.runtime().returncode, 0)
        peer = self.workspace / "wrong_peer.so"
        subprocess.run(
            ["gcc", "-m64", "-shared", "-fPIC", str(dummy), "-o", str(peer)],
            env=self.env,
            check=True,
            capture_output=True,
        )
        result = subprocess.run(
            [
                sys.executable,
                str(RUNTIME),
                "--root",
                str(self.root),
                "--peer",
                str(peer),
            ],
            capture_output=True,
            text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("peer/library ELF ABI mismatch", result.stderr)

    def test_openssl_source_drift_and_unrelated_warnings_remain_errors(self):
        policy = REPO / "scripts/openssl-component-cc.py"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "crypto/bio/bss_log.c"
            source.parent.mkdir(parents=True)
            archive = self.root / "components/ext_libCryptov1/source-1.0.2g.tar.gz"
            with tarfile.open(archive) as packed:
                stream = packed.extractfile("source-1.0.2g/crypto/bio/bss_log.c")
                assert stream is not None
                source.write_bytes(stream.read() + b"\n")
            result = subprocess.run(
                [
                    sys.executable,
                    str(policy),
                    "gcc",
                    str(root),
                    "-c",
                    str(source),
                    "-o",
                    str(root / "bad.o"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("re-audit required", result.stderr)
            unknown = root / "bss_log.c"
            unknown.write_text("int f(void) { int unused; return 0; }\n")
            result = subprocess.run(
                [
                    sys.executable,
                    str(policy),
                    "gcc",
                    str(root),
                    "-Wall",
                    "-c",
                    str(unknown),
                    "-o",
                    str(root / "unknown.o"),
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unused", result.stderr)

    def test_live_process_without_readiness_marker_times_out_as_failure(self):
        # Controlled process traffic tests the supervisor deadline, not loader
        # correctness (the other cases use the real compiled PHY and models).
        path = self.root / "bin/bs_2G4_phy_v1"
        data = path.read_bytes()
        try:
            path.write_bytes(
                b"#!/usr/bin/env bash\nprintf 'still loading\\n'\nexec sleep 30\n"
            )
            result = subprocess.run(
                [
                    sys.executable,
                    str(RUNTIME),
                    "--root",
                    str(self.root),
                    "--timeout",
                    "0.1",
                ],
                capture_output=True,
                text=True,
                timeout=5,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("before deadline", result.stderr)
            self.assertNotIn("runtime ready", result.stdout)
        finally:
            path.write_bytes(data)
        self.assertEqual(self.runtime().returncode, 0)

    def assert_checker_cancellation(self, cancellation, *, ignore_term=False):
        path = self.root / "bin/bs_2G4_phy_v1"
        data = path.read_bytes()
        pid_file = self.workspace / "preflight-child.pid"
        pid_file.unlink(missing_ok=True)
        checker = None
        child_pid = None
        try:
            script = b"#!/usr/bin/env bash\n"
            if ignore_term:
                script += b"trap '' TERM\n"
            script += b'printf \'%s\\n\' "$$" > "$BSIM_TEST_PID_FILE"\n'
            path.write_bytes(script + b"exec sleep 30\n")
            checker = subprocess.Popen(
                [sys.executable, str(RUNTIME), "--root", str(self.root)],
                env={**os.environ, "BSIM_TEST_PID_FILE": str(pid_file)},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    text = pid_file.read_text().strip()
                except FileNotFoundError:
                    text = ""
                if text.isdecimal():
                    child_pid = int(text)
                    break
                time.sleep(0.01)
            self.assertIsNotNone(child_pid, "controlled PHY did not start")
            assert child_pid is not None
            checker.send_signal(cancellation)
            _stdout, stderr = checker.communicate(timeout=5)
            self.assertNotEqual(checker.returncode, 0)
            self.assertIn("cancelled", stderr)
            with self.assertRaises(ProcessLookupError):
                os.kill(child_pid, 0)
            child_pid = None
        finally:
            if checker is not None and checker.poll() is None:
                checker.terminate()
                try:
                    checker.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    checker.kill()
                    checker.communicate()
            if child_pid is not None:
                try:
                    os.kill(child_pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            path.write_bytes(data)

    def test_sigterm_cancels_checker_and_reaps_owned_phy(self):
        self.assert_checker_cancellation(signal.SIGTERM)

    def test_sigint_cancels_checker_and_reaps_owned_phy(self):
        self.assert_checker_cancellation(signal.SIGINT)

    def test_cancellation_kills_and_reaps_phy_ignoring_sigterm(self):
        self.assert_checker_cancellation(signal.SIGTERM, ignore_term=True)


if __name__ == "__main__":
    unittest.main()
