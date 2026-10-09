"""Own asynchronous BlueZ Acquire replies until leases are transferred."""

import os
import threading


class AcquireOwner:
    def __init__(self):
        self._lock = threading.RLock()
        self._requests = {}
        self._closing = False
        self._finished = False
        self._errors = []

    @property
    def errors(self):
        with self._lock:
            return list(self._errors)

    def register(self, path, role):
        with self._lock:
            if (
                self._closing
                or type(path) is not str
                or not path
                or role not in ("source", "sink")
                or len(self._requests) >= 2
                or any(
                    r["path"] == path or r["role"] == role
                    for r in self._requests.values()
                )
            ):
                raise ValueError("Invalid Acquire registration")
            token = object()
            self._requests[token] = {
                "path": path,
                "role": role,
                "fd": None,
                "read_mtu": None,
                "write_mtu": None,
                "pending": None,
                "replied": False,
                "error": None,
                "taken": False,
            }
            return token

    def bind(self, token, pending_call):
        with self._lock:
            request = self._requests[token]
            if (
                not callable(getattr(pending_call, "cancel", None))
                or request["pending"] is not None
            ):
                raise ValueError("Invalid PendingCall")
            if not self._finished:
                request["pending"] = pending_call
                return
        try:
            pending_call.cancel()
        except Exception as exc:
            with self._lock:
                self._errors.append(f"Cancel {request['path']}: {exc}")

    def received(self, token, fd, read_mtu, write_mtu):
        with self._lock:
            request = self._requests.get(token)
            if request is None:
                self._errors.append("Unknown Acquire reply token")
                reason = "Unknown Acquire reply token"
            elif self._closing or request["replied"]:
                self._errors.append(
                    f"{'Late' if self._closing else 'Duplicate'} Acquire reply: {request['path']}"
                )
                reason = "Late or duplicate Acquire reply"
            elif any(
                type(value) is not int or value < 0
                for value in (fd, read_mtu, write_mtu)
            ):
                request["replied"] = True
                request["error"] = ValueError("Invalid Acquire fd or MTU")
                reason = "Invalid Acquire fd or MTU"
            else:
                request.update(
                    fd=fd, read_mtu=read_mtu, write_mtu=write_mtu, replied=True
                )
                return
            if type(fd) is int and fd >= 0:
                try:
                    os.close(fd)
                except OSError as exc:
                    self._errors.append(
                        f"Close {request['path'] if request is not None else 'unknown'}: {exc}"
                    )
            raise ValueError(reason)

    def failed(self, token, error):
        with self._lock:
            request = self._requests[token]
            if request["replied"]:
                self._errors.append(f"Duplicate Acquire failure: {request['path']}")
                return
            request["replied"] = True
            request["error"] = error

    def take(self, token):
        with self._lock:
            request = self._requests[token]
            if (
                self._closing
                or not request["replied"]
                or request["error"] is not None
                or request["taken"]
                or request["fd"] is None
            ):
                raise ValueError(f"Acquire not ready: {request['path']}")
            request["taken"] = True
            fd = request["fd"]
            request["fd"] = None
            return {
                key: request[key] for key in ("path", "role", "read_mtu", "write_mtu")
            } | {
                "fd": fd,
                "socket": None,
            }

    def begin_cleanup(self):
        with self._lock:
            self._closing = True
            return sorted(
                [
                    (token, request["path"], request["role"])
                    for token, request in self._requests.items()
                    if not request["taken"]
                ],
                key=lambda item: (item[2] != "source", item[1]),
            )

    def finish_cleanup(self):
        with self._lock:
            if self._finished:
                return []
            self._closing = True
            self._finished = True
            pending = [
                (r["path"], r["pending"])
                for r in self._requests.values()
                if r["pending"] is not None
            ]
            fds = [
                (r["path"], r["fd"])
                for r in self._requests.values()
                if r["fd"] is not None
            ]
            for request in self._requests.values():
                request["fd"] = None
        errors = []
        for path, handle in pending:
            try:
                handle.cancel()
            except Exception as exc:
                errors.append(f"Cancel {path}: {exc}")
        for path, fd in fds:
            try:
                os.close(fd)
            except OSError as exc:
                errors.append(f"Close {path}: {exc}")
        with self._lock:
            self._errors.extend(errors)
        return errors
