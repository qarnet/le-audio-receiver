"""Owned, direct-ALSA capture process boundary.

No command in this module uses a shell. Discovery and process creation remain
injected so host tests exercise exact argv and cleanup without opening ALSA.
Production callers create this object only after fixture/binding/qualification
validation has completed.
"""

import hashlib
import os
import re
import signal
import subprocess
import time
from dataclasses import dataclass

from hil import model


ARECORD_DEVICE_RE = re.compile(
    r"^card\s+(?P<card>[0-9]+):\s+(?P<name>[^,]+),\s+device\s+(?P<device>[0-9]+):"
)
XRUN_RE = re.compile(
    r"(?i)\b(xrun|overrun|underrun|usb reset|format drift|short read)\b"
)
ERROR_RE = re.compile(r"(?i)\b(error|warning|failed|invalid)\b")
STOP_TIMEOUT = 10.0


class CaptureError(Exception):
    """Raised for capture setup, process, WAV, or cleanup failure."""


@dataclass(frozen=True)
class CaptureIdentity:
    """Observed identity and frozen binding identity for evidence."""

    device: str
    resolved_card_index: int | None
    udev: dict
    mixer: dict
    fixture_metadata_path: str
    fixture_metadata_sha256: str


def _as_text(value):
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _command_result(proc):
    return {
        "argv": list(getattr(proc, "args", []) or []),
        "status": getattr(proc, "returncode", None),
        "stdout": _as_text(getattr(proc, "stdout", "")),
        "stderr": _as_text(getattr(proc, "stderr", "")),
    }


def _contains_frozen_value(output, expected):
    """Require one exact textual mixer value, never a substring coincidence."""
    pattern = r"(?<![A-Za-z0-9_.-])%s(?![A-Za-z0-9_.-])" % re.escape(expected)
    return re.search(pattern, output) is not None


def default_run_cmd(argv, timeout):
    return subprocess.run(
        argv, capture_output=True, text=True, timeout=timeout, check=False
    )


def default_popen(argv):
    return subprocess.Popen(
        argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )


def parse_arecord_devices(output):
    """Return exact direct endpoints listed by ``arecord --list-devices``."""
    endpoints = []
    for line in output.splitlines():
        match = ARECORD_DEVICE_RE.match(line)
        if match is None:
            continue
        endpoints.append(
            {
                "numeric": "hw:%s,%s" % (match.group("card"), match.group("device")),
                "name": match.group("name").strip(),
                "device": match.group("device"),
            }
        )
    return tuple(endpoints)


def alsa_card_name(device):
    """Extract exact ALSA card token from ``hw:CARD,DEV``."""
    if not isinstance(device, str) or not model.CAPTURE_DEVICE_RE.fullmatch(device):
        raise CaptureError("capture device is not exact direct hw:CARD,DEV")
    return device[3:].split(",", 1)[0]


