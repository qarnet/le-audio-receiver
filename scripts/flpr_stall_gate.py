#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p "python3Packages.pyserial"
"""
flpr_stall_gate.py — Automated FLPR timed-stall gate for nRF54L15.

Single pyserial process owns UART for entire injection round-trip.
No serial-MCP polling latency.

Algorithm — timed 60ms auto-clear, no external on/wait/off:
  1. Open configured console, preserve raw log to file.
  2. Send `flpr offload`; wait until State=ACTIVE AND success >= 500.
  3. Send `flpr ring stall_flpr_ms 1 60`.
  4. Require exact ACK: bits=0x01 duration=60.
  5. Read until at least one fault/fallback detected.
  6. Monitor status until ACTIVE, recovery>=1, exhaustion=0, probation_cleared>=1,
     success increased by >=100 after injection, 0 ASRC integrity faults.
  7. Send final status commands, drain until the console is quiet, then
     PARSE the captured responses (not merely send): the final offload
     status becomes final_status and the final `audio status`/`audio perf`
     fault fields (decode errors, I2S underruns, stream resets, push
     failures) must each be present and exactly zero.  Close port.
     Exit 0 on success, nonzero on timeout or missing predicate.

Testability: GateRunner class accepts a Transport interface.
RealSerial uses pyserial; FakeSerial replays pre-recorded line streams.
"""

import argparse
import re
import sys
import time
from abc import ABC, abstractmethod

# Shared offload-status grammar (flpr_status.py is the single source of
# truth for the console status block; both hardware gates consume it).
from flpr_status import (  # noqa: E402
    RE_COUNTERS,
    RE_FAULTS,
    RE_PROBATION,
    RE_RECOVERY,
    RE_RECOVERY_OK,
    RE_STATE_LINE,
    parse_audio_faults,
    parse_offload_status,
)

# ── Regex patterns against current shell/log output ─────────────────────

# Timed stall ACK: "FLPR timed stall applied: bits=0x01 duration=60 ms"
RE_STALL_TIMED_ACK = re.compile(
    r"FLPR timed stall applied:\s*bits=0x([0-9a-fA-F]+)\s+duration=(\d+)\s+ms"
)


class StallGateError(Exception):
    """Non-recoverable gate failure."""


class GateResult:
    """Structured result from gate execution."""

    def __init__(self):
        self.passed: bool = False
        self.error: str = ""
        self.injection_time: float = 0.0
        self.ack_time: float = 0.0
        self.first_fallback_time: float = 0.0
        self.baseline_success: int = -1
        self.final_status: dict = {}
        self.audio_status: dict = {}
        self.full_log: str = ""


# ── Transport interface ─────────────────────────────────────────────────


class Transport(ABC):
    @abstractmethod
    def open(self, port, baud): ...
    @abstractmethod
    def write(self, data): ...
    @abstractmethod
    def read_all(self) -> bytes: ...
    @abstractmethod
    def reset_input(self): ...
    @abstractmethod
    def close(self): ...


class RealSerial(Transport):
    """pyserial backend — one process owns UART."""

    def __init__(self):
        self._ser = None
        self._log_fh = None
        self._log_path = None

    def open(self, port, baud, log_path="flpr_stall_gate.log"):
        import serial

        self._ser = serial.Serial(port, baud, timeout=0.05)
        self._log_path = log_path
        self._log_fh = open(log_path, "w", buffering=1)

    def write(self, data):
        self._ser.write(data)
        self._ser.flush()

    def read_all(self) -> bytes:
        waiting = self._ser.in_waiting
        if waiting:
            chunk = self._ser.read(waiting)
            self._log_fh.write(chunk.decode("utf-8", errors="replace"))
            self._log_fh.flush()
            return chunk
        return b""

    def reset_input(self):
        self._ser.reset_input_buffer()

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()
        if self._log_fh:
            self._log_fh.close()


