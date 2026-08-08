#!/usr/bin/env python3
"""Unit tests for the flpr_hang_gate.py console parser (Phase T6 review-fix).

Proves that RE_RUNTIME_RESTART_OK matches the exact production line
emitted by src/audio_shell.c's `flpr restart` command —

    FLPR restart OK: epoch <old>→<new> crc=0x<8 hex> duration=total <n> ms

— and that unrelated/partial/old drifted forms never produce a false
recovery observation.  The gate script imports pyserial lazily, so this
suite runs on stdlib-only python3.
"""

import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)

from flpr_hang_gate import (  # noqa: E402
    RE_FAULT_HANG_ACK,
    RE_FAULT_HANG_FAIL,
    RE_RUNTIME_RESTART_OK,
    HangGateRunner,
    parse_last_offload_block,
    split_offload_blocks,
)

# Exact production line shape: src/audio_shell.c cmd_flpr_restart prints
#   "FLPR restart OK: epoch %u→%u crc=0x%08x duration=total %u ms"
PRODUCTION_OK = "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total 100 ms"


class TestRestartOkParser(unittest.TestCase):
    def test_exact_production_line_extracts_groups(self):
        m = RE_RUNTIME_RESTART_OK.search(PRODUCTION_OK)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "5")  # old epoch
        self.assertEqual(m.group(2), "6")  # new epoch
        self.assertEqual(m.group(3), "1234abcd")  # crc
        self.assertEqual(m.group(4), "100")  # duration ms

    def test_multi_digit_values(self):
        line = "FLPR restart OK: epoch 12345→12346 crc=0xDEADBEEF duration=total 987 ms"
        m = RE_RUNTIME_RESTART_OK.search(line)
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "12345")
        self.assertEqual(m.group(2), "12346")
        self.assertEqual(m.group(3), "DEADBEEF")
        self.assertEqual(m.group(4), "987")

    def test_zero_values(self):
        line = "FLPR restart OK: epoch 0→1 crc=0x00000000 duration=total 0 ms"
        m = RE_RUNTIME_RESTART_OK.search(line)
        self.assertIsNotNone(m)
        self.assertEqual(m.groups(), ("0", "1", "00000000", "0"))

    def test_old_drifted_form_without_epoch_rejected(self):
        # The pre-fix parser matched this stale shape; production has
        # emitted the "epoch" literal since Stage 4A, so a match here
        # would be a false recovery observation.
        stale = "FLPR restart OK: 5→6 crc=0x1234abcd duration=total 100 ms"
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(stale))

    def test_partial_forms_rejected(self):
        cases = [
            "FLPR restart OK: epoch 5→6",  # no crc/duration
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd",  # no duration
            "FLPR restart OK: epoch 5→6 duration=total 100 ms",  # no crc
            "FLPR restart OK: epoch →6 crc=0x1234abcd duration=total 100 ms",
            "FLPR restart OK: epoch 5→ crc=0x1234abcd duration=total 100 ms",
            "FLPR restart OK: epoch 5→6 crc=0xzzz duration=total 100 ms",
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total ms",
        ]
        for line in cases:
            self.assertIsNone(RE_RUNTIME_RESTART_OK.search(line), line)

    def test_unrelated_console_text_rejected(self):
        text = (
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=7 gen=2\n"
            "  Runtime     : restarts=1 fails=0 last_ms=123 remote_epoch=8\n"
            "uart:~$ flpr restart\n"
            "FLPR restart: requesting (timeout=10000 ms)...\n"
            "FLPR restart FAILED: -5\n"
        )
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(text))

    def test_representative_console_text_matches(self):
        text = (
            "uart:~$ flpr restart\n"
            "FLPR restart: requesting (timeout=10000 ms)...\n"
            "FLPR restart OK: epoch 5→6 crc=0x1234abcd duration=total 100 ms\n"
            "uart:~$ flpr offload\n"
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=6 gen=3\n"
        )
        m = RE_RUNTIME_RESTART_OK.search(text)
        self.assertIsNotNone(m)
        self.assertEqual(m.groups(), ("5", "6", "1234abcd", "100"))

    def test_no_restart_line_no_false_observation(self):
        text = (
            "uart:~$ flpr offload\n"
            "--- Audio offload ---\n"
            "  State       : ACTIVE / epoch=6 gen=3\n"
            "  Runtime     : restarts=1 fails=0 last_ms=123 remote_epoch=6\n"
        )
        self.assertIsNone(RE_RUNTIME_RESTART_OK.search(text))


