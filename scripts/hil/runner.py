"""One-row orchestrator for RH2 and checked-in RH3 HIL rows.

Production method runs one immutable row with exact safe ordering:
fixture/binding validation, lock, run directory, identity resolution, image
hashing, source tty ownership preflight, both consoles armed before either
flash helper, flash, boot markers, controlled fresh or preserved-bond setup,
one configured/started row, active receiver FLPR capture during source
streaming, live ISO quality during source tail, receiver stream summaries,
post-stop diagnostics, source idle, log scanning, and one CleanupStack-driven
resource close with evidence
finalization after cleanup result is known.

Every hardware boundary is injected: command execution (flash helpers,
lsof, openocd, nrf-probes, udevadm), discovery, the serial factory, the
clock, and signal cancellation.  No owned stream, serial descriptor,
process, or fixture lock survives any exit path.
"""

import hashlib
import json
import os
import subprocess
import time
from collections import Counter
from datetime import datetime, timezone

import hil.artifacts as artifact_resolver

from hil import (
    discovery,
    evidence,
    lifecycle,
    model,
    protocol,
    receiver,
    rows,
    serial_io,
    source_client,
)
from hil import qualification
from hil.discovery import HilDiscoveryError
from hil.evidence import (
    EvidenceError,
    capture_environment,
    finalize_evidence,
    write_junit,
    write_json_evidence,
)
from hil.lifecycle import CleanupStack, FixtureLock, HilLifecycleError
from hil.receiver import ReceiverError
from hil.serial_io import SerialConsoleError
from hil.source_client import SourceClientCancelled, SourceClientError

#: Short orchestration witness row (not RH3 duration acceptance). Kept as a
#: compatibility export for existing fake-lab callers; new code receives a
#: ``rows.RowSpec`` explicitly.
RH2_ROW = rows.RH2_ROW
ROW_MODE = RH2_ROW.mode
ROW_PROFILE = RH2_ROW.profile
ROW_SCORED_SDU_COUNT = RH2_ROW.scored_sdu_count
ROW_SIGNAL_SEED = RH2_ROW.signal_seed
ROW_RECONNECT_POLICY = RH2_ROW.reconnect_policy

#: Exact image inputs hashed before any flash.
IMAGE_INPUTS = (
    ("source-app", "build/hil-source/app/zephyr/zephyr.hex"),
    ("source-cpunet", "build/hil-source/hci_ipc/zephyr/zephyr.hex"),
    ("receiver-cpuapp", "build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex"),
    ("receiver-flpr", "build/nrf54l15/flpr/zephyr/zephyr.hex"),
)

#: Receiver boot markers, exact (src/main.c, audio_i2s.c,
#: flpr_handshake.c, audio_offload.c, flpr_runtime.c).
RECEIVER_BOOT_MARKERS = (
    "BLE ready",
    "settings_load() OK",
    "I2S ready",
    "FLPR READY",
    "FLPR READY_ACK sent",
    "offload init OK",
    "FLPR runtime init OK",
    'Advertising as "LE Audio Receiver"',
)


def _receiver_boot_marker_matches(marker, line):
    """Match one required receiver boot marker without order assumptions.

    ``FLPR READY_ACK sent`` contains the substring ``FLPR READY``.  Require
    the actual READY log shape for that one marker so an ACK cannot satisfy
    both readiness gates.  Other firmware markers retain their stable payload
    substrings because Zephyr log prefixes differ by backend.
    """
    line = serial_io.strip_vt100(line)
    if marker == "FLPR READY":
        return "FLPR READY (" in line
    return marker in line


UNPAIR_SUCCESS_TEXT = (
    "Pairing reset complete: bonds cleared; BONDING advertising active."
)

#: The synchronous HCI quality read is tail-only. It consumes the short source
#: tail window, so other receiver diagnostics run after stream summaries.
LIVE_TAIL_COMMANDS = ("bt iso quality",)
POST_STOP_COMMANDS = (
    "audio status",
    "audio perf",
    "flpr offload",
    "flpr status",
)

# Bounded wait budgets (seconds).
CONSOLE_READY_TIMEOUT = 10.0
FLASH_TIMEOUT = 600.0
BOOT_TIMEOUT = 90.0
SUMMARY_TIMEOUT = 60.0

# ``asrc_precheck()`` publishes submit before the serialized FLPR transaction
# reaches ``asrc_commit()``.  A healthy source can therefore expose one live
# transaction during active capture before the source tail.  Give that
# transaction a short host-side settle window, well below the tail budget,
# without weakening the final strict validator.  Use injected clock/sleep so
# fake-lab tests do not wait in real time and cancellation remains observable
# between polls.
RECEIVER_OFFLOAD_SETTLE_TIMEOUT = 0.5
RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL = 0.05

# FLPR preparation can include a coordinated reset with a 5000 ms bound and
# up to five retry/backoff attempts. This capture runs during the scored phase,
# not inside the short source tail.
RECEIVER_ACTIVE_OFFLOAD_TIMEOUT = 45.0
RECEIVER_ACTIVE_OFFLOAD_POLL_INTERVAL = 0.2

# Recovery must complete while the HIL source is still in its scored phase.
# Source emits scored_complete before a five-second tail, so fault injection
# starts only after an observable ACTIVE/success baseline and polling stops at
# this bounded host deadline. These values mirror accepted standalone gates,
# but one runner owns the UART and all retained evidence.
FAULT_BASELINE_SUCCESS = {
    "hang": 1000,
    "stall": 500,
}
FAULT_READY_TIMEOUT = 45.0
FAULT_RECOVERY_TIMEOUT = 60.0
FAULT_POLL_INTERVAL = 0.2

FAULT_COMMANDS = {
    "hang": "flpr hang",
    "stall": "flpr ring stall_flpr_ms 1 60",
}


class HilRunnerError(Exception):
    """One failed row boundary carrying its stable boundary name."""

    def __init__(self, boundary, message):
        super().__init__(message)
        self.boundary = boundary
        self.message = message


class RunnerCancelled(Exception):
    """Row cancelled by SIGINT/SIGTERM (bounded cleanup, retained
    evidence, cancelled verdict)."""


class CommandCancelled(RunnerCancelled):
    """Cancellation while an owned child process was still running.

    Keep its completed process result so the command ledger and a flash log
    retain partial stdout/stderr even though the row stops before the caller
    can receive a normal ``CompletedProcess`` return value.
    """

    def __init__(self, message, completed_process):
        super().__init__(message)
        self.completed_process = completed_process


def default_run_cmd(argv, timeout, env=None, cancel=None):
    """Run one external command with cancellation-aware bounded teardown.

    Flash helpers may run for minutes. ``subprocess.run()`` alone cannot react
    to SIGINT/SIGTERM until the helper exits, so own the child process here,
    poll cancellation at 100 ms, terminate it on cancellation, and wait a
    short bounded grace period before kill. Captured stdout/stderr remain part
    of the failed/cancelled evidence through the returned CompletedProcess.
    """
    proc = subprocess.Popen(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=env,
    )
    deadline = time.monotonic() + timeout
    cancelled = False
    try:
        while proc.poll() is None:
            if cancel is not None and cancel():
                cancelled = True
                proc.terminate()
                break
            if time.monotonic() >= deadline:
                proc.kill()
                stdout, stderr = proc.communicate()
                raise subprocess.TimeoutExpired(argv, timeout, stdout, stderr)
            time.sleep(0.1)
        try:
            stdout, stderr = proc.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            stdout, stderr = proc.communicate()
        if cancelled:
            raise CommandCancelled(
                "cancelled during command",
                subprocess.CompletedProcess(argv, proc.returncode, stdout, stderr),
            )
        return subprocess.CompletedProcess(argv, proc.returncode, stdout, stderr)
    except BaseException:
        if proc.poll() is None:
            proc.kill()
            try:
                proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        raise


def default_cancel():
    return False


class RunnerDeps:
    """Injected boundaries for one runner instance."""

    def __init__(
        self,
        run_cmd=None,
        discover=None,
        serial_factory=None,
        clock=None,
        sleep=None,
        cancel=None,
        sysfs_root="/sys",
        repo_root=None,
        environment=None,
        capture_session_factory=None,
        boot_timeout=BOOT_TIMEOUT,
        summary_timeout=SUMMARY_TIMEOUT,
    ):
        self.run_cmd = run_cmd if run_cmd is not None else default_run_cmd
        self.discover = discover
        self.serial_factory = serial_factory
        self.clock = clock if clock is not None else time.monotonic
        self.sleep = sleep if sleep is not None else time.sleep
        self.cancel = cancel if cancel is not None else default_cancel
        self.sysfs_root = sysfs_root
        self.repo_root = repo_root
        # environment: callable (argv, status) -> provenance dict; the
        # production default reads real tools read-only.
        self.environment = environment
        # Receives validated CaptureBinding, output WAV path, observed udev
        # properties, and resolved ALSA card index. Production factory owns
        # direct ALSA process; fake suites inject a process-free public boundary.
        self.capture_session_factory = capture_session_factory
        # Bounded wait budgets (override in fake tests).
        self.boot_timeout = boot_timeout
        self.summary_timeout = summary_timeout

    def resolve(self, binding, run_cmd=None):
        command = run_cmd if run_cmd is not None else self.run_cmd
        if self.discover is not None:
            return self.discover(binding, sysfs_root=self.sysfs_root, run_cmd=command)
        return discovery.resolve_fixture(
            binding, run_cmd=command, sysfs_root=self.sysfs_root
        )

    def console(self, role, path, baud, evidence_path, dtr=False, rts=False):
        if self.serial_factory is not None:
            return self.serial_factory(role, path, baud, evidence_path, dtr, rts)
        return serial_io.SerialConsole(
            role, path, baud, evidence_path, dtr=dtr, rts=rts
        )

    def capture_session(
        self,
        capture_binding,
        output_path,
        observed_udev,
        row,
        resolved_card_index,
        pre_start_validator,
        post_stop_validator,
    ):
        from hil import capture, capture_analyzer, capture_signal

        if self.capture_session_factory is not None:
            return self.capture_session_factory(
                capture_binding, output_path, observed_udev
            )
        return capture.CaptureSession(
            capture_binding,
            output_path,
            observed_udev=observed_udev,
            resolved_card_index=resolved_card_index,
            pre_start_validator=pre_start_validator,
            post_stop_validator=post_stop_validator,
            wav_validator=lambda path: capture_analyzer.validate_complete_wav(
                path,
                capture_binding.channels,
                minimum_frames=capture_signal.total_samples(row),
            ),
        )


