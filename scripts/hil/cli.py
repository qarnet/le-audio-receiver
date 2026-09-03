"""HIL runner CLI for validation, local rows, and immutable RH3/RH4 matrix runs.

Commands:

    hil-runner.py validate --fixture PATH --binding PATH
    hil-runner.py prepare --fixture PATH --binding PATH \
        --output-root PATH --run-id ID
    hil-runner.py run --fixture PATH --binding PATH \
        --output-root PATH --run-id ID --junit PATH
    hil-runner.py run-rh3-matrix --fixture PATH --binding PATH \
        --output-root PATH --run-id ID --junit PATH
    hil-runner.py run-rh4-matrix --fixture PATH --binding PATH \
        --output-root PATH --run-id ID --junit PATH \
        --receiver-artifact PATH --source-artifact PATH

``validate`` parses and cross-validates the files and prints one JSON object
with fixture ID and capture capability; it performs no filesystem mutation.
``prepare`` validates, checks the output root, acquires the exclusive
fixture lock, creates the run directory, copies exact logical/binding JSON
bytes into it, writes a prepared record, finalizes MANIFEST.md and
SHA256SUMS, releases the lock, and prints one JSON object with the run
directory and outcome.  Neither command touches hardware.

``run`` is the production hardware path. It runs exactly one checked-in row;
without ``--row`` it preserves the RH2 short-mono witness. No skip/ignore/
no-flash/arbitrary-command/erase flags exist. SIGINT and SIGTERM request
bounded cancellation (cancelled outcome, retained evidence).

``run-rh3-matrix`` is production path for exactly two checked-in RH3 passes.
It accepts no row, repeat, skip, or recovery override. Each child row owns its
fixture lifecycle; matrix evidence aggregates child outcomes.

``run-rh4-matrix`` resolves exact receiver and source archives before fixture
or hardware work, then runs same fixed schedule with staged image paths only.
Exit status: 0 passed, 1 failed, 130 cancelled.

Failures print one line prefixed ``hil-runner: error: `` to stderr with no
traceback and return 2; success returns 0 with no stderr.
"""

import argparse
import json
import os
import signal
import sys
import tempfile
import threading

from hil import lifecycle, matrix, model, rows, runner
import hil.artifacts as artifact_resolver
from hil.evidence import EvidenceError, finalize_evidence


class HilCliError(Exception):
    """Raised for CLI usage errors (formatted as hil-runner: error:)."""


def _read_bytes(path):
    with open(path, "rb") as fh:
        return fh.read()


def _atomic_write_bytes(path, data):
    fd, tmp = tempfile.mkstemp(
        prefix=".%s.tmp" % os.path.basename(path), dir=os.path.dirname(path)
    )
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def cmd_validate(args):
    fixture = model.load_logical_fixture(args.fixture)
    model.load_physical_binding(args.binding, fixture)
    sys.stdout.write(
        json.dumps(
            {
                "fixture_id": fixture.fixture_id,
                "capture_capability": fixture.capture_capability.value,
            },
            sort_keys=True,
        )
        + "\n"
    )
    return 0


def cmd_prepare(args):
    fixture = model.load_logical_fixture(args.fixture)
    model.load_physical_binding(args.binding, fixture)
    canon = lifecycle.validate_output_root(args.output_root)
    lifecycle.validate_run_id(args.run_id)
    fixture_bytes = _read_bytes(args.fixture)
    binding_bytes = _read_bytes(args.binding)
    with lifecycle.CleanupStack() as stack:
        lifecycle.FixtureLock.acquire(canon, fixture.fixture_id, stack)
        run_dir = lifecycle.create_run_dir(canon, args.run_id)
        _atomic_write_bytes(os.path.join(run_dir, "fixture.json"), fixture_bytes)
        _atomic_write_bytes(os.path.join(run_dir, "binding.json"), binding_bytes)
        prepared = {
            "schema_version": model.SCHEMA_VERSION,
            "fixture_id": fixture.fixture_id,
            "run_id": args.run_id,
            "capture_capability": fixture.capture_capability.value,
            "outcome": "prepared",
        }
        _atomic_write_bytes(
            os.path.join(run_dir, "prepared.json"),
            json.dumps(prepared, sort_keys=True).encode("utf-8") + b"\n",
        )
        finalize_evidence(
            run_dir,
            fixture_id=fixture.fixture_id,
            run_id=args.run_id,
            capture_capability=fixture.capture_capability.value,
            outcome="prepared",
        )
    sys.stdout.write(
        json.dumps({"run_directory": run_dir, "outcome": "prepared"}, sort_keys=True)
        + "\n"
    )
    return 0


def _selected_row(name):
    """Resolve one stable checked-in row name into immutable runner input."""
    if name is None:
        return rows.RH2_ROW
    try:
        return rows.get_row(name)
    except rows.RowSpecError as exc:
        raise HilCliError(str(exc)) from exc


