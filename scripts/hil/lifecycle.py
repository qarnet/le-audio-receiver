"""Host-only lifecycle ownership: cleanup stacks, exclusive fixture locks,
and safe run-directory creation (RH0).

Hardware-independent by design.  All write side effects register immediate
cleanup; setup failure after lock acquisition releases the owned lock; run
evidence is never deleted during cleanup.
"""

import hashlib
import json
import os
import re
import socket
import sys
import uuid
from datetime import datetime, timezone

RUN_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


class HilLifecycleError(Exception):
    """Raised for unsafe output roots or invalid run IDs."""


class CleanupFailure(Exception):
    """Aggregates every callback failure from one CleanupStack close."""

    def __init__(self, failures):
        # failures: list of (name, original_exception)
        super().__init__(
            "cleanup failures: %s" % ", ".join(name for name, _ in failures)
        )
        self.failures = list(failures)

    def __str__(self):
        parts = []
        for name, exc in self.failures:
            parts.append("%s: %s" % (name, exc))
        return "cleanup failed: " + "; ".join(parts)


class CleanupStack:
    """Immediate-registration cleanup ownership.

    ``close()`` runs every registered callback exactly once in reverse
    registration order; one callback failure never prevents later callbacks,
    and repeated close is idempotent.  Context exit invokes close for normal
    return, exception, and KeyboardInterrupt; a cleanup failure never hides
    an original body failure.  On Python 3.11+ an ``Exception`` body plus
    cleanup failure is exposed in an ``ExceptionGroup``, while a
    ``BaseException`` body such as ``KeyboardInterrupt`` plus cleanup failure
    is exposed in a ``BaseExceptionGroup``.
    """

    def __init__(self):
        self._callbacks = []  # list of (name, callback)
        self._closed = False

    def register(self, name, callback):
        """Add one callback; rejects registration after close begins."""
        if self._closed:
            raise RuntimeError("cleanup stack already closed")
        if not callable(callback):
            raise TypeError("cleanup callback must be callable: %s" % name)
        self._callbacks.append((name, callback))

    def close(self):
        """Run every callback exactly once in reverse registration order.

        Every callback runs even when an earlier (later-registered) callback
        raises any ``BaseException``, including ``KeyboardInterrupt``; all
        failures are aggregated in a ``CleanupFailure`` with the original
        exception objects retained.
        """
        if self._closed:
            return
        self._closed = True
        failures = []
        for name, callback in reversed(self._callbacks):
            try:
                callback()
            except BaseException as exc:  # noqa: BLE001 - aggregate, keep going
                failures.append((name, exc))
        if failures:
            raise CleanupFailure(failures)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            self.close()
        except CleanupFailure as cleanup_exc:
            if exc is not None:
                if sys.version_info >= (3, 11):
                    # A non-Exception body (KeyboardInterrupt) requires a
                    # BaseExceptionGroup; an Exception body uses ExceptionGroup.
                    group_type = (
                        ExceptionGroup
                        if isinstance(exc, Exception)
                        else BaseExceptionGroup
                    )
                    raise group_type(
                        "body failed and cleanup failed", [exc, cleanup_exc]
                    ) from None
                raise CleanupFailure(
                    [("body", exc), ("cleanup", cleanup_exc)]
                ) from None
            raise
        return False  # never swallow the original body exception


class FixtureBusy(Exception):
    """Raised when the exclusive fixture lock is already held."""

    def __init__(self, fixture_id):
        super().__init__("fixture %s is busy" % fixture_id)
        self.fixture_id = fixture_id


class LockReleaseError(Exception):
    """Raised when a lock cannot be safely released (changed or malformed)."""


