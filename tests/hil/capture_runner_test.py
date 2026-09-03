#!/usr/bin/env python3
"""Host-only capture process, runner lifecycle, matrix verdict, and CLI tests."""

import os
import signal
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts"))
sys.path.insert(0, os.path.dirname(__file__))

from hil import capture, cli, discovery, matrix, model, rows, runner  # noqa: E402
import capture_model_test  # noqa: E402
import hil_fakes  # noqa: E402
import rh2_test  # noqa: E402


class TimelineCapture:
    def __init__(self, timeline, output_path):
        self.timeline = timeline
        self.output_path = output_path
        self.stopped = False

    def start(self):
        self.timeline.append("capture-start")

    def stop(self):
        self.timeline.append("capture-stop")
        self.stopped = True
        with open(self.output_path, "wb") as fh:
            fh.write(b"synthetic capture for runner lifecycle test\n")
        return self.output_path

    def abort(self):
        if not self.stopped:
            self.timeline.append("capture-abort")

    def evidence(self):
        return {
            "process": {"stdout": "", "stderr": "", "status": 0},
            "wav_path": self.output_path if self.stopped else None,
            "wav_sha256": "synthetic" if self.stopped else None,
            "partial_path": self.output_path.replace(".wav", ".partial.wav"),
        }


class FakeProc:
    def __init__(self, stdout="", stderr="", returncode=0, timeout_once=False):
        self.stdout = stdout
        self.stderr = stderr
        self.returncode = returncode
        self.timeout_once = timeout_once
        self.signals = []
        self.killed = False

    def poll(self):
        return None if not self.signals and not self.killed else self.returncode

    def send_signal(self, value):
        self.signals.append(value)

    def communicate(self, timeout=None):
        if self.timeout_once:
            self.timeout_once = False
            raise subprocess.TimeoutExpired(["arecord"], timeout)
        return self.stdout, self.stderr

    def kill(self):
        self.killed = True


class RetryingStopProc:
    """Fake process whose first SIGINT/kill cleanup attempt cannot exit."""

    def __init__(self):
        self.returncode = None
        self.running = True
        self.signals = []
        self.kill_calls = 0
        self.communicate_calls = 0

    def poll(self):
        return None if self.running else self.returncode

    def send_signal(self, value):
        self.signals.append(value)
        if len(self.signals) == 1:
            raise OSError("synthetic SIGINT failure")
        self.running = False
        self.returncode = -signal.SIGINT

    def communicate(self, timeout=None):
        self.communicate_calls += 1
        if self.running:
            raise subprocess.TimeoutExpired(["arecord"], timeout)
        return "", ""

    def kill(self):
        self.kill_calls += 1
        if self.kill_calls == 1:
            raise OSError("synthetic kill failure")
        self.running = False
        self.returncode = -signal.SIGKILL


class AbortAfterValidationProc(FakeProc):
    """Fake process that exits cleanly after SIGINT for validation failures."""

    def poll(self):
        return None if not self.signals else self.returncode


class CaptureFactory:
    def __init__(self, list_output, mixer_output="12 on off", proc=None):
        self.calls = []
        self.list_output = list_output
        self.mixer_output = mixer_output
        self.proc = proc if proc is not None else FakeProc()

    def run(self, argv, timeout):
        self.calls.append((list(argv), timeout))
        if argv == ["arecord", "--list-devices"]:
            return SimpleNamespace(
                args=argv, stdout=self.list_output, stderr="", returncode=0
            )
        return SimpleNamespace(
            args=argv, stdout=self.mixer_output, stderr="", returncode=0
        )

    def popen(self, argv):
        self.calls.append((list(argv), "popen"))
        return self.proc


def capture_binding(directory):
    fixture, binding, _fixture_path, _binding_path = (
        capture_model_test.load_fixture_binding(directory)
    )
    return fixture, binding.roles["capture"]


