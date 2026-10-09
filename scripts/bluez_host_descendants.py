"""Dedicated Linux lane subreaper; contain newly adopted detached descendants."""

import ctypes
import os
from pathlib import Path
import select
import signal
import threading
import time


_LIBC = ctypes.CDLL(None, use_errno=True)
_LIBC.prctl.argtypes = [
    ctypes.c_int,
    ctypes.c_ulong,
    ctypes.c_ulong,
    ctypes.c_ulong,
    ctypes.c_ulong,
]
_LIBC.prctl.restype = ctypes.c_int
_SET = 36
_GET = 37


def _flag():
    value = ctypes.c_int()
    if _LIBC.prctl(_GET, ctypes.addressof(value), 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "PR_GET_CHILD_SUBREAPER failed")
    return value.value


def _set_flag(value):
    if _LIBC.prctl(_SET, value, 0, 0, 0) != 0:
        raise OSError(ctypes.get_errno(), "PR_SET_CHILD_SUBREAPER failed")


def _stat(pid):
    try:
        raw = Path(f"/proc/{pid}/stat").read_text()
        fields = raw[raw.rfind(")") + 2 :].split()
        return int(fields[1]), int(fields[19]), fields[0]
    except (OSError, ValueError, IndexError):
        return None


def _children(owner):
    children = {}
    with os.scandir("/proc") as entries:
        for entry in entries:
            if entry.name.isdigit():
                state = _stat(int(entry.name))
                if state is not None and state[0] == owner:
                    children[int(entry.name)] = state
    return children


class DescendantScope:
    """Fail closed unless sole-threaded caller has exclusive direct-child ownership."""

    def __init__(self):
        self.record = {
            "owner_pid": os.getpid(),
            "saved_flag": None,
            "restored_flag": None,
            "adopted": [],
            "actions": [],
            "errors": [],
            "unexpected_live_descendants": False,
            "cancelled_signal": None,
            "ok": False,
        }
        self._handlers = {}
        self._cleaning = False
        self.lane_record = None
        self.lane_output = None

    def _signal(self, signum, _frame):
        if self.record["cancelled_signal"] is None:
            self.record["cancelled_signal"] = signum
        if not self._cleaning:
            raise KeyboardInterrupt(f"Scope cancelled by signal {signum}")

    def __enter__(self):
        if (
            threading.current_thread() is not threading.main_thread()
            or threading.active_count() != 1
        ):
            raise ValueError("Descendant scope requires sole main thread")
        if _children(self.record["owner_pid"]):
            raise ValueError("Descendant scope requires no existing direct children")
        self.record["saved_flag"] = _flag()
        try:
            _set_flag(1)
            for signum in (signal.SIGINT, signal.SIGTERM):
                self._handlers[signum] = signal.getsignal(signum)
                signal.signal(signum, self._signal)
        except BaseException:
            for signum, handler in self._handlers.items():
                signal.signal(signum, handler)
            _set_flag(self.record["saved_flag"])
            raise
        return self

    def _observe(self, pid, start):
        """Only signal a still-owned direct child with same process birth time."""
        fd = os.pidfd_open(pid)
        state = _stat(pid)
        if state is None or state[0] != self.record["owner_pid"] or state[1] != start:
            os.close(fd)
            return None
        return fd

    def _send(self, pid, start, sig):
        try:
            fd = self._observe(pid, start)
            if fd is None:
                return
            try:
                signal.pidfd_send_signal(fd, sig)
                self.record["actions"].append(
                    {"pid": pid, "start_ticks": start, "signal": sig}
                )
            finally:
                os.close(fd)
        except ProcessLookupError:
            pass
        except OSError as exc:
            self.record["errors"].append(f"pidfd signal {pid}: {exc}")

    def _sweep(self, deadline):
        known = set()
        while time.monotonic() < deadline:
            children = _children(self.record["owner_pid"])
            for pid, (parent, start, state) in children.items():
                key = (pid, start)
                if key not in known:
                    known.add(key)
                    self.record["adopted"].append(
                        {"pid": pid, "ppid": parent, "start_ticks": start}
                    )
                if state in ("Z", "X"):
                    try:
                        reaped, status = os.waitpid(pid, os.WNOHANG)
                        if reaped:
                            self.record["actions"].append(
                                {
                                    "pid": pid,
                                    "start_ticks": start,
                                    "wait_status": status,
                                }
                            )
                    except ChildProcessError:
                        try:
                            fd = self._observe(pid, start)
                            if fd is not None:
                                try:
                                    poller = select.poll()
                                    poller.register(fd, select.POLLIN)
                                    if not poller.poll(0):
                                        self.record["errors"].append(
                                            f"unreapable live child {pid}"
                                        )
                                finally:
                                    os.close(fd)
                        except ProcessLookupError:
                            pass
                    continue
                if key not in self._term_sent:
                    self._term_sent.add(key)
                    self._send(pid, start, signal.SIGTERM)
                    self._term_at[key] = time.monotonic()
                elif (
                    time.monotonic() - self._term_at[key] >= 2
                    and key not in self._kill_sent
                ):
                    self._kill_sent.add(key)
                    self._send(pid, start, signal.SIGKILL)
            if not children:
                return
            time.sleep(0.02)
        if _children(self.record["owner_pid"]):
            self.record["errors"].append("direct children remain after 8s cleanup")

    def __exit__(self, kind, value, traceback):
        self._cleaning = True
        self._term_sent = set()
        self._kill_sent = set()
        self._term_at = {}
        try:
            initial = _children(self.record["owner_pid"])
            self.record["unexpected_live_descendants"] = kind is None and any(
                state[2] not in ("Z", "X") for state in initial.values()
            )
            self._sweep(time.monotonic() + 8)
        except BaseException as exc:
            self.record["errors"].append(f"cleanup: {type(exc).__name__}: {exc}")
        finally:
            try:
                _set_flag(self.record["saved_flag"])
                self.record["restored_flag"] = _flag()
                if self.record["restored_flag"] != self.record["saved_flag"]:
                    self.record["errors"].append("subreaper flag restore mismatch")
            except OSError as exc:
                self.record["errors"].append(f"subreaper restore: {exc}")
            for signum, handler in self._handlers.items():
                signal.signal(signum, handler)
        self.record["ok"] = not (
            self.record["errors"]
            or self.record["unexpected_live_descendants"]
            or self.record["cancelled_signal"] is not None
        )
        if kind is None and not self.record["ok"]:
            raise ValueError(f"Descendant scope failed: {self.record}")
        return False