class CaptureSession:
    """One owned capture process plus retained pre-stop evidence.

    ``start()`` creates only ``.partial.wav``. ``stop()`` leaves partial data
    on every failure and atomically promotes it only after termination,
    post-stop identity validation, and WAV validation succeed. Failed process
    termination remains retryable through ``abort()``; cleanup after confirmed
    process exit is idempotent.
    """

    def __init__(
        self,
        binding,
        output_path,
        *,
        run_cmd=None,
        popen=None,
        clock=None,
        sleep=None,
        wav_validator=None,
        observed_udev=None,
        resolved_card_index=None,
        pre_start_validator=None,
        post_stop_validator=None,
    ):
        if not isinstance(binding, model.CaptureBinding):
            raise CaptureError("validated CaptureBinding is required")
        if not isinstance(output_path, str) or not os.path.isabs(output_path):
            raise CaptureError("capture output path must be absolute")
        if not output_path.endswith(".wav"):
            raise CaptureError("capture output path must end in .wav")
        if os.path.lexists(output_path):
            raise CaptureError("capture output already exists: %s" % output_path)
        self.binding = binding
        self.output_path = output_path
        self.partial_path = output_path[: -len(".wav")] + ".partial.wav"
        if os.path.lexists(self.partial_path):
            raise CaptureError(
                "capture partial output already exists: %s" % self.partial_path
            )
        self._run_cmd = run_cmd if run_cmd is not None else default_run_cmd
        self._popen = popen if popen is not None else default_popen
        self._clock = clock if clock is not None else time.monotonic
        self._sleep = sleep if sleep is not None else time.sleep
        self._wav_validator = wav_validator
        if pre_start_validator is not None and not callable(pre_start_validator):
            raise CaptureError("capture pre-start validator must be callable")
        if post_stop_validator is not None and not callable(post_stop_validator):
            raise CaptureError("capture post-stop validator must be callable")
        self._pre_start_validator = pre_start_validator
        self._post_stop_validator = post_stop_validator
        self._observed_udev = dict(observed_udev) if observed_udev is not None else None
        if resolved_card_index is not None and (
            not isinstance(resolved_card_index, int)
            or isinstance(resolved_card_index, bool)
            or resolved_card_index < 0
        ):
            raise CaptureError(
                "resolved capture card index must be a nonnegative integer"
            )
        self._resolved_card_index = resolved_card_index
        self._proc = None
        self._started_at = None
        self._stopped_at = None
        self._preflight = []
        self._process_result = None
        self._pre_start_validation = None
        self._post_stop_validation = None
        # Process exit confirmation, terminal cleanup, and normal WAV
        # finalization differ. A failed stop must remain retryable while its
        # owned process is live; post-exit validation failure must not cause
        # validation/promotion retry.
        self._process_exit_confirmed = False
        self._process_exit_status = None
        self._cleanup_complete = False
        self._normal_finalization_completed = False
        self._process_attempts = []

    @property
    def argv(self):
        device = self.binding.device
        if self._resolved_card_index is not None:
            _card, pcm_device = device[3:].split(",", 1)
            device = "hw:%d,%s" % (self._resolved_card_index, pcm_device)
        return [
            "arecord",
            "-D",
            device,
            "-t",
            "wav",
            "-f",
            self.binding.sample_format,
            "-r",
            str(self.binding.sample_rate),
            "-c",
            str(self.binding.channels),
            "--period-size",
            "480",
            "--buffer-size",
            "1920",
            self.partial_path,
        ]

    def identity(self):
        return CaptureIdentity(
            device=self.binding.device,
            resolved_card_index=self._resolved_card_index,
            udev=dict(self.binding.udev.values),
            mixer={
                "control": self.binding.mixer.control,
                "volume": self.binding.mixer.volume,
                "capture_switch": self.binding.mixer.capture_switch,
                "agc_control": self.binding.mixer.agc_control,
                "agc": self.binding.mixer.agc,
            },
            fixture_metadata_path=self.binding.fixture_metadata.path,
            fixture_metadata_sha256=self.binding.fixture_metadata.sha256,
        )

    def _run(self, argv, timeout):
        try:
            proc = self._run_cmd(argv, timeout)
        except (OSError, subprocess.SubprocessError) as exc:
            raise CaptureError("capture command failed: %s" % exc) from None
        self._preflight.append(_command_result(proc))
        if getattr(proc, "returncode", None) != 0:
            raise CaptureError(
                "capture command failed: %s" % " ".join(str(part) for part in argv)
            )
        stderr = _as_text(getattr(proc, "stderr", ""))
        if ERROR_RE.search(stderr):
            raise CaptureError(
                "capture command emitted stderr diagnostic: %s" % stderr.strip()
            )
        return proc

    def _validate_device(self):
        devices = self._run(["arecord", "--list-devices"], 10)
        endpoints = parse_arecord_devices(_as_text(devices.stdout))
        _card, device = self.binding.device[3:].split(",", 1)
        if self._resolved_card_index is not None:
            matches = [
                endpoint
                for endpoint in endpoints
                if endpoint["numeric"]
                == "hw:%d,%s" % (self._resolved_card_index, device)
            ]
        else:
            matches = [
                endpoint
                for endpoint in endpoints
                if endpoint["numeric"] == self.binding.device
                or (endpoint["name"] == _card and endpoint["device"] == device)
            ]
        if len(matches) != 1:
            raise CaptureError(
                "capture device %s must occur exactly once in arecord list"
                % self.binding.device
            )
        if self._observed_udev is None:
            raise CaptureError("capture udev evidence is required")
        for key, expected in self.binding.udev.values.items():
            if self._observed_udev.get(key) != expected:
                raise CaptureError("capture udev identity drift for %s" % key)

    def _validate_mixer(self):
        card = (
            str(self._resolved_card_index)
            if self._resolved_card_index is not None
            else alsa_card_name(self.binding.device)
        )
        mixer = self.binding.mixer
        commands = (
            (
                ["amixer", "-c", card, "get", mixer.control],
                (mixer.volume, mixer.capture_switch),
            ),
            (["amixer", "-c", card, "get", mixer.agc_control], mixer.agc),
        )
        for argv, expected_values in commands:
            proc = self._run(argv, 10)
            output = _as_text(proc.stdout)
            if isinstance(expected_values, str):
                expected_values = (expected_values,)
            for expected in expected_values:
                if _contains_frozen_value(output, expected):
                    continue
                raise CaptureError(
                    "capture mixer state mismatch for %s: expected %r"
                    % (" ".join(argv[3:]), expected)
                )

    def start(self):
        """Validate direct endpoint/state, then launch exact non-shell argv."""
        if (
            self._cleanup_complete
            or self._normal_finalization_completed
            or self._proc is not None
        ):
            raise CaptureError("capture session cannot be started twice")
        if self._pre_start_validator is not None:
            try:
                self._pre_start_validator()
            except Exception as exc:
                self._pre_start_validation = {"outcome": "failed", "error": str(exc)}
                raise CaptureError(
                    "capture identity validation failed: %s" % exc
                ) from exc
            self._pre_start_validation = {"outcome": "passed"}
        self._validate_device()
        self._validate_mixer()
        try:
            self._proc = self._popen(self.argv)
        except OSError as exc:
            raise CaptureError("cannot start arecord: %s" % exc) from None
        self._process_exit_confirmed = False
        self._process_exit_status = None
        self._started_at = self._clock()
        return self

    def _process_exited(self):
        """Return whether process exit is confirmed through ``poll()``."""
        if self._proc is None:
            self._process_exit_confirmed = True
            self._process_exit_status = None
            return True
        if self._process_exit_confirmed:
            return True
        try:
            status = self._proc.poll()
        except OSError as exc:
            raise CaptureError(
                "cannot inspect arecord process state: %s" % exc
            ) from None
        if status is None:
            return False
        self._process_exit_confirmed = True
        self._process_exit_status = status
        return True

    def _record_process_result(
        self,
        stdout,
        stderr,
        killed_after_timeout,
        kill_attempted,
        termination_error,
    ):
        status = self._process_exit_status if self._process_exit_confirmed else None
        result = {
            "argv": self.argv,
            "status": status,
            "stdout": _as_text(stdout),
            "stderr": _as_text(stderr),
            "killed_after_timeout": killed_after_timeout,
            "kill_attempted": kill_attempted,
            "exit_confirmed": self._process_exit_confirmed,
        }
        if termination_error is not None:
            result["termination_error"] = str(termination_error)
        self._process_result = result
        self._process_attempts.append(result)

    def _stop_process(self):
        if self._proc is None:
            self._process_exit_confirmed = True
            return "", "", None
        already_exited = self._process_exited()
        stdout = ""
        stderr = ""
        killed_after_timeout = False
        kill_attempted = False
        termination_error = None
        try:
            if already_exited:
                try:
                    stdout, stderr = self._proc.communicate(timeout=STOP_TIMEOUT)
                except subprocess.TimeoutExpired as exc:
                    stdout = getattr(exc, "output", "")
                    stderr = getattr(exc, "stderr", "")
                    termination_error = CaptureError(
                        "cannot collect exited arecord output: %s" % exc
                    )
            else:
                try:
                    self._proc.send_signal(signal.SIGINT)
                except OSError as exc:
                    termination_error = CaptureError(
                        "cannot stop arecord with SIGINT: %s" % exc
                    )
                    if not self._process_exited():
                        kill_attempted = True
                        try:
                            self._proc.kill()
                        except OSError as kill_exc:
                            termination_error = CaptureError(
                                "%s; cannot kill arecord after SIGINT failure: %s"
                                % (termination_error, kill_exc)
                            )
                        else:
                            try:
                                stdout, stderr = self._proc.communicate(
                                    timeout=STOP_TIMEOUT
                                )
                            except subprocess.TimeoutExpired as final_exc:
                                stdout = getattr(final_exc, "output", "")
                                stderr = getattr(final_exc, "stderr", "")
                                termination_error = CaptureError(
                                    "%s; arecord did not stop after forced kill: %s"
                                    % (termination_error, final_exc)
                                )
                else:
                    try:
                        stdout, stderr = self._proc.communicate(timeout=STOP_TIMEOUT)
                    except subprocess.TimeoutExpired as exc:
                        stdout = getattr(exc, "output", "")
                        stderr = getattr(exc, "stderr", "")
                        killed_after_timeout = True
                        kill_attempted = True
                        try:
                            self._proc.kill()
                        except OSError as kill_exc:
                            termination_error = CaptureError(
                                "cannot kill timed-out arecord: %s" % kill_exc
                            )
                        else:
                            try:
                                stdout, stderr = self._proc.communicate(
                                    timeout=STOP_TIMEOUT
                                )
                            except subprocess.TimeoutExpired as final_exc:
                                if not stdout:
                                    stdout = getattr(final_exc, "output", "")
                                if not stderr:
                                    stderr = getattr(final_exc, "stderr", "")
                                termination_error = CaptureError(
                                    "arecord did not stop after timeout: %s" % final_exc
                                )
        except CaptureError as exc:
            if termination_error is None:
                termination_error = exc
        except OSError as exc:
            if termination_error is None:
                termination_error = CaptureError(
                    "cannot collect arecord process output: %s" % exc
                )
        finally:
            try:
                self._process_exited()
            except CaptureError as state_exc:
                if termination_error is None:
                    termination_error = state_exc
            self._record_process_result(
                stdout,
                stderr,
                killed_after_timeout,
                kill_attempted,
                termination_error,
            )
        if termination_error is not None:
            raise termination_error
        if not self._process_exit_confirmed:
            raise CaptureError("arecord process exit was not confirmed")
        if killed_after_timeout:
            raise CaptureError("arecord exceeded stop timeout")
        if self._process_exit_status not in (0, -signal.SIGINT, 130):
            raise CaptureError(
                "arecord exited unexpectedly: %s" % self._process_exit_status
            )
        diagnostics = self._process_result["stderr"] if self._process_result else ""
        if XRUN_RE.search(diagnostics) or ERROR_RE.search(diagnostics):
            raise CaptureError("arecord stderr diagnostic: %s" % diagnostics.strip())
        return stdout, stderr, self._process_exit_status

    def stop(self):
        """Stop process, validate final WAV, atomically promote partial evidence."""
        if self._normal_finalization_completed or self._cleanup_complete:
            return self.output_path if os.path.isfile(self.output_path) else None
        self._stopped_at = self._clock()
        try:
            self._stop_process()
            if self._post_stop_validator is not None:
                try:
                    self._post_stop_validator()
                except Exception as exc:  # recheck must retain partial WAV evidence
                    self._post_stop_validation = {
                        "outcome": "failed",
                        "error": str(exc),
                    }
                    raise CaptureError(
                        "capture identity revalidation failed: %s" % exc
                    ) from exc
                self._post_stop_validation = {"outcome": "passed"}
            if not os.path.isfile(self.partial_path):
                raise CaptureError("arecord did not create partial WAV")
            if self._wav_validator is None:
                raise CaptureError("capture WAV validator is required")
            try:
                self._wav_validator(self.partial_path)
            except Exception as exc:  # validator gives precise parser/oracle reason
                raise CaptureError("captured WAV invalid: %s" % exc) from exc
            if os.path.lexists(self.output_path):
                raise CaptureError(
                    "capture final output collision: %s" % self.output_path
                )
            try:
                os.replace(self.partial_path, self.output_path)
            except OSError as exc:
                raise CaptureError("cannot promote captured WAV: %s" % exc) from None
        except CaptureError:
            # Retain process diagnostics and partial evidence. Once process exit
            # is confirmed, CleanupStack cleanup must not re-run validation or
            # promote a retained partial WAV.
            if self._process_exit_confirmed:
                self._cleanup_complete = True
            raise
        self._normal_finalization_completed = True
        self._cleanup_complete = True
        return self.output_path

    def abort(self):
        """Bounded cleanup path. Never deletes completed or partial evidence."""
        if self._cleanup_complete:
            return
        self._stopped_at = self._clock()
        try:
            self._stop_process()
        finally:
            if self._process_exit_confirmed:
                self._cleanup_complete = True

    def evidence(self):
        """Return serializable capture session evidence without mutating files."""

        def sha256_if_regular(path):
            if not os.path.isfile(path):
                return None
            digest = hashlib.sha256()
            with open(path, "rb") as fh:
                for chunk in iter(lambda: fh.read(65536), b""):
                    digest.update(chunk)
            return digest.hexdigest()

        final_sha = sha256_if_regular(self.output_path)
        partial_sha = sha256_if_regular(self.partial_path)
        return {
            "identity": self.identity().__dict__,
            "argv": self.argv,
            "preflight": self._preflight,
            "process": self._process_result,
            "process_attempts": list(self._process_attempts),
            "pre_start_validation": self._pre_start_validation,
            "post_stop_validation": self._post_stop_validation,
            "started_monotonic": self._started_at,
            "stopped_monotonic": self._stopped_at,
            "partial_path": self.partial_path,
            "partial_sha256": partial_sha,
            "wav_path": self.output_path if os.path.isfile(self.output_path) else None,
            "wav_sha256": final_sha,
        }