class FakeSerial(Transport):
    """Index-based line replay for unit tests.

    Schedule: dict mapping call_index → data_string.
    read_all() returns the scheduled data on that specific call number,
    or empty bytes if no data scheduled for this call.

    Commands written are recorded in self.written list.
    """

    def __init__(self, schedule=None):
        self._schedule = dict(schedule) if schedule else {}
        self._call_count = 0
        self.written = []
        self._recv_buf = bytearray()

    def open(self, port=None, baud=None):
        pass

    def write(self, data):
        self.written.append(data.decode("utf-8", errors="replace").strip())

    def read_all(self) -> bytes:
        """Return scheduled line if this call_index has one, else empty."""
        self._call_count += 1
        data = self._schedule.get(self._call_count - 1)
        if data is not None:
            line = data + "\n"
            self._recv_buf.extend(line.encode("utf-8"))
            return line.encode("utf-8")
        return b""

    def reset_input(self):
        self._call_count = 0
        self._recv_buf.clear()

    def close(self):
        pass

    def add_schedule(self, call_index, data):
        """Schedule data for a specific read_all() call index."""
        self._schedule[call_index] = data

    @property
    def all_received(self) -> str:
        return self._recv_buf.decode("utf-8", errors="replace")

    @property
    def call_count(self) -> int:
        return self._call_count


# ── Gate runner ──────────────────────────────────────────────────────────