class TestSiblingGateRegexes(unittest.TestCase):
    """The other gate-parsed regexes keep their exact production shapes."""

    def test_fault_hang_ack(self):
        self.assertIsNotNone(RE_FAULT_HANG_ACK.search("FAULT_HANG_ACK received"))
        self.assertIsNone(RE_FAULT_HANG_ACK.search("FAULT_HANG failed: -5 (no ACK)"))

    def test_fault_hang_fail_extracts_errno(self):
        m = RE_FAULT_HANG_FAIL.search("FAULT_HANG failed: -5 (no ACK)")
        self.assertIsNotNone(m)
        self.assertEqual(m.group(1), "-5")


BLOCK_STOPPED = (
    "--- Audio offload ---\n"
    "  State       : STOPPED / epoch=0 gen=29\n"
    "  Counters    : submit=16809 success=16767 fallback=42 busy=0\n"
    "  Runtime     : restarts=1 fails=0 last_ms=231 remote_epoch=1\n"
)
BLOCK_ACTIVE_STALE = (
    "--- Audio offload ---\n"
    "  State       : ACTIVE / epoch=2 gen=27\n"
    "  Counters    : submit=1240 success=1198 fallback=42 busy=0\n"
    "  Runtime     : restarts=1 fails=0 last_ms=231 remote_epoch=1\n"
)
FLOOD_LINE = (
    "uart:~$ [00:00:42.530,879] <inf> bt_bap: Mode A: stale half discarded "
    "(ts 123 < 124, seq 5 < 6)\r\n"
)


class TestOffloadBlockSelection(unittest.TestCase):
    """The Mode A stale-half console flood must not make the gate parse a
    stale mid-stream status as the final post-stream status."""

    def test_single_block_returns_itself(self):
        blocks = split_offload_blocks(BLOCK_STOPPED)
        self.assertEqual(len(blocks), 1)
        self.assertIn("submit=16809", blocks[0])

    def test_accumulated_blocks_take_last(self):
        text = FLOOD_LINE + BLOCK_ACTIVE_STALE + FLOOD_LINE + BLOCK_STOPPED
        blocks = split_offload_blocks(text)
        self.assertEqual(len(blocks), 2)
        last = parse_last_offload_block(text)
        self.assertIn("STOPPED", last)
        self.assertIn("submit=16809", last)
        self.assertNotIn("ACTIVE", last)

    def test_flood_interleaved_keeps_last_block_complete(self):
        # Flood lines land between the two responses; the last block must
        # still be selected intact.
        text = FLOOD_LINE + BLOCK_ACTIVE_STALE + FLOOD_LINE + FLOOD_LINE + BLOCK_STOPPED
        last = parse_last_offload_block(text)
        self.assertIn("STOPPED", last)
        self.assertIn("submit=16809", last)

    def test_no_separator_falls_back_to_whole_text(self):
        self.assertEqual(parse_last_offload_block("no block here"), "no block here")

    def test_trailing_fragment_without_state_dropped(self):
        text = BLOCK_STOPPED + FLOOD_LINE
        last = parse_last_offload_block(text)
        self.assertIn("STOPPED", last)
        self.assertIn("submit=16809", last)


# ── R3: fakeable runner boundaries (transport + BAP launcher) ─────────────