class TestCaptureProcess(unittest.TestCase):
    def test_exact_argv_lifecycle_and_atomic_promotion(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            factory = CaptureFactory("card 1: Capture_1, device 0: Audio [Capture_1]\n")

            def valid_wav(path):
                with open(path, "wb") as fh:
                    fh.write(b"wav")

            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=valid_wav,
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            self.assertEqual(
                factory.calls[-1][0],
                [
                    "arecord",
                    "-D",
                    "hw:Capture_1,0",
                    "-t",
                    "wav",
                    "-f",
                    "S16_LE",
                    "-r",
                    "48000",
                    "-c",
                    "1",
                    "--period-size",
                    "480",
                    "--buffer-size",
                    "1920",
                    os.path.join(td, "capture.partial.wav"),
                ],
            )
            with open(session.partial_path, "wb") as fh:
                fh.write(b"synthetic partial WAV")
            self.assertEqual(session.stop(), output)
            self.assertTrue(os.path.isfile(output))
            self.assertFalse(os.path.exists(session.partial_path))
            self.assertEqual(factory.proc.signals, [signal.SIGINT])

    def test_xrun_stderr_timeout_and_partial_retention_fail(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            proc = FakeProc(stderr="arecord: xrun")
            factory = CaptureFactory(
                "card 1: Capture_1, device 0: Audio [Capture_1]\n", proc=proc
            )
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")
            with self.assertRaises(capture.CaptureError) as ctx:
                session.stop()
            self.assertIn("stderr diagnostic", str(ctx.exception))
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))

    def test_identity_and_mixer_drift_block_before_process(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            factory = CaptureFactory("card 2: Other, device 0: Audio\n")
            session = capture.CaptureSession(
                binding,
                os.path.join(td, "capture.wav"),
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
            )
            with self.assertRaises(capture.CaptureError) as ctx:
                session.start()
            self.assertIn("exactly once", str(ctx.exception))
            self.assertFalse(any(call[1] == "popen" for call in factory.calls))

    def test_numeric_direct_device_requires_numeric_arecord_match(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            binding = type(binding)(
                backend=binding.backend,
                device="hw:1,0",
                channels=binding.channels,
                sample_rate=binding.sample_rate,
                sample_format=binding.sample_format,
                udev=binding.udev,
                mixer=binding.mixer,
                fixture_metadata=binding.fixture_metadata,
            )
            factory = CaptureFactory("card 2: 1, device 0: Audio [1]\n")
            session = capture.CaptureSession(
                binding,
                os.path.join(td, "capture.wav"),
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
                resolved_card_index=1,
            )
            with self.assertRaises(capture.CaptureError) as ctx:
                session.start()
            self.assertIn("exactly once", str(ctx.exception))
            self.assertFalse(any(call[1] == "popen" for call in factory.calls))

    def test_card_alias_uses_resolved_numeric_endpoint_for_arecord_and_mixer(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            factory = CaptureFactory("card 1: Capture_1, device 0: Audio [Capture_1]\n")
            session = capture.CaptureSession(
                binding,
                os.path.join(td, "capture.wav"),
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
                resolved_card_index=1,
            )
            session.start()
            self.assertEqual(factory.calls[-1][0][2], "hw:1,0")
            self.assertEqual(factory.calls[1][0][:4], ["amixer", "-c", "1", "get"])

    def test_timeout_retains_process_evidence_and_partial_file(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            proc = FakeProc(timeout_once=True)
            factory = CaptureFactory(
                "card 1: Capture_1, device 0: Audio [Capture_1]\n", proc=proc
            )
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")
            with self.assertRaises(capture.CaptureError) as ctx:
                session.stop()
            self.assertIn("exceeded stop timeout", str(ctx.exception))
            self.assertTrue(proc.killed)
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertTrue(session.evidence()["process"]["killed_after_timeout"])

    def test_stop_failure_keeps_process_retryable_for_abort(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            proc = RetryingStopProc()
            factory = CaptureFactory(
                "card 1: Capture_1, device 0: Audio [Capture_1]\n", proc=proc
            )
            wav_validator = mock.Mock()
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=wav_validator,
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")

            with self.assertRaises(capture.CaptureError) as ctx:
                session.stop()

            self.assertIn("cannot stop arecord with SIGINT", str(ctx.exception))
            self.assertTrue(proc.running)
            self.assertEqual(proc.signals, [signal.SIGINT])
            self.assertEqual(proc.kill_calls, 1)
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))
            wav_validator.assert_not_called()
            first_attempt = session.evidence()["process_attempts"][-1]
            self.assertFalse(first_attempt["exit_confirmed"])
            self.assertIn("synthetic kill failure", first_attempt["termination_error"])

            session.abort()

            self.assertFalse(proc.running)
            self.assertIsNotNone(proc.poll())
            self.assertEqual(proc.signals, [signal.SIGINT, signal.SIGINT])
            self.assertEqual(proc.kill_calls, 1)
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))
            wav_validator.assert_not_called()
            evidence = session.evidence()
            self.assertEqual(len(evidence["process_attempts"]), 2)
            self.assertTrue(evidence["process"]["exit_confirmed"])

            session.abort()
            self.assertEqual(proc.signals, [signal.SIGINT, signal.SIGINT])

    def test_abort_is_idempotent_after_finished_success_and_failure(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            for name, stderr, expects_final in (
                ("success", "", True),
                ("stderr", "arecord: xrun", False),
            ):
                with self.subTest(name=name):
                    output = os.path.join(td, name + ".wav")
                    proc = FakeProc(stderr=stderr)
                    factory = CaptureFactory(
                        "card 1: Capture_1, device 0: Audio [Capture_1]\n",
                        proc=proc,
                    )
                    session = capture.CaptureSession(
                        binding,
                        output,
                        run_cmd=factory.run,
                        popen=factory.popen,
                        wav_validator=lambda path: None,
                        observed_udev=dict(binding.udev.values),
                    )
                    session.start()
                    with open(session.partial_path, "wb") as fh:
                        fh.write(b"partial")
                    if expects_final:
                        self.assertEqual(session.stop(), output)
                    else:
                        with self.assertRaises(capture.CaptureError):
                            session.stop()
                    signals_before_abort = list(proc.signals)

                    session.abort()
                    session.abort()

                    self.assertEqual(proc.signals, signals_before_abort)
                    self.assertEqual(os.path.isfile(output), expects_final)
                    self.assertEqual(
                        os.path.isfile(session.partial_path), not expects_final
                    )

    def test_stop_after_preexited_process_validates_and_promotes_once(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            proc = FakeProc(returncode=0)

            def exited_poll():
                return proc.returncode

            proc.poll = exited_poll
            factory = CaptureFactory(
                "card 1: Capture_1, device 0: Audio [Capture_1]\n", proc=proc
            )
            wav_validator = mock.Mock()
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=wav_validator,
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")

            self.assertEqual(session.stop(), output)
            self.assertEqual(proc.signals, [])
            wav_validator.assert_called_once_with(session.partial_path)

            session.abort()
            self.assertEqual(proc.signals, [])
            wav_validator.assert_called_once_with(session.partial_path)

    def test_short_wav_validator_blocks_promotion(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            factory = CaptureFactory("card 1: Capture_1, device 0: Audio [Capture_1]\n")
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: (_ for _ in ()).throw(
                    ValueError("short WAV")
                ),
                observed_udev=dict(binding.udev.values),
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")
            with self.assertRaises(capture.CaptureError) as ctx:
                session.stop()
            self.assertIn("captured WAV invalid", str(ctx.exception))
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))

    def test_post_stop_identity_drift_keeps_partial_file(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            output = os.path.join(td, "capture.wav")
            factory = CaptureFactory(
                "card 1: Capture_1, device 0: Audio [Capture_1]\n",
                proc=AbortAfterValidationProc(),
            )
            post_stop_validator = mock.Mock(
                side_effect=RuntimeError("capture USB identity changed")
            )
            session = capture.CaptureSession(
                binding,
                output,
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
                post_stop_validator=post_stop_validator,
            )
            session.start()
            with open(session.partial_path, "wb") as fh:
                fh.write(b"partial")
            with self.assertRaises(capture.CaptureError) as ctx:
                session.stop()
            self.assertIn("identity revalidation", str(ctx.exception))
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))
            self.assertEqual(
                session.evidence()["post_stop_validation"]["outcome"], "failed"
            )
            self.assertEqual(factory.proc.signals, [signal.SIGINT])

            session.abort()
            session.abort()

            self.assertEqual(factory.proc.signals, [signal.SIGINT])
            post_stop_validator.assert_called_once_with()
            self.assertTrue(os.path.isfile(session.partial_path))
            self.assertFalse(os.path.exists(output))

    def test_pre_start_identity_drift_blocks_arecord(self):
        with tempfile.TemporaryDirectory() as td:
            _fixture, binding = capture_binding(td)
            factory = CaptureFactory("card 1: Capture_1, device 0: Audio [Capture_1]\n")
            session = capture.CaptureSession(
                binding,
                os.path.join(td, "capture.wav"),
                run_cmd=factory.run,
                popen=factory.popen,
                wav_validator=lambda path: None,
                observed_udev=dict(binding.udev.values),
                pre_start_validator=lambda: (_ for _ in ()).throw(
                    RuntimeError("capture card changed")
                ),
            )
            with self.assertRaises(capture.CaptureError) as ctx:
                session.start()
            self.assertIn("identity validation", str(ctx.exception))
            self.assertFalse(factory.calls)
            self.assertEqual(
                session.evidence()["pre_start_validation"]["outcome"], "failed"
            )

    def test_runner_capture_lifecycle_brackets_source_start_and_terminal(self):
        timeline = []

        class Source:
            def register_prestart_cleanup(self):
                timeline.append("source-prestart-cleanup")

            def configure(self, **kwargs):
                del kwargs
                timeline.append("source-configure")

            def start(self, **kwargs):
                kwargs["before_start_hook"]()
                timeline.append("source-start")
                timeline.append("source-streaming")
                kwargs["streaming_hook"](0)
                timeline.append("source-scored-complete")
                kwargs["scored_complete_hook"](0)
                kwargs["segment_teardown_hook"](0)
                timeline.append("source-terminal")
                return {"terminal": "seen"}

            def query_status(self):
                return {"active": True}

            def validate_active_status(self, status, row, segment):
                del status, row, segment

        class ProbeRunner:
            def _run_fault_window(self, receiver_console, row):
                del receiver_console, row
                raise AssertionError("unexpected fault window")

            def _collect_receiver_active_offload(self, receiver_console, row):
                del receiver_console, row
                timeline.append("receiver-active")
                return {
                    "offload": {"state": "ACTIVE", "success": 1},
                    "offload_settle": None,
                }

            def _collect_receiver_tail(self, receiver_console, row):
                del receiver_console, row
                timeline.append("receiver-tail")
                return {}

            def _step_session_end(self, receiver_console, source_client, row, segment):
                del receiver_console, source_client, row, segment
                timeline.append("receiver-summary")
                return {}

            def _collect_receiver_post_stop(
                self, receiver_console, row, active_offload, recovery
            ):
                del receiver_console, row, active_offload, recovery
                timeline.append("receiver-post-stop")
                return {}

        session = TimelineCapture(
            timeline, os.path.join(tempfile.mkdtemp(), "capture.wav")
        )
        result = runner.Runner._step_run_row(
            ProbeRunner(),
            receiver_console=object(),
            source_client=Source(),
            identity={"address": "AA", "address_type": "random"},
            row=rows.RH2_ROW,
            capture_session=session,
        )
        self.assertLess(timeline.index("capture-start"), timeline.index("source-start"))
        self.assertLess(
            timeline.index("source-streaming"), timeline.index("receiver-active")
        )
        self.assertLess(
            timeline.index("receiver-tail"), timeline.index("receiver-summary")
        )
        self.assertLess(
            timeline.index("receiver-summary"), timeline.index("receiver-post-stop")
        )
        self.assertIn("source-terminal", timeline)
        self.assertEqual(result["source_final_status"], {"terminal": "seen"})

    def test_full_runner_capture_brackets_start_terminal_and_final_idle(self):
        class RecordingWire(hil_fakes.Wire):
            def __init__(self, *args, timeline, **kwargs):
                super().__init__(*args, **kwargs)
                self._timeline = timeline

            def on_write(self, data):
                text = data.decode("utf-8")
                if text.startswith("hil "):
                    command = __import__("json").loads(text[4:])["command"]
                    self._timeline.append("source-" + command)
                super().on_write(data)

        timeline = []
        run_id = "capture-life"
        with tempfile.TemporaryDirectory() as td:
            fixture, binding, fixture_path, binding_path = (
                capture_model_test.load_fixture_binding(td)
            )
            qualification_path, _doc = capture_model_test.make_qualification(
                td, fixture, binding
            )
            out = os.path.join(td, "out")
            os.mkdir(out)
            repo = os.path.join(td, "repo")
            os.mkdir(repo)
            hil_fakes.make_images(repo)
            command = hil_fakes.ScriptedRunner()
            command.script_exit(["lsof", "--", "/dev/ttyACM1"], 1)
            command.script(["fw-flash-hil-source"], hil_fakes.FakeProc(stdout="ok\n"))
            command.script(["fw-flash-54l15"], hil_fakes.FakeProc(stdout="ok\n"))
            transcript = hil_fakes.SourceTranscript(run_id)
            transcript.hello(bond_count=0)
            transcript.idle()
            transcript.unpair()
            transcript.hello(bond_count=0)
            transcript.configure(rows.RH2_ROW)
            transcript.start(scored=rows.RH2_ROW.scored_sdu_count, row=rows.RH2_ROW)
            transcript.final_idle()
            source_chunks, source_writes = transcript.build()
            source_wire = RecordingWire(
                "source",
                chunks=source_chunks,
                assert_writes=source_writes,
                timeline=timeline,
            )
            deps = rh2_test.make_runner_deps(
                rh2_test._receiver_passing_wire(run_id),
                source_wire,
                run_cmd=command,
            )
            deps.repo_root = repo

            def capture_factory(_binding, output_path, _properties):
                return TimelineCapture(timeline, output_path)

            deps.capture_session_factory = capture_factory
            resolved_capture = discovery.CaptureResolution(
                device=binding.roles["capture"].device,
                card_index=1,
                card_id="Capture_1",
                sound_path="/sys/class/sound/card1",
                properties={
                    "ID_VENDOR_ID": "0d8c",
                    "ID_MODEL_ID": "0014",
                    "ID_SERIAL_SHORT": "capture",
                },
            )
            analysis = {
                "schema_version": 1,
                "algorithm_version": 1,
                "outcome": "passed",
                "metrics": {},
            }
            with (
                mock.patch.object(
                    runner.discovery,
                    "resolve_capture_binding",
                    return_value=resolved_capture,
                ),
                mock.patch(
                    "hil.capture_analyzer.analyze_capture", return_value=analysis
                ),
            ):
                outcome, boundary, cleanup = runner.Runner(deps).run(
                    fixture_path,
                    binding_path,
                    out,
                    run_id,
                    os.path.join(out, run_id + ".xml"),
                    argv=["hil-runner.py", "run-ma1-matrix"],
                    status=0,
                    row=rows.RH2_ROW,
                    qualification_path=qualification_path,
                )
            self.assertEqual((outcome, boundary, cleanup), ("passed", None, []))
            self.assertLess(
                timeline.index("capture-start"), timeline.index("source-start")
            )
            self.assertLess(
                max(
                    index
                    for index, item in enumerate(timeline)
                    if item == "source-status"
                ),
                timeline.index("capture-stop"),
            )
            self.assertLess(
                timeline.index("capture-stop"),
                max(
                    index
                    for index, item in enumerate(timeline)
                    if item == "source-idle"
                ),
            )
            run_dir = os.path.join(out, run_id)
            for name in (
                "capture-identity.json",
                "capture-fixture-metadata.json",
                "qualification-accepted.json",
                "capture-analysis.json",
                "capture-session.json",
                "capture-summary.json",
            ):
                self.assertTrue(os.path.isfile(os.path.join(run_dir, name)), name)


class FakeCaptureRows:
    def __init__(self):
        self.calls = []

    def factory(self, _cancel):
        return self

    def run(
        self, fixture_path, binding_path, output_root, run_id, junit_path, **kwargs
    ):
        self.calls.append(kwargs)
        os.makedirs(os.path.join(output_root, run_id))
        for name, content in (
            ("result.json", '{"outcome":"passed"}\n'),
            ("junit.xml", "<testsuite/>\n"),
            ("MANIFEST.md", "manifest\n"),
            ("SHA256SUMS", "sums\n"),
            ("capture-analysis.json", '{"outcome":"passed"}\n'),
            ("capture-session.json", '{"wav_sha256":"synthetic"}\n'),
            ("capture-summary.json", '{"outcome":"passed"}\n'),
            ("capture.wav", "synthetic wav\n"),
        ):
            with open(
                os.path.join(output_root, run_id, name), "w", encoding="utf-8"
            ) as fh:
                fh.write(content)
        with open(junit_path, "w", encoding="utf-8") as fh:
            fh.write("<testsuite/>\n")
        return "passed", None, []


class TestCaptureMatrixAndCli(unittest.TestCase):
    def test_capture_matrix_verdict_requires_full_pass_and_passes_qualification_to_rows(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            fixture, binding, fixture_path, binding_path = (
                capture_model_test.load_fixture_binding(td)
            )
            qualification_path, _doc = capture_model_test.make_qualification(
                td, fixture, binding
            )
            out = os.path.join(td, "out")
            os.mkdir(out)
            fake = FakeCaptureRows()
            engine = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory, environment=lambda _a, _s: {}
                )
            )
            result = engine.run(
                fixture_path,
                binding_path,
                out,
                "ma1",
                os.path.join(out, "ma1.xml"),
                qualification_path=qualification_path,
                capture_verdict="MONO_OUTPUT_SMOKE_ACCEPTED",
            )
            self.assertEqual(result[0], "passed")
            self.assertTrue(fake.calls)
            self.assertTrue(
                all(
                    call["qualification_path"] == qualification_path
                    for call in fake.calls
                )
            )
            import json

            with open(os.path.join(out, "ma1", "result.json"), encoding="utf-8") as fh:
                aggregate = json.load(fh)
            self.assertEqual(aggregate["verdict"], "MONO_OUTPUT_SMOKE_ACCEPTED")

    def test_capture_matrix_rejects_missing_qualification_before_rows(self):
        with tempfile.TemporaryDirectory() as td:
            fixture, _binding, fixture_path, binding_path = (
                capture_model_test.load_fixture_binding(td)
            )
            out = os.path.join(td, "out")
            os.mkdir(out)
            fake = FakeCaptureRows()
            engine = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory, environment=lambda _a, _s: {}
                )
            )
            with self.assertRaises(matrix.MatrixError):
                engine.run(
                    fixture_path,
                    binding_path,
                    out,
                    "ma1",
                    os.path.join(out, "ma1.xml"),
                    capture_verdict="MONO_OUTPUT_SMOKE_ACCEPTED",
                )
            self.assertEqual(fake.calls, [])

    def test_capture_matrix_rejects_pass_without_capture_evidence(self):
        class IncompleteRows(FakeCaptureRows):
            def run(
                self,
                fixture_path,
                binding_path,
                output_root,
                run_id,
                junit_path,
                **kwargs,
            ):
                self.calls.append(kwargs)
                os.makedirs(os.path.join(output_root, run_id))
                for name, content in (
                    ("result.json", '{"outcome":"passed"}\n'),
                    ("junit.xml", "<testsuite/>\n"),
                    ("MANIFEST.md", "manifest\n"),
                    ("SHA256SUMS", "sums\n"),
                ):
                    with open(
                        os.path.join(output_root, run_id, name), "w", encoding="utf-8"
                    ) as fh:
                        fh.write(content)
                with open(junit_path, "w", encoding="utf-8") as fh:
                    fh.write("<testsuite/>\n")
                return "passed", None, []

        with tempfile.TemporaryDirectory() as td:
            fixture, binding, fixture_path, binding_path = (
                capture_model_test.load_fixture_binding(td)
            )
            qualification_path, _doc = capture_model_test.make_qualification(
                td, fixture, binding
            )
            out = os.path.join(td, "out")
            os.mkdir(out)
            fake = IncompleteRows()
            engine = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory, environment=lambda _a, _s: {}
                )
            )
            result = engine.run(
                fixture_path,
                binding_path,
                out,
                "ma1",
                os.path.join(out, "ma1.xml"),
                qualification_path=qualification_path,
                capture_verdict="MONO_OUTPUT_SMOKE_ACCEPTED",
            )
            self.assertEqual(result[0], "failed")

    def test_cli_commands_are_fixed_and_capability_gated(self):
        parser = cli.build_parser()
        args = parser.parse_args(
            [
                "run-ma1-matrix",
                "--fixture",
                "f",
                "--binding",
                "b",
                "--qualification",
                "q",
                "--output-root",
                "/tmp/out",
                "--run-id",
                "ma1",
                "--junit",
                "/tmp/out/ma1.xml",
            ]
        )
        self.assertIs(args.func, cli.cmd_run_ma1_matrix)
        with self.assertRaises(cli.HilCliError):
            parser.parse_args(
                [
                    "run-ma1-matrix",
                    "--fixture",
                    "f",
                    "--binding",
                    "b",
                    "--qualification",
                    "q",
                    "--output-root",
                    "/tmp/out",
                    "--run-id",
                    "ma1",
                    "--junit",
                    "/tmp/out/ma1.xml",
                    "--row",
                    rows.RH2_ROW.name,
                ]
            )

    def test_none_matrix_result_has_no_capture_verdict_field(self):
        with tempfile.TemporaryDirectory() as td:
            fixture_path = os.path.join(REPO, "tests", "hil", "fixture.json")
            binding_path = os.path.join(
                REPO, "tests", "hil", "fixture.local.example.json"
            )
            out = os.path.join(td, "out")
            os.mkdir(out)
            fake = FakeCaptureRows()
            engine = matrix.MatrixCoordinator(
                matrix.MatrixDeps(
                    row_runner_factory=fake.factory, environment=lambda _a, _s: {}
                )
            )
            result = engine.run(
                fixture_path,
                binding_path,
                out,
                "rh3",
                os.path.join(out, "rh3.xml"),
            )
            self.assertEqual(result[0], "passed")
            import json

            with open(os.path.join(out, "rh3", "result.json"), encoding="utf-8") as fh:
                aggregate = json.load(fh)
            self.assertNotIn("verdict", aggregate)


if __name__ == "__main__":
    unittest.main(verbosity=2)