class GateRunner:
    """Stateless gate logic operating on a Transport."""

    @staticmethod
    def parse_offload(text):
        """Parse flpr offload status output into the shared superset dict."""
        return parse_offload_status(text)

    def __init__(
        self, transport: Transport, total_timeout: float, status_interval: float = 0.5
    ):
        self._tr = transport
        self._timeout = total_timeout
        self._status_interval = status_interval
        self._recv_buf = bytearray()

    def _read_all(self):
        chunk = self._tr.read_all()
        if chunk:
            self._recv_buf.extend(chunk)
        return chunk

    def _all_text(self):
        return self._recv_buf.decode("utf-8", errors="replace")

    def _clear_text(self):
        """Reset accumulated text buffer (between steps to avoid first-match
        problems with re.search on stale old data)."""
        self._recv_buf.clear()

    def _send_cmd(self, cmd):
        self._tr.write((cmd + "\n").encode("utf-8"))
        time.sleep(0.05)

    def _drain_until_quiet(self, grace_s=0.15, max_iter=50):
        """Read until no new bytes arrive for grace_s (bounded by max_iter
        so an always-noisy transport cannot hang the gate)."""
        quiet = 0.0
        iters = 0
        while quiet < grace_s and iters < max_iter:
            iters += 1
            before = len(self._recv_buf)
            self._read_all()
            if len(self._recv_buf) == before:
                quiet += 0.05
            else:
                quiet = 0.0
            time.sleep(0.05)

    def run(self) -> GateResult:
        result = GateResult()
        total_deadline = time.monotonic() + self._timeout

        try:
            self._tr.reset_input()
            self._recv_buf.clear()

            # ── Step 1: Wait for State=ACTIVE and success >= 500 ──
            st = None
            pre_stall_fallback = 0
            while time.monotonic() < total_deadline:
                self._send_cmd("flpr offload")
                time.sleep(0.1)
                self._clear_text()
                self._read_all()
                st = self.parse_offload(self._all_text())
                if st["state"] == "ACTIVE" and st["success"] >= 500:
                    pre_stall_fallback = st["fallback"]
                    break
                time.sleep(0.4)
            else:
                raise StallGateError(
                    f"Timeout waiting for ACTIVE with success>=500 "
                    f"(got state={st['state'] if st else None} "
                    f"success={st['success'] if st else -1})"
                )

            # ── Step 2: Inject timed stall (60ms auto-clear) ───────
            result.injection_time = time.monotonic()
            self._send_cmd("flpr ring stall_flpr_ms 1 60")

            # ── Step 3: Wait for exact timed stall ACK ─────────────
            time.sleep(0.05)
            ack_seen = False
            while time.monotonic() < total_deadline:
                self._read_all()
                text = self._all_text()
                for m in RE_STALL_TIMED_ACK.finditer(text):
                    mask_val = int(m.group(1), 16)
                    dur_val = int(m.group(2))
                    if mask_val == 0x01 and dur_val == 60:
                        result.ack_time = time.monotonic()
                        ack_seen = True
                        break
                if ack_seen:
                    break
                time.sleep(0.05)
            if not ack_seen:
                raise StallGateError(
                    "Timeout waiting for timed stall ACK (bits=0x01 duration=60)"
                )

            # ── Step 4: Capture baseline success BEFORE monitoring ──
            self._send_cmd("flpr offload")
            time.sleep(0.05)
            self._clear_text()
            self._read_all()
            st = self.parse_offload(self._all_text())
            result.baseline_success = st["success"]
            result.final_status = st

            # ── Step 5: Wait until fallback evidence appears ───────
            # (stall injection causes faults → fallback count increases)
            while time.monotonic() < total_deadline:
                self._read_all()
                self._send_cmd("flpr offload")
                time.sleep(0.05)
                self._clear_text()
                self._read_all()
                st = self.parse_offload(self._all_text())
                if st["fallback"] > pre_stall_fallback:
                    result.first_fallback_time = time.monotonic()
                    break
                time.sleep(self._status_interval)

            # ── Step 6: Monitor until all evidence predicates met ──
            gate_passed = False
            while time.monotonic() < total_deadline:
                self._read_all()
                self._send_cmd("flpr offload")
                time.sleep(0.05)
                self._clear_text()
                self._read_all()
                st = self.parse_offload(self._all_text())
                result.final_status = st

                if st["state"] != "ACTIVE":
                    time.sleep(self._status_interval)
                    continue
                if st["recovery_attempts"] < 1:
                    time.sleep(self._status_interval)
                    continue
                if st["exhaustion"] > 0:
                    raise StallGateError(f"max_exhaustion_count={st['exhaustion']} > 0")
                if st["probation_cleared"] < 1:
                    time.sleep(self._status_interval)
                    continue
                if st["probation_active"] != 0:
                    time.sleep(self._status_interval)
                    continue
                if st["fallback"] <= 0:
                    time.sleep(self._status_interval)
                    continue
                if st["success"] < result.baseline_success + 100:
                    time.sleep(self._status_interval)
                    continue

                # Integrity-fault rejection: stale, seq, frame, crc, payload.
                # timeout and full are expected transport fault evidence of
                # stall — they do NOT gate-fail (acceptance requires
                # timeout/fallback >= 1).
                if (
                    st["fault_stale"] > 0
                    or st["fault_seq"] > 0
                    or st["fault_frame"] > 0
                    or st["fault_crc"] > 0
                    or st["fault_payload"] > 0
                ):
                    time.sleep(self._status_interval)
                    continue

                gate_passed = True
                break

            if not gate_passed:
                raise StallGateError(
                    f"Timeout waiting for probation cleared. "
                    f"baseline_success={result.baseline_success} "
                    f"Last status: {result.final_status}"
                )

            # ── Step 7: Final status dump (parsed, not merely sent) ─────
            # Start from a clean buffer so the parsed final offload status
            # and audio fault fields reflect ONLY the final responses —
            # a stale mid-stream status can never be mistaken for the
            # post-run snapshot.
            self._recv_buf.clear()
            for cmd in (
                "flpr offload",
                "flpr ring status",
                "flpr status",
                "audio status",
                "audio perf",
            ):
                self._send_cmd(cmd)
            self._drain_until_quiet()
            result.full_log = self._all_text()

            # Parse the final offload status from the captured responses.
            st_final = self.parse_offload(result.full_log)
            if st_final["state"] is not None:
                result.final_status = st_final

            # Parse audio fault fields: each must be present and zero.
            audio = parse_audio_faults(result.full_log)
            result.audio_status = audio
            if not audio["status_seen"]:
                raise StallGateError(
                    "final 'audio status' block missing from console output"
                )
            for field in (
                "decode_errors",
                "i2s_underruns",
                "stream_resets",
                "push_failures",
            ):
                if audio[field] is None:
                    raise StallGateError(
                        "audio fault field %s missing from final status" % field
                    )
                if audio[field] != 0:
                    raise StallGateError(
                        "audio fault field %s=%s (must be zero)" % (field, audio[field])
                    )

            result.passed = True

        except StallGateError as e:
            result.error = str(e)
            result.full_log = self._all_text()
        except Exception as e:
            result.error = f"Unexpected: {e}"
            result.full_log = self._all_text()

        return result


# ── CLI ──────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(description="FLPR timed-stall gate automation")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--status-interval", type=float, default=0.5)
    parser.add_argument("--log", default="flpr_stall_gate.log")
    args = parser.parse_args()

    print(f"[gate] Opening {args.port} @ {args.baud}…")
    transport = RealSerial()
    transport.open(args.port, args.baud, args.log)

    runner = GateRunner(transport, args.timeout, args.status_interval)
    result = runner.run()

    transport.close()

    if result.passed:
        print(f"\n[gate] GATE PASSED (timed 60ms)")
        print(f"[gate]   injection_time: {result.injection_time}")
        print(f"[gate]   ack_time: {result.ack_time}")
        print(f"[gate]   baseline_success: {result.baseline_success}")
        print(f"[gate]   Log: {args.log}")
        return 0
    else:
        print(f"\n[gate] GATE FAILED: {result.error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
