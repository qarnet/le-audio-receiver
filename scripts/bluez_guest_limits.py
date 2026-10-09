"""Bounded guest file and event evidence for PB-053."""

import json
import os
import stat
import threading


def _limit(value):
    if type(value) is not int or value <= 0:
        raise ValueError("Limit must be a positive integer")


def read_bounded(path, limit):
    _limit(limit)
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size > limit:
            raise ValueError(f"Invalid or oversized regular file: {path}")
        with os.fdopen(fd, "rb") as stream:
            fd = None
            data = stream.read(limit + 1)
        if len(data) > limit:
            raise ValueError(f"Regular file exceeds limit: {path}")
        return data
    finally:
        if fd is not None:
            os.close(fd)


class EventLedger(list):
    def __init__(self, iterable=(), max_events=4096, max_bytes=524288):
        _limit(max_events)
        _limit(max_bytes)
        super().__init__()
        self._lock = threading.RLock()
        self._max_events = max_events
        self._max_bytes = max_bytes
        self._bytes = 0
        self._error = None
        for item in iterable:
            self.append(item)

    def append(self, value):
        with self._lock:
            try:
                encoded = json.dumps(value, allow_nan=False).encode("utf-8")
                if (
                    len(self) >= self._max_events
                    or self._bytes + len(encoded) > self._max_bytes
                ):
                    raise ValueError("Guest event quota exceeded")
                copy = json.loads(encoded)
                super().append(copy)
                self._bytes += len(encoded)
            except Exception as exc:
                if self._error is None:
                    self._error = f"{type(exc).__name__}: {exc}"
                raise

    def safe_append(self, value):
        """Retain cleanup evidence when possible without interrupting resource release."""
        try:
            self.append(value)
        except Exception as exc:
            with self._lock:
                if self._error is None:
                    self._error = f"{type(exc).__name__}: {exc}"
            return False
        return True

    @property
    def error(self):
        with self._lock:
            return self._error
