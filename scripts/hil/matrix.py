"""Fixed two-pass RH3/RH4 matrix coordination.

``Runner`` remains the sole owner of one row's fixture lock, consoles,
processes, cleanup, and flat evidence.  This module owns only matrix-level
metadata and invokes one fresh row runner for each fixed schedule entry.  Child
evidence lives in a sibling output root, never below the aggregate matrix
directory, so row finalization keeps its existing flat layout.
"""

import hashlib
import json
import os
import re
import tempfile
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

from hil import evidence, lifecycle, model, rows, runner
import hil.artifacts as artifact_resolver
from hil.evidence import (
    capture_environment,
    finalize_evidence,
    write_json_evidence,
)


class MatrixError(Exception):
    """Matrix-level failure after aggregate evidence directory creation."""


def _default_cancel():
    return False


def _default_utc_now():
    return datetime.now(timezone.utc)


def _default_row_runner_factory(cancel):
    """Create one production row runner sharing matrix cancellation state."""
    return runner.Runner(
        runner.RunnerDeps(repo_root=lifecycle.default_repo_root(), cancel=cancel)
    )


class MatrixDeps:
    """Injected matrix boundaries.

    ``row_runner_factory`` receives the matrix cancellation callable and must
    return either a ``Runner``-like object with ``run(...)`` or a callable with
    the public ``Runner.run`` signature.  Fake tests inject this boundary, so
    matrix tests never open a serial port or start a process.
    """

    def __init__(
        self,
        row_runner_factory=None,
        cancel=None,
        clock=None,
        utc_now=None,
        environment=None,
    ):
        self.row_runner_factory = (
            row_runner_factory
            if row_runner_factory is not None
            else _default_row_runner_factory
        )
        self.cancel = cancel if cancel is not None else _default_cancel
        self.clock = clock if clock is not None else time.monotonic
        self.utc_now = utc_now if utc_now is not None else _default_utc_now
        self.environment = environment


def _safe_row_label(name):
    """Return bounded filename-safe row label for a child run ID."""
    label = re.sub(r"[^A-Za-z0-9._-]", "-", name)
    return label[:24]


def child_run_id(matrix_run_id, pass_index, row_index, row):
    """Return one deterministic safe child ID for a fixed schedule entry."""
    digest = hashlib.sha256(
        ("%s\0%d\0%d\0%s" % (matrix_run_id, pass_index, row_index, row.name)).encode(
            "utf-8"
        )
    ).hexdigest()[:12]
    suffix = ".p%d.r%d.%s.%s" % (
        pass_index,
        row_index,
        _safe_row_label(row.name),
        digest,
    )
    return matrix_run_id[: 64 - len(suffix)] + suffix


def _children_root_id(matrix_run_id):
    """Return safe sibling root ID without consuming matrix evidence space."""
    digest = hashlib.sha256((matrix_run_id + "\0children").encode("utf-8")).hexdigest()[
        :12
    ]
    suffix = ".children.%s" % digest
    return matrix_run_id[: 64 - len(suffix)] + suffix


def _validate_child_root(canon_output_root, matrix_run_id):
    """Validate a deterministic sibling child root without creating it."""
    child_root_id = _children_root_id(matrix_run_id)
    lifecycle.validate_run_id(child_root_id)
    child_root = os.path.join(canon_output_root, child_root_id)
    if os.path.islink(child_root):
        raise lifecycle.HilLifecycleError(
            "child output root must not traverse a symlink: %s" % child_root
        )
    if os.path.lexists(child_root):
        raise lifecycle.HilLifecycleError(
            "child output root already exists: %s" % child_root
        )
    if os.path.realpath(child_root) != os.path.normpath(child_root):
        raise lifecycle.HilLifecycleError(
            "child output root path escapes through a symlink: %s" % child_root
        )
    return child_root_id, child_root


