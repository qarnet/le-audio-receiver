#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p "python3Packages.pyserial"
"""
flpr_hang_gate.py — Automated FLPR FAULT_HANG gate for nRF54L15.

Injects `flpr hang` via console shell, monitors full recovery
chain (heartbeat→RECOVERING→runtime restart→ACTIVE→probation cleared).

Gates checked (all must pass):
  - FAULT_HANG_ACK received
  - Exactly ONE recovery since baseline (recovery_attempts − baseline == 1;
    relapses and duplicate restarts rejected)
  - Exactly ONE runtime restart since baseline (runtime_restarts − baseline == 1)
  - New remote+ring epoch (epoch changes)
  - Probation cleared >=1
  - Resumed success until end: final success+fallback >= 85% of duration*100
    (explicit tolerance grounded in the 100 fps cadence — the 15% slack
    covers the pre-injection threshold, recovery downtime, and startup
    variance)
  - ASRC fallback triggered (fallback > 0 — the hang actually faulted)
  - Faults: verify=0, crc=0, seq=0, frame=0, state=0 (ring faults from the
    offload status block; verify/state from the ASRC section)
  - Audio faults zero from final `audio status`/`audio perf`: decode
    errors, I2S underruns, stream resets, push failures — each field must
    be present in the captured output and exactly zero
  - No exhaustion (exhaustion==0)

Usage:
  python3 scripts/flpr_hang_gate.py --duration 180
  python3 scripts/flpr_hang_gate.py --duration 180 --stereo
  python3 scripts/flpr_hang_gate.py --duration 300
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from abc import ABC, abstractmethod
from datetime import datetime

# Shared offload-status grammar (flpr_status.py is the single source of
# truth for the console status block; both hardware gates consume it).
from flpr_status import (  # noqa: E402
    RE_COUNTERS,
    RE_FAULTS,
    RE_HB_DEDUP,
    RE_PROBATION,
    RE_RECOVERY,
    RE_RECOVERY_OK,
    RE_RUNTIME,
    RE_STATE_LINE,
    parse_audio_faults,
    parse_offload_status,
)

# Resumed-success gate: final success+fallback must reach this fraction of
# duration*100 (100 fps cadence).  The 15% slack covers the pre-injection
# threshold (~10 s), recovery downtime, and startup/teardown variance.
RESUMED_SUCCESS_TOLERANCE_FRACTION = 0.85


# ── Command-specific regexes (hang gate owns these) ─────────────────────

RE_RUNTIME_RESTART_OK = re.compile(
    r"FLPR restart OK: epoch\s+(\d+)→(\d+)\s+crc=0x([0-9a-fA-F]+)\s+duration=total\s+(\d+)\s+ms"
)
RE_FAULT_HANG_ACK = re.compile(r"FAULT_HANG_ACK received")
RE_FAULT_HANG_FAIL = re.compile(r"FAULT_HANG failed:\s*(-?\d+)\s+\(no ACK\)")
RE_OFFLOAD_RECOVERING = re.compile(r"State\s*:\s*RECOVERING")


def split_offload_blocks(text):
    """Split accumulated console text into individual '--- Audio offload ---'
    response blocks (the leading fragment before the first separator is
    discarded; a trailing fragment is kept only when it carries a State
    line, i.e. it is a complete response)."""
    parts = re.split(r"--- Audio offload ---", text)
    blocks = []
    for part in parts[1:]:
        if RE_STATE_LINE.search(part):
            blocks.append(part)
    return blocks


def parse_last_offload_block(text):
    """Return the LAST complete '--- Audio offload ---' block in text.

    During a Mode A stream the receiver floods the console with 'Mode A:
    stale half discarded' INF lines (RTN retransmission duplicates).  The
    flood can delay the final status response and leave several earlier
    'flpr offload' responses accumulated in the read buffer; parsing the
    first match then yields a stale mid-stream snapshot.  Taking the last
    complete block returns the newest state.  Falls back to the whole
    text when no block separator is present."""
    blocks = split_offload_blocks(text)
    return blocks[-1] if blocks else text


# ASRC stats (shadow-verify gate)
RE_ASRC_COUNTERS = re.compile(
    r"Counters\s*:\s*submit=(\d+)\s+success=(\d+)\s+fallback=(\d+)"
)
RE_ASRC_FAULTS = re.compile(
    r"Faults\s*:\s*timeout=(\d+)\s+full=(\d+)\s+stale=(\d+)\s+seq=(\d+)\s+frame=(\d+)\s+crc=(\d+)\s+"
    r"state=(\d+)\s+verify=(\d+)"
)

# Audio status faults
RE_AUDIO_FAULTS = re.compile(
    r"I2S underruns\s*=\s*(\d+)|decode errors\s*=\s*(\d+)|push fails\s*=\s*(\d+)"
)
RE_AUDIO_RATE = re.compile(r"rate\s*=\s*(\d+)\s*fps")

# Heartbeat dedup
RE_HB_DEDUP = re.compile(r"HB dedup\s*:\s*(\d+)")

# New epoch from offload recovery or restart
RE_NEW_EPOCH = re.compile(r"FLPR READY.*epoch=(\d+).*new")


# ── Gate result ────────────────────────────────────────────────────────


class GateResult:
    def __init__(self):
        self.passed = False
        self.error = ""
        self.mode = ""
        self.duration_s = 0
        self.injection_time = 0.0
        self.ack_time = 0.0
        self.first_fallback_time = 0.0
        self.recovery_time = 0.0
        self.baseline_epoch = 0
        self.baseline_success = 0
        self.baseline_recovery_attempts = 0
        self.baseline_runtime_restarts = 0
        self.baseline_runtime_fails = 0
        self.baseline_relapses = 0
        self.baseline_exhaustion = 0
        self.baseline_probation_cleared = 0
        self.final_status = {}
        self.active_status = {}
        self.active_audio_text = ""
        self.asrc_status = {}
        self.audio_status = {}
        self.checks = {}
        self.full_log = ""


# ── Console transport interface (fakeable boundary) ─────────────────────


class HangTransport(ABC):
    """Serial console boundary for the hang gate.

    The production backend is RealHangSerial (lazy pyserial); unit tests
    inject a scripted fake, exactly like the stall gate's Transport.
    """

    @abstractmethod
    def open(self, port, baud): ...
    @abstractmethod
    def write(self, data): ...
    @abstractmethod
    def flush(self): ...
    @abstractmethod
    def read(self, size) -> bytes: ...
    @property
    @abstractmethod
    def in_waiting(self) -> int: ...
    @abstractmethod
    def reset_input(self): ...
    @abstractmethod
    def close(self): ...


class RealHangSerial(HangTransport):
    """pyserial backend — imported lazily so the module stays importable
    (and unit-testable) without pyserial."""

    def __init__(self):
        self._ser = None

    def open(self, port, baud):
        import serial

        self._ser = serial.Serial(port, baud, timeout=0.05)

    def write(self, data):
        self._ser.write(data)

    def flush(self):
        self._ser.flush()

    def read(self, size) -> bytes:
        return self._ser.read(size)

    @property
    def in_waiting(self) -> int:
        return self._ser.in_waiting

    def reset_input(self):
        self._ser.reset_input_buffer()

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None


# ── BAP process launcher (fakeable boundary) ─────────────────────────────


def launch_bap_central(duration_s, stereo, peer_addr):
    """Production launcher: Popen bap_central.py with the same argv the
    pre-R3 gate built inline.  Returns a Popen with poll/communicate/
    kill/wait/returncode."""
    repo_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    bap_script = os.path.join(repo_dir, "scripts", "bap_central.py")
    bap_args = [
        "python3",
        bap_script,
        "--duration",
        str(duration_s),
    ]
    if stereo:
        bap_args.append("--stereo")
    if peer_addr:
        # Target one specific receiver: with several LE Audio
        # Receiver boards on the bench, discovery may attach the
        # wrong one.  --peer-addr pins the exact peer.
        bap_args += ["--peer-addr", peer_addr]
    return subprocess.Popen(
        bap_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )


# ── Gate runner ────────────────────────────────────────────────────────


class HangGateRunner:
    """Hang gate logic with injectable console transport and BAP launcher.

    Production defaults (RealHangSerial + launch_bap_central) retain the
    exact pyserial/subprocess behavior of the pre-R3 gate; unit tests
    inject scripted fakes for both boundaries.
    """

    def __init__(
        self, port, baud, log_path, peer_addr=None, transport=None, launcher=None
    ):
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.peer_addr = peer_addr
        self._tr = transport if transport is not None else RealHangSerial()
        self._launcher = launcher if launcher is not None else launch_bap_central
        self._recv_buf = bytearray()
        self._log_fh = None

    def open(self):
        self._tr.open(self.port, self.baud)
        self._log_fh = open(self.log_path, "w", buffering=1)

    def close(self):
        if self._log_fh:
            self._log_fh.close()
            self._log_fh = None
        self._tr.close()

    def _read_all(self):
        waiting = self._tr.in_waiting
        if waiting:
            chunk = self._tr.read(waiting)
            self._recv_buf.extend(chunk)
            if self._log_fh:
                self._log_fh.write(chunk.decode("utf-8", errors="replace"))
                self._log_fh.flush()
            return chunk
        return b""

    def _read_status(self, settle_s=0.35, grace_s=0.15):
        """Read until the console goes quiet, returning all accumulated text.

        The status commands print over ~100-200 ms; a single in_waiting
        read catches only a partial block and the parser then misses the
        Counters line.  Drain until no new bytes arrive for grace_s.
        """
        self._clear_buf()
        deadline = time.monotonic() + settle_s
        while time.monotonic() < deadline:
            if self._tr.in_waiting:
                self._read_all()
            time.sleep(0.02)
        quiet = 0.0
        while quiet < grace_s:
            before = len(self._recv_buf)
            self._read_all()
            if len(self._recv_buf) == before:
                quiet += 0.05
            else:
                quiet = 0.0
            time.sleep(0.05)
        return self._all_text()

    def _all_text(self):
        return self._recv_buf.decode("utf-8", errors="replace")

    def _clear_buf(self):
        self._recv_buf.clear()

    def _send_cmd(self, cmd):
        self._tr.write((cmd + "\n").encode("utf-8"))
        self._tr.flush()
        time.sleep(0.05)

    def parse_offload(self, text):
        """Parse flpr offload status output into the shared superset dict."""
        return parse_offload_status(text)

    def parse_asrc(self, text):
        """Parse ASRC offload section."""
        result = {
            "submit": -1,
            "success": -1,
            "fallback": -1,
            "timeout": -1,
            "full": -1,
            "stale": -1,
            "seq": -1,
            "frame": -1,
            "crc": -1,
            "state": -1,
            "verify": -1,
        }
        m = RE_ASRC_COUNTERS.search(text)
        if m:
            result["submit"] = int(m.group(1))
            result["success"] = int(m.group(2))
            result["fallback"] = int(m.group(3))
        m = RE_ASRC_FAULTS.search(text)
        if m:
            result["timeout"] = int(m.group(1))
            result["full"] = int(m.group(2))
            result["stale"] = int(m.group(3))
            result["seq"] = int(m.group(4))
            result["frame"] = int(m.group(5))
            result["crc"] = int(m.group(6))
            result["state"] = int(m.group(7))
            result["verify"] = int(m.group(8))
        return result

    def run(self, duration_s, stereo=False) -> GateResult:
        result = GateResult()
        result.mode = "Mode B (stereo)" if stereo else "Mode A (mono/2-ASE)"
        result.duration_s = duration_s
        expected_frames = duration_s * 100  # 100 fps

        # Total timeout: stream duration + 60s for discovery + 30s for recovery + 30s margin
        total_timeout_s = duration_s + 120
        total_deadline = time.monotonic() + total_timeout_s

        try:
            self._tr.reset_input()
            self._clear_buf()

            # ── Step 1: Wait for device console to be responsive ──
            # Probe via shell command (don't rely on boot messages which may
            # have scrolled past before we opened the port).
            print(f"[{datetime.now().strftime('%H:%M:%S')}] Probing device console...")
            self._tr.reset_input()
            self._clear_buf()
            console_ready = False
            probe_deadline = time.monotonic() + 15
            while time.monotonic() < probe_deadline:
                self._send_cmd("flpr offload")
                time.sleep(0.15)
                self._read_all()
                text = self._all_text()
                if "uart:~$" in text or "State" in text or "no-init" in text:
                    console_ready = True
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] Console responsive")
                    break
                time.sleep(0.5)
            if not console_ready:
                raise RuntimeError("Device console not responsive within 15s")

            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] Device ready. Starting bap_central..."
            )

            # ── Step 2: Launch bap_central.py in background ──
            bap_proc = self._launcher(duration_s, stereo, self.peer_addr)
            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] Running bap_central "
                f"(duration={duration_s}s{' stereo' if stereo else ''})"
            )

            # ── Step 3: Wait for ACTIVE + success >= 1000 ──
            st = None
            inject_threshold = 1000
            while time.monotonic() < total_deadline:
                # Check if bap_central died
                if bap_proc.poll() is not None:
                    bap_out, _ = bap_proc.communicate()
                    raise RuntimeError(
                        f"bap_central.py exited early (code={bap_proc.returncode}):\n{bap_out[-500:]}"
                    )

                # Read until the console goes quiet and parse the LAST
                # complete block: a single in_waiting read can catch only
                # a partial status block, and the baseline Runtime line
                # (the last line of the block) then parses as -1, making
                # the runtime_restarts baseline-diff fail by one.
                self._send_cmd("flpr offload")
                st = self.parse_offload(parse_last_offload_block(self._read_status()))
                if st["state"] == "ACTIVE" and st["success"] >= inject_threshold:
                    break
                time.sleep(0.4)
            else:
                raise RuntimeError(
                    f"Timeout waiting for ACTIVE with success>={inject_threshold}. "
                    f"Last: state={st['state'] if st else None} success={st['success'] if st else -1}"
                )

            result.baseline_epoch = st["epoch"]
            result.baseline_success = st["success"]
            result.baseline_recovery_attempts = st["recovery_attempts"]
            # The Runtime line is only printed after the first restart,
            # so a pre-injection ACTIVE snapshot has no Runtime line and
            # the parser leaves the -1 sentinel: the absence means zero.
            result.baseline_runtime_restarts = max(0, st["runtime_restarts"])
            result.baseline_runtime_fails = max(0, st["runtime_fails"])
            result.baseline_relapses = st["relapses"]
            result.baseline_exhaustion = st["exhaustion"]
            result.baseline_probation_cleared = st["probation_cleared"]
            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] ACTIVE with success={st['success']}, "
                f"epoch={st['epoch']}. Injecting flpr hang..."
            )

            # ── Step 4: Inject flpr hang ──
            result.injection_time = time.monotonic()
            self._send_cmd("flpr hang")

            # ── Step 5: Wait for FAULT_HANG_ACK ──
            time.sleep(0.1)
            ack_seen = False
            ack_deadline = time.monotonic() + 3.0
            while time.monotonic() < ack_deadline:
                self._read_all()
                text = self._all_text()
                if RE_FAULT_HANG_ACK.search(text):
                    result.ack_time = time.monotonic()
                    ack_seen = True
                    print(
                        f"[{datetime.now().strftime('%H:%M:%S')}] FAULT_HANG_ACK received "
                        f"({(result.ack_time - result.injection_time) * 1000:.0f} ms)"
                    )
                    break
                m = RE_FAULT_HANG_FAIL.search(text)
                if m:
                    raise RuntimeError(f"FAULT_HANG failed: errno={m.group(1)}")
                time.sleep(0.05)
            if not ack_seen:
                raise RuntimeError("Timeout waiting for FAULT_HANG_ACK (3s)")

            # ── Step 6: Wait for recovery evidence ──
            # Recovery chain: timeout → RECOVERING → runtime restart → ACTIVE → probation cleared
            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] Waiting for recovery chain..."
            )
            recovery_deadline = min(total_deadline, time.monotonic() + 60)

            recov_seen = False
            fallback_seen = False
            active_seen = False
            probation_seen = False
            pre_hang_fallback = st["fallback"]

            while time.monotonic() < recovery_deadline:
                self._read_all()
                text = self._all_text()

                # Check for runtime restart OK
                m = RE_RUNTIME_RESTART_OK.search(text)
                if m:
                    recov_seen = True
                    old_epoch = int(m.group(1))
                    new_epoch = int(m.group(2))
                    dur_ms = int(m.group(4))
                    print(
                        f"[{datetime.now().strftime('%H:%M:%S')}] Runtime restart: "
                        f"{old_epoch}→{new_epoch} in {dur_ms} ms"
                    )

                # Poll offload status periodically
                self._send_cmd("flpr offload")
                time.sleep(0.05)
                self._clear_buf()
                self._read_all()
                text2 = self._all_text()
                st = self.parse_offload(text2)

                if not fallback_seen and st["fallback"] > pre_hang_fallback:
                    result.first_fallback_time = time.monotonic()
                    fallback_seen = True
                    print(
                        f"[{datetime.now().strftime('%H:%M:%S')}] First fallback block detected"
                    )

                if st["state"] == "ACTIVE" and st["recovery_attempts"] >= 1:
                    if not active_seen:
                        result.recovery_time = time.monotonic()
                        active_seen = True
                        print(
                            f"[{datetime.now().strftime('%H:%M:%S')}] Back to ACTIVE after "
                            f"{(result.recovery_time - result.injection_time) * 1000:.0f} ms"
                        )

                    if st["probation_cleared"] >= 1 and st["probation_active"] == 0:
                        probation_seen = True
                        print(
                            f"[{datetime.now().strftime('%H:%M:%S')}] Probation cleared. "
                            f"Recovery complete."
                        )
                        break

                time.sleep(0.5)

            if not probation_seen:
                raise RuntimeError(
                    f"Timeout waiting for recovery+probation. "
                    f"Last: state={st['state'] if st else '?'} "
                    f"probation_cleared={st['probation_cleared'] if st else -1}"
                )

            # ── Step 6b: Capture active-stream status BEFORE disconnect ──
            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] "
                f"Capturing active-stream status..."
            )
            self._send_cmd("flpr offload")
            active_st = self.parse_offload(
                parse_last_offload_block(self._read_status())
            )
            result.active_status = active_st
            print(
                f"  State={active_st['state']} submit={active_st['submit']} "
                f"success={active_st['success']} fallback={active_st['fallback']}"
            )

            # Also capture audio status while streaming
            self._send_cmd("audio status")
            time.sleep(0.05)
            self._read_all()
            result.active_audio_text = self._all_text()

            # ── Step 7: Wait for stream to complete ──
            # bap_central runs for duration_s from stream start; recovery ate some of that time.
            # Wait for bap_central to finish or total_deadline.
            remaining = total_deadline - time.monotonic()
            print(
                f"[{datetime.now().strftime('%H:%M:%S')}] Waiting for stream to finish "
                f"(remaining={remaining:.0f}s)..."
            )

            try:
                bap_out, _ = bap_proc.communicate(timeout=remaining + 10)
                bap_rc = bap_proc.returncode
                print(
                    f"[{datetime.now().strftime('%H:%M:%S')}] bap_central exited with code {bap_rc}"
                )
                if bap_out:
                    # Print last few lines
                    lines = bap_out.strip().split("\n")
                    for line in lines[-10:]:
                        print(f"  [bap] {line}")
            except subprocess.TimeoutExpired:
                bap_proc.kill()
                bap_out, _ = bap_proc.communicate()
                print(
                    f"[{datetime.now().strftime('%H:%M:%S')}] bap_central killed (timeout)"
                )

            # ── Step 8: Collect final status ──
            time.sleep(0.5)
            # Send every final command, then drain until the console is
            # quiet so ALL responses (including the audio status / perf
            # blocks) are captured before parsing.  The final status
            # commands are parsed below, not merely sent.
            for cmd in (
                "flpr offload",
                "flpr status",
                "flpr runtime",
                "audio status",
                "audio perf",
            ):
                self._send_cmd(cmd)

            result.full_log = self._read_status()

            # Parse final status (the drain already captured the full
            # response; the last complete offload block is the newest)
            self._send_cmd("flpr offload")
            st_final = self.parse_offload(parse_last_offload_block(self._read_status()))
            result.final_status = st_final

            # Parse ASRC section
            asrc = self.parse_asrc(parse_last_offload_block(result.full_log))
            result.asrc_status = asrc

            # Parse audio fault fields from the captured final
            # `audio status` / `audio perf` output.
            audio = parse_audio_faults(result.full_log)
            result.audio_status = audio

            # ── Step 9: Verify all gates ──
            checks = {}

            # Gate: ACK received
            checks["ack_received"] = ack_seen
            if not ack_seen:
                result.error = "FAULT_HANG_ACK not received"
                return result

            # Gate: exactly one recovery (attempts >= 1, no multiple)
            attempts = st_final["recovery_attempts"]
            checks["recovery_attempts_eq_1"] = (
                attempts - result.baseline_recovery_attempts
            ) == 1
            if attempts != 1:
                print(f"  WARNING: recovery_attempts={attempts} (expected 1)")

            # Gate: exactly one runtime restart (restarts==1)
            restarts = st_final["runtime_restarts"]
            checks["runtime_restarts_eq_1"] = (
                restarts - result.baseline_runtime_restarts
            ) == 1
            if restarts != 1:
                print(f"  WARNING: runtime_restarts={restarts} (expected 1)")

            # Gate: no exhaustion
            exhaustion = st_final["exhaustion"]
            checks["exhaustion_zero"] = exhaustion == 0

            # Gate: probation cleared
            checks["probation_cleared"] = (
                st_final["probation_cleared"] - result.baseline_probation_cleared
            ) >= 1

            # Gate: back to ACTIVE (use active-stream snapshot, final state is
            # STOPPED after disconnect)
            checks["state_active"] = active_st.get("state") == "ACTIVE"

            # Gate: new epoch (must differ from baseline)
            epoch_changed = st_final["epoch"] != result.baseline_epoch
            checks["epoch_changed"] = epoch_changed

            # Gate: verify faults zero (on verify build)
            checks["asrc_verify_zero"] = asrc["verify"] == 0

            # Gate: CRC faults zero
            checks["crc_zero"] = st_final["fault_crc"] == 0

            # Gate: seq faults zero
            checks["seq_zero"] = st_final["fault_seq"] == 0

            # Gate: frame faults zero
            checks["frame_zero"] = st_final["fault_frame"] == 0

            # Gate: state faults zero (ASRC)
            checks["asrc_state_zero"] = asrc["state"] == 0

            # Gate: no duplicate restart (dedup count, recovery relapses == 0)
            checks["relapses_zero"] = st_final["relapses"] == 0

            # Gate: runtime fails zero
            checks["runtime_fails_zero"] = st_final["runtime_fails"] == 0

            # Gate: frame count plausible (~duration*100, allow tolerance)
            total_frames = st_final["submit"]
            frame_tolerance = 100  # 1 second worth at 100fps
            checks["frame_count_plausible"] = (
                abs(total_frames - expected_frames) <= frame_tolerance
            )

            # Gate: resumed success after recovery (success must keep growing)
            # Explicit tolerance grounded in duration/cadence: final
            # success+fallback must reach 85% of duration*100 frames.
            resumed = st_final["success"] + st_final["fallback"]
            resumed_expected = int(expected_frames * RESUMED_SUCCESS_TOLERANCE_FRACTION)
            checks["resumed_success_until_end"] = resumed >= resumed_expected
            if not checks["resumed_success_until_end"]:
                print(
                    f"  WARNING: resumed success+fallback={resumed} < "
                    f"{resumed_expected} ({int(RESUMED_SUCCESS_TOLERANCE_FRACTION * 100)}% of "
                    f"{expected_frames})"
                )

            # Gate: ASRC fallback > 0 (we did trigger fallback)
            asrc_fallback_triggered = asrc["fallback"] > 0
            checks["asrc_fallback_triggered"] = asrc_fallback_triggered

            # Gate: audio fault fields zero — each field must be present in
            # the captured final `audio status`/`audio perf` output and
            # exactly zero.  An absent/unparseable field is missing
            # evidence, never zero.
            checks["audio_status_seen"] = bool(audio["status_seen"])
            for field in (
                "decode_errors",
                "i2s_underruns",
                "stream_resets",
                "push_failures",
            ):
                checks["audio_%s_zero" % field] = audio[field] == 0
                if audio[field] != 0:
                    print(f"  WARNING: audio {field}={audio[field]} (must be zero)")
            if not audio["status_seen"]:
                print(
                    "  WARNING: final 'audio status' block missing from console output"
                )

            # Determine overall pass
            required_checks = [
                "ack_received",
                "recovery_attempts_eq_1",
                "runtime_restarts_eq_1",
                "exhaustion_zero",
                "probation_cleared",
                "state_active",
                "epoch_changed",
                "asrc_verify_zero",
                "crc_zero",
                "seq_zero",
                "frame_zero",
                "asrc_state_zero",
                "relapses_zero",
                "runtime_fails_zero",
                "frame_count_plausible",
                "resumed_success_until_end",
                "asrc_fallback_triggered",
                "audio_status_seen",
                "audio_decode_errors_zero",
                "audio_i2s_underruns_zero",
                "audio_stream_resets_zero",
                "audio_push_failures_zero",
            ]

            result.checks = checks
            all_passed = all(checks.get(k, False) for k in required_checks)
            result.passed = all_passed

            if not all_passed:
                failed = [k for k in required_checks if not checks.get(k, False)]
                result.error = f"Failed checks: {', '.join(failed)}"

        except RuntimeError as e:
            result.error = str(e)
            result.full_log = self._all_text()
        except Exception as e:
            result.error = f"Unexpected: {e}"
            result.full_log = self._all_text()
        finally:
            # Ensure bap_central is dead
            if "bap_proc" in locals() and bap_proc.poll() is None:
                bap_proc.kill()
                bap_proc.wait()

        return result


# ── CLI ────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(
        description="FLPR FAULT_HANG gate automation (Stage 4B)"
    )
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--duration",
        type=int,
        required=True,
        help="Stream duration in seconds (e.g. 180, 300)",
    )
    parser.add_argument(
        "--stereo", action="store_true", help="Stereo Mode B (single ASE, chan_count=2)"
    )
    parser.add_argument(
        "--log",
        default=None,
        help="Log file path (default: flpr_hang_gate_<mode>_<dur>s.log)",
    )
    parser.add_argument(
        "--peer-addr",
        default=None,
        help="Receiver BLE address to pass through to bap_central.py "
        "(--peer-addr).  Required when several LE Audio Receiver boards "
        "are on the bench so the gate targets the exact receiver.",
    )
    args = parser.parse_args()

    if args.log is None:
        mode_tag = "stereo" if args.stereo else "mono"
        args.log = f"flpr_hang_gate_{mode_tag}_{args.duration}s.log"

    log_full = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", args.log)
    log_full = os.path.normpath(log_full)

    print(f"{'=' * 70}")
    print(
        f"[gate] FLPR FAULT_HANG Gate — {args.duration}s {'Mode B stereo' if args.stereo else 'Mode A mono'}"
    )
    print(f"[gate] Port: {args.port} @ {args.baud}")
    print(f"[gate] Log:  {log_full}")
    print(f"{'=' * 70}")

    runner = HangGateRunner(args.port, args.baud, log_full, peer_addr=args.peer_addr)
    runner.open()
    try:
        result = runner.run(args.duration, stereo=args.stereo)
    finally:
        runner.close()

    print(f"\n{'=' * 70}")
    print(f"[gate] RESULT: {'PASSED' if result.passed else 'FAILED'}")
    print(f"[gate] Mode: {result.mode}  Duration: {result.duration_s}s")
    if result.error:
        print(f"[gate] Error: {result.error}")
    print(
        f"[gate] Injection → ACK: {(result.ack_time - result.injection_time) * 1000:.0f} ms"
        if result.ack_time
        else "[gate] (no ACK timestamp)"
    )
    print(
        f"[gate] Injection → ACTIVE: {(result.recovery_time - result.injection_time) * 1000:.0f} ms"
        if result.recovery_time
        else "[gate] (no recovery timestamp)"
    )

    fs = result.final_status
    print(f"\n[gate] Final offload status:")
    print(f"  State       : {fs.get('state', '?')} / epoch={fs.get('epoch', '?')}")
    print(
        f"  Counters    : submit={fs.get('submit', '?')} success={fs.get('success', '?')} "
        f"fallback={fs.get('fallback', '?')}"
    )
    print(
        f"  Faults      : timeout={fs.get('fault_timeout', '?')} crc={fs.get('fault_crc', '?')} "
        f"seq={fs.get('fault_seq', '?')} frame={fs.get('fault_frame', '?')}"
    )
    print(
        f"  Recovery    : attempts={fs.get('recovery_attempts', '?')} "
        f"fail={fs.get('recovery_fail', '?')} exhaustion={fs.get('exhaustion', '?')}"
    )
    print(
        f"  Probation   : cleared={fs.get('probation_cleared', '?')} "
        f"active={fs.get('probation_active', '?')}"
    )
    print(
        f"  Runtime     : restarts={fs.get('runtime_restarts', '?')} "
        f"fails={fs.get('runtime_fails', '?')}"
    )

    asrc = result.asrc_status
    print(f"\n[gate] ASRC stats:")
    print(
        f"  Counters    : submit={asrc.get('submit', '?')} success={asrc.get('success', '?')} "
        f"fallback={asrc.get('fallback', '?')}"
    )
    print(
        f"  Faults      : verify={asrc.get('verify', '?')} state={asrc.get('state', '?')} "
        f"crc={asrc.get('crc', '?')} seq={asrc.get('seq', '?')} frame={asrc.get('frame', '?')}"
    )

    print(f"\n[gate] Checks:")
    for k, v in sorted(result.checks.items()):
        status = "PASS" if v else "FAIL"
        print(f"  [{status}] {k}")

    if not result.passed and result.error:
        print(f"\n[gate] FAILED checks: {result.error}")

    print(f"\n[gate] Full log: {log_full}")
    print(f"{'=' * 70}")

    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