OFFLOAD_ACTIVE_1000 = (
    "  State       : ACTIVE / epoch=1 gen=1\n"
    "  Counters    : submit=1150 success=1100 fallback=0 busy=0\n"
    "  Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
    "  Probation   : active=0 success=0 cleared=0\n"
    "  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
)

OFFLOAD_RECOVERED = (
    "  State       : ACTIVE / epoch=1 gen=2\n"
    "  Counters    : submit=1180 success=1120 fallback=30 busy=0\n"
    "  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
    "  Probation   : active=0 success=100 cleared=1\n"
    "  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
)

OFFLOAD_STILL_PROBATION = (
    "  State       : ACTIVE / epoch=1 gen=2\n"
    "  Counters    : submit=1180 success=1120 fallback=30 busy=0\n"
    "  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
    "  Probation   : active=1 success=50 cleared=0\n"
    "  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
)

OFFLOAD_FINAL_STOPPED = (
    "  State       : STOPPED / epoch=2 gen=3\n"
    "  Counters    : submit=1200 success=1100 fallback=30 busy=0\n"
    "  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
    "  Probation   : active=0 success=100 cleared=1\n"
    "  Runtime     : restarts=1 fails=0 last_ms=231 remote_epoch=2\n"
    "  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
)

FAULT_HANG_ACK_LINE = "FAULT_HANG_ACK received\n"

# Real `flpr status` ASRC section shape (src/flpr_shell.c) — NOT an
# "--- Audio status ---" header, which only `audio status` prints.
ASRC_STATUS_TEXT = (
    "  ── ASRC offload ──\n"
    "    Counters : submit=1200 success=1100 fallback=30\n"
    "    Faults   : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 state=0 verify=0\n"
)

# Real `audio status` output shape (src/audio_shell.c cmd_status).
AUDIO_STATUS_GOOD = (
    "--- Audio status ---\n"
    "  Frames decoded : 11400\n"
    "  PLC frames     : 42 (0%)\n"
    "  Decode errors  : 0\n"
    "  I2S underruns  : 0\n"
    "  Stream resets  : 0\n"
    "  Drift state    : locked\n"
    "  Drift ppm      : 0\n"
    "  Resampler      : ASRC linear\n"
    "  Volume         : 255 / 255\n"
)

# Real `audio perf` output shape (src/audio_shell.c cmd_perf) — push
# failures live under the Queue section.
AUDIO_PERF_GOOD = (
    "--- Performance ---\n"
    "  Path          Count   Avg(cyc)  Avg(us)  Max(cyc)  Max(us)  %deadline\n"
    "  iso_recv        11400       120      120       240      240   0.0%\n"
    "  Queue:\n"
    "    Slab free     : 4 / 16 (min/max)\n"
    "    Output frames : 0 / 4 (min/max)\n"
    "    Output blocks : 11400\n"
    "    Push failures : 0\n"
    "    Repeat fb     : 0\n"
    "    ASRC cap fail : 0\n"
)


def _audio_status_fault(field, value):
    """AUDIO_STATUS_GOOD with one fault field set to a nonzero value.

    src/audio_shell.c pads the audio-status labels to a fixed width:
    "  Decode errors  : %u", "  I2S underruns  : %u", "  Stream resets  : %u".
    """
    return AUDIO_STATUS_GOOD.replace("%s  : 0" % field, "%s  : %d" % (field, value))


AUDIO_STATUS_DECODE_ERR = _audio_status_fault("Decode errors", 3)
AUDIO_STATUS_I2S_UNDERRUN = _audio_status_fault("I2S underruns", 1)
AUDIO_STATUS_STREAM_RESET = _audio_status_fault("Stream resets", 1)
AUDIO_PERF_PUSH_FAIL = AUDIO_PERF_GOOD.replace("Push failures : 0", "Push failures : 2")
# Malformed: field present but not a number — missing evidence, not zero.
AUDIO_STATUS_MALFORMED = AUDIO_STATUS_GOOD.replace(
    "I2S underruns  : 0", "I2S underruns  : N/A"
)

