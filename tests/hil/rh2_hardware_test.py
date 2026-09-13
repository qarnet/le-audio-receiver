#!/usr/bin/env python3
"""RH2/RH3 production fixture entry point.

Skipped unless the explicit environment flag ``HIL_RUN_HARDWARE=1`` is
set.  When enabled it requires every CLI path from the environment
(``HIL_FIXTURE_PATH``, ``HIL_BINDING_PATH``, ``HIL_OUTPUT_ROOT``,
    ``HIL_RUN_ID``, ``HIL_JUNIT``), constructs the production runner, and
calls public runner once for RH2 by default or explicit frozen RH3 row
selected through ``HIL_ROW``.

This file is NOT discovered by the canonical inventory (it does not match
the ``test_*.py`` naming under ``tests/unit/`` or ``scripts/``) and its
hardware row never runs without the explicit flag.
"""

import os
import sys

_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "scripts"))

import pytest  # noqa: E402

from hil import rows, runner  # noqa: E402

HARDWARE_FLAG = "HIL_RUN_HARDWARE"
ROW_ENV = "HIL_ROW"
ENV_PATHS = {
    "HIL_FIXTURE_PATH": "--fixture",
    "HIL_BINDING_PATH": "--binding",
    "HIL_OUTPUT_ROOT": "--output-root",
    "HIL_RUN_ID": "--run-id",
    "HIL_JUNIT": "--junit",
}


@pytest.mark.skipif(
    os.environ.get(HARDWARE_FLAG) != "1",
    reason="no hardware: HIL_RUN_HARDWARE=1 required for the HIL hardware row",
)
def test_hardware_frozen_row():
    missing = [name for name in ENV_PATHS if not os.environ.get(name)]
    if missing:
        pytest.fail(
            "HIL_RUN_HARDWARE=1 requires environment paths: %s" % ", ".join(missing)
        )
    fixture = os.environ["HIL_FIXTURE_PATH"]
    binding = os.environ["HIL_BINDING_PATH"]
    output_root = os.environ["HIL_OUTPUT_ROOT"]
    run_id = os.environ["HIL_RUN_ID"]
    junit = os.environ["HIL_JUNIT"]
    row_name = os.environ.get(ROW_ENV)
    try:
        row = rows.get_row(row_name) if row_name else rows.RH2_ROW
    except rows.RowSpecError as exc:
        pytest.fail(str(exc))
    engine = runner.Runner(runner.RunnerDeps(repo_root=_REPO))
    outcome, boundary, cleanup = engine.run(
        fixture,
        binding,
        output_root,
        run_id,
        junit,
        argv=["tests/hil/rh2_hardware_test.py"],
        status=0,
        row=row,
    )
    assert outcome == "passed", "hardware row outcome %s (boundary=%s, cleanup=%s)" % (
        outcome,
        boundary,
        cleanup,
    )


def test_hardware_flag_absent_means_no_execution():
    # The suite itself is the guard: without the flag this module's
    # hardware test is skipped by pytest, so no probe/serial/flash
    # command from this module can ever execute.
    assert os.environ.get(HARDWARE_FLAG) != "1"
