#!/usr/bin/env python3
"""
Unit tests for the fw-flash-dongle probe-selection behavior, tested through
public script execution with fake west/openocd/nrf-probes.  No hardware
needed.

Moved from the retired tests/unit/gate/ child (R3) unchanged in behavioral
intent; this file is the canonical home of the five dongle flash tests.
"""

import os
import shutil
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_SCRIPTS_BIN = os.path.join(_REPO_ROOT, "scripts", "bin")


def _write_fake(dirpath, name, body):
    path = os.path.join(dirpath, name)
    with open(path, "w", encoding="utf-8") as f:
        f.write(body)
    os.chmod(path, 0o755)
    return path


def _make_dongle_harness(tmpdir):
    """Temp repo layout running the real fw-flash-dongle + fw-common.sh.

    Fakes on PATH: west (env check), openocd (records argv, exit 0),
    nrf-probes (fails loudly if invoked).  Returns (env, paths).
    """
    repo = os.path.join(tmpdir, "repo")
    bin_dir = os.path.join(repo, "scripts", "bin")
    os.makedirs(bin_dir)
    shutil.copy(
        os.path.join(_SCRIPTS_BIN, "fw-flash-dongle"),
        os.path.join(bin_dir, "fw-flash-dongle"),
    )
    shutil.copy(
        os.path.join(_SCRIPTS_BIN, "fw-common.sh"),
        os.path.join(bin_dir, "fw-common.sh"),
    )

    hex_dirs = [
        os.path.join(repo, "build", "dongle", "hci_uart", "zephyr"),
        os.path.join(repo, "build", "dongle", "hci_ipc", "zephyr"),
    ]
    for d in hex_dirs:
        os.makedirs(d)
        with open(os.path.join(d, "zephyr.hex"), "w", encoding="utf-8"):
            pass

    fakebin = os.path.join(tmpdir, "fakebin")
    os.makedirs(fakebin)
    _write_fake(fakebin, "west", "#!/usr/bin/env bash\nexit 0\n")
    _write_fake(
        fakebin,
        "openocd",
        "#!/usr/bin/env bash\n"
        'printf \'%s\\0\' "$@" > "${OPENOCD_ARGV_FILE:?}"\n'
        "exit 0\n",
    )
    _write_fake(
        fakebin,
        "nrf-probes",
        "#!/usr/bin/env bash\n"
        'touch "${NRF_PROBES_CALLED_FILE:?}"\n'
        'echo "nrf-probes must not be invoked" >&2\n'
        "exit 99\n",
    )

    zephyr_base = os.path.join(tmpdir, "zephyrbase")
    os.makedirs(zephyr_base)

    argv_file = os.path.join(tmpdir, "openocd.argv")
    nrf_called = os.path.join(tmpdir, "nrf-probes.called")

    env = dict(os.environ)
    env["PATH"] = fakebin + os.pathsep + env["PATH"]
    env["ZEPHYR_BASE"] = zephyr_base
    env["OPENOCD_ARGV_FILE"] = argv_file
    env["NRF_PROBES_CALLED_FILE"] = nrf_called
    env.pop("FW_DONGLE_JLINK_SERIAL", None)

    return env, {
        "script": os.path.join(bin_dir, "fw-flash-dongle"),
        "argv_file": argv_file,
        "nrf_called": nrf_called,
        "app_hex": os.path.join(hex_dirs[0], "zephyr.hex"),
        "net_hex": os.path.join(hex_dirs[1], "zephyr.hex"),
    }


def _run_dongle(env, harness, extra_env=None):
    e = dict(env)
    if extra_env:
        e.update(extra_env)
    return subprocess.run(
        [harness["script"]], env=e, capture_output=True, text=True, timeout=30
    )