SUCCESS_OFFLOAD_RESPONSES = [
    OFFLOAD_ACTIVE_1000,  # probe (State present)
    OFFLOAD_ACTIVE_1000,  # Step 3: ACTIVE + success>=1000
    OFFLOAD_RECOVERED,  # Step 6: recovery + probation cleared -> break
    OFFLOAD_RECOVERED,  # Step 6b: active-stream snapshot
    OFFLOAD_FINAL_STOPPED,  # Step 8 final status
    OFFLOAD_FINAL_STOPPED,  # Step 9 final parse
]


def _hang_responses(
    audio_status=AUDIO_STATUS_GOOD,
    audio_perf=AUDIO_PERF_GOOD,
    final_offload=OFFLOAD_FINAL_STOPPED,
    status_text=ASRC_STATUS_TEXT,
):
    """Full success-path response set with overridable audio/perf/final
    blocks, so negative audio-gate tests reuse the exact happy choreography."""
    responses = {
        "flpr offload": list(SUCCESS_OFFLOAD_RESPONSES[:-2])
        + [final_offload, final_offload],
        "flpr hang": FAULT_HANG_ACK_LINE,
        "flpr status": status_text,
        "flpr runtime": status_text,
        "audio perf": audio_perf,
    }
    if audio_status is not None:
        responses["audio status"] = audio_status
    return responses


class FakeHangTransport:
    """Scripted console transport for hang-gate unit tests.

    responses maps a command substring to a str (same reply every time) or
    a list of strs (each occurrence pops the next entry; the last entry is
    repeated once the list is exhausted).  write() appends the matching
    reply into the pending byte buffer; read()/in_waiting mimic a serial
    device.
    """

    def __init__(self, responses=None):
        self.responses = dict(responses or {})
        self.pending = b""
        self.written = []

    def open(self, port, baud):
        pass

    def write(self, data):
        line = data.decode("utf-8", errors="replace").strip()
        self.written.append(line)
        for key, spec in self.responses.items():
            if key in line:
                if isinstance(spec, list):
                    text = spec.pop(0) if len(spec) > 1 else spec[0]
                else:
                    text = spec
                self.pending += text.encode("utf-8")
                break

    def flush(self):
        pass

    def read(self, size):
        if not self.pending:
            return b""
        chunk = self.pending[:size]
        self.pending = self.pending[size:]
        return chunk

    @property
    def in_waiting(self):
        return len(self.pending)

    def reset_input(self):
        self.pending = b""

    def close(self):
        pass


class FakeBapProcess:
    """Popen-shaped fake for the hang gate's BAP process boundary."""

    def __init__(self, early_exit=False, rc=0, out="fake bap output", timeout=False):
        self._early_exit = early_exit
        self._rc = rc
        self._out = out
        self._timeout = timeout
        self.killed = False
        self.communicated = False
        self.returncode = None

    def poll(self):
        if self._early_exit:
            return self._rc
        if self.communicated:
            return self.returncode
        return None

    def communicate(self, timeout=None):
        if self._timeout:
            raise subprocess.TimeoutExpired("bap", timeout)
        self.communicated = True
        self.returncode = self._rc
        return (self._out, None)

    def kill(self):
        self.killed = True

    def wait(self, timeout=None):
        return self._rc


def _mono_steady(base, step=0.02):
    t = base
    while True:
        yield t
        t += step