class Runner:
    """Runs one checked-in HIL row. Call ``run()`` once."""

    def __init__(self, deps):
        self.deps = deps
        self.commands = []  # commands.jsonl records
        self._run_dir = None
        self._receiver_console = None
        self._source_console = None
        self._artifact_set = None

    # ── command ledger ──────────────────────────────────────────────

    @staticmethod
    def _command_output(value):
        """Return ``(status, stdout, stderr)`` from a process result/error.

        ``subprocess.TimeoutExpired`` carries partial output on ``output``;
        ``CommandCancelled`` carries a completed process explicitly.  Preserve
        either form in evidence instead of losing command output on abnormal
        process exits.
        """
        process = getattr(value, "completed_process", value)
        status = getattr(process, "returncode", None)
        stdout = getattr(process, "stdout", None)
        if stdout is None:
            stdout = getattr(process, "output", None)
        stderr = getattr(process, "stderr", None)

        def as_text(data):
            if data is None:
                return ""
            if isinstance(data, bytes):
                return data.decode("utf-8", errors="replace")
            return str(data)

        return status, as_text(stdout), as_text(stderr)

    def _write_flash_log(self, name, process_or_error):
        """Retain normal or partial failed flash-helper output."""
        _status, stdout, stderr = self._command_output(process_or_error)
        self._write(name, stdout + "\n" + stderr)

    def _command(self, argv, timeout, env=None):
        self._check_cancel("cancelled before command")
        start_mono = self.deps.clock()
        start_utc = datetime.now(timezone.utc).isoformat()
        merged_env = None
        if env is not None:
            merged_env = dict(os.environ)
            merged_env.update(env)
        try:
            proc = self._run_command(argv, timeout, merged_env)
        except BaseException as exc:  # noqa: BLE001 - retain cancel/process boundary
            end_utc = datetime.now(timezone.utc).isoformat()
            command_status, stdout, stderr = self._command_output(exc)
            self.commands.append(
                {
                    "argv": list(argv),
                    "env": dict(env) if env is not None else None,
                    "start_utc": start_utc,
                    "end_utc": end_utc,
                    "duration_s": round(self.deps.clock() - start_mono, 6),
                    "status": command_status,
                    "error": str(exc),
                    "stdout": stdout,
                    "stderr": stderr,
                }
            )
            raise
        end_utc = datetime.now(timezone.utc).isoformat()
        command_status, stdout, stderr = self._command_output(proc)
        self.commands.append(
            {
                "argv": list(argv),
                "env": dict(env) if env is not None else None,
                "start_utc": start_utc,
                "end_utc": end_utc,
                "duration_s": round(self.deps.clock() - start_mono, 6),
                "status": command_status,
                "error": None,
                "stdout": stdout,
                "stderr": stderr,
            }
        )
        self._check_cancel("cancelled after command")
        return proc

    def _run_command(self, argv, timeout, env):
        """Call injected command boundary, passing cancellation when supported.

        RH2 fake runners intentionally use the historical three-argument
        callable. Production default accepts ``cancel``. Detect the default
        directly rather than catching ``TypeError`` from a fake's body, which
        would hide a real test failure.
        """
        if self.deps.run_cmd is default_run_cmd:
            return self.deps.run_cmd(argv, timeout, env=env, cancel=self.deps.cancel)
        return self.deps.run_cmd(argv, timeout, env=env)

    def _check_cancel(self, boundary="cancelled"):
        if self.deps.cancel():
            raise RunnerCancelled(boundary)

    def _wait_console_line(self, console, predicate, timeout, boundary):
        """Cancellation-aware console wait.

        SerialConsole waits in short bounded slices. Keep cancellation at the
        runner boundary too, so SIGINT/SIGTERM cannot be delayed by a boot or
        teardown wait budget.
        """
        deadline = self.deps.clock() + timeout
        while True:
            self._check_cancel(boundary)
            self._check_console_health(console)
            remaining = deadline - self.deps.clock()
            if remaining <= 0:
                return None
            line = console.wait_line(predicate, min(0.1, remaining))
            self._check_console_health(console)
            if line is not None:
                return line

    def _check_console_health(self, *consoles):
        """Fail row if strict decode or reader I/O failed on any console."""
        for console in consoles:
            decode_error = console.decode_error()
            if decode_error is not None:
                raise HilRunnerError(
                    "serial",
                    "%s console invalid UTF-8: %s" % (console.role, decode_error),
                )
            read_error = console.read_error()
            if read_error is not None:
                raise HilRunnerError(
                    "serial", "%s console read failed: %s" % (console.role, read_error)
                )

    # ── evidence helpers ────────────────────────────────────────────

    def _write(self, name, text):
        with open(os.path.join(self._run_dir, name), "w", encoding="utf-8") as fh:
            fh.write(text)

    def _write_bytes(self, name, data):
        with open(os.path.join(self._run_dir, name), "wb") as fh:
            fh.write(data)

    def _read_bytes(self, path):
        with open(path, "rb") as fh:
            return fh.read()

    def _sha256(self, path):
        digest = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(65536), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def _write_jsonl(self, name, records):
        """Write one flat JSONL evidence stream atomically.

        The RH2 contract names these files ``*.jsonl``. One JSON object per
        line keeps them stream-readable while evidence finalization hashes the
        same stable byte sequence as every other retained file.
        """
        text = "".join(
            json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
            for record in records
        )
        evidence._atomic_write_text(os.path.join(self._run_dir, name), text)

    # ── row steps ───────────────────────────────────────────────────

    def _step_validate(self, fixture_path, binding_path, output_root, run_id):
        fixture = model.load_logical_fixture(fixture_path)
        binding = model.load_physical_binding(binding_path, fixture)
        canon = lifecycle.validate_output_root(output_root)
        lifecycle.validate_run_id(run_id)
        return fixture, binding, canon

    def _step_run_dir(self, canon, run_id, fixture_bytes, binding_bytes):
        run_dir = lifecycle.create_run_dir(canon, run_id)
        self._run_dir = run_dir
        self._write_bytes("fixture.json", fixture_bytes)
        self._write_bytes("binding.json", binding_bytes)
        return run_dir

    def _step_identities(self, binding, run_dir, argv, status):
        env_capture = self.deps.environment
        if env_capture is None:
            env_capture = lambda a, s: capture_environment(  # noqa: E731
                self.deps.repo_root or os.getcwd(),
                a,
                s,
                run_cmd=self._environment_command,
            )
        environment = env_capture(argv, status)
        artifact_set = getattr(self, "_artifact_set", None)
        if artifact_set is not None:
            environment = dict(environment)
            environment["artifacts"] = artifact_set.evidence()
        write_json_evidence(run_dir, "environment.json", environment)
        try:
            resolution = self.deps.resolve(binding, run_cmd=self._discovery_command)
        except HilDiscoveryError as exc:
            self._write_identity_raw(getattr(exc, "raw", {}))
            raise
        self._write_identity_raw(resolution.raw)
        write_json_evidence(run_dir, "identity.json", self._identity_dict(resolution))
        return resolution

    def _discovery_command(self, argv, timeout):
        """Ledger discovery commands with same evidence fields as flashing.

        Identity discovery must stay injectable, but its nrf-probes, udevadm,
        and read-only OpenOCD calls are still external process boundaries and
        belong in commands.jsonl. Cancellation is checked between commands;
        production command execution uses the same cancellable child owner.
        """
        return self._command(argv, timeout)

    def _environment_command(self, argv, timeout):
        """Ledger read-only provenance probes without making them fatal."""
        try:
            return self._command(argv, timeout)
        except RunnerCancelled:
            raise
        except Exception as exc:  # noqa: BLE001 - provenance stays best effort
            return subprocess.CompletedProcess(argv, -1, "", str(exc))

    def _write_identity_raw(self, raw):
        """Retain full or partial discovery evidence before any verdict."""
        raw = dict(raw)
        # A later successful resolution overwrites this minimal record with
        # full role identity. On discovery failure it proves that identity
        # resolution was incomplete while keeping raw boundary evidence flat.
        write_json_evidence(
            self._run_dir,
            "identity.json",
            {"resolved": False, "raw_keys": sorted(raw)},
        )
        self._write("nrf-probes.txt", self._raw_text(raw.get("nrf-probes")))
        self._write("nrf-probes-find.txt", self._raw_text(raw.get("nrf-probes-find")))
        for role in ("receiver", "source"):
            udev_records = raw.get("%s-udev" % role, {})
            lines = []
            for node, props in sorted(udev_records.items()):
                lines.append("## %s" % node)
                if props is None:
                    lines.append("(no udev record)")
                else:
                    for key in sorted(props):
                        lines.append("%s=%s" % (key, props[key]))
            self._write("%s-udev.txt" % role, "\n".join(lines) + "\n")
        jlink = raw.get("source-jlink-fingerprint")
        if jlink is not None:
            self._write("source-jlink.txt", jlink.get("output", ""))
        source_usb = raw.get("source-usb-udev", {})
        lines = []
        for node, props in sorted(source_usb.items()):
            lines.append("## %s" % node)
            if props is None:
                lines.append("(no udev record)")
            else:
                for key in sorted(props):
                    lines.append("%s=%s" % (key, props[key]))
        self._write("source-probe-udev.txt", "\n".join(lines) + "\n")

    def _identity_dict(self, resolution):
        out = {"roles": {}}
        for role_name, role in sorted(resolution.roles.items()):
            out["roles"][role_name] = {
                "probe": {
                    "backend": role.probe.backend,
                    "family": role.probe.family,
                    "serial": role.probe.serial,
                    "target": role.probe.target,
                    "dpidr": role.probe.dpidr,
                    "part": role.probe.part,
                    "variant": role.probe.variant,
                },
                "serial": {
                    "path": role.serial.path,
                    "baud": role.serial.baud,
                    "dtr": role.serial.dtr,
                    "rts": role.serial.rts,
                    "usb_parent": role.serial.usb_parent,
                    "properties": dict(role.serial.properties),
                },
            }
        return out

    def _raw_text(self, record):
        if record is None:
            return ""
        parts = [
            "argv: %s" % " ".join(record.get("argv", [])),
            "status: %s" % record.get("status"),
        ]
        if record.get("stdout"):
            parts.append("-- stdout --")
            parts.append(record["stdout"])
        if record.get("stderr"):
            parts.append("-- stderr --")
            parts.append(record["stderr"])
        return "\n".join(parts) + "\n"

    def _step_images(self, run_dir, repo_root, artifact_set=None):
        if artifact_set is not None:
            try:
                artifact_resolver.revalidate_artifact_set(artifact_set)
            except artifact_resolver.ArtifactError as exc:
                raise HilRunnerError("hash images", str(exc)) from exc
            payload = {
                "mode": "artifact",
                "artifacts": artifact_set.evidence(),
            }
            write_json_evidence(run_dir, "images.json", payload)
            return payload
        images = []
        missing = []
        for logical, rel in IMAGE_INPUTS:
            path = os.path.join(repo_root, rel)
            if not os.path.isfile(path):
                missing.append(rel)
                continue
            images.append(
                {
                    "logical_image": logical,
                    "path": rel,
                    "size": os.path.getsize(path),
                    "sha256": self._sha256(path),
                    "build_info": self._build_info(path),
                }
            )
        if missing:
            raise HilRunnerError(
                "hash images", "missing build images: %s" % ", ".join(missing)
            )
        write_json_evidence(run_dir, "images.json", {"images": images})
        return images

    def _build_info(self, path):
        """Best-effort build-info identity from the elf/hex directory."""
        directory = os.path.dirname(path)
        info = {}
        for name in ("zephyr.elf", "build.log"):
            candidate = os.path.join(directory, name)
            if os.path.isfile(candidate):
                info[name] = {
                    "size": os.path.getsize(candidate),
                    "mtime_ns": os.path.getmtime(candidate),
                }
        return info

    def _step_preflight_tty(self, resolution):
        tty = resolution.roles["source"].serial.path
        proc = self._command(["lsof", "--", tty], 15)
        if proc.returncode == 0:
            raise HilRunnerError(
                "preflight tty",
                "source tty %s is held by another process:\n%s"
                % (tty, proc.stdout or ""),
            )
        if proc.returncode not in (1,):
            raise HilRunnerError(
                "preflight tty", "lsof failed with status %d" % proc.returncode
            )

    def _step_open_consoles(self, resolution, stack):
        rec = resolution.roles["receiver"]
        src = resolution.roles["source"]
        receiver_console = self.deps.console(
            "receiver",
            rec.serial.path,
            rec.serial.baud,
            os.path.join(self._run_dir, "receiver-console.bin"),
            rec.serial.dtr,
            rec.serial.rts,
        )
        stack.register("close receiver console", receiver_console.close)
        source_console = self.deps.console(
            "source",
            src.serial.path,
            src.serial.baud,
            os.path.join(self._run_dir, "source-console.bin"),
            src.serial.dtr,
            src.serial.rts,
        )
        stack.register("close source console", source_console.close)
        try:
            receiver_console.open()
            source_console.open()
        except SerialConsoleError as exc:
            raise HilRunnerError("open consoles", str(exc)) from exc
        deadline = self.deps.clock() + CONSOLE_READY_TIMEOUT
        while not (receiver_console.reader_ready() and source_console.reader_ready()):
            self._check_cancel("cancelled opening consoles")
            if self.deps.clock() >= deadline:
                raise HilRunnerError("open consoles", "reader threads not ready")
            self.deps.sleep(0.05)
        return receiver_console, source_console

    def _step_flash(
        self, resolution, receiver_console, source_console, artifact_set=None
    ):
        # Start parsing each boot at a fresh host-side mark immediately before
        # its own reset. Consoles were opened before flash deliberately, so
        # stale UART data remains in raw evidence but cannot satisfy a
        # post-reset readiness gate. Marking receiver only before receiver
        # flash matters: source flashing can take long enough for old receiver
        # output to otherwise leak into its boot window.
        try:
            source_console.mark_rx()
        except SerialConsoleError as exc:
            raise HilRunnerError("mark boot", str(exc)) from exc
        source_serial = resolution.roles["source"].probe.serial
        source_env = {"FW_HIL_SOURCE_JLINK_SERIAL": source_serial}
        if artifact_set is not None:
            try:
                artifact_resolver.revalidate_artifact_set(artifact_set)
                source_env.update(
                    {
                        "FW_HIL_SOURCE_CPUAPP_HEX": artifact_resolver.image_by_role(
                            artifact_set.source_images, "cpuapp"
                        ).path,
                        "FW_HIL_SOURCE_CPUNET_HEX": artifact_resolver.image_by_role(
                            artifact_set.source_images, "cpunet"
                        ).path,
                    }
                )
            except artifact_resolver.ArtifactError as exc:
                raise HilRunnerError("flash source", str(exc)) from exc
        # Artifact hashes were revalidated after both consoles were armed and
        # immediately before this source flash command.
        try:
            src_proc = self._command(
                ["fw-flash-hil-source"],
                FLASH_TIMEOUT,
                env=source_env,
            )
        except BaseException as exc:
            self._write_flash_log("source-flash.log", exc)
            raise
        self._write_flash_log("source-flash.log", src_proc)
        self._check_flash("flash source", src_proc)
        try:
            receiver_console.mark_rx()
        except SerialConsoleError as exc:
            raise HilRunnerError("mark boot", str(exc)) from exc
        try:
            receiver_serial = resolution.roles["receiver"].probe.serial
            receiver_env = {"FW_NRF54L15_PROBE_SERIAL": receiver_serial}
            if artifact_set is not None:
                try:
                    artifact_resolver.revalidate_artifact_set(artifact_set)
                    receiver_env.update(
                        {
                            "FW_NRF54L15_CPUAPP_HEX": artifact_resolver.image_by_role(
                                artifact_set.receiver_images, "cpuapp"
                            ).path,
                            "FW_NRF54L15_FLPR_HEX": artifact_resolver.image_by_role(
                                artifact_set.receiver_images, "flpr"
                            ).path,
                        }
                    )
                except artifact_resolver.ArtifactError as exc:
                    raise HilRunnerError("flash receiver", str(exc)) from exc
            # Artifact hashes were revalidated immediately before this
            # receiver flash command.
            rec_proc = self._command(
                ["fw-flash-54l15"],
                FLASH_TIMEOUT,
                env=receiver_env,
            )
        except BaseException as exc:
            self._write_flash_log("receiver-flash.log", exc)
            raise
        self._write_flash_log("receiver-flash.log", rec_proc)
        self._check_flash("flash receiver", rec_proc)

    def _check_flash(self, boundary, proc):
        combined = (proc.stdout or "") + "\n" + (proc.stderr or "")
        failures = [
            line
            for line in combined.splitlines()
            if discovery.OPENOCD_FAILURE_RE.search(line)
        ]
        if proc.returncode != 0:
            raise HilRunnerError(
                boundary,
                "%s helper exited %d\n%s" % (boundary, proc.returncode, combined),
            )
        if failures:
            raise HilRunnerError(
                boundary, "%s OpenOCD failure lines:\n%s" % (boundary, failures)
            )

    def _step_boot(self, receiver_console, source_client):
        # `platform_init()` logs offload/runtime readiness synchronously, but
        # FLPR READY/READY_ACK arrive asynchronously over IPC.  Real ordering
        # is therefore not stable.  Consume one stream while tracking every
        # required marker, rather than discarding earlier markers while waiting
        # for a later one.
        pending = set(RECEIVER_BOOT_MARKERS)
        deadline = self.deps.clock() + self.deps.boot_timeout
        while pending:
            self._check_cancel("cancelled during boot")
            self._check_console_health(receiver_console)
            remaining = deadline - self.deps.clock()
            if remaining <= 0:
                missing = [
                    marker for marker in RECEIVER_BOOT_MARKERS if marker in pending
                ]
                raise HilRunnerError(
                    "boot", "receiver boot marker(s) missing: %r" % missing
                )
            line = receiver_console.next_line(min(0.1, remaining))
            self._check_console_health(receiver_console)
            if line is None:
                continue
            for marker in tuple(pending):
                if _receiver_boot_marker_matches(marker, line):
                    pending.remove(marker)
        try:
            return source_client.hello()
        except (SourceClientError, SerialConsoleError) as exc:
            raise HilRunnerError("boot", "source hello failed: %s" % exc) from exc

    def _step_clean_state(self, receiver_console, source_client, row):
        try:
            transcript = self._checked_receiver_command(receiver_console, "bt identity")
            identity = receiver.parse_identity(transcript)
            if identity is None:
                raise ReceiverError("bt identity did not return an identity line")
            if row.state == "fresh":
                unpair = self._checked_receiver_command(receiver_console, "bt unpair")
                if UNPAIR_SUCCESS_TEXT not in unpair:
                    raise ReceiverError("bt unpair success text missing")
                receiver_bonds = self._checked_receiver_command(
                    receiver_console, "bt bonds"
                )
                bond_count = receiver.parse_bond_count(receiver_bonds)
                if bond_count != 0:
                    raise ReceiverError(
                        "receiver bond count after unpair is %r" % bond_count
                    )
                source_client.idle()
                source_client.unpair(identity["address"], identity["address_type"])
                hello = source_client.hello()
                if hello.bond_count != 0:
                    raise ReceiverError(
                        "source bond_count after unpair is %r" % hello.bond_count
                    )
            elif row.state == "preserved":
                receiver_bonds = self._checked_receiver_command(
                    receiver_console, "bt bonds"
                )
                bond_count = receiver.parse_bond_count(receiver_bonds)
                if bond_count != 1:
                    raise ReceiverError(
                        "receiver preserved bond count is %r" % bond_count
                    )
                hello = source_client.hello()
                if hello.bond_count != 1:
                    raise ReceiverError(
                        "source preserved bond_count is %r" % hello.bond_count
                    )
            else:
                raise ReceiverError("unsupported row state %r" % row.state)
            second = self._checked_receiver_command(receiver_console, "bt identity")
            again = receiver.parse_identity(second)
            if again is None or again["address"] != identity["address"]:
                raise ReceiverError("receiver identity changed after unpair")
        except (ReceiverError, SourceClientError, SerialConsoleError) as exc:
            raise HilRunnerError("clean state", str(exc)) from exc
        return identity

    def _step_run_row(
        self, receiver_console, source_client, identity, row, capture_session=None
    ):
        from hil import capture

        active_snapshots = []
        tail_snapshots = []
        segment_summaries = []
        active_offload_by_segment = {}
        recovery = None

        def collect_streaming(segment):
            nonlocal recovery
            # Source status response has its own command ID and is consumed by
            # SourceClient, so an active snapshot proves selected ASE shape,
            # encrypted connection, and group ownership before any fault or
            # scored-tail work begins. A terminal race fails closed rather
            # than allowing cleanup-era zero fields to stand in for an active
            # stream.
            status = source_client.query_status()
            source_client.validate_active_status(status, row, segment)
            active_snapshot = {"segment": segment, "source": status}
            active_snapshots.append(active_snapshot)
            if row.fault is not None and segment == 0:
                recovery = self._run_fault_window(receiver_console, row)
                active_offload = {
                    "offload": recovery["window_final"],
                    "offload_settle": None,
                }
            else:
                active_offload = self._collect_receiver_active_offload(
                    receiver_console, row
                )
            active_snapshot["receiver_offload"] = active_offload
            active_offload_by_segment[segment] = active_offload

        def start_capture_before_source():
            if capture_session is not None:
                capture_session.start()

        def collect_tail(segment):
            tail = self._collect_receiver_tail(receiver_console, row)
            tail_snapshots.append({"segment": segment, "receiver": tail})

        def collect_summary(segment):
            summary = self._step_session_end(
                receiver_console, source_client, row, segment
            )
            summary["post_stop"] = self._collect_receiver_post_stop(
                receiver_console,
                row,
                active_offload_by_segment[segment],
                recovery,
            )
            segment_summaries.append(summary)

        try:
            source_client.register_prestart_cleanup()
            source_client.configure(
                peer_address=identity["address"],
                peer_address_type=identity["address_type"],
                mode=row.mode,
                profile=row.profile,
                scored_sdu_count=row.scored_sdu_count,
                signal_seed=row.signal_seed,
                reconnect_policy=row.reconnect_policy,
            )
            status = source_client.start(
                row=row,
                before_start_hook=start_capture_before_source,
                streaming_hook=collect_streaming,
                scored_complete_hook=collect_tail,
                segment_teardown_hook=collect_summary,
            )
        except (
            SourceClientError,
            ReceiverError,
            SerialConsoleError,
            capture.CaptureError,
        ) as exc:
            raise HilRunnerError("run row", str(exc)) from exc
        return {
            "source_final_status": status,
            "source_active": active_snapshots,
            "receiver_tail": tail_snapshots,
            "receiver_streams": segment_summaries,
            "recovery": recovery,
        }

    def _start_capture(
        self, fixture, binding, capture_resolution, qualification_record, row
    ):
        """Build optional session after all schema/identity checks, before START."""
        if fixture.capture_capability is model.CaptureCapability.NONE:
            return None
        capture_binding = binding.roles.get("capture")
        if not isinstance(capture_binding, model.CaptureBinding):
            raise HilRunnerError("capture", "validated capture binding missing")
        if qualification_record is None:
            raise HilRunnerError(
                "capture qualification", "accepted qualification missing"
            )
        if qualification_record.capability is not fixture.capture_capability:
            raise HilRunnerError(
                "capture qualification", "qualification capability mismatch"
            )
        try:
            metadata = model.verify_capture_fixture_metadata(capture_binding)
            qualification.validate_current_qualification(
                qualification_record, fixture, binding
            )
        except (model.HilSchemaError, qualification.QualificationError) as exc:
            raise HilRunnerError("capture qualification", str(exc)) from exc
        metadata_path = os.path.join(self._run_dir, "capture-fixture-metadata.json")
        with open(metadata.path, "rb") as fh:
            metadata_bytes = fh.read()
        self._write_bytes("capture-fixture-metadata.json", metadata_bytes)
        if self._sha256(metadata_path) != metadata.sha256:
            raise HilRunnerError(
                "capture qualification", "capture fixture metadata copy hash mismatch"
            )

        def revalidate_capture_identity():
            model.verify_capture_fixture_metadata(capture_binding)
            qualification.validate_current_qualification(
                qualification_record, fixture, binding
            )
            current = discovery.resolve_capture_binding(
                capture_binding,
                run_cmd=self._discovery_command,
                sysfs_root=self.deps.sysfs_root,
            )
            if (
                current.device != capture_resolution.device
                or current.card_index != capture_resolution.card_index
                or current.card_id != capture_resolution.card_id
                or dict(current.properties) != dict(capture_resolution.properties)
            ):
                raise HilDiscoveryError("capture identity changed during capture")

        output_path = os.path.join(self._run_dir, "capture.wav")
        return self.deps.capture_session(
            capture_binding,
            output_path,
            dict(capture_resolution.properties),
            row,
            capture_resolution.card_index,
            revalidate_capture_identity,
            revalidate_capture_identity,
        )

    def _finish_capture(self, session, row, fixture, qualification_record):
        """Stop owned capture after source terminal/tail and run pure analyzer."""
        if session is None:
            return None
        from hil import capture, capture_analyzer

        try:
            wav_path = session.stop()
            result = capture_analyzer.analyze_capture(
                wav_path, row, fixture.capture_capability.value, qualification_record
            )
        except (capture.CaptureError, capture_analyzer.CaptureAnalyzerError) as exc:
            self._write_capture_evidence(
                session,
                summary={
                    "outcome": "failed",
                    "error": str(exc),
                    "analysis_path": None,
                },
            )
            raise HilRunnerError("capture", str(exc)) from exc
        evidence._atomic_write_text(
            os.path.join(self._run_dir, "capture-analysis.json"),
            capture_analyzer.canonical_json(result),
        )
        self._write_capture_evidence(
            session,
            summary={
                "outcome": result["outcome"],
                "error": None,
                "analysis_path": "capture-analysis.json",
            },
        )
        if result["outcome"] != "passed":
            raise HilRunnerError("capture analyzer", "capture oracle limits failed")
        return {"session": session.evidence(), "analysis": result}

    def _write_capture_evidence(self, session, summary=None):
        """Persist owned capture process state after normal or failed cleanup."""
        payload = session.evidence()
        write_json_evidence(self._run_dir, "capture-session.json", payload)
        process = payload.get("process") or {}
        self._write("capture-arecord.stdout.txt", process.get("stdout", ""))
        self._write("capture-arecord.stderr.txt", process.get("stderr", ""))
        if summary is not None:
            summary = dict(summary)
            summary.update(
                {
                    "wav_path": payload.get("wav_path"),
                    "wav_sha256": payload.get("wav_sha256"),
                    "partial_path": payload.get("partial_path"),
                    "partial_sha256": payload.get("partial_sha256"),
                    "process_status": process.get("status"),
                }
            )
            write_json_evidence(self._run_dir, "capture-summary.json", summary)

    def _checked_receiver_command(self, receiver_console, command):
        try:
            transcript = receiver.run_receiver_command(
                receiver_console, command, cancel=self.deps.cancel
            )
        except serial_io.SerialConsoleCancelled as exc:
            raise RunnerCancelled(str(exc)) from exc
        shell_errors = receiver.scan_shell_errors(transcript)
        if shell_errors:
            raise ReceiverError(
                "receiver command %r shell error: %s"
                % (command, "; ".join(shell_errors))
            )
        return transcript

    def _fault_baseline_ready(self, status, fault):
        """True when named fault injection has a stable active baseline."""
        return (
            status.get("state") == "ACTIVE"
            and isinstance(status.get("success"), int)
            and status["success"] >= FAULT_BASELINE_SUCCESS[fault]
        )

    def _recovery_complete(self, status, baseline, fault):
        """Cheap polling predicate before strict final recovery validation.

        Require named recovery complete while source still streams. Full field
        and integrity validation stays centralized in receiver.py, where final
        tail collection has all status blocks available.
        """
        if status.get("state") != "ACTIVE":
            return False
        if status.get("recovery_attempts") != baseline["recovery_attempts"] + 1:
            return False
        if status.get("probation_active") != 0:
            return False
        if status.get("probation_cleared", -1) < baseline["probation_cleared"] + 1:
            return False
        if status.get("epoch") == baseline["epoch"]:
            return False
        if status.get("fallback", -1) <= baseline["fallback"]:
            return False
        if status.get("success", -1) <= baseline["success"]:
            return False
        restarts = status.get("runtime_restarts")
        if fault == "hang":
            return restarts == baseline["runtime_restarts"] + 1
        return restarts == baseline["runtime_restarts"]

    def _write_fault_evidence(self, name, payload):
        """Persist a fault boundary immediately, before later waits can fail."""
        write_json_evidence(self._run_dir, name, payload)

    def _run_fault_window(self, receiver_console, row):
        """Inject one named FLPR fault through runner-owned receiver UART.

        No standalone hang/stall helper may run beside this runner. Every
        shell response, baseline, acknowledgement, and recovery snapshot is
        retained here under the same fixture lock and raw console capture.
        """
        fault = row.fault
        if fault not in FAULT_COMMANDS:
            raise HilRunnerError("fault injection", "unsupported row fault %r" % fault)

        window = []
        ready_deadline = self.deps.clock() + FAULT_READY_TIMEOUT
        baseline = None
        while self.deps.clock() < ready_deadline:
            self._check_cancel("cancelled awaiting FLPR fault baseline")
            transcript = self._checked_receiver_command(
                receiver_console, "flpr offload"
            )
            window.append("# flpr offload (baseline)\n%s" % transcript)
            status = receiver.parse_offload_status(transcript)
            if self._fault_baseline_ready(status, fault):
                baseline = receiver.normalized_recovery_baseline(status)
                break
            self.deps.sleep(FAULT_POLL_INTERVAL)
        if baseline is None:
            self._write("recovery-window.txt", "\n\n".join(window) + "\n")
            raise HilRunnerError(
                "fault injection",
                "FLPR %s baseline never reached ACTIVE success>=%d"
                % (fault, FAULT_BASELINE_SUCCESS[fault]),
            )

        self._write_fault_evidence(
            "fault-baseline.json",
            {"fault": fault, "baseline": baseline, "command": FAULT_COMMANDS[fault]},
        )

        command = FAULT_COMMANDS[fault]
        start_offset = receiver_console.rx_offset()
        transcript = self._checked_receiver_command(receiver_console, command)
        window.append("# %s\n%s" % (command, transcript))
        ack = receiver.parse_fault_ack(transcript, fault)
        if ack is None:
            self._write("recovery-window.txt", "\n\n".join(window) + "\n")
            raise HilRunnerError("fault injection", "%s ACK missing" % fault)

        recovery_deadline = self.deps.clock() + FAULT_RECOVERY_TIMEOUT
        final_status = None
        end_offset = None
        while self.deps.clock() < recovery_deadline:
            self._check_cancel("cancelled awaiting FLPR recovery")
            transcript = self._checked_receiver_command(
                receiver_console, "flpr offload"
            )
            window.append("# flpr offload (recovery)\n%s" % transcript)
            status = receiver.normalized_recovery_baseline(
                receiver.parse_offload_status(transcript)
            )
            if self._recovery_complete(status, baseline, fault):
                final_status = status
                end_offset = receiver_console.rx_offset()
                break
            self.deps.sleep(FAULT_POLL_INTERVAL)
        self._write("recovery-window.txt", "\n\n".join(window) + "\n")
        if final_status is None or end_offset is None:
            raise HilRunnerError(
                "fault recovery",
                "FLPR %s recovery did not complete before deadline" % fault,
            )
        recovery = {
            "fault": fault,
            "command": command,
            "ack": ack,
            "baseline": baseline,
            "window_final": final_status,
            "raw_window": {
                "start_offset": start_offset,
                "end_offset": end_offset,
            },
        }
        self._write_fault_evidence("recovery.json", recovery)
        return recovery

    @staticmethod
    def _offload_counters_are_single_pending(offload):
        """Recognize one valid in-flight FLPR transaction snapshot."""
        submit = offload.get("submit")
        success = offload.get("success")
        return (
            isinstance(submit, int)
            and isinstance(success, int)
            and submit >= 1
            and success >= 0
            and submit == success + 1
        )

    @staticmethod
    def _offload_counters_are_equal(offload):
        """Recognize a parsed counter pair that has reached equality."""
        submit = offload.get("submit")
        success = offload.get("success")
        return (
            isinstance(submit, int)
            and isinstance(success, int)
            and submit >= 0
            and success >= 0
            and submit == success
        )

    def _settle_receiver_offload(
        self, receiver_console, row, recovery, offload, blocks
    ):
        """Re-read one healthy 10 ms offload snapshot when one submit is live.

        Named recovery rows retain their intentionally non-equal counters and
        must go straight to the existing recovery validator.  The retry loop
        records either equality or proof that one pending transaction is
        moving; it never accepts a static ``submit == success + 1`` state.
        """

        initial = offload

        def settle_evidence(outcome, retries, final):
            return {
                "outcome": outcome,
                "retries": retries,
                "initial": {
                    "submit": initial.get("submit"),
                    "success": initial.get("success"),
                },
                "final": {
                    "submit": final.get("submit"),
                    "success": final.get("success"),
                },
            }

        if self._offload_counters_are_equal(initial):
            return initial, settle_evidence("equal", 0, initial)
        if row.profile != "48_4_1" or row.fault is not None or recovery is not None:
            return initial, settle_evidence("unproven", 0, initial)
        if not self._offload_counters_are_single_pending(initial):
            return initial, settle_evidence("unproven", 0, initial)

        deadline = self.deps.clock() + RECEIVER_OFFLOAD_SETTLE_TIMEOUT
        retry = 0
        final = initial
        while self.deps.clock() < deadline:
            self._check_cancel("cancelled settling receiver offload")
            retry += 1
            transcript = self._checked_receiver_command(
                receiver_console, "flpr offload"
            )
            blocks.append("# flpr offload (settle retry %d)\n%s" % (retry, transcript))
            final = receiver.parse_offload_status(transcript)
            if self._offload_counters_are_equal(final):
                return final, settle_evidence("equal", retry, final)
            if self._offload_counters_are_single_pending(final):
                if (
                    final["submit"] > initial["submit"]
                    and final["success"] > initial["success"]
                ):
                    return final, settle_evidence("moving_single_pending", retry, final)
            remaining = deadline - self.deps.clock()
            if remaining <= 0:
                break
            self._check_cancel("cancelled settling receiver offload")
            self.deps.sleep(min(RECEIVER_OFFLOAD_SETTLE_POLL_INTERVAL, remaining))
            self._check_cancel("cancelled settling receiver offload")
        return final, settle_evidence("unproven", retry, final)

    def _collect_receiver_active_offload(self, receiver_console, row):
        """Capture one active FLPR snapshot before the source tail begins.

        FLPR preparation is asynchronous and may include coordinated reset and
        retry work. Keep polling one prompt-bounded ``flpr offload`` command
        until the stream owns an ACTIVE snapshot, then settle the same live
        transaction using the existing bounded settle logic.
        """
        blocks = []
        deadline = self.deps.clock() + RECEIVER_ACTIVE_OFFLOAD_TIMEOUT
        attempt = 0
        last_offload = None
        try:
            while self.deps.clock() < deadline:
                self._check_cancel("cancelled awaiting active receiver offload")
                attempt += 1
                transcript = self._checked_receiver_command(
                    receiver_console, "flpr offload"
                )
                blocks.append(
                    "# flpr offload (active poll %d)\n%s" % (attempt, transcript)
                )
                last_offload = receiver.parse_offload_status(transcript)
                submit = last_offload.get("submit")
                work_ready = row.profile != "48_4_1" or (
                    isinstance(submit, int)
                    and not isinstance(submit, bool)
                    and submit >= 1
                )
                if last_offload.get("state") == "ACTIVE" and work_ready:
                    final_offload, settle_evidence = self._settle_receiver_offload(
                        receiver_console,
                        row,
                        recovery=None,
                        offload=last_offload,
                        blocks=blocks,
                    )
                    active_errors = receiver._validate_active_offload(
                        final_offload,
                        row.profile,
                        recovery=None,
                        allow_moving_single_pending=(
                            settle_evidence["outcome"] == "moving_single_pending"
                        ),
                    )
                    if active_errors:
                        raise HilRunnerError(
                            "receiver active",
                            "invalid active offload: %s" % "; ".join(active_errors),
                        )
                    self._write(
                        "receiver-active-status.txt", "\n\n".join(blocks) + "\n"
                    )
                    return {
                        "offload": final_offload,
                        "offload_settle": settle_evidence,
                    }
                remaining = deadline - self.deps.clock()
                if remaining <= 0:
                    break
                self._check_cancel("cancelled awaiting active receiver offload")
                self.deps.sleep(min(RECEIVER_ACTIVE_OFFLOAD_POLL_INTERVAL, remaining))
                self._check_cancel("cancelled awaiting active receiver offload")
            raise HilRunnerError(
                "receiver active",
                "FLPR active offload deadline expired after %d attempt(s): last=%r"
                % (attempt, last_offload),
            )
        except BaseException:
            # Retain every complete poll and settle-retry transcript before a
            # timeout, cancellation, parser failure, or strict validation error.
            self._write("receiver-active-status.txt", "\n\n".join(blocks) + "\n")
            raise

    def _collect_receiver_tail(self, receiver_console, row):
        """Capture and validate only CIS-dependent ISO quality in live tail."""
        blocks = []
        iso_link_quality = None
        try:
            for command in LIVE_TAIL_COMMANDS:
                self._check_cancel("cancelled during receiver tail")
                transcript = self._checked_receiver_command(receiver_console, command)
                blocks.append("# %s\n%s" % (command, transcript))
                if command == "bt iso quality":
                    iso_link_quality = receiver.parse_iso_link_quality(transcript)
        except BaseException:
            # Keep every completed command transcript even if a later command
            # times out, is cancelled, or parser validation fails. Raw console
            # bytes already exist; this readable partial transcript is the
            # public evidence of where the tail stopped.
            self._write("receiver-status.txt", "\n\n".join(blocks) + "\n")
            raise
        self._write("receiver-status.txt", "\n\n".join(blocks) + "\n")
        iso_link_quality_errors = receiver.validate_iso_link_quality(
            iso_link_quality, row.stream_count
        )
        if iso_link_quality_errors:
            raise HilRunnerError(
                "receiver tail",
                "invalid ISO link quality: %s" % "; ".join(iso_link_quality_errors),
            )
        return {"iso_link_quality": iso_link_quality}

    def _collect_receiver_post_stop(
        self, receiver_console, row, active_offload, recovery
    ):
        """Capture terminal receiver diagnostics after stream summaries."""
        blocks = []
        audio_blocks = []
        post_stop_offload = {}
        handshake = {}
        try:
            for command in POST_STOP_COMMANDS:
                self._check_cancel("cancelled during receiver post-stop")
                transcript = self._checked_receiver_command(receiver_console, command)
                blocks.append("# %s\n%s" % (command, transcript))
                if command in ("audio status", "audio perf"):
                    audio_blocks.append(transcript)
                elif command == "flpr offload":
                    post_stop_offload = receiver.parse_offload_status(transcript)
                elif command == "flpr status":
                    handshake = receiver.parse_flpr_handshake(transcript)
        except BaseException:
            self._write("receiver-post-stop-status.txt", "\n\n".join(blocks) + "\n")
            raise

        self._write("receiver-post-stop-status.txt", "\n\n".join(blocks) + "\n")
        faults = receiver.parse_audio_faults("\n".join(audio_blocks))
        if isinstance(active_offload, dict) and isinstance(
            active_offload.get("offload"), dict
        ):
            active_snapshot = active_offload["offload"]
            settle = active_offload.get("offload_settle")
        else:
            active_snapshot = active_offload
            settle = None
        errors = receiver.validate_receiver_lifecycle_blocks(
            faults,
            active_snapshot,
            post_stop_offload,
            handshake,
            profile=row.profile,
            recovery=recovery,
            allow_moving_single_pending=(
                isinstance(settle, dict)
                and settle.get("outcome") == "moving_single_pending"
            ),
        )
        if errors:
            raise HilRunnerError(
                "receiver post-stop",
                "invalid receiver status: %s" % "; ".join(errors),
            )
        return {
            "audio_faults": faults,
            "offload": post_stop_offload,
            "handshake": handshake,
        }

    def _step_session_end(self, receiver_console, source_client, row, segment):
        del source_client
        deadline = self.deps.clock() + self.deps.summary_timeout
        summaries = []
        seen_slots = set()
        while len(summaries) < row.stream_count:
            remaining = deadline - self.deps.clock()
            if remaining <= 0:
                missing = [
                    slot for slot in range(row.stream_count) if slot not in seen_slots
                ]
                raise HilRunnerError(
                    "session end",
                    "missing receiver stream summary slot(s): %r" % missing,
                )
            summary_line = self._wait_console_line(
                receiver_console,
                lambda l: bool(receiver.parse_stream_summary(l)),
                remaining,
                "cancelled during session end",
            )
            if summary_line is None:
                missing = [
                    slot for slot in range(row.stream_count) if slot not in seen_slots
                ]
                raise HilRunnerError(
                    "session end",
                    "missing receiver stream summary slot(s): %r" % missing,
                )
            parsed = receiver.parse_stream_summary(summary_line)
            if not parsed:
                raise HilRunnerError("session end", "no parseable stream summary")
            for summary in parsed:
                slot = summary["slot"]
                if slot not in range(row.stream_count):
                    raise HilRunnerError(
                        "session end",
                        "receiver stream summary slot %d out of range [0, %d)"
                        % (slot, row.stream_count),
                    )
                if slot in seen_slots:
                    raise HilRunnerError(
                        "session end",
                        "duplicate receiver stream summary slot %d" % slot,
                    )
                seen_slots.add(slot)
                summaries.append(summary)
        last = summaries[-1]
        for summary in summaries:
            for field in ("decode_err", "i2s_underrun", "stream_reset"):
                if summary[field] != 0:
                    raise HilRunnerError(
                        "session end",
                        "stream summary slot %d %s=%d"
                        % (summary["slot"], field, summary[field]),
                    )
        # Frozen receiver transport limits (system-hil-milestones.md,
        # 2026-09-03): delivery ratio, concealment ceiling, and clean extended
        # counters.  A row that delivers a small fraction of submitted audio
        # fails here instead of passing (H42 class: rx_valid=24 of 16859).
        expected_submitted = row.expected_submitted_per_segment
        for summary in summaries:
            violations = receiver.validate_stream_transport(summary, expected_submitted)
            if violations:
                raise HilRunnerError(
                    "session end",
                    "stream summary slot %d transport limits: %s"
                    % (summary["slot"], "; ".join(violations)),
                )
        return {"segment": segment, "streams": summaries, "last": last}

    def _step_scan_logs(
        self,
        source,
        row,
        recovery=None,
        hci_remove_iso_path_trace=False,
        sdc_hci_remove_iso_path_trace=False,
    ):
        # Retain the source record evidence before any scan verdict.
        source_record_lines = source.record_lines()
        self._write_jsonl(
            "source-records.jsonl", [{"line": line} for line in source_record_lines]
        )
        # Every source HIL1 record seen on wire must have been consumed by the
        # source protocol client. This catches an unsolicited post-terminal
        # state/terminal as well as boot/parse diagnostics that arrive after a
        # normal command boundary. Counter semantics preserve duplicate lines.
        consumed_hil1 = Counter(
            line.rstrip("\r")
            for line in source_record_lines
            if line.startswith("HIL1 ")
        )
        source_raw_log = os.path.join(self._run_dir, "source-console.bin")
        try:
            with open(source_raw_log, "rb") as fh:
                source_text = fh.read().decode("utf-8")
        except UnicodeDecodeError as exc:
            raise HilRunnerError(
                "log scan", "source raw log invalid UTF-8: %s" % exc
            ) from exc
        except OSError as exc:
            raise HilRunnerError(
                "log scan", "cannot read source raw log: %s" % exc
            ) from exc

        source_warnings = receiver.scan_warnings(source_text.splitlines())
        source_protocol_errors = []
        for line in source_text.splitlines():
            if not line.startswith("HIL1 "):
                continue
            try:
                record = protocol.parse_hil1_line(line)
            except Exception as exc:  # noqa: BLE001 - protocol boundary failure
                source_protocol_errors.append("malformed source HIL1: %s" % exc)
                continue
            if source_client.is_diagnostic_record(record):
                source_protocol_errors.append(
                    "source diagnostic HIL1: command_id=%r run_id=%r"
                    % (record.command_id, record.run_id)
                )
            normalized = line.rstrip("\r")
            if consumed_hil1[normalized] <= 0:
                source_protocol_errors.append(
                    "unconsumed source HIL1 record: %s" % line
                )
            else:
                consumed_hil1[normalized] -= 1
        raw_log = os.path.join(self._run_dir, "receiver-console.bin")
        receiver_warnings = []
        receiver_shell_errors = []
        receiver_log_error = None
        trace_payload = None
        sdc_trace_payload = None
        text = ""
        if os.path.isfile(raw_log):
            try:
                with open(raw_log, "rb") as fh:
                    raw = fh.read()
                if hci_remove_iso_path_trace:
                    trace_payload = receiver.parse_hci_remove_iso_path_trace(raw)
                    write_json_evidence(
                        self._run_dir,
                        "hci-remove-iso-path-trace.json",
                        trace_payload,
                    )
                if sdc_hci_remove_iso_path_trace:
                    sdc_trace_payload = receiver.parse_sdc_hci_remove_iso_path_trace(
                        raw
                    )
                    write_json_evidence(
                        self._run_dir,
                        "sdc-hci-remove-iso-path-trace.json",
                        sdc_trace_payload,
                    )
                try:
                    receiver_warnings = receiver.scan_raw_warnings(
                        raw, fault=row.fault, recovery=recovery
                    )
                except UnicodeDecodeError as exc:
                    receiver_log_error = "receiver raw log invalid UTF-8: %s" % exc
                except ReceiverError as exc:
                    receiver_log_error = str(exc)
                if hci_remove_iso_path_trace and trace_payload is not None:
                    arm_failure_lines = {
                        record["line"]
                        for record in trace_payload["arm_failure_markers"]
                        if isinstance(record, dict)
                        and isinstance(record.get("line"), str)
                    }
                    receiver_warnings = [
                        line
                        for line in receiver_warnings
                        if serial_io.strip_vt100(line).strip() not in arm_failure_lines
                    ]
                try:
                    text = raw.decode("utf-8")
                except UnicodeDecodeError as exc:
                    receiver_log_error = "receiver raw log invalid UTF-8: %s" % exc
                    text = ""
            except OSError as exc:
                receiver_log_error = "cannot read receiver raw log: %s" % exc
                if hci_remove_iso_path_trace:
                    trace_payload = receiver.parse_hci_remove_iso_path_trace(b"")
                    trace_payload["parser_errors"].append(receiver_log_error)
                    trace_payload["validation_errors"].append(receiver_log_error)
                    write_json_evidence(
                        self._run_dir,
                        "hci-remove-iso-path-trace.json",
                        trace_payload,
                    )
                if sdc_hci_remove_iso_path_trace:
                    sdc_trace_payload = receiver.parse_sdc_hci_remove_iso_path_trace(
                        b""
                    )
                    sdc_trace_payload["parser_errors"].append(receiver_log_error)
                    sdc_trace_payload["validation_errors"].append(receiver_log_error)
                    write_json_evidence(
                        self._run_dir,
                        "sdc-hci-remove-iso-path-trace.json",
                        sdc_trace_payload,
                    )
            if text:
                receiver_shell_errors = receiver.scan_shell_errors(text)
        elif row.fault is not None:
            try:
                receiver.scan_raw_warnings(b"", fault=row.fault, recovery=recovery)
            except ReceiverError as exc:
                receiver_log_error = str(exc)
        if hci_remove_iso_path_trace and trace_payload is None:
            trace_payload = receiver.parse_hci_remove_iso_path_trace(b"")
            write_json_evidence(
                self._run_dir,
                "hci-remove-iso-path-trace.json",
                trace_payload,
            )
        if sdc_hci_remove_iso_path_trace and sdc_trace_payload is None:
            sdc_trace_payload = receiver.parse_sdc_hci_remove_iso_path_trace(b"")
            write_json_evidence(
                self._run_dir,
                "sdc-hci-remove-iso-path-trace.json",
                sdc_trace_payload,
            )

        ordinary_failures = (
            source_warnings
            or source_protocol_errors
            or receiver_warnings
            or receiver_shell_errors
            or receiver_log_error
        )
        if ordinary_failures:
            raise HilRunnerError(
                "log scan",
                "log failures: source_warnings=%r source_protocol=%r receiver=%r shell=%r error=%r"
                % (
                    source_warnings,
                    source_protocol_errors,
                    receiver_warnings,
                    receiver_shell_errors,
                    receiver_log_error,
                ),
            )
        if hci_remove_iso_path_trace and trace_payload["validation_errors"]:
            raise HilRunnerError(
                "hci remove iso path trace evidence",
                "; ".join(trace_payload["validation_errors"]),
            )
        if sdc_hci_remove_iso_path_trace and sdc_trace_payload["validation_errors"]:
            raise HilRunnerError(
                "sdc hci remove iso path trace evidence",
                "; ".join(sdc_trace_payload["validation_errors"]),
            )
        if hci_remove_iso_path_trace:
            return trace_payload
        if sdc_hci_remove_iso_path_trace:
            return sdc_trace_payload
        return None

    # ── result / evidence ───────────────────────────────────────────

    def _result_dict(
        self,
        fixture_id,
        run_id,
        outcome,
        boundary,
        failure_detail,
        cleanup_failures,
        summary,
        row,
        artifact_set=None,
        capture_capability="none",
    ):
        result = {
            "schema_version": 1,
            "fixture_id": fixture_id,
            "run_id": run_id,
            "outcome": outcome,
            "first_failed_boundary": boundary,
            "failure_detail": failure_detail,
            "cleanup_failures": [
                {"name": name, "error": error} for name, error in cleanup_failures
            ],
            "row": self._row_dict(row),
            "summary": summary,
            "capture_capability": capture_capability,
        }
        if artifact_set is not None:
            result["artifacts"] = artifact_set.evidence()
        return result

    @staticmethod
    def _row_dict(row):
        return {
            "name": row.name,
            "state": row.state,
            "mode": row.mode,
            "profile": row.profile,
            "scored_sdu_count": row.scored_sdu_count,
            "signal_seed": row.signal_seed,
            "reconnect_policy": row.reconnect_policy,
            "fault": row.fault,
            "stream_count": row.stream_count,
            "segment_count": row.segment_count,
            "expected_scored_per_stream": row.expected_scored_per_stream,
            "expected_submitted_per_segment": row.expected_submitted_per_segment,
            "expected_submitted_per_stream": row.expected_submitted_per_stream,
            "minimum_scored_interval_s": row.minimum_scored_interval_s,
        }

    def _finalize(
        self,
        fixture_id,
        run_id,
        outcome,
        boundary,
        failure_detail,
        cleanup_failures,
        summary,
        junit_path,
        row,
        artifact_set=None,
        capture_capability="none",
    ):
        """Write the complete result.json and junit.xml, then finalize
        MANIFEST.md/SHA256SUMS so every payload (including junit.xml) is
        hashed.  A finalization failure never deletes or overwrites
        primary evidence."""
        if self._run_dir is None:
            return
        self._write_result_and_junit(
            fixture_id,
            run_id,
            outcome,
            boundary,
            failure_detail,
            cleanup_failures,
            summary,
            junit_path,
            row,
            artifact_set,
            capture_capability,
        )
        finalize_evidence(
            self._run_dir,
            fixture_id=fixture_id,
            run_id=run_id,
            capture_capability=capture_capability,
            outcome=outcome,
            artifact_identity=(
                artifact_set.evidence() if artifact_set is not None else None
            ),
        )

    def _write_result_and_junit(
        self,
        fixture_id,
        run_id,
        outcome,
        boundary,
        failure_detail,
        cleanup_failures,
        summary,
        junit_path,
        row,
        artifact_set=None,
        capture_capability="none",
    ):
        """Write matching result and JUnit payloads for one known verdict."""
        write_json_evidence(
            self._run_dir,
            "result.json",
            self._result_dict(
                fixture_id,
                run_id,
                outcome,
                boundary,
                failure_detail,
                cleanup_failures,
                summary,
                row,
                artifact_set,
                capture_capability,
            ),
        )
        failure_message = None
        if outcome != "passed":
            parts = ["first failed boundary: %s" % boundary]
            if failure_detail:
                parts.append("failure detail: %s" % failure_detail)
            if cleanup_failures:
                parts.append(
                    "cleanup failures: %s"
                    % "; ".join(
                        "%s (%s)" % (name, error) for name, error in cleanup_failures
                    )
                )
            failure_message = "; ".join(parts)
        write_junit(
            self._run_dir,
            junit_path,
            outcome=outcome,
            failure_message=failure_message,
            system_out=None,
            testcase=row.name,
        )

    def _record_finalization_failure(
        self,
        fixture_id,
        run_id,
        original_boundary,
        failure_detail,
        cleanup_failures,
        summary,
        junit_path,
        row,
        artifact_set=None,
        capture_capability="none",
    ):
        """Best-effort failed result/JUnit after finalization rejected evidence.

        Primary raw evidence remains untouched. If the filesystem still allows
        writes, retained result/JUnit are corrected to the same failed verdict
        before a best-effort failed manifest is attempted. A second write
        failure is intentionally not allowed to escape ``run()``.
        """
        boundary = "evidence finalization: %s" % original_boundary
        try:
            self._write_result_and_junit(
                fixture_id,
                run_id,
                "failed",
                boundary,
                failure_detail,
                cleanup_failures,
                summary,
                junit_path,
                row,
                artifact_set,
                capture_capability,
            )
        except Exception:
            pass
        try:
            evidence._best_effort_failed_manifest(
                self._run_dir,
                fixture_id,
                run_id,
                capture_capability,
                artifact_set.evidence() if artifact_set is not None else None,
            )
        except Exception:
            pass

    # ── public entry ────────────────────────────────────────────────

    def run(
        self,
        fixture_path,
        binding_path,
        output_root,
        run_id,
        junit_path,
        argv=None,
        status=0,
        row=RH2_ROW,
        artifacts=None,
        qualification_path=None,
        hci_remove_iso_path_trace=False,
        sdc_hci_remove_iso_path_trace=False,
    ):
        """Run one row and return ``(outcome, first_boundary,
        cleanup_failures)``.  The caller maps the outcome to the process
        status (0 passed, 1 failed, 130 cancelled). ``row`` must be one of
        the immutable checked-in row specs. Evidence is finalized
        after the cleanup result is known; a cleanup failure forces
        ``failed``."""
        if not isinstance(row, rows.RowSpec):
            raise TypeError("row must be a RowSpec")
        if not isinstance(hci_remove_iso_path_trace, bool):
            raise TypeError("hci_remove_iso_path_trace must be a bool")
        if not isinstance(sdc_hci_remove_iso_path_trace, bool):
            raise TypeError("sdc_hci_remove_iso_path_trace must be a bool")
        if hci_remove_iso_path_trace and sdc_hci_remove_iso_path_trace:
            raise ValueError(
                "hci_remove_iso_path_trace and sdc_hci_remove_iso_path_trace "
                "are mutually exclusive"
            )
        if artifacts is not None:
            artifact_resolver.revalidate_artifact_set(artifacts)
        self.commands = []
        self._run_dir = None
        self._artifact_set = artifacts
        self._receiver_console = None
        self._source_console = None
        fixture_bytes = self._read_bytes(fixture_path)
        binding_bytes = self._read_bytes(binding_path)
        stack = CleanupStack()
        outcome = "failed"
        boundary = None
        failure_detail = None
        summary = {}
        src_client = None
        fault_recovery = None
        fixture = None
        capture_session = None
        qualification_record = None
        artifact_evidence = artifacts.evidence() if artifacts is not None else None
        try:
            fixture, binding, canon = self._step_validate(
                fixture_path, binding_path, output_root, run_id
            )
            if fixture.capture_capability is model.CaptureCapability.NONE:
                if qualification_path is not None:
                    raise HilRunnerError(
                        "capture qualification",
                        "none capture capability forbids qualification",
                    )
            else:
                if qualification_path is None:
                    raise HilRunnerError(
                        "capture qualification",
                        "capture requires accepted qualification",
                    )
                try:
                    qualification_record = qualification.load_qualification(
                        qualification_path, fixture, binding
                    )
                    model.verify_capture_fixture_metadata(binding.roles["capture"])
                    qualification.validate_current_qualification(
                        qualification_record, fixture, binding
                    )
                except qualification.QualificationError as exc:
                    raise HilRunnerError("capture qualification", str(exc)) from exc
                except model.HilSchemaError as exc:
                    raise HilRunnerError("capture qualification", str(exc)) from exc
            FixtureLock.acquire(canon, fixture.fixture_id, stack)
            self._run_dir = self._step_run_dir(
                canon, run_id, fixture_bytes, binding_bytes
            )
            write_json_evidence(self._run_dir, "row.json", self._row_dict(row))
            if artifact_evidence is not None:
                write_json_evidence(self._run_dir, "artifacts.json", artifact_evidence)
            resolution = self._step_identities(binding, self._run_dir, argv, status)
            capture_resolution = None
            if fixture.capture_capability is not model.CaptureCapability.NONE:
                try:
                    capture_resolution = discovery.resolve_capture_binding(
                        binding.roles["capture"],
                        run_cmd=self._discovery_command,
                        sysfs_root=self.deps.sysfs_root,
                    )
                except HilDiscoveryError as exc:
                    raise HilRunnerError("capture identity", str(exc)) from exc
                write_json_evidence(
                    self._run_dir,
                    "capture-identity.json",
                    {
                        "device": capture_resolution.device,
                        "card_index": capture_resolution.card_index,
                        "card_id": capture_resolution.card_id,
                        "sound_path": capture_resolution.sound_path,
                        "properties": dict(capture_resolution.properties),
                        "fixture_metadata": {
                            "path": binding.roles["capture"].fixture_metadata.path,
                            "sha256": binding.roles["capture"].fixture_metadata.sha256,
                        },
                    },
                )
                write_json_evidence(
                    self._run_dir,
                    "qualification.json",
                    {
                        "path": qualification_record.path,
                        "sha256": qualification_record.sha256,
                        "accepted_by": qualification_record.accepted_by,
                        "accepted_at_utc": qualification_record.accepted_at_utc,
                    },
                )
            self._step_images(
                self._run_dir,
                self.deps.repo_root or os.getcwd(),
                artifact_set=artifacts,
            )
            self._step_preflight_tty(resolution)
            receiver_console, source_console = self._step_open_consoles(
                resolution, stack
            )
            self._receiver_console = receiver_console
            self._source_console = source_console
            src_client = source_client.SourceClient(
                source_console,
                run_id,
                cleanup=stack,
                clock=self.deps.clock,
                cancel=self.deps.cancel,
            )
            self._step_flash(
                resolution, receiver_console, source_console, artifact_set=artifacts
            )
            self._step_boot(receiver_console, src_client)
            identity = self._step_clean_state(receiver_console, src_client, row)
            capture_session = self._start_capture(
                fixture, binding, capture_resolution, qualification_record, row
            )
            if capture_session is not None:
                stack.register("capture", capture_session.abort)
                # Qualification evidence is immutable external input. Copy its
                # exact bytes only after validation, then recheck copied hash.
                with open(qualification_record.path, "rb") as fh:
                    qualification_bytes = fh.read()
                self._write_bytes("qualification-accepted.json", qualification_bytes)
                if (
                    self._sha256(
                        os.path.join(self._run_dir, "qualification-accepted.json")
                    )
                    != qualification_record.sha256
                ):
                    raise HilRunnerError(
                        "capture qualification",
                        "qualification evidence copy hash mismatch",
                    )
            row_result = self._step_run_row(
                receiver_console,
                src_client,
                identity,
                row,
                capture_session=capture_session,
            )
            fault_recovery = row_result["recovery"]
            # SourceClient.start() has observed source terminal/final status and
            # receiver summary at this point. Stop capture now, before source
            # post-run idle, so capture lifetime ends at exact stream teardown.
            capture_result = self._finish_capture(
                capture_session, row, fixture, qualification_record
            )
            if capture_result is not None:
                row_result["capture"] = capture_result
            try:
                src_client.idle()
            except (SourceClientError, SerialConsoleError) as exc:
                raise HilRunnerError(
                    "session end", "source idle failed: %s" % exc
                ) from exc
            self._check_console_health(receiver_console, source_console)
            self._check_cancel("cancelled after row")
            summary = {
                "identity": identity,
                "row": self._row_dict(row),
                **row_result,
            }
            if artifacts is not None:
                summary["artifacts"] = artifacts.evidence()
            write_json_evidence(self._run_dir, "summary.json", summary)
            outcome = "passed"
        except RunnerCancelled as exc:
            outcome = "cancelled"
            boundary = str(exc)
            failure_detail = str(exc)
        except SourceClientCancelled as exc:
            outcome = "cancelled"
            boundary = str(exc)
            failure_detail = str(exc)
        except serial_io.SerialConsoleCancelled as exc:
            outcome = "cancelled"
            boundary = str(exc)
            failure_detail = str(exc)
        except (
            HilRunnerError,
            HilDiscoveryError,
            HilLifecycleError,
            EvidenceError,
            SerialConsoleError,
        ) as exc:
            outcome = "failed"
            boundary = getattr(exc, "boundary", "setup")
            failure_detail = str(exc)
        except qualification.QualificationError as exc:
            outcome = "failed"
            boundary = "capture qualification: %s" % exc
            failure_detail = str(exc)
        except Exception as exc:  # noqa: BLE001 - any boundary failure
            outcome = "failed"
            boundary = "unexpected: %s" % exc
            failure_detail = str(exc)

        cleanup_failures = []
        try:
            stack.close()
        except lifecycle.CleanupFailure as exc:
            cleanup_failures = [(name, str(orig)) for name, orig in exc.failures]
            if outcome == "passed":
                outcome = "failed"
                failure_detail = "; ".join(
                    "%s: %s" % (name, error) for name, error in cleanup_failures
                )
            if boundary is None:
                boundary = "cleanup"

        # Capture failure/cancellation still needs owned process evidence. The
        # session is closed by CleanupStack before this point, so stdout,
        # stderr, status, and any retained partial WAV state are stable.
        if capture_session is not None and self._run_dir is not None:
            try:
                if not os.path.exists(
                    os.path.join(self._run_dir, "capture-summary.json")
                ):
                    self._write_capture_evidence(
                        capture_session,
                        summary={
                            "outcome": "not_analyzed",
                            "error": boundary,
                            "analysis_path": None,
                        },
                    )
            except Exception as exc:  # noqa: BLE001 - evidence boundary
                if outcome == "passed":
                    outcome = "failed"
                    boundary = "evidence capture"
                    failure_detail = str(exc)
                elif boundary is None:
                    boundary = "evidence capture: %s" % exc
                    failure_detail = str(exc)

        # Console files become stable only after their CleanupStack-owned
        # reader threads closed. Scan raw evidence now, not while a reader can
        # append a partial HIL1 record. Preserve an earlier failed/cancelled
        # boundary rather than letting a secondary scan obscure it.
        if self._receiver_console is not None and self._source_console is not None:
            try:
                self._check_console_health(self._receiver_console, self._source_console)
            except HilRunnerError as exc:
                if outcome == "passed":
                    outcome = "failed"
                    boundary = exc.boundary
                    failure_detail = str(exc)
        if src_client is not None:
            try:
                trace_payload = self._step_scan_logs(
                    src_client,
                    row,
                    recovery=fault_recovery,
                    hci_remove_iso_path_trace=hci_remove_iso_path_trace,
                    sdc_hci_remove_iso_path_trace=sdc_hci_remove_iso_path_trace,
                )
                if trace_payload is not None and outcome == "passed":
                    if hci_remove_iso_path_trace:
                        summary["hci_remove_iso_path_trace"] = trace_payload
                    if sdc_hci_remove_iso_path_trace:
                        summary["sdc_hci_remove_iso_path_trace"] = trace_payload
                    write_json_evidence(self._run_dir, "summary.json", summary)
            except HilRunnerError as exc:
                if outcome == "passed":
                    outcome = "failed"
                    boundary = exc.boundary
                    failure_detail = str(exc)

        # Evidence must never run before cleanup.  In particular, a failed
        # atomic commands.jsonl write cannot strand an acquired fixture lock
        # or an open reader thread.  Preserve the first earlier failure, but a
        # ledger-write failure turns a formerly passing row into failed.
        if self._run_dir is not None:
            try:
                self._write_jsonl("commands.jsonl", self.commands)
            except Exception as exc:  # noqa: BLE001 - evidence boundary
                if outcome == "passed":
                    outcome = "failed"
                    boundary = "evidence commands"
                    failure_detail = str(exc)
                elif boundary is None:
                    boundary = "evidence commands"
                    failure_detail = str(exc)

        # A cancellation may arrive while the final bounded cleanup/scan was
        # running. Preserve prior failures, but do not publish a false PASS
        # after SIGINT/SIGTERM reached the runner before evidence finalizes.
        if outcome == "passed":
            try:
                self._check_cancel("cancelled before evidence finalization")
            except RunnerCancelled as exc:
                outcome = "cancelled"
                boundary = str(exc)
                failure_detail = str(exc)

        fixture_id = _fixture_id_of(binding_bytes)
        capture_capability = (
            fixture.capture_capability.value if fixture is not None else "none"
        )
        if self._run_dir is not None:
            try:
                self._finalize(
                    fixture_id,
                    run_id,
                    outcome,
                    boundary,
                    failure_detail,
                    cleanup_failures,
                    summary,
                    junit_path,
                    row,
                    artifacts,
                    capture_capability,
                )
            except Exception as exc:  # noqa: BLE001 - no finalization leak
                outcome = "failed"
                boundary = "evidence finalization: %s" % exc
                failure_detail = str(exc)
                self._record_finalization_failure(
                    fixture_id,
                    run_id,
                    str(exc),
                    failure_detail,
                    cleanup_failures,
                    summary,
                    junit_path,
                    row,
                    artifacts,
                    capture_capability,
                )
        return outcome, boundary, cleanup_failures


def _fixture_id_of(binding_bytes):
    """Best-effort fixture id for evidence when validation failed early."""
    try:
        obj = json.loads(binding_bytes.decode("utf-8"))
        return obj.get("fixture_id", "unknown")
    except (ValueError, UnicodeDecodeError):
        return "unknown"