def _external_junit_path(path, canon_output_root, matrix_run_id, child_root):
    """Validate one no-clobber aggregate JUnit destination outside matrix dir."""
    if not isinstance(path, str) or not path or not os.path.isabs(path):
        raise lifecycle.HilLifecycleError(
            "external JUnit path must be an absolute path"
        )
    parent = os.path.realpath(os.path.dirname(path))
    if not os.path.isdir(parent):
        raise lifecycle.HilLifecycleError(
            "external JUnit parent does not exist: %s" % os.path.dirname(path)
        )
    name = os.path.basename(path)
    if name in ("", ".", ".."):
        raise lifecycle.HilLifecycleError("external JUnit path must name a file")
    canonical_path = os.path.join(parent, name)
    repo_root = os.path.realpath(lifecycle.default_repo_root())
    if canonical_path == repo_root or canonical_path.startswith(repo_root + os.sep):
        raise lifecycle.HilLifecycleError(
            "external JUnit path must not be inside the repository"
        )
    matrix_dir = os.path.join(canon_output_root, matrix_run_id)
    if canonical_path == matrix_dir or canonical_path.startswith(matrix_dir + os.sep):
        raise lifecycle.HilLifecycleError(
            "external JUnit path must be outside the matrix directory"
        )
    if canonical_path == child_root or canonical_path.startswith(child_root + os.sep):
        raise lifecycle.HilLifecycleError(
            "external JUnit path must be outside the child output root"
        )
    if os.path.lexists(canonical_path):
        raise lifecycle.HilLifecycleError(
            "external JUnit path already exists: %s" % canonical_path
        )
    return canonical_path


