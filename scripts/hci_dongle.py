#!/usr/bin/env python3
"""Session-bound source-only nRF54L15 HCI helpers.

The session source firmware expectation describes standalone HIL source firmware,
not HCI firmware. Reuse only its physical role binding here. Attachment's HCI
preflight proves the running firmware before btattach. The session's
115200 baud console contract is not the HCI UART contract. SIGKILL cannot be
handled by Python: hard containment of root descendants needs a supervisor.
"""

import argparse
import hashlib
import json
import os
import re
import math
import signal
import stat
import subprocess
import sys
import time

from hil import discovery, lifecycle, model, session

APP_RRAM_END = 0x165000
BOARD_CFG = "boards/seeed/xiao_nrf54l15/support/openocd.cfg"
COMMAND_TIMEOUT = 90


class DongleError(Exception):
    pass


class Cancelled(DongleError):
    pass


class ChildTimeout(DongleError):
    pass


def _json(value):
    if isinstance(value, dict) or hasattr(value, "items"):
        return {key: _json(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_json(item) for item in value]
    return value


def _run(argv, timeout, records, *, env=None):
    record = {"argv": list(argv), "timeout": timeout}
    records.append(record)
    try:
        proc = subprocess.run(
            argv, capture_output=True, text=True, timeout=timeout, env=env
        )
    except subprocess.TimeoutExpired as exc:
        record.update(
            stdout=_text(exc.stdout),
            stderr=_text(exc.stderr),
            returncode=None,
            error="timeout",
        )
        raise DongleError("command timed out: %s" % argv[0]) from exc
    except OSError as exc:
        record.update(stdout="", stderr="", returncode=None, error=str(exc))
        raise DongleError("command failed: %s" % exc) from exc
    record.update(stdout=proc.stdout, stderr=proc.stderr, returncode=proc.returncode)
    return proc


def _text(value):
    return (
        value.decode("utf-8", errors="replace")
        if isinstance(value, bytes)
        else value or ""
    )


def _snapshot(image, run_dir, supplied_sha):
    from intelhex import IntelHex

    if os.path.islink(image):
        raise DongleError("image must be a regular non-symlink HEX")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    fd = os.open(image, flags)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size == 0:
            raise DongleError("image must be a nonempty regular HEX")
        with os.fdopen(fd, "rb", closefd=False) as source:
            data = source.read()
    finally:
        os.close(fd)
    digest = hashlib.sha256(data).hexdigest()
    if supplied_sha is not None and digest != supplied_sha:
        raise DongleError("image SHA-256 mismatch")
    snapshot = os.path.join(run_dir, "image.hex")
    with open(snapshot, "xb") as out:
        out.write(data)
    os.chmod(snapshot, 0o400)
    try:
        intel = IntelHex(snapshot)
        segments = intel.segments()
    except Exception as exc:
        raise DongleError("invalid Intel HEX: %s" % exc) from exc
    if not segments or any(
        start < 0 or end > APP_RRAM_END or start >= end for start, end in segments
    ):
        raise DongleError("image segments outside application RRAM [0, 0x165000)")
    return {
        "path": image,
        "snapshot": snapshot,
        "sha256": digest,
        "segments": [[start, end] for start, end in segments],
    }


def _config_check(repo):
    path = os.path.join(repo, "build", "dongle", "zephyr", ".config")
    with open(path, encoding="utf-8") as fh:
        config = set(fh.read().splitlines())
    for key in ("CONFIG_SOC_NRF54L15_CPUAPP=y", "CONFIG_BT_HCI_RAW=y"):
        if key not in config:
            raise DongleError("dongle build config missing %s" % key)


def _openocd(argv, records, env):
    proc = _run(argv, COMMAND_TIMEOUT, records, env=env)
    failures = [
        line
        for line in (proc.stdout + "\n" + proc.stderr).splitlines()
        if discovery.OPENOCD_FAILURE_RE.search(line) or re.search(r"(?i)\bwarn\b", line)
    ]
    if proc.returncode != 0 or failures:
        raise DongleError(
            "OpenOCD failed (status %s): %s" % (proc.returncode, "; ".join(failures))
        )


def _hci_preflight(tty, role, result, serial_factory, settle):
    """Probe raw H4 before btattach owns tty; no console-style serial open."""
    if serial_factory is None:
        import serial

        serial_factory = serial.Serial
    port = serial_factory(
        port=None,
        baudrate=1000000,
        bytesize=8,
        parity="N",
        stopbits=1,
        rtscts=False,
        xonxoff=False,
        dsrdtr=False,
        timeout=0.2,
        write_timeout=1,
        exclusive=True,
    )
    port.port = tty
    port.dtr = role.serial.dtr
    port.rts = role.serial.rts
    evidence = result.setdefault("hci_preflight", [])
    try:
        port.open()
        time.sleep(settle)
        port.reset_input_buffer()
        for command, expected in (
            (bytes.fromhex("01030c00"), bytes.fromhex("040e0401030c00")),
            (bytes.fromhex("01091000"), bytes.fromhex("040e0a01091000eeddccbbaac0")),
        ):
            received = bytearray()
            item = {"tx": command.hex(), "expected": expected.hex()}
            evidence.append(item)
            port.write(command)
            deadline = time.monotonic() + 2
            while len(received) < len(expected) and time.monotonic() < deadline:
                chunk = port.read(len(expected) - len(received) + 1)
                received.extend(chunk)
                if not expected.startswith(received):
                    break
            # Reject extra buffered events as well as malformed and truncated replies.
            if getattr(port, "in_waiting", 0):
                received.extend(port.read(port.in_waiting))
            item["rx"] = received.hex()
            if received != expected:
                raise DongleError("H4 preflight mismatch for %s" % command.hex())
    finally:
        port.close()


def _adapters(sysfs_root):
    directory = os.path.join(sysfs_root, "class", "bluetooth")
    return {name for name in os.listdir(directory) if re.fullmatch(r"hci[0-9]+", name)}


def _checked(argv, records, timeout=15):
    proc = _run(argv, timeout, records)
    if proc.returncode != 0:
        raise DongleError("command failed (%d): %s" % (proc.returncode, argv))
    return proc.stdout


def _info(adapter, records):
    text = _checked(["sudo", "-n", "btmgmt", "--index", adapter, "info"], records)
    address = re.search(r"(?im)^\s*addr\s+(\S+)", text)
    if not address or address.group(1).upper() != "C0:AA:BB:CC:DD:EE":
        raise DongleError("owned adapter address mismatch")
    return text


def _settings(text, key):
    line = next(
        (
            line
            for line in text.splitlines()
            if re.match(r"^\s*%s\s*:\s*" % key, line, re.I)
        ),
        "",
    )
    return set(line.split(":", 1)[1].strip().split()) if line else set()


def _stop_child(proc, record):
    if proc is None:
        return
    # Own process group, including descendants even if immediate child exited.
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        record["returncode"] = proc.wait(timeout=2)
    except subprocess.TimeoutExpired as exc:
        raise DongleError("child process group not reaped") from exc


def _group_alive(pgid):
    """Kernel-owned group check; sudo exit codes cannot prove absence."""
    try:
        os.killpg(pgid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _wait_group_absent(proc, seconds):
    deadline = time.monotonic() + seconds
    while True:
        proc.poll()  # Reap exited leader before kernel process-group lookup.
        if not _group_alive(proc.pid):
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.05)


def _stop_attach(proc, record, records):
    if proc is None:
        return
    # sudo itself may exit while root btattach remains in inherited process group.
    failures = []
    term = _run(["sudo", "-n", "kill", "-TERM", "--", "-%d" % proc.pid], 5, records)
    if term.returncode not in (0, 1):
        failures.append("btattach TERM command failed (status %d)" % term.returncode)
    if not _wait_group_absent(proc, 2):
        killed = _run(
            ["sudo", "-n", "kill", "-KILL", "--", "-%d" % proc.pid], 5, records
        )
        if killed.returncode not in (0, 1):
            failures.append(
                "btattach KILL command failed (status %d)" % killed.returncode
            )
        if not _wait_group_absent(proc, 2):
            failures.append("btattach process group still alive")
    try:
        record["returncode"] = proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        failures.append("btattach process not reaped")
    if _group_alive(proc.pid):
        failures.append("btattach process group not absent")
    if failures:
        raise DongleError("; ".join(failures))


def _attach(args, tty, role, run_dir, result, sysfs_root, serial_factory, settle):
    records = result["commands"]
    _hci_preflight(tty, role, result, serial_factory, settle)
    before = _adapters(sysfs_root)
    result["adapters_before"] = sorted(before)
    attach = child = None
    record = child_record = None
    owned = None
    errors = []
    try:
        with (
            open(os.path.join(run_dir, "btattach.stdout"), "xb") as out,
            open(os.path.join(run_dir, "btattach.stderr"), "xb") as err,
        ):
            argv = ["sudo", "-n", "btattach", "-B", tty, "-S1000000", "-P", "h4", "-N"]
            record = {"argv": argv, "stdout_file": out.name, "stderr_file": err.name}
            records.append(record)
            attach = subprocess.Popen(
                argv, stdout=out, stderr=err, start_new_session=True
            )
            record["pid"] = attach.pid
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if attach.poll() is not None:
                    raise DongleError("btattach exited before adapter appeared")
                new = _adapters(sysfs_root) - before
                if len(new) > 1:
                    raise DongleError("ambiguous new HCI adapters")
                if len(new) == 1:
                    owned = next(iter(new))
                    result["owned_adapter"] = owned
                    break
                time.sleep(0.05)
            if owned is None:
                raise ChildTimeout("new HCI adapter not found within 15s")
            time.sleep(settle)
            if attach.poll() is not None or (_adapters(sysfs_root) - before) != {owned}:
                raise DongleError("owned adapter disappeared or became ambiguous")
            info = _info(owned, records)
            if "cis-central" not in _settings(info, "supported settings"):
                raise DongleError("owned adapter lacks cis-central")
            for command in (
                ["power", "off"],
                ["power", "on"],
                ["io-cap", "3"],
                ["sc", "on"],
            ):
                if attach.poll() is not None:
                    raise DongleError("btattach exited during setup")
                _checked(["sudo", "-n", "btmgmt", "--index", owned, *command], records)
            info = _info(owned, records)
            if not {"powered", "le", "secure-conn", "cis-central"} <= _settings(
                info, "current settings"
            ):
                raise DongleError("owned adapter not ready")
            if attach.poll() is not None:
                raise DongleError("btattach exited before child")
            argv = [owned if token == "@HCI@" else token for token in args.command]
            if not argv:
                raise DongleError("attach requires child command")
            with (
                open(os.path.join(run_dir, "child.stdout"), "xb") as child_out,
                open(os.path.join(run_dir, "child.stderr"), "xb") as child_err,
            ):
                child_record = {
                    "argv": argv,
                    "stdout_file": child_out.name,
                    "stderr_file": child_err.name,
                }
                records.append(child_record)
                child = subprocess.Popen(
                    argv,
                    stdout=child_out,
                    stderr=child_err,
                    start_new_session=True,
                    env=dict(os.environ, HCI_ADAPTER=owned),
                )
                child_record["pid"] = child.pid
                deadline = time.monotonic() + args.timeout
                while child.poll() is None:
                    if attach.poll() is not None:
                        raise DongleError("btattach exited during child")
                    if time.monotonic() >= deadline:
                        raise ChildTimeout("child timed out")
                    time.sleep(0.05)
                child_record["returncode"] = child.returncode
                if child.returncode != 0:
                    raise DongleError("child exited with status %d" % child.returncode)
    except BaseException as exc:
        result["operation_error"] = str(exc)
        raise
    finally:
        try:
            _stop_child(child, child_record if child_record is not None else {})
        except Exception as exc:
            errors.append("child cleanup: %s" % exc)
        if owned is not None and owned not in before:
            try:
                if owned in _adapters(sysfs_root):
                    _info(owned, records)  # refuse to power off a replaced adapter
                    _checked(
                        ["sudo", "-n", "btmgmt", "--index", owned, "power", "off"],
                        records,
                    )
            except Exception as exc:
                errors.append("adapter cleanup: %s" % exc)
        try:
            _stop_attach(attach, record if record is not None else {}, records)
        except Exception as exc:
            errors.append("btattach cleanup: %s" % exc)
        if attach is not None:
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and owned in _adapters(sysfs_root):
                time.sleep(0.05)
            if owned is not None and owned in _adapters(sysfs_root):
                errors.append("owned adapter still present")
        if errors:
            result["cleanup_errors"] = errors
            raise DongleError("; ".join(errors))


def _identity(manifest, args, binding, records, result, sysfs_root):
    try:
        resolution = session.revalidate_session(
            manifest,
            args.fixture,
            args.binding,
            binding,
            run_cmd=lambda argv, timeout: _run(argv, timeout, records),
            sysfs_root=sysfs_root,
        )
    except discovery.HilDiscoveryError as exc:
        result["raw_identity"] = _json(exc.raw)
        raise
    result["raw_identity"] = _json(resolution.raw)
    source = resolution.roles["source"]
    result["source_identity"] = {
        "probe_serial": source.probe.serial,
        "tty": source.serial.path,
        "dpidr": source.probe.dpidr,
        "ap_idrs": dict(source.probe.ap_idrs),
        "part": source.probe.part,
        "variant_raw": source.probe.variant_raw,
    }
    return source


def execute(
    action, args, *, sysfs_root="/sys", repo_root=None, serial_factory=None, settle=0.5
):
    """Execute source-only operation; sysfs injection supports synthetic hardware tests."""
    if action not in ("flash", "reset", "attach"):
        raise DongleError("unknown action")
    if getattr(args, "image", None) and action != "flash":
        raise DongleError("--image applies only to flash")
    if getattr(args, "image", None) and not args.sha256:
        raise DongleError("--image requires --sha256")
    if getattr(args, "sha256", None) and (
        action != "flash" or not re.fullmatch(r"[0-9a-f]{64}", args.sha256)
    ):
        raise DongleError("invalid --sha256")
    if action == "attach":
        if (
            not isinstance(args.timeout, (int, float))
            or not math.isfinite(args.timeout)
            or not 0 < args.timeout <= 3600
        ):
            raise DongleError("--timeout must be finite seconds in (0, 3600]")
        if not args.command:
            raise DongleError("attach requires -- COMMAND [ARGS...]")
    repo = repo_root or lifecycle.default_repo_root()
    root = lifecycle.validate_output_root(args.output_root)
    lifecycle.validate_run_id(args.run_id)
    manifest = session.load_session(args.session_manifest, args.fixture, args.binding)
    fixture = model.load_logical_fixture(args.fixture)
    binding = model.load_physical_binding(args.binding, fixture)
    if action != "attach":
        sdk = os.environ.get("ZEPHYR_BASE", "")
        if not sdk:
            raise DongleError("ZEPHYR_BASE not set")
        cfg = os.path.join(sdk, BOARD_CFG)
        if not os.path.isfile(cfg):
            raise DongleError("OpenOCD board config missing: %s" % cfg)

    old_mask = os.umask(0o077)
    handlers = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    cancellation = {"requested": False}

    def cancel_once(signum, frame):
        if cancellation["requested"]:
            return
        cancellation["requested"] = True
        raise Cancelled("signal %d" % signum)

    for sig in handlers:
        signal.signal(sig, cancel_once)
    try:
        with lifecycle.CleanupStack() as cleanup:
            lifecycle.FixtureLock.acquire(root, manifest.fixture_id, cleanup)
            run_dir = lifecycle.create_run_dir(root, args.run_id)
            result = {
                "action": action,
                "status": "error",
                "session_sha256": manifest.sha256,
                "fixture_id": manifest.fixture_id,
                "commands": [],
                "raw_identity": {},
            }
            records = result["commands"]
            try:
                image = None
                if action == "flash":
                    image_path = args.image or os.path.join(
                        repo, "build", "dongle", "zephyr", "zephyr.hex"
                    )
                    if not args.image:
                        _config_check(repo)
                    image = _snapshot(image_path, run_dir, args.sha256)
                    result["image"] = image

                source = _identity(manifest, args, binding, records, result, sysfs_root)
                serial = source.probe.serial
                tty = source.serial.path
                if not session.PROBE_SERIAL_RE.fullmatch(
                    serial
                ) or not session.TTY_PATH_RE.fullmatch(tty):
                    raise DongleError("unsafe source identity")
                check = _run(["sudo", "-n", "lsof", "--", tty], 15, records)
                if check.returncode != 1:
                    raise DongleError(
                        "source tty held or lsof failed (status %d)" % check.returncode
                    )

                if action == "attach":
                    _attach(
                        args,
                        tty,
                        source,
                        run_dir,
                        result,
                        sysfs_root,
                        serial_factory,
                        settle,
                    )
                    result["status"] = "completed"
                    return

                env = dict(os.environ, OPENOCD_INTERFACE="cmsis-dap")
                base = [
                    "openocd",
                    "-c",
                    "adapter serial %s" % serial,
                    "-f",
                    cfg,
                    "-c",
                    "gdb port disabled",
                    "-c",
                    "tcl port disabled",
                    "-c",
                    "telnet port disabled",
                    "-c",
                    "init",
                ]
                reset = base + ["-c", "reset run", "-c", "shutdown"]
                if action == "reset":
                    _openocd(reset, records, env)
                else:
                    path = image["snapshot"]
                    if any(char in path for char in "{}\\\n\r"):
                        raise DongleError("snapshot path is not Tcl-safe")
                    flash = base + [
                        "-c",
                        "reset halt",
                        "-c",
                        "nrf54l-load {%s}" % path,
                        "-c",
                        "verify_image {%s}" % path,
                        "-c",
                        "reset run",
                        "-c",
                        "shutdown",
                    ]
                    try:
                        _openocd(flash, records, env)
                    except BaseException:
                        # Reset only when fresh identity and exclusive tty still pass.
                        try:
                            recovered = _identity(
                                manifest, args, binding, records, result, sysfs_root
                            )
                            if recovered.probe.serial != serial:
                                raise DongleError("source probe changed during flash")
                            probe_tty = recovered.serial.path
                            check = _run(
                                ["sudo", "-n", "lsof", "--", probe_tty], 15, records
                            )
                            if check.returncode != 1:
                                raise DongleError(
                                    "source tty held or lsof failed during recovery"
                                )
                            _openocd(reset, records, env)
                        except BaseException as exc:
                            result["recovery_error"] = str(exc)
                        raise
                result["status"] = "completed"
            except BaseException as exc:
                result["status"] = (
                    "cancelled"
                    if isinstance(exc, (Cancelled, KeyboardInterrupt))
                    else "timeout"
                    if isinstance(exc, ChildTimeout)
                    else "error"
                )
                result["error"] = str(exc)
                raise
            finally:
                with open(
                    os.path.join(run_dir, "result.json"), "x", encoding="utf-8"
                ) as fh:
                    json.dump(result, fh, indent=2, sort_keys=True)
                    fh.write("\n")
    finally:
        for sig, handler in handlers.items():
            signal.signal(sig, handler)
        os.umask(old_mask)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if "--" in argv:
        cut = argv.index("--")
        options, child = argv[:cut], argv[cut + 1 :]
    else:
        options, child = argv, []
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("flash", "reset", "attach"))
    for opt in ("session-manifest", "fixture", "binding", "output-root", "run-id"):
        parser.add_argument("--" + opt, required=True)
    parser.add_argument("--image")
    parser.add_argument("--sha256")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(options)
    if args.action == "attach":
        if not child:
            parser.error("attach requires -- COMMAND [ARGS...]")
        args.command = child
    elif child:
        parser.error("child command only valid for attach")
    try:
        execute(args.action, args)
    except (Exception, KeyboardInterrupt) as exc:
        print("dongle %s failed: %s" % (args.action, exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
