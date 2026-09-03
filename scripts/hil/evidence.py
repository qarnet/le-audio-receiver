"""Evidence finalization for one flat HIL evidence directory.

Hashes retained regular files with SHA-256, atomically writes ``SHA256SUMS``
and a concise ``MANIFEST.md``, and fails closed: symlinks, non-regular files,
and paths escaping the run directory are rejected; evidence files are never
deleted.  Metadata finalization is transactional: all payload evidence is
validated and hashed before either metadata file changes, both new metadata
contents are staged before either file is replaced, and a failure after one
replacement restores both metadata files to their exact prior state
(including prior absence) before ``EvidenceError`` is raised.  Temporary or
backup files never remain after a handled failure.  Ordinary failures (for
example ``OSError`` during hashing, snapshot, staging, or replacement) are
reported as ``EvidenceError``; non-``Exception`` cancellation such as
``KeyboardInterrupt`` rolls back the transaction and propagates unchanged.
"""

import hashlib
import io
import json
import os
import socket
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

EVIDENCE_EXCLUDED = frozenset({"SHA256SUMS", "MANIFEST.md"})

#: Legal run outcomes. ``prepared`` belongs to prepare-only validation;
#: ``passed``/``failed``/``cancelled`` cover direct rows and RH3 aggregates.
RUN_OUTCOMES = ("prepared", "passed", "failed", "cancelled")


class EvidenceError(Exception):
    """Raised for any evidence finalization failure."""


