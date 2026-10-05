#!/usr/bin/env python3
"""Fail-closed PB-051 policy, public trace and owned execution check."""

import argparse
import json
import sys

from ascs_results import (
    EXECUTION_LIMIT,
    LOG_LIMIT,
    POLICY_LIMIT,
    TraceError,
    _regular_snapshot,
    check_execution_record,
    check_family_logs,
    load_inventory,
)


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise TraceError(f"arguments: {message}")


def main(argv=None):
    try:
        parser = Parser(description=__doc__, add_help=False)
        for name in (
            "inventory",
            "expected-inventory-sha256",
            "family",
            "client-log",
            "receiver-log",
            "execution-record",
            "expected-run-id",
        ):
            parser.add_argument("--" + name, required=True)
        args = parser.parse_args(argv)
        policy = load_inventory(
            _regular_snapshot(args.inventory, POLICY_LIMIT),
            args.expected_inventory_sha256,
        )
        # Read CLI-named raw logs independently and require same identity as owned record.
        client = _regular_snapshot(args.client_log, LOG_LIMIT)
        receiver = _regular_snapshot(args.receiver_log, LOG_LIMIT)
        execution = check_execution_record(
            _regular_snapshot(args.execution_record, EXECUTION_LIMIT),
            policy,
            args.family,
            args.expected_run_id,
            args.client_log,
            args.receiver_log,
        )
        if client != execution.pop("client") or receiver != execution.pop("receiver"):
            raise TraceError("CLI peer bytes differ from owned snapshots")
        trace = check_family_logs(policy, args.family, client, receiver)
        print(
            json.dumps(
                {
                    "accepted": True,
                    "family": args.family,
                    "run_id": args.expected_run_id,
                    "trace": trace,
                    "execution": execution,
                },
                sort_keys=True,
            )
        )
        return 0
    except (TraceError, ValueError, OSError, KeyError, TypeError) as exc:
        print(
            json.dumps(
                {"accepted": False, "errors": [f"{type(exc).__name__}: {exc}"]},
                sort_keys=True,
            )
        )
        return 1


if __name__ == "__main__":
    sys.exit(main())