def cmd_run(args):
    """Production hardware path: run exactly one frozen row. SIGINT/SIGTERM
    request bounded cancellation. Returns process status (0 passed, 1 failed,
    130 cancelled)."""
    cancel_event = threading.Event()
    deps = runner.RunnerDeps(
        repo_root=lifecycle.default_repo_root(),
        cancel=cancel_event.is_set,
    )
    engine = runner.Runner(deps)
    argv = list(sys.argv)
    row = _selected_row(getattr(args, "row", None))

    def _request_cancel(signum, _frame):
        cancel_event.set()

    old_int = signal.signal(signal.SIGINT, _request_cancel)
    old_term = signal.signal(signal.SIGTERM, _request_cancel)
    try:
        outcome, _boundary, _cleanup = engine.run(
            args.fixture,
            args.binding,
            args.output_root,
            args.run_id,
            args.junit,
            argv=argv,
            status=0,
            row=row,
            hci_remove_iso_path_trace=getattr(args, "hci_remove_iso_path_trace", False),
            sdc_hci_remove_iso_path_trace=getattr(
                args, "sdc_hci_remove_iso_path_trace", False
            ),
        )
    finally:
        signal.signal(signal.SIGINT, old_int)
        signal.signal(signal.SIGTERM, old_term)
    if outcome == "passed":
        return 0
    if outcome == "cancelled":
        return 130
    return 1


def cmd_run_rh3_matrix(args):
    """Run fixed two-pass RH3 matrix with bounded signal cancellation."""
    cancel_event = threading.Event()
    engine = matrix.MatrixCoordinator(matrix.MatrixDeps(cancel=cancel_event.is_set))
    argv = list(sys.argv)

    def _request_cancel(signum, _frame):
        cancel_event.set()

    old_int = signal.signal(signal.SIGINT, _request_cancel)
    old_term = signal.signal(signal.SIGTERM, _request_cancel)
    try:
        outcome, _boundary, _cleanup = engine.run(
            args.fixture,
            args.binding,
            args.output_root,
            args.run_id,
            args.junit,
            argv=argv,
            status=0,
        )
    finally:
        signal.signal(signal.SIGINT, old_int)
        signal.signal(signal.SIGTERM, old_term)
    if outcome == "passed":
        return 0
    if outcome == "cancelled":
        return 130
    return 1


def _capture_matrix_command(args, capability, verdict):
    """Run fixed local-image RH3 schedule with mandatory accepted capture proof."""
    cancel_event = threading.Event()
    engine = matrix.MatrixCoordinator(matrix.MatrixDeps(cancel=cancel_event.is_set))
    argv = list(sys.argv)

    def _request_cancel(signum, _frame):
        del signum, _frame
        cancel_event.set()

    old_int = signal.signal(signal.SIGINT, _request_cancel)
    old_term = signal.signal(signal.SIGTERM, _request_cancel)
    try:
        fixture = model.load_logical_fixture(args.fixture)
        if fixture.capture_capability.value != capability:
            raise HilCliError(
                "%s matrix requires %s capture fixture"
                % (args.command.replace("run-", "").replace("-matrix", ""), capability)
            )
        outcome, _boundary, _cleanup = engine.run(
            args.fixture,
            args.binding,
            args.output_root,
            args.run_id,
            args.junit,
            argv=argv,
            status=0,
            qualification_path=args.qualification,
            capture_verdict=verdict,
        )
    finally:
        signal.signal(signal.SIGINT, old_int)
        signal.signal(signal.SIGTERM, old_term)
    if outcome == "passed":
        return 0
    if outcome == "cancelled":
        return 130
    return 1


def cmd_run_ma1_matrix(args):
    return _capture_matrix_command(args, "mono", "MONO_OUTPUT_SMOKE_ACCEPTED")


def cmd_run_sa1_matrix(args):
    return _capture_matrix_command(args, "stereo", "STEREO_OUTPUT_ACCEPTED")


def cmd_run_rh4_matrix(args):
    """Resolve exact archives once, then run fixed RH4 matrix."""
    artifact_set = None
    outcome = None
    cleanup_error = None
    cancel_event = threading.Event()
    engine = matrix.MatrixCoordinator(matrix.MatrixDeps(cancel=cancel_event.is_set))
    argv = list(sys.argv)

    def _request_cancel(signum, _frame):
        del signum
        cancel_event.set()

    old_int = signal.signal(signal.SIGINT, _request_cancel)
    old_term = signal.signal(signal.SIGTERM, _request_cancel)
    try:
        artifact_set = artifact_resolver.resolve_artifacts(
            args.receiver_artifact, args.source_artifact
        )
        outcome, _boundary, _cleanup = engine.run(
            args.fixture,
            args.binding,
            args.output_root,
            args.run_id,
            args.junit,
            argv=argv,
            status=0,
            artifacts=artifact_set,
        )
    finally:
        signal.signal(signal.SIGINT, old_int)
        signal.signal(signal.SIGTERM, old_term)
        if artifact_set is not None:
            try:
                artifact_resolver.cleanup_artifacts(artifact_set)
            except Exception as exc:  # noqa: BLE001 - teardown must change verdict
                cleanup_error = exc
    if cleanup_error is not None:
        raise HilCliError("artifact staging cleanup failed: %s" % cleanup_error)
    if outcome == "passed":
        return 0
    if outcome == "cancelled":
        return 130
    return 1