def _read_openocd_argv(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return [a.decode("utf-8") for a in f.read().split(b"\0") if a]


def _argv_index(args, item):
    return args.index(item)


def test_dongle_flash_default_autodetect():
    """Default: J-Link auto-detection, no adapter serial, net-first/app-second."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h)
        assert r.returncode == 0, f"exit {r.returncode}: {r.stderr}"
        assert "J-Link auto-detection" in r.stdout
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"

        args = _read_openocd_argv(h["argv_file"])
        assert args is not None, "openocd never invoked"
        assert "interface/jlink.cfg" in args
        assert "target/nordic/nrf53.cfg" in args
        assert not any(a.startswith("adapter serial") for a in args), (
            "default run must not pass adapter serial"
        )
        # net-first / app-second program + verify, reset, shutdown
        assert _argv_index(args, "targets nrf53.cpunet") < _argv_index(
            args, "program %s verify" % h["net_hex"]
        )
        assert _argv_index(args, "program %s verify" % h["net_hex"]) < _argv_index(
            args, "targets nrf53.cpuapp"
        )
        assert _argv_index(args, "targets nrf53.cpuapp") < _argv_index(
            args, "program %s verify" % h["app_hex"]
        )
        assert "reset run" in args
        assert "shutdown" in args
    print("  PASS: dongle default J-Link auto-detection argv")


def test_dongle_flash_explicit_serial():
    """Explicit override: exactly one adapter serial command, order unchanged."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h, {"FW_DONGLE_JLINK_SERIAL": "ABC-123.45"})
        assert r.returncode == 0, f"exit {r.returncode}: {r.stderr}"
        assert "J-Link override (serial ABC-123.45)" in r.stdout
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"

        args = _read_openocd_argv(h["argv_file"])
        assert args is not None, "openocd never invoked"
        serial_cmds = [a for a in args if a.startswith("adapter serial")]
        assert serial_cmds == ["adapter serial ABC-123.45"], serial_cmds
        # override lands after J-Link config, before target init
        assert _argv_index(args, "interface/jlink.cfg") < _argv_index(
            args, "adapter serial ABC-123.45"
        )
        assert _argv_index(args, "adapter serial ABC-123.45") < _argv_index(
            args, "target/nordic/nrf53.cfg"
        )
        # programming order unchanged
        assert _argv_index(args, "targets nrf53.cpunet") < _argv_index(
            args, "program %s verify" % h["net_hex"]
        )
        assert _argv_index(args, "program %s verify" % h["net_hex"]) < _argv_index(
            args, "targets nrf53.cpuapp"
        )
        assert _argv_index(args, "targets nrf53.cpuapp") < _argv_index(
            args, "program %s verify" % h["app_hex"]
        )
    print("  PASS: dongle explicit J-Link serial override argv")


def test_dongle_flash_invalid_serial():
    """Invalid override: nonzero exit, clear error, openocd/nrf-probes untouched."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        r = _run_dongle(env, h, {"FW_DONGLE_JLINK_SERIAL": "bad!serial"})
        assert r.returncode != 0, "invalid serial must fail"
        assert "Invalid FW_DONGLE_JLINK_SERIAL" in r.stderr
        assert "^[[:alnum:]_.:-]+$" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle invalid J-Link serial rejected before openocd")


def test_dongle_flash_missing_artifacts():
    """Missing build artifacts still fail with the same error (no tools run)."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        os.remove(h["app_hex"])
        r = _run_dongle(env, h)
        assert r.returncode != 0, "missing artifact must fail"
        assert "No build artifacts found" in r.stderr
        assert "fw-build-dongle" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle missing-artifact error unchanged")


def test_dongle_flash_missing_dev_shell():
    """Missing dev shell (no ZEPHYR_BASE) still fails with the same error."""
    with tempfile.TemporaryDirectory() as tmp:
        env, h = _make_dongle_harness(tmp)
        env.pop("ZEPHYR_BASE", None)
        r = _run_dongle(env, h)
        assert r.returncode != 0, "missing dev shell must fail"
        assert "firmware tool error" in r.stderr
        assert not os.path.exists(h["argv_file"]), "openocd must not be invoked"
        assert not os.path.exists(h["nrf_called"]), "nrf-probes must not be invoked"
    print("  PASS: dongle missing-dev-shell error unchanged")


def run_tests():
    tests = [
        test_dongle_flash_default_autodetect,
        test_dongle_flash_explicit_serial,
        test_dongle_flash_invalid_serial,
        test_dongle_flash_missing_artifacts,
        test_dongle_flash_missing_dev_shell,
    ]

    failures = 0
    for test in tests:
        try:
            test()
        except Exception as e:
            print(f"  FAIL: {test.__name__}: {e}")
            failures += 1

    print(f"\n{len(tests) - failures}/{len(tests)} tests passed")
    return failures


if __name__ == "__main__":
    sys.exit(run_tests())
