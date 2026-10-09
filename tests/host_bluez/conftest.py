"""Independent actual pytest collection for PB-053 external accounting."""

import json
import os
from pathlib import Path

import pytest


REPO = Path(__file__).resolve().parents[2]
MODULE = REPO / "tests/host_bluez/test_host_lane.py"


def pytest_configure(config):
    if pytest.__version__ != "8.4.2":
        raise pytest.UsageError(f"Pinned pytest 8.4.2 required: {pytest.__version__}")
    for key in ("PB053_SUITE_ROOT", "PB053_PREPARED_ROOT", "PB053_MANIFEST_SHA256"):
        if not os.environ.get(key):
            raise pytest.UsageError(f"Missing required environment: {key}")
    root = Path(os.environ["PB053_SUITE_ROOT"])
    if not root.is_absolute() or not root.is_dir() or root.is_symlink():
        raise pytest.UsageError("PB053_SUITE_ROOT must be existing exclusive directory")
    if not Path(os.environ["PB053_PREPARED_ROOT"]).is_dir():
        raise pytest.UsageError("PB053_PREPARED_ROOT missing")
    digest = os.environ["PB053_MANIFEST_SHA256"]
    if len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
        raise pytest.UsageError("Invalid PB053_MANIFEST_SHA256")


def pytest_collection_finish(session):
    observed = []
    nodeids = []
    for item in session.items:
        path = Path(str(item.path)).resolve()
        if path != MODULE or len(item.nodeid.split("::")) != 2:
            raise pytest.UsageError(f"Unexpected PB-053 selection: {item.nodeid}")
        classname = path.relative_to(REPO).with_suffix("").as_posix().replace("/", ".")
        observed.append([classname, item.nodeid.split("::", 1)[1]])
        nodeids.append(item.nodeid)
    root = Path(os.environ["PB053_SUITE_ROOT"])
    try:
        with (root / "collection.json").open("x", encoding="utf-8") as output:
            json.dump(
                {
                    "pytest_version": pytest.__version__,
                    "nodeids": nodeids,
                    "discovered_cases": observed,
                },
                output,
                indent=2,
            )
            output.write("\n")
    except OSError as exc:
        raise pytest.UsageError(f"PB-053 collection output failed: {exc}") from exc