class _HilArgumentParser(argparse.ArgumentParser):
    """ArgumentParser whose usage errors become hil-runner: error: lines."""

    def error(self, message):
        raise HilCliError(message)


def build_parser():
    parser = _HilArgumentParser(
        prog="hil-runner.py",
        description="system HIL runner (host-only validation; frozen row run)",
    )
    sub = parser.add_subparsers(dest="command", required=True)
    validate = sub.add_parser("validate", help="validate fixture and binding")
    validate.add_argument("--fixture", required=True)
    validate.add_argument("--binding", required=True)
    validate.set_defaults(func=cmd_validate)
    prepare = sub.add_parser("prepare", help="prepare a run directory")
    prepare.add_argument("--fixture", required=True)
    prepare.add_argument("--binding", required=True)
    prepare.add_argument("--output-root", required=True)
    prepare.add_argument("--run-id", required=True)
    prepare.set_defaults(func=cmd_prepare)
    run = sub.add_parser("run", help="run one frozen HIL row (production hardware)")
    run.add_argument("--fixture", required=True)
    run.add_argument("--binding", required=True)
    run.add_argument("--output-root", required=True)
    run.add_argument("--run-id", required=True)
    run.add_argument("--junit", required=True)
    run.add_argument(
        "--row",
        choices=rows.row_names(),
        help="checked-in row name (default: rh2.short_mono_48_4_1)",
    )
    trace_group = run.add_mutually_exclusive_group()
    trace_group.add_argument(
        "--hci-remove-iso-path-trace",
        action="store_true",
        help="retain callback-timed bt_hci_core and bt_sdc_hci_driver trace evidence",
    )
    trace_group.add_argument(
        "--sdc-hci-remove-iso-path-trace",
        action="store_true",
        help="retain direct SDC LE Remove ISO Data Path wrapper trace evidence",
    )
    run.set_defaults(func=cmd_run)
    matrix_run = sub.add_parser(
        "run-rh3-matrix", help="run fixed two-pass RH3 matrix (production hardware)"
    )
    matrix_run.add_argument("--fixture", required=True)
    matrix_run.add_argument("--binding", required=True)
    matrix_run.add_argument("--output-root", required=True)
    matrix_run.add_argument("--run-id", required=True)
    matrix_run.add_argument("--junit", required=True)
    matrix_run.set_defaults(func=cmd_run_rh3_matrix)
    ma1_matrix_run = sub.add_parser(
        "run-ma1-matrix",
        help="run fixed two-pass MA1 mono-capture matrix (production hardware)",
    )
    ma1_matrix_run.add_argument("--fixture", required=True)
    ma1_matrix_run.add_argument("--binding", required=True)
    ma1_matrix_run.add_argument("--qualification", required=True)
    ma1_matrix_run.add_argument("--output-root", required=True)
    ma1_matrix_run.add_argument("--run-id", required=True)
    ma1_matrix_run.add_argument("--junit", required=True)
    ma1_matrix_run.set_defaults(func=cmd_run_ma1_matrix)
    sa1_matrix_run = sub.add_parser(
        "run-sa1-matrix",
        help="run fixed two-pass SA1 stereo-capture matrix (production hardware)",
    )
    sa1_matrix_run.add_argument("--fixture", required=True)
    sa1_matrix_run.add_argument("--binding", required=True)
    sa1_matrix_run.add_argument("--qualification", required=True)
    sa1_matrix_run.add_argument("--output-root", required=True)
    sa1_matrix_run.add_argument("--run-id", required=True)
    sa1_matrix_run.add_argument("--junit", required=True)
    sa1_matrix_run.set_defaults(func=cmd_run_sa1_matrix)
    rh4_matrix_run = sub.add_parser(
        "run-rh4-matrix",
        help="run fixed artifact-backed RH4 matrix (production hardware)",
    )
    rh4_matrix_run.add_argument("--fixture", required=True)
    rh4_matrix_run.add_argument("--binding", required=True)
    rh4_matrix_run.add_argument("--output-root", required=True)
    rh4_matrix_run.add_argument("--run-id", required=True)
    rh4_matrix_run.add_argument("--junit", required=True)
    rh4_matrix_run.add_argument("--receiver-artifact", required=True)
    rh4_matrix_run.add_argument("--source-artifact", required=True)
    rh4_matrix_run.set_defaults(func=cmd_run_rh4_matrix)
    return parser


def main(argv=None):
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
        return args.func(args)
    except HilCliError as exc:
        print("hil-runner: error: %s" % exc, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("hil-runner: error: interrupted", file=sys.stderr)
        return 2
    except Exception as exc:  # noqa: BLE001 - one-line error, no traceback
        print("hil-runner: error: %s" % exc, file=sys.stderr)
        return 2