def _atomic_write_text(path, text):
    fd, tmp = tempfile.mkstemp(
        prefix=".%s.tmp" % os.path.basename(path), dir=os.path.dirname(path)
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def _stage_metadata(path, payload):
    """Write one metadata payload (str or bytes) to a temporary file next to
    ``path`` and return the temporary path.  The temporary is removed on any
    write failure."""
    if isinstance(payload, str):
        payload = payload.encode("utf-8")
    fd, tmp = tempfile.mkstemp(
        prefix=".%s.tmp" % os.path.basename(path), dir=os.path.dirname(path)
    )
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(payload)
            fh.flush()
            os.fsync(fh.fileno())
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return tmp


def _commit_metadata(tmp_path, path):
    """Replace one metadata file with its staged temporary (commit step)."""
    os.replace(tmp_path, path)


def _unlink_if_exists(path):
    try:
        os.unlink(path)
    except OSError:
        pass


def _snapshot_bytes(path):
    """Exact prior bytes of one metadata file, or None when it was absent."""
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except FileNotFoundError:
        return None


def _restore_metadata(path, prior):
    """Restore one metadata file to its exact prior bytes, or remove it when
    it was absent.  Best effort: a secondary failure during restore never
    masks the original finalization failure, and the temporary this function
    owns is always removed."""
    if prior is None:
        _unlink_if_exists(path)
        return
    tmp = None
    try:
        tmp = _stage_metadata(path, prior)
        os.replace(tmp, path)
        tmp = None
    except BaseException:
        pass
    finally:
        if tmp is not None:
            _unlink_if_exists(tmp)


def _enumerate_evidence(run_dir):
    """Sorted retained evidence names; rejects symlinks, non-regular files,
    and paths escaping the run directory."""
    entries = []
    with os.scandir(run_dir) as it:
        for entry in it:
            name = entry.name
            if entry.is_symlink():
                raise EvidenceError("evidence symlink not allowed: %s" % name)
            if not entry.is_file():
                raise EvidenceError("non-regular evidence not allowed: %s" % name)
            if name in EVIDENCE_EXCLUDED:
                continue
            if os.sep in name or name in ("", ".", ".."):
                raise EvidenceError("evidence path escapes run directory: %s" % name)
            entries.append(name)
    return sorted(entries)


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _hash_lines(run_dir):
    names = _enumerate_evidence(run_dir)
    lines = []
    for name in names:
        lines.append("%s  %s" % (_sha256_file(os.path.join(run_dir, name)), name))
    return lines


def _manifest_text(
    fixture_id, run_id, capture_capability, outcome, lines, artifact_identity=None
):
    parts = [
        "# HIL run manifest",
        "",
        "fixture_id: %s" % fixture_id,
        "run_id: %s" % run_id,
        "capture_capability: %s" % capture_capability,
        "outcome: %s" % outcome,
        "",
        "## Evidence",
        "",
    ]
    parts.extend(lines)
    if artifact_identity is not None:
        parts.extend(
            [
                "",
                "## Artifacts",
                "",
                json.dumps(artifact_identity, sort_keys=True, separators=(",", ":")),
            ]
        )
    return "\n".join(parts) + "\n"


def _best_effort_failed_manifest(
    run_dir, fixture_id, run_id, capture_capability, artifact_identity=None
):
    """Write a failed MANIFEST.md when possible (best effort, never raises).

    A secondary failure (including any ``BaseException``) is swallowed so it
    can never mask the original finalization failure; the atomic writer
    removes any temporary it owns."""
    try:
        lines = _hash_lines(run_dir)
        text = _manifest_text(
            fixture_id,
            run_id,
            capture_capability,
            "failed",
            lines,
            artifact_identity,
        )
        _atomic_write_text(os.path.join(run_dir, "MANIFEST.md"), text)
    except BaseException:
        pass


def finalize_evidence(
    run_dir,
    *,
    fixture_id,
    run_id,
    capture_capability,
    outcome,
    artifact_identity=None,
):
    """Finalize one prepared run directory.

    ``outcome`` is a checked-in HIL outcome. All payload evidence is
    validated and hashed before either metadata file changes, and both new
    metadata contents are staged before either is replaced.  A failure while
    staging preserves prior metadata bytes exactly; a failure after one
    replacement restores both metadata files to their exact prior state
    (including prior absence) when possible and then raises
    ``EvidenceError``.  When no prior manifest existed, a best-effort failed
    manifest may be written; a valid prior manifest is never overwritten
    merely to label a failed re-finalization.  No temporary or backup file
    remains after a handled failure.  Ordinary failures become
    ``EvidenceError``; non-``Exception`` cancellation such as
    ``KeyboardInterrupt`` rolls back the transaction and propagates
    unchanged.  Returns the manifest path on success.
    """
    if outcome not in RUN_OUTCOMES:
        raise EvidenceError("unknown evidence outcome %r" % outcome)
    try:
        lines = _hash_lines(run_dir)
    except EvidenceError:
        raise
    except Exception as exc:  # noqa: BLE001 - raw I/O becomes EvidenceError
        raise EvidenceError("evidence finalization failed: %s" % exc) from exc
    sha_text = "".join(line + "\n" for line in lines)
    manifest_text = _manifest_text(
        fixture_id, run_id, capture_capability, outcome, lines, artifact_identity
    )
    sums_path = os.path.join(run_dir, "SHA256SUMS")
    manifest_path = os.path.join(run_dir, "MANIFEST.md")

    try:
        prior_sums = _snapshot_bytes(sums_path)
        prior_manifest = _snapshot_bytes(manifest_path)
    except Exception as exc:  # noqa: BLE001 - raw I/O becomes EvidenceError
        raise EvidenceError("evidence finalization failed: %s" % exc) from exc

    staged = []
    try:
        staged.append(_stage_metadata(sums_path, sha_text))
        staged.append(_stage_metadata(manifest_path, manifest_text))
    except BaseException as exc:  # noqa: BLE001 - fail closed, prior bytes intact
        for tmp in staged:
            _unlink_if_exists(tmp)
        if isinstance(exc, Exception):
            raise EvidenceError("evidence finalization failed: %s" % exc) from exc
        raise

    committed = 0
    try:
        _commit_metadata(staged[0], sums_path)
        committed = 1
        _commit_metadata(staged[1], manifest_path)
        committed = 2
    except BaseException as exc:  # noqa: BLE001 - fail closed, roll back metadata
        # No staged or partially committed temporary may remain.
        for tmp in staged:
            _unlink_if_exists(tmp)
        if committed >= 1:
            _restore_metadata(sums_path, prior_sums)
            _restore_metadata(manifest_path, prior_manifest)
            if committed == 1 and prior_manifest is None:
                _best_effort_failed_manifest(
                    run_dir, fixture_id, run_id, capture_capability
                )
        if isinstance(exc, Exception):
            raise EvidenceError("evidence finalization failed: %s" % exc) from exc
        raise
    return manifest_path


# ── JUnit XML ─────────────────────────────────────────────────────

#: Legacy default for direct callers that do not yet carry a row spec.
RUN_TESTCASE = "rh2.short_mono_48_4_1"


def _junit_element(outcome, failure_message, system_out, testcase=RUN_TESTCASE):
    """Build one well-formed JUnit testsuite/testcase element tree.

    ``outcome`` maps to the element state: passed -> no child, failed ->
    a ``failure`` child, cancelled -> a ``skipped`` child.  The failure
    message names the first failed boundary and any cleanup failures.
    All text is escaped by ElementTree's serializer.
    """
    if not isinstance(testcase, str) or not testcase:
        raise EvidenceError("JUnit testcase must be a nonempty string")
    suite = testcase.split(".", 1)[0]
    ts = ET.Element("testsuite")
    ts.set("name", "hil.%s" % suite)
    ts.set("tests", "1")
    ts.set("failures", "1" if outcome == "failed" else "0")
    ts.set("skipped", "1" if outcome == "cancelled" else "0")
    tc = ET.SubElement(ts, "testcase")
    tc.set("name", testcase)
    tc.set("classname", "hil.%s" % suite)
    if outcome == "passed":
        ts.set("errors", "0")
    elif outcome == "cancelled":
        ts.set("errors", "0")
        sk = ET.SubElement(tc, "skipped")
        if failure_message:
            sk.set("message", failure_message)
    else:
        ts.set("errors", "0")
        failure = ET.SubElement(tc, "failure")
        if failure_message:
            failure.set("message", failure_message)
            failure.text = failure_message
    if system_out:
        out = ET.SubElement(tc, "system-out")
        out.text = system_out
    return ET.ElementTree(ts)


def write_junit(
    run_dir,
    external_junit_path,
    *,
    outcome,
    failure_message=None,
    system_out=None,
    testcase=RUN_TESTCASE,
):
    """Write identical JUnit XML to ``<run_dir>/junit.xml`` and the
    requested external path (both paths are written from one serialized
    document).  The XML is well formed and all text escaped.  Returns the
    run-dir junit.xml path; raises ``EvidenceError`` on write failure
    (the evidence finalization transaction never sees a half-written
    JUnit file because the writer stages atomically)."""
    if outcome not in RUN_OUTCOMES:
        raise EvidenceError("unknown junit outcome %r" % outcome)
    tree = _junit_element(outcome, failure_message, system_out, testcase=testcase)
    parts = []
    try:
        for path in (os.path.join(run_dir, "junit.xml"), external_junit_path):
            _atomic_write_text(path, _serialize_junit(tree))
            parts.append(path)
    except EvidenceError:
        raise
    except Exception as exc:  # noqa: BLE001 - raw I/O becomes EvidenceError
        raise EvidenceError("cannot write JUnit evidence: %s" % exc) from exc
    return parts[0]


def _serialize_junit(tree):
    buf = io.StringIO()
    tree.write(buf, encoding="unicode", xml_declaration=False)
    return "<?xml version='1.0' encoding='UTF-8'?>\n" + buf.getvalue() + "\n"


# ── environment provenance ─────────────────────────────────────────


def _run_capture(argv, timeout=10, run_cmd=None):
    """Capture one read-only version probe command (never fails the row:
    a missing tool is recorded, not raised)."""
    try:
        if run_cmd is None:
            proc = subprocess.run(
                argv, capture_output=True, text=True, timeout=timeout, check=False
            )
        else:
            proc = run_cmd(argv, timeout)
        return {
            "argv": list(argv),
            "status": proc.returncode,
            "stdout": (proc.stdout or "")[:4000],
            "stderr": (proc.stderr or "")[:4000],
        }
    except FileNotFoundError:
        return {"argv": list(argv), "status": None, "stdout": "", "stderr": "not found"}
    except subprocess.TimeoutExpired:
        return {"argv": list(argv), "status": None, "stdout": "", "stderr": "timeout"}
    except OSError as exc:
        return {"argv": list(argv), "status": None, "stdout": "", "stderr": str(exc)}


def _tool_version(argv, run_cmd=None):
    info = _run_capture(argv, run_cmd=run_cmd)
    if info["status"] != 0:
        return None
    first = (info["stdout"].splitlines() or [""])[0].strip()
    return first or None


def capture_environment(repo_root, argv, status, run_cmd=None):
    """Read-only environment provenance for one run.

    Records repo HEAD and dirty status, NCS version/path, west, Python,
    pytest, pyserial, OpenOCD, nrf-probes, host/kernel, UTC timestamps,
    and the exact command argv/status.  A dirty tree is recorded, never
    accepted as exact release provenance.  Version probing is best
    effort; a missing tool is recorded as None, never raised."""
    now = datetime.now(timezone.utc)
    env = {
        "utc_timestamp": now.isoformat(),
        "command": {"argv": list(argv), "status": status},
        "host": {
            "hostname": socket.gethostname(),
            "platform": os.uname().sysname if hasattr(os, "uname") else None,
            "kernel": " ".join(os.uname()[:3]) if hasattr(os, "uname") else None,
        },
        "repo": {"root": repo_root},
    }
    try:
        if run_cmd is None:
            head_proc = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
                cwd=repo_root,
            )
            dirty_proc = subprocess.run(
                ["git", "status", "--porcelain"],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
                cwd=repo_root,
            )
        else:
            head_proc = run_cmd(["git", "-C", repo_root, "rev-parse", "HEAD"], 5)
            dirty_proc = run_cmd(["git", "-C", repo_root, "status", "--porcelain"], 5)
        if head_proc.returncode == 0:
            env["repo"]["head"] = head_proc.stdout.strip()
        else:
            env["repo"]["head"] = None
        if dirty_proc.returncode == 0:
            env["repo"]["dirty"] = bool(dirty_proc.stdout.strip())
        else:
            env["repo"]["dirty"] = None
    except (OSError, subprocess.SubprocessError):
        env["repo"]["head"] = None
        env["repo"]["dirty"] = None
    ncs_root = os.environ.get("NCS_BASE") or os.environ.get("ZEPHYR_BASE")
    ncs_version = None
    if ncs_root:
        # ZEPHYR_BASE points at <ncs>/zephyr in this shell. NCS_BASE, when
        # present, is already <ncs>. Record only locally readable version
        # evidence; provenance capture must never mutate or require a tool.
        candidate_root = (
            os.path.dirname(ncs_root)
            if os.path.basename(os.path.normpath(ncs_root)) == "zephyr"
            else ncs_root
        )
        try:
            with open(
                os.path.join(candidate_root, "nrf", "VERSION"), "r", encoding="utf-8"
            ) as fh:
                value = fh.read().strip()
            ncs_version = value or None
        except OSError:
            pass
    env["ncs"] = {
        "path": ncs_root,
        "version": ncs_version,
    }
    env["tools"] = {
        "python": _tool_version(["python3", "--version"], run_cmd=run_cmd),
        "pytest": _tool_version(["pytest", "--version"], run_cmd=run_cmd),
        "pyserial": _tool_version(
            ["python3", "-c", "import serial; print(serial.VERSION)"], run_cmd=run_cmd
        ),
        "openocd": _tool_version(["openocd", "--version"], run_cmd=run_cmd),
        "nrf-probes": _tool_version(["nrf-probes", "--help"], run_cmd=run_cmd)
        or "present",
        "west": _tool_version(["west", "--version"], run_cmd=run_cmd),
        "arecord": _tool_version(["arecord", "--version"], run_cmd=run_cmd),
        "numpy": _tool_version(
            ["python3", "-c", "import numpy; print(numpy.__version__)"], run_cmd=run_cmd
        ),
    }
    return env


def write_json_evidence(run_dir, name, obj):
    """Atomically write one flat JSON evidence file (sorted keys)."""
    try:
        _atomic_write_text(
            os.path.join(run_dir, name),
            json.dumps(obj, sort_keys=True, indent=2) + "\n",
        )
    except EvidenceError:
        raise
    except Exception as exc:  # noqa: BLE001 - raw I/O becomes EvidenceError
        raise EvidenceError("cannot write JSON evidence %s: %s" % (name, exc)) from exc
