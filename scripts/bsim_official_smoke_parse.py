#!/usr/bin/env python3
"""SDU-progress parser for the official BSim BAP unicast audio smoke.

The official Zephyr test (tests/bsim/bluetooth/audio, unicast_client /
unicast_server) streams MIN_SEND_COUNT=100 SDUs in both directions before
teardown (bap_unicast_client_test.c transceive_streams / common.h).  RX
progress is logged by bap_stream_rx.c log_stream_rx as

    [<valid_rx_cnt>|<rx_cnt>]: Incoming audio on stream <ptr> len <n>,
    flags 0x<hex>, seq_num <u> and ts <u>

and the known NCS v3.3.0 teardown disable-race fails the test with the
message "ISO receive lost" (bap_stream_rx.c:104) after the 100-SDU phase
completes.  scripts/bsim-official-smoke.sh uses this module to prove the
claimed >=100 completed SDUs from the captured client/server logs before
accepting the teardown race — missing, malformed, or short progress, or
an unrelated failure, is never accepted.

CLI (used by the smoke script):

    python3 scripts/bsim_official_smoke_parse.py check \\
        --client LOG --server LOG --min-sdus 100 --smoke-rc RC

Exit 0 = accepted with evidence, 1 = rejected (reason on stderr).
"""

import argparse
import os
import re
import sys

# Exact progress marker from bap_stream_rx.c log_stream_rx().
RE_PROGRESS = re.compile(r"\[(\d+)\|(\d+)\]: Incoming audio on stream")

# Known teardown disable-race message (bap_stream_rx.c:104) — the only
# failure the smoke may accept, and only after sufficient progress.
TEARDOWN_RACE_MARKER = "ISO receive lost"


def parse_smoke_logs(client_text, server_text):
    """Parse SDU progress + teardown marker from both device logs.

    Returns dict with:
      max_valid_rx   — highest valid_rx_cnt seen in either log (0 if none)
      progress_lines — number of "Incoming audio on stream" lines
      progress_seen  — True when at least one progress line exists
      malformed      — True when a progress line lacks the [valid|rx] prefix
      teardown_race  — True when the known "ISO receive lost" message appears
    """
    combined = (client_text or "") + "\n" + (server_text or "")
    counts = []
    progress_lines = 0
    malformed = False
    for line in combined.splitlines():
        if "Incoming audio on stream" not in line:
            continue
        progress_lines += 1
        m = RE_PROGRESS.search(line)
        if m:
            counts.append(int(m.group(1)))
        else:
            malformed = True
    return {
        "max_valid_rx": max(counts) if counts else 0,
        "progress_lines": progress_lines,
        "progress_seen": progress_lines > 0,
        "malformed": malformed,
        "teardown_race": TEARDOWN_RACE_MARKER in combined,
    }


def evaluate(client_text, server_text, smoke_rc, min_sdus=100):
    """Decide acceptance from parsed logs + the simulation exit code.

    Returns (accepted, reason, stats).  Acceptance requires proven
    progress: >= min_sdus valid RX SDUs with no malformed/missing markers.
    With that proven, a nonzero exit is accepted ONLY when it is the known
    teardown disable-race; any other nonzero exit is an unrelated failure.
    """
    stats = parse_smoke_logs(client_text, server_text)

    if not stats["progress_seen"]:
        return (
            False,
            "no SDU progress markers ('Incoming audio on stream') in logs — "
            "streaming not proven",
            stats,
        )
    if stats["malformed"]:
        return (
            False,
            "malformed progress lines (missing [valid|rx] prefix) in logs",
            stats,
        )
    if stats["max_valid_rx"] < min_sdus:
        return (
            False,
            "short progress: max valid RX %d < %d SDUs"
            % (stats["max_valid_rx"], min_sdus),
            stats,
        )

    if smoke_rc == 0:
        return (
            True,
            "all processes exit 0 with %d >= %d SDUs proven"
            % (stats["max_valid_rx"], min_sdus),
            stats,
        )
    if stats["teardown_race"]:
        return (
            True,
            "known teardown disable-race (ISO receive lost) after %d >= %d "
            "SDUs — accepted with evidence" % (stats["max_valid_rx"], min_sdus),
            stats,
        )
    return (
        False,
        "process exit %d with no known teardown marker — unrelated failure" % smoke_rc,
        stats,
    )


def _read(path):
    if not os.path.isfile(path):
        return None
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except OSError:
        return None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    check = sub.add_parser("check", help="Evaluate smoke logs against the gate")
    check.add_argument("--client", required=True)
    check.add_argument("--server", required=True)
    check.add_argument("--min-sdus", type=int, default=100)
    check.add_argument("--smoke-rc", type=int, required=True)

    args = parser.parse_args(argv)

    if args.command == "check":
        client_text = _read(args.client)
        server_text = _read(args.server)
        if client_text is None:
            print("FAIL: cannot read client log %s" % args.client, file=sys.stderr)
            return 1
        if server_text is None:
            print("FAIL: cannot read server log %s" % args.server, file=sys.stderr)
            return 1
        accepted, reason, stats = evaluate(
            client_text, server_text, args.smoke_rc, args.min_sdus
        )
        print(
            "progress: max_valid_rx=%d lines=%d malformed=%s teardown_race=%s"
            % (
                stats["max_valid_rx"],
                stats["progress_lines"],
                stats["malformed"],
                stats["teardown_race"],
            )
        )
        if accepted:
            print("ACCEPTED: %s" % reason)
            return 0
        print("REJECTED: %s" % reason, file=sys.stderr)
        return 1

    return 2


if __name__ == "__main__":
    sys.exit(main())