class MatrixCoordinator:
    """Run exactly two fixed RH3 passes and retain aggregate evidence."""

    def __init__(self, deps=None):
        self.deps = deps if deps is not None else MatrixDeps()
        self._matrix_dir = None

    @staticmethod
    def _read_bytes(path):
        with open(path, "rb") as fh:
            return fh.read()

    @staticmethod
    def _write_bytes(path, data):
        fd, temporary = None, None
        try:
            fd, temporary = tempfile.mkstemp(
                prefix=".%s.tmp" % os.path.basename(path), dir=os.path.dirname(path)
            )
            with os.fdopen(fd, "wb") as fh:
                fd = None
                fh.write(data)
                fh.flush()
                os.fsync(fh.fileno())
            os.replace(temporary, path)
            temporary = None
        finally:
            if fd is not None:
                os.close(fd)
            if temporary is not None:
                try:
                    os.unlink(temporary)
                except OSError:
                    pass

    @staticmethod
    def _row_dict(row):
        return {
            "name": row.name,
            "state": row.state,
            "mode": row.mode,
            "profile": row.profile,
            "scored_sdu_count": row.scored_sdu_count,
            "signal_seed": row.signal_seed,
            "reconnect_policy": row.reconnect_policy,
            "fault": row.fault,
            "stream_count": row.stream_count,
            "segment_count": row.segment_count,
            "expected_scored_per_stream": row.expected_scored_per_stream,
            "expected_submitted_per_segment": row.expected_submitted_per_segment,
            "expected_submitted_per_stream": row.expected_submitted_per_stream,
            "minimum_scored_interval_s": row.minimum_scored_interval_s,
        }

    def _timestamp(self):
        value = self.deps.utc_now()
        if not isinstance(value, datetime):
            raise MatrixError("UTC clock must return datetime")
        if value.tzinfo is None:
            value = value.replace(tzinfo=timezone.utc)
        return value.isoformat()

    def _write_jsonl(self, name, records):
        text = "".join(
            json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
            for record in records
        )
        evidence._atomic_write_text(os.path.join(self._matrix_dir, name), text)

    def _schedule(self, matrix_run_id, child_root, artifact_set=None):
        entries = []
        for pass_index, row_index, row in rows.rh3_schedule():
            run_id = child_run_id(matrix_run_id, pass_index, row_index, row)
            entry = {
                "pass_index": pass_index,
                "row_index": row_index,
                "row_name": row.name,
                "testcase": "pass%d.%s" % (pass_index, row.name),
                "child_run_id": run_id,
                "child_evidence_path": os.path.join(child_root, run_id),
                "child_junit_path": os.path.join(child_root, run_id + ".junit.xml"),
                "row": self._row_dict(row),
                "_row": row,
            }
            if artifact_set is not None:
                entry["artifacts"] = artifact_set.evidence()
            entries.append(entry)
        return entries

    @staticmethod
    def _schedule_for_evidence(entries):
        return [
            {key: value for key, value in entry.items() if key != "_row"}
            for entry in entries
        ]

    @staticmethod
    def _schedule_evidence(entries, matrix_run_id, artifact_set=None):
        record = {
            "schema_version": 1,
            "matrix_run_id": matrix_run_id,
            "pass_count": rows.RH3_PASS_COUNT,
            "children": MatrixCoordinator._schedule_for_evidence(entries),
        }
        if artifact_set is not None:
            record["artifacts"] = artifact_set.evidence()
        return record

    @staticmethod
    def _normalize_cleanup(cleanup):
        if cleanup is None:
            return []
        if not isinstance(cleanup, (list, tuple)):
            raise MatrixError("child cleanup failures must be a list")
        normalized = []
        for item in cleanup:
            if isinstance(item, dict):
                name = item.get("name")
                error = item.get("error")
            elif isinstance(item, (list, tuple)) and len(item) == 2:
                name, error = item
            else:
                raise MatrixError("child cleanup failure has invalid shape")
            if not isinstance(name, str) or not isinstance(error, str):
                raise MatrixError(
                    "child cleanup failure must contain text name and error"
                )
            normalized.append({"name": name, "error": error})
        return normalized

    def _invoke_child(
        self,
        entry,
        fixture_path,
        binding_path,
        child_root,
        argv,
        status,
        artifact_set=None,
        qualification_path=None,
    ):
        start_mono = self.deps.clock()
        start_utc = self._timestamp()
        outcome = "failed"
        boundary = None
        cleanup_failures = []
        try:
            row_runner = self.deps.row_runner_factory(self.deps.cancel)
            run_child = getattr(row_runner, "run", row_runner)
            if not callable(run_child):
                raise MatrixError("row runner factory did not return a callable")
            call_kwargs = {"argv": argv, "status": status, "row": entry["_row"]}
            if artifact_set is not None:
                call_kwargs["artifacts"] = artifact_set
            if qualification_path is not None:
                # Existing non-capture fake runners intentionally expose the
                # historical public signature. Only capture matrices need this
                # added public input.
                call_kwargs["qualification_path"] = qualification_path
            result = run_child(
                fixture_path,
                binding_path,
                child_root,
                entry["child_run_id"],
                entry["child_junit_path"],
                **call_kwargs,
            )
            if not isinstance(result, tuple) or len(result) != 3:
                raise MatrixError("row runner returned invalid result")
            outcome, boundary, cleanup = result
            if outcome not in ("passed", "failed", "cancelled"):
                raise MatrixError("row runner returned unknown outcome %r" % outcome)
            if boundary is not None and not isinstance(boundary, str):
                raise MatrixError("row runner boundary must be text or None")
            cleanup_failures = self._normalize_cleanup(cleanup)
            if cleanup_failures:
                if boundary is None:
                    boundary = "cleanup"
                outcome = "failed"
            if outcome == "passed":
                self._validate_child_evidence(
                    entry, require_capture=qualification_path is not None
                )
        except runner.RunnerCancelled as exc:
            outcome = "cancelled"
            boundary = str(exc)
        except Exception as exc:  # noqa: BLE001 - row boundary becomes matrix evidence
            outcome = "failed"
            boundary = "row invocation: %s" % exc
            cleanup_failures = []
        end_utc = self._timestamp()
        record = {
            "pass_index": entry["pass_index"],
            "row_index": entry["row_index"],
            "row_name": entry["row_name"],
            "child_run_id": entry["child_run_id"],
            "outcome": outcome,
            "first_failed_boundary": boundary,
            "cleanup_failures": cleanup_failures,
            "child_evidence_path": entry["child_evidence_path"],
            "child_junit_path": entry["child_junit_path"],
            "start_utc": start_utc,
            "end_utc": end_utc,
            "duration_s": round(self.deps.clock() - start_mono, 6),
        }
        if artifact_set is not None:
            record["artifacts"] = artifact_set.evidence()
        return record

    @staticmethod
    def _validate_child_evidence(entry, require_capture=False):
        """Require passed child to retain finalized flat row evidence."""
        run_dir = entry["child_evidence_path"]
        if os.path.islink(run_dir) or not os.path.isdir(run_dir):
            raise MatrixError("passed child evidence directory missing: %s" % run_dir)
        required = (
            "result.json",
            "junit.xml",
            "MANIFEST.md",
            "SHA256SUMS",
        )
        for name in required:
            path = os.path.join(run_dir, name)
            if os.path.islink(path) or not os.path.isfile(path):
                raise MatrixError("passed child evidence missing: %s" % path)
        external_junit = entry["child_junit_path"]
        if os.path.islink(external_junit) or not os.path.isfile(external_junit):
            raise MatrixError(
                "passed child external JUnit missing: %s" % external_junit
            )
        try:
            with open(os.path.join(run_dir, "result.json"), encoding="utf-8") as fh:
                result = json.load(fh)
        except (OSError, ValueError) as exc:
            raise MatrixError("passed child result unreadable: %s" % exc) from exc
        if not isinstance(result, dict) or result.get("outcome") != "passed":
            raise MatrixError("passed child result does not report passed")
        if not require_capture:
            return
        capture_analysis = os.path.join(run_dir, "capture-analysis.json")
        capture_session = os.path.join(run_dir, "capture-session.json")
        capture_summary = os.path.join(run_dir, "capture-summary.json")
        capture_wav = os.path.join(run_dir, "capture.wav")
        for path in (capture_analysis, capture_session, capture_summary, capture_wav):
            if os.path.islink(path) or not os.path.isfile(path):
                raise MatrixError("passed capture child evidence missing: %s" % path)
        try:
            with open(capture_analysis, encoding="utf-8") as fh:
                analysis = json.load(fh)
            with open(capture_session, encoding="utf-8") as fh:
                session = json.load(fh)
            with open(capture_summary, encoding="utf-8") as fh:
                summary = json.load(fh)
        except (OSError, ValueError) as exc:
            raise MatrixError(
                "passed capture child evidence unreadable: %s" % exc
            ) from exc
        if not isinstance(analysis, dict) or analysis.get("outcome") != "passed":
            raise MatrixError("passed capture child analysis does not report passed")
        if not isinstance(session, dict) or not isinstance(
            session.get("wav_sha256"), str
        ):
            raise MatrixError("passed capture child session lacks WAV SHA-256")
        if not isinstance(summary, dict) or summary.get("outcome") != "passed":
            raise MatrixError("passed capture child summary does not report passed")

    @staticmethod
    def _stop_reason(outcome, boundary, record=None):
        if record is not None:
            prefix = "pass%d row%d %s" % (
                record["pass_index"],
                record["row_index"],
                record["row_name"],
            )
            detail = record["first_failed_boundary"] or record["outcome"]
            return "%s stopped matrix: %s" % (prefix, detail)
        if boundary:
            return boundary
        return "matrix %s before child execution" % outcome

    @staticmethod
    def _matrix_cleanup_failures(records):
        return [
            {
                "pass_index": record["pass_index"],
                "row_index": record["row_index"],
                "row_name": record["row_name"],
                "name": failure["name"],
                "error": failure["error"],
            }
            for record in records
            for failure in record["cleanup_failures"]
        ]

    def _result_dict(
        self,
        fixture,
        matrix_run_id,
        outcome,
        boundary,
        entries,
        records,
        artifact_set=None,
        capture_verdict="none",
    ):
        result = {
            "schema_version": 1,
            "fixture_id": fixture.fixture_id,
            "run_id": matrix_run_id,
            "outcome": outcome,
            "first_failed_boundary": boundary,
            "scheduled_child_count": len(entries),
            "attempted_child_count": len(records),
            "passed_child_count": sum(
                record["outcome"] == "passed" for record in records
            ),
            "failed_child_count": sum(
                record["outcome"] == "failed" for record in records
            ),
            "cancelled_child_count": sum(
                record["outcome"] == "cancelled" for record in records
            ),
            "cleanup_failures": self._matrix_cleanup_failures(records),
            "schedule_path": "schedule.json",
            "children_path": "children.jsonl",
        }
        if fixture.capture_capability is not model.CaptureCapability.NONE:
            result["verdict"] = capture_verdict if outcome == "passed" else "none"
        if artifact_set is not None:
            result["artifacts"] = artifact_set.evidence()
        return result

    @staticmethod
    def _junit_tree(entries, records, outcome, boundary):
        records_by_key = {
            (record["pass_index"], record["row_index"]): record for record in records
        }
        failures = sum(record["outcome"] == "failed" for record in records)
        skipped = sum(record["outcome"] == "cancelled" for record in records)
        skipped += len(entries) - len(records)
        matrix_error = outcome == "failed" and failures == 0
        suite = ET.Element("testsuite")
        suite.set("name", "hil.rh3.matrix")
        suite.set("tests", str(len(entries) + int(matrix_error)))
        suite.set("failures", str(failures))
        suite.set("skipped", str(skipped))
        suite.set("errors", str(int(matrix_error)))
        stop_reason = MatrixCoordinator._stop_reason(outcome, boundary)
        for entry in entries:
            testcase = ET.SubElement(suite, "testcase")
            testcase.set("name", entry["testcase"])
            testcase.set("classname", "hil.rh3")
            record = records_by_key.get((entry["pass_index"], entry["row_index"]))
            if record is None:
                skipped_node = ET.SubElement(testcase, "skipped")
                skipped_node.set("message", "not attempted: %s" % stop_reason)
                continue
            if record["outcome"] == "failed":
                message = record["first_failed_boundary"] or "child failed"
                failure = ET.SubElement(testcase, "failure")
                failure.set("message", message)
                failure.text = message
            elif record["outcome"] == "cancelled":
                message = record["first_failed_boundary"] or "child cancelled"
                skipped_node = ET.SubElement(testcase, "skipped")
                skipped_node.set("message", message)
        if matrix_error:
            message = "matrix failure: %s" % (boundary or "evidence failure")
            testcase = ET.SubElement(suite, "testcase")
            testcase.set("name", "matrix.orchestration")
            testcase.set("classname", "hil.rh3")
            error = ET.SubElement(testcase, "error")
            error.set("message", message)
            error.text = message
        return ET.ElementTree(suite)

    def _write_result_and_junit(
        self,
        fixture,
        matrix_run_id,
        outcome,
        boundary,
        entries,
        records,
        external_junit_path,
        artifact_set=None,
        capture_verdict="none",
    ):
        write_json_evidence(
            self._matrix_dir,
            "result.json",
            self._result_dict(
                fixture,
                matrix_run_id,
                outcome,
                boundary,
                entries,
                records,
                artifact_set,
                capture_verdict,
            ),
        )
        payload = evidence._serialize_junit(
            self._junit_tree(entries, records, outcome, boundary)
        )
        evidence._atomic_write_text(
            os.path.join(self._matrix_dir, "junit.xml"), payload
        )
        evidence._atomic_write_text(external_junit_path, payload)

    def _finalize(
        self,
        fixture,
        matrix_run_id,
        outcome,
        boundary,
        entries,
        records,
        external_junit_path,
        artifact_set=None,
        capture_verdict="none",
    ):
        self._write_result_and_junit(
            fixture,
            matrix_run_id,
            outcome,
            boundary,
            entries,
            records,
            external_junit_path,
            artifact_set,
            capture_verdict,
        )
        finalize_evidence(
            self._matrix_dir,
            fixture_id=fixture.fixture_id,
            run_id=matrix_run_id,
            capture_capability=fixture.capture_capability.value,
            outcome=outcome,
            artifact_identity=(
                artifact_set.evidence() if artifact_set is not None else None
            ),
        )

    def _record_finalization_failure(
        self,
        fixture,
        matrix_run_id,
        original_error,
        entries,
        records,
        external_junit_path,
        artifact_set=None,
        capture_verdict="none",
    ):
        boundary = "evidence finalization: %s" % original_error
        try:
            self._write_result_and_junit(
                fixture,
                matrix_run_id,
                "failed",
                boundary,
                entries,
                records,
                external_junit_path,
                artifact_set,
                capture_verdict,
            )
        except Exception:
            pass
        try:
            evidence._best_effort_failed_manifest(
                self._matrix_dir,
                fixture.fixture_id,
                matrix_run_id,
                fixture.capture_capability.value,
                artifact_set.evidence() if artifact_set is not None else None,
            )
        except Exception:
            pass

    def run(
        self,
        fixture_path,
        binding_path,
        output_root,
        run_id,
        junit_path,
        argv=None,
        status=0,
        artifacts=None,
        qualification_path=None,
        capture_verdict="none",
    ):
        """Run fixed RH3 schedule and return ``(outcome, boundary, cleanup)``.

        Input validation and no-clobber destination checks happen before matrix
        creation or a child runner.  Once aggregate evidence exists, any later
        setup, child, cleanup, cancellation, or finalization result is retained
        as a software execution outcome.
        """
        fixture = model.load_logical_fixture(fixture_path)
        binding = model.load_physical_binding(binding_path, fixture)
        if fixture.capture_capability is model.CaptureCapability.NONE:
            if qualification_path is not None or capture_verdict != "none":
                raise MatrixError(
                    "none capture fixture cannot use capture qualification"
                )
        else:
            if qualification_path is None:
                raise MatrixError("capture matrix requires accepted qualification")
            if capture_verdict not in (
                "MONO_OUTPUT_SMOKE_ACCEPTED",
                "STEREO_OUTPUT_ACCEPTED",
            ):
                raise MatrixError("capture matrix requires known verdict")
            expected_verdict = (
                "MONO_OUTPUT_SMOKE_ACCEPTED"
                if fixture.capture_capability is model.CaptureCapability.MONO
                else "STEREO_OUTPUT_ACCEPTED"
            )
            if capture_verdict != expected_verdict:
                raise MatrixError("capture verdict does not match fixture capability")
            from hil import qualification

            qualification.load_qualification(qualification_path, fixture, binding)
        if artifacts is not None:
            artifact_resolver.revalidate_artifact_set(artifacts)
        canon_output_root = lifecycle.validate_output_root(output_root)
        lifecycle.validate_run_id(run_id)
        child_root_id, child_root = _validate_child_root(canon_output_root, run_id)
        external_junit_path = _external_junit_path(
            junit_path, canon_output_root, run_id, child_root
        )
        fixture_bytes = self._read_bytes(fixture_path)
        binding_bytes = self._read_bytes(binding_path)

        self._matrix_dir = lifecycle.create_run_dir(canon_output_root, run_id)
        entries = self._schedule(run_id, child_root, artifacts)
        records = []
        outcome = "failed"
        boundary = None
        try:
            child_root = lifecycle.create_run_dir(canon_output_root, child_root_id)
            self._write_bytes(
                os.path.join(self._matrix_dir, "fixture.json"), fixture_bytes
            )
            self._write_bytes(
                os.path.join(self._matrix_dir, "binding.json"), binding_bytes
            )
            if artifacts is not None:
                write_json_evidence(
                    self._matrix_dir, "artifacts.json", artifacts.evidence()
                )
                self._copy_artifacts(artifacts)
            environment = self.deps.environment
            if environment is None:
                environment = lambda a, s: capture_environment(  # noqa: E731
                    lifecycle.default_repo_root(), a, s
                )
            environment_record = environment(argv or [], status)
            if artifacts is not None:
                environment_record = dict(environment_record)
                environment_record["artifacts"] = artifacts.evidence()
            write_json_evidence(
                self._matrix_dir, "environment.json", environment_record
            )
            write_json_evidence(
                self._matrix_dir,
                "schedule.json",
                self._schedule_evidence(entries, run_id, artifacts),
            )
            self._write_jsonl("children.jsonl", records)
            if child_root != entries[0]["child_evidence_path"].rsplit(os.sep, 1)[0]:
                raise MatrixError("child output root changed after schedule creation")

            outcome = "passed"
            for entry in entries:
                if self.deps.cancel():
                    outcome = "cancelled"
                    boundary = "cancelled before pass%d row%d %s" % (
                        entry["pass_index"],
                        entry["row_index"],
                        entry["row_name"],
                    )
                    break
                record = self._invoke_child(
                    entry,
                    fixture_path,
                    binding_path,
                    child_root,
                    argv or [],
                    status,
                    artifacts,
                    qualification_path,
                )
                records.append(record)
                try:
                    self._write_jsonl("children.jsonl", records)
                except Exception as exc:  # noqa: BLE001 - aggregate evidence failure
                    outcome = "failed"
                    boundary = "evidence children: %s" % exc
                    break
                if record["outcome"] != "passed":
                    outcome = record["outcome"]
                    boundary = self._stop_reason(
                        outcome, record["first_failed_boundary"], record
                    )
                    break
            if outcome == "passed" and self.deps.cancel():
                outcome = "cancelled"
                boundary = "cancelled before matrix finalization"
        except Exception as exc:  # noqa: BLE001 - retain partial aggregate evidence
            outcome = "failed"
            boundary = "matrix setup: %s" % exc

        if outcome == "failed":
            try:
                write_json_evidence(
                    self._matrix_dir,
                    "schedule.json",
                    self._schedule_evidence(entries, run_id, artifacts),
                )
            except Exception:
                pass
            try:
                self._write_jsonl("children.jsonl", records)
            except Exception:
                pass

        try:
            self._finalize(
                fixture,
                run_id,
                outcome,
                boundary,
                entries,
                records,
                external_junit_path,
                artifacts,
                capture_verdict,
            )
        except Exception as exc:  # noqa: BLE001 - never lose child evidence
            outcome = "failed"
            boundary = "evidence finalization: %s" % exc
            self._record_finalization_failure(
                fixture,
                run_id,
                str(exc),
                entries,
                records,
                external_junit_path,
                artifacts,
                capture_verdict,
            )
        return outcome, boundary, self._matrix_cleanup_failures(records)

    def _copy_artifacts(self, artifacts):
        """Copy verified original archive bytes into aggregate evidence."""
        for identity, name in (
            (artifacts.receiver, "receiver-artifact.zip"),
            (artifacts.source, "source-artifact.zip"),
        ):
            payload = artifact_resolver.read_verified_archive(identity)
            self._write_bytes(os.path.join(self._matrix_dir, name), payload)
            copied = self._read_bytes(os.path.join(self._matrix_dir, name))
            if hashlib.sha256(copied).hexdigest() != identity.outer_sha256:
                raise MatrixError(
                    "artifact copy verification failed: %s" % identity.kind
                )