class FixtureLock:
    """Exclusive local fixture lock.

    Lock file lives under ``<output-root>/.locks/`` named by the SHA-256 of
    the fixture ID.  Acquisition is atomic via ``O_CREAT | O_EXCL``; an
    existing file fails closed with ``FixtureBusy`` (staleness is never
    inferred and the file is never removed automatically).  Release unlinks
    only when the on-disk token still equals the owner token.
    """

    def __init__(self, path, token, fixture_id, pid, host, created):
        self.path = path
        self.token = token
        self.fixture_id = fixture_id
        self.pid = pid
        self.host = host
        self.created = created

    @classmethod
    def acquire(cls, output_root, fixture_id, cleanup=None):
        """Acquire the exclusive lock and register release immediately.

        ``cleanup`` may be a ``CleanupStack``; release is registered as soon
        as acquisition succeeds so setup failure never leaves the lock held.
        The ``.locks`` directory must be a real directory directly under the
        (already existing, canonical) output root: an existing symlink or any
        non-directory ``.locks`` entry fails closed with
        ``HilLifecycleError`` before any lock file is created, so nothing is
        ever written outside the output root.
        """
        lock_dir = os.path.join(output_root, ".locks")
        if os.path.islink(lock_dir):
            raise HilLifecycleError(
                "lock directory must not be a symlink: %s" % lock_dir
            )
        if os.path.lexists(lock_dir) and not os.path.isdir(lock_dir):
            raise HilLifecycleError("lock path is not a directory: %s" % lock_dir)
        if not os.path.lexists(lock_dir):
            try:
                os.mkdir(lock_dir)
            except OSError as exc:
                raise HilLifecycleError(
                    "cannot create lock directory %s: %s" % (lock_dir, exc)
                ) from None
        digest = hashlib.sha256(fixture_id.encode("utf-8")).hexdigest()
        path = os.path.join(lock_dir, digest)
        token = uuid.uuid4().hex
        pid = os.getpid()
        host = socket.gethostname()
        created = datetime.now(timezone.utc).isoformat()
        payload = (
            json.dumps(
                {
                    "token": token,
                    "fixture_id": fixture_id,
                    "pid": pid,
                    "host": host,
                    "created": created,
                },
                sort_keys=True,
            )
            + "\n"
        )
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError:
            raise FixtureBusy(fixture_id) from None
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                fh.write(payload)
                fh.flush()
                os.fsync(fh.fileno())
        except BaseException:
            # Abort our own acquisition; never leave a partial lock behind.
            try:
                os.unlink(path)
            except OSError:
                pass
            raise
        lock = cls(path, token, fixture_id, pid, host, created)
        if cleanup is not None:
            cleanup.register("fixture lock %s" % fixture_id, lock.release)
        return lock

    def release(self):
        """Unlink only when the on-disk token still equals the owner token."""
        try:
            with open(self.path, "r", encoding="utf-8") as fh:
                text = fh.read()
        except FileNotFoundError:
            raise LockReleaseError(
                "lock file %s is missing; token changed" % self.path
            ) from None
        try:
            on_disk = json.loads(text)
        except (json.JSONDecodeError, UnicodeDecodeError):
            raise LockReleaseError(
                "lock file %s is malformed; left untouched" % self.path
            ) from None
        if not isinstance(on_disk, dict) or on_disk.get("token") != self.token:
            raise LockReleaseError(
                "lock file %s token changed; left untouched" % self.path
            )
        try:
            os.unlink(self.path)
        except OSError as exc:
            raise LockReleaseError(
                "cannot remove lock file %s: %s" % (self.path, exc)
            ) from None


def default_repo_root():
    # lifecycle.py lives at <repo>/scripts/hil/lifecycle.py.
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def validate_output_root(path, repo_root=None):
    """Validate and canonicalize an output root.

    Mirrors ``scripts/test-all.sh`` safety semantics: must be absolute, not
    ``/``, not the home directory, not the repository root, not inside the
    repository, and not an ancestor containing the repository.  Paths are
    canonicalized before decisions and the root must already exist.
    """
    if not isinstance(path, str) or not path or not os.path.isabs(path):
        raise HilLifecycleError("output root must be an absolute path")
    canon = os.path.realpath(path)
    home = os.path.realpath(os.path.expanduser("~"))
    if canon == "/":
        raise HilLifecycleError("output root must not be /")
    if canon == home:
        raise HilLifecycleError("output root must not be the home directory")
    repo = (
        os.path.realpath(repo_root)
        if repo_root
        else os.path.realpath(default_repo_root())
    )
    if canon == repo:
        raise HilLifecycleError("output root must not be the repository root")
    if canon.startswith(repo + os.sep):
        raise HilLifecycleError("output root must not be inside the repository")
    if repo.startswith(canon + os.sep):
        raise HilLifecycleError("output root must not contain the repository root")
    if not os.path.isdir(canon):
        raise HilLifecycleError("output root does not exist: %s" % path)
    return canon


def validate_run_id(run_id):
    """Validate a run ID against the checked-in safe pattern."""
    if not isinstance(run_id, str) or not RUN_ID_RE.match(run_id):
        raise HilLifecycleError("run id must match [A-Za-z0-9][A-Za-z0-9._-]{0,63}")


def create_run_dir(canon_output_root, run_id):
    """Create ``<output-root>/<run-id>`` with no overwrite and no symlink
    traversal.  ``canon_output_root`` must already be canonical (see
    ``validate_output_root``).  Evidence is never deleted here."""
    validate_run_id(run_id)
    run_dir = os.path.join(canon_output_root, run_id)
    if os.path.islink(run_dir):
        raise HilLifecycleError(
            "run directory must not traverse a symlink: %s" % run_dir
        )
    if os.path.lexists(run_dir):
        raise HilLifecycleError("run directory already exists: %s" % run_dir)
    if os.path.realpath(run_dir) != os.path.normpath(run_dir):
        raise HilLifecycleError(
            "run directory path escapes through a symlink: %s" % run_dir
        )
    try:
        os.mkdir(run_dir)
    except OSError as exc:
        raise HilLifecycleError(
            "cannot create run directory %s: %s" % (run_dir, exc)
        ) from None
    return run_dir