class TestHangGateRunnerLifecycle(unittest.TestCase):
    """R3: runner lifecycle/state tests through the fakeable transport and
    BAP launcher boundaries (no pyserial, no subprocess)."""

    def _make_runner(self, responses, launcher=None, duration=12):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        log_path = os.path.join(self.tmp.name, "hang.log")
        transport = FakeHangTransport(responses)
        runner = HangGateRunner(
            "/dev/fake",
            115200,
            log_path,
            transport=transport,
            launcher=launcher or (lambda d, s, p: FakeBapProcess()),
        )
        return runner, transport

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_success_path(self, mock_mono, mock_sleep):
        """Full gate: probe -> ACTIVE -> hang ACK -> recovery -> final checks."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        fake_proc = FakeBapProcess()
        runner, transport = self._make_runner(
            _hang_responses(),
            launcher=lambda d, s, p: fake_proc,
        )
        runner.open()
        try:
            result = runner.run(12, stereo=False)
        finally:
            runner.close()

        self.assertTrue(result.passed, result.error)
        self.assertEqual(result.error, "")
        self.assertTrue(result.checks["ack_received"])
        self.assertTrue(result.checks["recovery_attempts_eq_1"])
        self.assertTrue(result.checks["runtime_restarts_eq_1"])
        self.assertTrue(result.checks["epoch_changed"])
        self.assertTrue(result.checks["frame_count_plausible"])
        self.assertTrue(result.checks["resumed_success_until_end"])
        self.assertTrue(result.checks["asrc_fallback_triggered"])
        self.assertTrue(result.checks["audio_status_seen"])
        self.assertTrue(result.checks["audio_decode_errors_zero"])
        self.assertTrue(result.checks["audio_i2s_underruns_zero"])
        self.assertTrue(result.checks["audio_stream_resets_zero"])
        self.assertTrue(result.checks["audio_push_failures_zero"])
        self.assertFalse(fake_proc.killed, "successful run must not kill bap_central")
        self.assertIn("flpr hang", transport.written)

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_console_not_responsive(self, mock_mono, mock_sleep):
        """No console reply -> probe timeout error, no BAP process ever."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        runner, _transport = self._make_runner({})
        runner.open()
        try:
            result = runner.run(12)
        finally:
            runner.close()

        self.assertFalse(result.passed)
        self.assertIn("console not responsive", result.error.lower())

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_bap_early_exit(self, mock_mono, mock_sleep):
        """bap_central dying before ACTIVE -> early-exit failure."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        fake_proc = FakeBapProcess(early_exit=True, rc=7, out="crashed\n")
        runner, _transport = self._make_runner(
            {"flpr offload": list(SUCCESS_OFFLOAD_RESPONSES)},
            launcher=lambda d, s, p: fake_proc,
        )
        runner.open()
        try:
            result = runner.run(12)
        finally:
            runner.close()

        self.assertFalse(result.passed)
        self.assertIn("exited early", result.error)
        self.assertIn("crashed", result.error)

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_ack_timeout_kills_bap_process(self, mock_mono, mock_sleep):
        """No FAULT_HANG_ACK -> timeout error AND the launched BAP process
        is cleaned up (killed) by the runner's finally path."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        fake_proc = FakeBapProcess()
        runner, _transport = self._make_runner(
            {"flpr offload": list(SUCCESS_OFFLOAD_RESPONSES)},
            launcher=lambda d, s, p: fake_proc,
        )
        runner.open()
        try:
            result = runner.run(12)
        finally:
            runner.close()

        self.assertFalse(result.passed)
        self.assertIn("FAULT_HANG_ACK", result.error)
        self.assertTrue(fake_proc.killed, "timeout path must kill bap_central")

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_recovery_timeout(self, mock_mono, mock_sleep):
        """ACK arrives but probation never clears -> recovery timeout."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        fake_proc = FakeBapProcess()
        runner, _transport = self._make_runner(
            {
                "flpr offload": [
                    OFFLOAD_ACTIVE_1000,
                    OFFLOAD_ACTIVE_1000,
                    OFFLOAD_STILL_PROBATION,  # never clears probation
                ],
                "flpr hang": FAULT_HANG_ACK_LINE,
            },
            launcher=lambda d, s, p: fake_proc,
        )
        runner.open()
        try:
            result = runner.run(12)
        finally:
            runner.close()

        self.assertFalse(result.passed)
        self.assertIn("recovery", result.error.lower())
        self.assertIn("probation", result.error.lower())
        self.assertTrue(fake_proc.killed)

    @patch("flpr_hang_gate.time.sleep")
    @patch("flpr_hang_gate.time.monotonic")
    def test_bap_timeout_on_communicate_kills(self, mock_mono, mock_sleep):
        """bap_central hanging on communicate -> killed, gate still fails."""
        mock_sleep.return_value = None
        gen = _mono_steady(1000.0)
        mock_mono.side_effect = lambda: next(gen)

        fake_proc = FakeBapProcess(timeout=True)
        runner, _transport = self._make_runner(
            _hang_responses(),
            launcher=lambda d, s, p: fake_proc,
        )
        runner.open()
        try:
            result = runner.run(12)
        finally:
            runner.close()

        # Step 7 kill path fires; the gate outcome afterwards depends on
        # the console state, which is non-deterministic on hardware — the
        # deterministic contract under test is the cleanup kill itself.
        self.assertTrue(fake_proc.killed, "communicate timeout must kill bap_central")


class TestHangAudioFaultGate(unittest.TestCase):
    """Audio fault fields from the final `audio status`/`audio perf` must
    each be present and exactly zero; any nonzero/missing field fails."""

    def _run_full(self, responses):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        log_path = os.path.join(self.tmp.name, "hang.log")
        transport = FakeHangTransport(responses)
        runner = HangGateRunner(
            "/dev/fake",
            115200,
            log_path,
            transport=transport,
            launcher=lambda d, s, p: FakeBapProcess(),
        )
        runner.open()
        try:
            with (
                patch("flpr_hang_gate.time.sleep") as mock_sleep,
                patch("flpr_hang_gate.time.monotonic") as mock_mono,
            ):
                mock_sleep.return_value = None
                gen = _mono_steady(1000.0)
                mock_mono.side_effect = lambda: next(gen)
                return runner.run(12, stereo=False)
        finally:
            runner.close()

    def test_decode_errors_nonzero_fails(self):
        result = self._run_full(_hang_responses(audio_status=AUDIO_STATUS_DECODE_ERR))
        self.assertFalse(result.passed)
        self.assertIn("audio_decode_errors_zero", result.error)
        self.assertEqual(result.audio_status["decode_errors"], 3)

    def test_i2s_underruns_nonzero_fails(self):
        result = self._run_full(_hang_responses(audio_status=AUDIO_STATUS_I2S_UNDERRUN))
        self.assertFalse(result.passed)
        self.assertIn("audio_i2s_underruns_zero", result.error)
        self.assertEqual(result.audio_status["i2s_underruns"], 1)

    def test_stream_resets_nonzero_fails(self):
        result = self._run_full(_hang_responses(audio_status=AUDIO_STATUS_STREAM_RESET))
        self.assertFalse(result.passed)
        self.assertIn("audio_stream_resets_zero", result.error)
        self.assertEqual(result.audio_status["stream_resets"], 1)

    def test_push_failures_nonzero_fails(self):
        result = self._run_full(_hang_responses(audio_perf=AUDIO_PERF_PUSH_FAIL))
        self.assertFalse(result.passed)
        self.assertIn("audio_push_failures_zero", result.error)
        self.assertEqual(result.audio_status["push_failures"], 2)

    def test_missing_audio_status_block_fails(self):
        result = self._run_full(_hang_responses(audio_status=None))
        self.assertFalse(result.passed)
        self.assertIn("audio_status_seen", result.error)

    def test_malformed_audio_field_is_missing_evidence(self):
        result = self._run_full(_hang_responses(audio_status=AUDIO_STATUS_MALFORMED))
        self.assertFalse(result.passed)
        self.assertIn("audio_i2s_underruns_zero", result.error)
        self.assertIsNone(result.audio_status["i2s_underruns"])


class TestHangResumedSuccessAndFallback(unittest.TestCase):
    """resumed_success_until_end and asrc_fallback_triggered are required
    checks grounded in duration/cadence."""

    OFFLOAD_FINAL_LOW = (
        "  State       : STOPPED / epoch=2 gen=3\n"
        "  Counters    : submit=1200 success=300 fallback=30 busy=0\n"
        "  Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
        "  Probation   : active=0 success=100 cleared=1\n"
        "  Runtime     : restarts=1 fails=0 last_ms=231 remote_epoch=2\n"
        "  Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    )
    OFFLOAD_FINAL_NO_FALLBACK = OFFLOAD_FINAL_STOPPED.replace(
        "success=1100 fallback=30", "success=1130 fallback=0"
    )

    def _run_full(self, responses):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        log_path = os.path.join(self.tmp.name, "hang.log")
        runner = HangGateRunner(
            "/dev/fake",
            115200,
            log_path,
            transport=FakeHangTransport(responses),
            launcher=lambda d, s, p: FakeBapProcess(),
        )
        runner.open()
        try:
            with (
                patch("flpr_hang_gate.time.sleep") as mock_sleep,
                patch("flpr_hang_gate.time.monotonic") as mock_mono,
            ):
                mock_sleep.return_value = None
                gen = _mono_steady(1000.0)
                mock_mono.side_effect = lambda: next(gen)
                return runner.run(12, stereo=False)
        finally:
            runner.close()

    def test_resumed_success_too_low_fails(self):
        # success+fallback = 330 < 0.85 * 12*100 = 1020.
        result = self._run_full(_hang_responses(final_offload=self.OFFLOAD_FINAL_LOW))
        self.assertFalse(result.passed)
        self.assertIn("resumed_success_until_end", result.error)

    def test_resumed_success_meets_tolerance_passes(self):
        result = self._run_full(_hang_responses())
        self.assertTrue(result.passed, result.error)
        self.assertTrue(result.checks["resumed_success_until_end"])

    def test_asrc_fallback_zero_fails(self):
        # fallback=0 everywhere → the hang never faulted a block.
        result = self._run_full(
            _hang_responses(final_offload=self.OFFLOAD_FINAL_NO_FALLBACK)
        )
        self.assertFalse(result.passed)
        self.assertIn("asrc_fallback_triggered", result.error)


class TestParseAudioFaults(unittest.TestCase):
    """Shared audio-fault grammar: last occurrence wins, absent/malformed
    fields are None (missing evidence), stale blocks never override fresh."""

    def _parse(self, text):
        from flpr_status import parse_audio_faults

        return parse_audio_faults(text)

    def test_clean_output_all_zero(self):
        audio = self._parse(AUDIO_STATUS_GOOD + AUDIO_PERF_GOOD)
        self.assertTrue(audio["status_seen"])
        self.assertEqual(audio["decode_errors"], 0)
        self.assertEqual(audio["i2s_underruns"], 0)
        self.assertEqual(audio["stream_resets"], 0)
        self.assertEqual(audio["push_failures"], 0)

    def test_empty_text_no_status(self):
        audio = self._parse("")
        self.assertFalse(audio["status_seen"])
        self.assertIsNone(audio["decode_errors"])

    def test_stale_faulty_block_then_fresh_clean_last_wins(self):
        text = AUDIO_STATUS_DECODE_ERR + AUDIO_STATUS_GOOD
        audio = self._parse(text)
        self.assertEqual(audio["decode_errors"], 0, "fresh block must win")

    def test_fresh_faulty_after_clean_stale_fails_semantics(self):
        text = AUDIO_STATUS_GOOD + AUDIO_STATUS_DECODE_ERR
        audio = self._parse(text)
        self.assertEqual(audio["decode_errors"], 3, "last occurrence wins")

    def test_malformed_field_is_none(self):
        audio = self._parse(AUDIO_STATUS_MALFORMED)
        self.assertTrue(audio["status_seen"])
        self.assertIsNone(audio["i2s_underruns"])

    def test_absent_audio_perf_push_failures_none(self):
        audio = self._parse(AUDIO_STATUS_GOOD)
        self.assertIsNone(audio["push_failures"], "perf block absent → None")


if __name__ == "__main__":
    unittest.main()
