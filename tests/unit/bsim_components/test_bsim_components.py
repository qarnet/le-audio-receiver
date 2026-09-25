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


if __name__ == "__main__":
    unittest.main()
