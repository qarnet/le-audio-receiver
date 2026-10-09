#!/usr/bin/env python3
"""Fail-closed canonical BSim build diagnostic check on retained raw logs."""

import argparse
import hashlib
import json
import sys
from pathlib import Path

from ascs_bsim_run import (
    BUILD_CAP,
    CONFIGURE_LOG_LIMIT,
    _regular_snapshot,
    inspect_warnings,
    sdk_root,
    write_json,
)


SOURCES = {
    "cmake.out": BUILD_CAP,
    "ninja.out": BUILD_CAP,
    "resolved.config": 2 * 1024 * 1024,
    "cmake-configure.yaml": CONFIGURE_LOG_LIMIT,
}


def check(root, role):
    area = root / "build" / role
    verdict = {
        "schema_version": 1,
        "role": role,
        "accepted": False,
        "files": {},
        "errors": [],
        "claim": "canonical native BSim build diagnostics only; scenarios checked separately",
    }
    try:
        if not root.is_absolute() or not root.is_dir() or not area.is_dir():
            raise ValueError("external retained build evidence root/role absent")
        sdk = sdk_root()
        raw = {}
        for name, limit in SOURCES.items():
            path = area / name
            raw[name] = _regular_snapshot(str(path), limit)
            verdict["files"][name] = {
                "path": str(path),
                "bytes": len(raw[name]),
                "sha256": hashlib.sha256(raw[name]).hexdigest(),
            }
        config = raw["resolved.config"].decode("utf-8", "strict")
        required = (
            "CONFIG_BT_LL_SW_SPLIT=y",
            "CONFIG_COVERAGE=y",
            "CONFIG_ASSERT=y",
            "CONFIG_COMPILER_WARNINGS_AS_ERRORS=y",
            "CONFIG_BT_CTLR_PERIPHERAL_ISO=y"
            if role == "receiver"
            else "CONFIG_BT_CTLR_CENTRAL_ISO=y",
        )
        if not all(line + "\n" in config for line in required):
            raise ValueError(
                f"{role}: resolved native controller/coverage profile drift"
            )
        probes = inspect_warnings(
            raw["cmake.out"], raw["ninja.out"], role, sdk, raw["cmake-configure.yaml"]
        )
        verdict["capability_probe_dispositions"] = probes
        verdict["sdk_root"] = str(sdk)
        verdict["accepted"] = True
    except (OSError, ValueError, KeyError, UnicodeError) as exc:
        verdict["errors"].append(f"{type(exc).__name__}: {exc}")
    if area.is_dir():
        write_json(area / "build-warning-verdict.json", verdict)
    return verdict


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log-root", required=True)
    parser.add_argument("--role", required=True, choices=("receiver", "client"))
    args = parser.parse_args(argv)
    result = check(Path(args.log_root), args.role)
    print(json.dumps(result, sort_keys=True))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    sys.exit(main())
