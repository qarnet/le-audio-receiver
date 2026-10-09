"""Native BSim link search-order public CLI checks with real filesystem entries."""

import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from bsim_link_env import _query  # noqa: E402


def elf(path, bits=1):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x7fELF" + bytes([bits, 1, 1]) + b"\0" * 11 + b"\x03\0")


class LinkEnvTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.libc = self.root / "glibc/lib/32"
        (self.libc / "libc.so").parent.mkdir(parents=True)
        (self.libc / "libc.so").write_text("GROUP ( libc.so.6 )")
        for name in ("libc.so.6", "libm.so", "libdl.so", "libpthread.so"):
            elf(self.libc / name)
        self.gcc = self.root / "gcc/lib64/libgcc_s.so.1"
        elf(self.gcc, bits=2)
        self.gcc32 = self.root / "gcc/lib/libgcc_s.so.1"
        elf(self.gcc32)
        self.compiler = self.root / "compiler"
        self.compiler.write_text(
            "#!/usr/bin/env python3\n"
            "import os,subprocess,sys,time\n"
            "assert len(sys.argv)==3 and sys.argv[1]=='-m32'\n"
            "name=sys.argv[2].removeprefix('-print-file-name=')\n"
            "assert name in ('libc.so','libgcc_s.so.1')\n"
            "mode=os.environ.get('QUERY_MODE')\n"
            "if mode in ('flood','stderr_flood','timeout'):\n"
            " child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])\n"
            " open(os.environ['QUERY_PID_FILE'],'w').write(str(child.pid))\n"
            " if mode=='timeout': time.sleep(60)\n"
            " while True: os.write(2 if mode=='stderr_flood' else 1,b'x'*4096)\n"
            "sys.stdout.write(os.environ.get('QUERY_'+name.replace('.','_'),''))\n"
            "sys.stderr.write(os.environ.get('QUERY_STDERR',''))\n"
            "sys.exit(int(os.environ.get('QUERY_EXIT','0')))\n"
        )
        self.compiler.chmod(0o755)
        self.env = os.environ.copy()
        self.env.pop("NIX_LDFLAGS", None)
        self.env.update(
            QUERY_libc_so=str(self.libc / "libc.so") + "\n",
            QUERY_libgcc_s_so_1=str(self.gcc) + "\n",
        )

    def cli(self, *args):
        return subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/bsim_link_env.py"),
                "--compiler",
                str(self.compiler),
                *args,
            ],
            capture_output=True,
            text=True,
            env=self.env,
            timeout=20,
        )

    def test_valid_32_and_reported_64_sibling_fallback(self):
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            shlex.split(result.stdout.strip()),
            ["export", f"NIX_LDFLAGS=-L{self.libc} -L{self.gcc32.parent}"],
        )
        self.env["QUERY_libgcc_s_so_1"] = str(self.gcc32) + "\n"
        self.assertEqual(self.cli().returncode, 0)

    def test_missing_non32_and_bad_queries(self):
        self.gcc32.unlink()
        self.assertNotEqual(self.cli().returncode, 0)
        elf(self.gcc32)
        for bad in (
            "libc.so\n",
            "",
            "relative/libc.so\n",
            "x\n" + str(self.libc / "libc.so") + "\n",
            str(self.libc / "libc.so") + "\t",
            str(self.libc / "lib c.so") + "\n",
        ):
            with self.subTest(bad=bad[:30]):
                self.env["QUERY_libc_so"] = bad
                result = self.cli()
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")
        self.env["QUERY_libc_so"] = str(self.libc / "libc.so") + "\n"
        self.env["QUERY_EXIT"] = "1"
        self.assertNotEqual(self.cli().returncode, 0)
        self.env["QUERY_EXIT"] = "0"
        self.env["QUERY_STDERR"] = "warning\n"
        result = self.cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")

    def test_continuous_output_and_deadline_close_owned_descendants(self):
        for mode in ("flood", "stderr_flood", "timeout"):
            with self.subTest(mode=mode):
                pid_file = self.root / (mode + ".pid")
                self.env.update(QUERY_MODE=mode, QUERY_PID_FILE=str(pid_file))
                with patch.dict(os.environ, self.env, clear=True):
                    with self.assertRaisesRegex(ValueError, "compiler query failed"):
                        _query(str(self.compiler), "libc.so", timeout=0.3)
                pid = int(pid_file.read_text())
                # Group owner kills child, even when compiler stalls or keeps writing.
                for _ in range(40):
                    try:
                        state = (
                            Path(f"/proc/{pid}/stat").read_text().split(") ", 1)[1][0]
                        )
                    except FileNotFoundError:
                        break
                    if state == "Z":
                        break
                    time.sleep(0.05)
                else:
                    self.fail(f"compiler descendant still live after {mode}: {pid}")

    def test_reported_32_in_32_or_lib32_directory(self):
        self.gcc32.unlink()
        in_32 = self.gcc.parent / "32/libgcc_s.so.1"
        elf(in_32)
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-L" + str(in_32.parent), shlex.split(result.stdout)[1])
        in_32.unlink()
        in_lib32 = self.root / "gcc/lib32/libgcc_s.so.1"
        elf(in_lib32)
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("-L" + str(in_lib32.parent), shlex.split(result.stdout)[1])

    def test_fifo_and_nonregular_rejected_without_read(self):
        (self.libc / "libm.so").unlink()
        os.mkfifo(self.libc / "libm.so")
        self.assertNotEqual(self.cli().returncode, 0)
        (self.libc / "libm.so").unlink()
        (self.libc / "libm.so").mkdir()
        self.assertNotEqual(self.cli().returncode, 0)

    def test_symlinks_and_header_validation(self):
        (self.libc / "libm.so").unlink()
        elf(self.libc / "libm.so.6")
        (self.libc / "libm.so").symlink_to("libm.so.6")
        self.assertEqual(self.cli().returncode, 0)
        elf(self.libc / "libm.so.6", bits=2)
        self.assertNotEqual(self.cli().returncode, 0)
        self.assertNotEqual(
            self.cli("--compiler", str(self.root / "absent")).returncode, 0
        )

    def test_shell_roundtrip_and_parent_environment_unchanged(self):
        original = os.environ.get("NIX_LDFLAGS")
        marker = self.root / "should-not-exist"
        old = f"-Wl,-z,relro ' ; touch {marker} # $(id)"
        self.env["NIX_LDFLAGS"] = old
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        child = subprocess.run(
            [
                "bash",
                "-c",
                'eval "$1"; printf "%s" "$NIX_LDFLAGS"',
                "bash",
                result.stdout.strip(),
            ],
            capture_output=True,
            text=True,
            env=self.env,
            timeout=5,
        )
        self.assertEqual(child.returncode, 0, child.stderr)
        self.assertEqual(child.stdout, f"-L{self.libc} -L{self.gcc32.parent} {old}")
        self.assertFalse(marker.exists())
        self.assertEqual(os.environ.get("NIX_LDFLAGS"), original)


if __name__ == "__main__":
    unittest.main()
