"""Bounded Linux process-group owner for PB-053 host tools."""

import hashlib
import math
import os
import selectors
import signal
import subprocess
import threading
import time
from pathlib import Path
from typing import Any


def _group_exists(pgid):
    try:
        os.killpg(pgid, 0)
        return True
    except ProcessLookupError:
        return False


def _group_live(pgid):
    """Exclude unreaped or init-adopted zombies from group liveness."""
    with os.scandir("/proc") as entries:
        for entry in entries:
            if not entry.name.isdigit():
                continue
            try:
                stat = Path(entry.path, "stat").read_text()
                fields = stat[stat.rfind(")") + 2 :].split()
                if int(fields[2]) == pgid and fields[0] not in ("Z", "X"):
                    return True
            except (OSError, ValueError, IndexError):
                continue
    return False


def run_owned(argv, log_path, timeout, max_log_bytes=33554432, env=None, cwd=None):
    """Run argv with bounded output and owned-group cleanup; return evidence record."""
    if (
        not isinstance(argv, (list, tuple))
        or not argv
        or any(not isinstance(arg, str) or not arg for arg in argv)
    ):
        raise ValueError("argv must be a nonempty sequence of nonempty strings")
    if (
        isinstance(timeout, bool)
        or not isinstance(timeout, (int, float))
        or not math.isfinite(timeout)
        or timeout <= 0
    ):
        raise ValueError("timeout must be positive and finite")
    if (
        isinstance(max_log_bytes, bool)
        or not isinstance(max_log_bytes, int)
        or max_log_bytes <= 0
    ):
        raise ValueError("max_log_bytes must be a positive integer")
    if threading.current_thread() is not threading.main_thread():
        raise ValueError("run_owned must run in main thread")
    path = Path(log_path)
    if not path.parent.is_dir() or path.is_symlink() or path.exists():
        raise ValueError("log path must be new and parent must exist")

    digest = hashlib.sha256()
    result: dict[str, Any] = dict(
        schema_version=1,
        argv=list(argv),
        pid=None,
        start_time=time.time(),
        end_time=None,
        returncode=None,
        ok=False,
        timed_out=False,
        cancelled_signal=None,
        log_limit_exceeded=False,
        bytes_logged=0,
        log_sha256=None,
        cleanup_errors=[],
        error=None,
        descendant_cleanup_required=False,
    )
    pending_signal = [None]

    def on_signal(signum, _frame):
        if pending_signal[0] is None:
            pending_signal[0] = signum
        result["cancelled_signal"] = pending_signal[0]
        result["ok"] = False

    def drain_allowed(deadline):
        if pending_signal[0] is not None:
            return False
        if time.monotonic() >= deadline:
            result["timed_out"] = True
            return False
        return True

    proc = None
    selector = selectors.DefaultSelector()
    old_handlers = {}

    def pump(wait):
        assert proc is not None and proc.stdout is not None
        for _key, _events in selector.select(wait):
            chunk = os.read(proc.stdout.fileno(), 65536)
            if not chunk:
                selector.unregister(proc.stdout)
                return False
            left = max_log_bytes - result["bytes_logged"]
            kept = chunk[:left]
            if kept:
                log.write(kept)
                log.flush()
                digest.update(kept)
                result["bytes_logged"] += len(kept)
            if len(chunk) > left:
                result["log_limit_exceeded"] = True
                return False
        return True

    with path.open("xb") as log:
        deadline = time.monotonic() + timeout
        try:
            for signum in (signal.SIGINT, signal.SIGTERM):
                old_handlers[signum] = signal.getsignal(signum)
                signal.signal(signum, on_signal)
            proc = subprocess.Popen(
                argv,
                start_new_session=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                cwd=cwd,
            )
            result["pid"] = proc.pid
            assert proc.stdout is not None
            os.set_blocking(proc.stdout.fileno(), False)
            selector.register(proc.stdout, selectors.EVENT_READ)

            while drain_allowed(deadline):
                if proc.poll() is not None:
                    # Drain bytes available now, not output held open by descendants.
                    while selector.get_map() and drain_allowed(deadline) and pump(0):
                        if not selector.select(0):
                            break
                    break
                if selector.get_map():
                    pump(min(0.05, max(0, deadline - time.monotonic())))
                    if result["log_limit_exceeded"]:
                        break
                else:
                    time.sleep(min(0.05, max(0, deadline - time.monotonic())))
        except Exception as exc:
            result["error"] = f"{type(exc).__name__}: {exc}"
        finally:
            if proc is not None:
                try:
                    proc.poll()
                    if proc.returncode is not None and _group_live(proc.pid):
                        result["descendant_cleanup_required"] = True
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"group inspect: {type(exc).__name__}: {exc}"
                    )
                try:
                    if _group_exists(proc.pid):
                        try:
                            os.killpg(proc.pid, signal.SIGTERM)
                        except ProcessLookupError:
                            pass
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"group TERM: {type(exc).__name__}: {exc}"
                    )
                try:
                    grace = time.monotonic() + 2
                    while _group_live(proc.pid) and time.monotonic() < grace:
                        time.sleep(0.05)
                    if _group_live(proc.pid):
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"group KILL: {type(exc).__name__}: {exc}"
                    )
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    result["cleanup_errors"].append("leader wait exceeded 2s")
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"leader wait: {type(exc).__name__}: {exc}"
                    )
                try:
                    if _group_live(proc.pid):
                        result["cleanup_errors"].append(
                            "live group remains after SIGKILL"
                        )
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"group final inspect: {type(exc).__name__}: {exc}"
                    )
                result["returncode"] = proc.returncode
                # Cleanup must not lose bounded output already in pipe.
                if proc.stdout is not None:
                    try:
                        while (
                            selector.get_map()
                            and drain_allowed(deadline)
                            and selector.select(0)
                        ):
                            if not pump(0):
                                break
                    except Exception as exc:
                        result["cleanup_errors"].append(
                            f"pipe drain: {type(exc).__name__}: {exc}"
                        )
                    try:
                        proc.stdout.close()
                    except Exception as exc:
                        result["cleanup_errors"].append(
                            f"pipe close: {type(exc).__name__}: {exc}"
                        )
            try:
                selector.close()
            except Exception as exc:
                result["cleanup_errors"].append(
                    f"selector close: {type(exc).__name__}: {exc}"
                )
            try:
                log.flush()
            except Exception as exc:
                result["cleanup_errors"].append(
                    f"log flush: {type(exc).__name__}: {exc}"
                )
            result["log_sha256"] = digest.hexdigest()
            result["end_time"] = time.time()
            result["cancelled_signal"] = pending_signal[0]
            result["ok"] = (
                result["returncode"] == 0
                and not result["timed_out"]
                and result["cancelled_signal"] is None
                and not result["log_limit_exceeded"]
                and not result["descendant_cleanup_required"]
                and not result["cleanup_errors"]
                and result["error"] is None
            )
            for signum, handler in old_handlers.items():
                signal.signal(signum, handler)
    return result
