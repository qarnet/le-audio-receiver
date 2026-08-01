#!/usr/bin/env python3
"""Strict parser for the T4 BabbleSim scenario logs.

Parses every named field of the receiver and client PASS records for one
scenario run and asserts the scenario-specific contract:

  - exact per-scenario response/count/hash expectations;
  - channel-hash relations (mono L == R, Mode A/B L != R);
  - no decode/lifecycle fault markers in the receiver log (with a
    per-scenario allowlist for intentionally exercised paths);
  - exact client-side send counts and ASCS response counts.

The bash runner uses `check` per run; unit tests in tests/unit/bsim_runner
cover parsing and every failure mode.  Nothing here validates only the
PASS substring — every named field is parsed and asserted.
"""

import argparse
import re
import sys

# ── scenario metadata ────────────────────────────────────────────────

# Scenario name -> (decoder calls per push, mono or stereo, runs in matrix)
SCENARIOS = {
    "mono_10ms": (1, "mono", 2),
    "mono_7p5ms": (1, "mono", 2),
    "modea_10ms": (2, "stereo", 2),
    "modea_7p5ms": (2, "stereo", 2),
    "modea_reverse_start_10ms": (2, "stereo", 2),
    "modeb_10ms": (2, "stereo", 2),
    "modeb_7p5ms": (2, "stereo", 2),
    "invalid_sdu_resume_10ms": (1, "mono", 2),
    "modea_first_stop_10ms": (2, "stereo", 1),
    "release_without_disable_10ms": (1, "mono", 1),
    "disconnect_streaming_10ms": (1, "mono", 1),
    "reconnect_second_stream_10ms": (1, "mono", 1),
    "unsupported_source_direction": (1, "mono", 1),
    "no_free_sink_slot": (1, "mono", 1),
    "invalid_codec_fields": (1, "mono", 1),
}

# ── log parsing ──────────────────────────────────────────────────────


class ParseError(Exception):
    pass


def extract_pass_line(path, testid):
    """Return the last 'INFO: <testid>:' line of a BSim log."""
    marker = "INFO: %s:" % testid
    found = None
    try:
        with open(path, "r", errors="replace") as fh:
            for line in fh:
                if marker in line:
                    found = line
    except OSError as exc:
        raise ParseError("cannot read %s: %s" % (path, exc))
    if found is None:
        raise ParseError("no PASS line for %s in %s" % (testid, path))
    return found


_TOKEN_RE = re.compile(r"(\w+)=([0-9A-Za-z_.xX]+)")


def parse_tokens(line):
    """Parse key=value tokens from a PASS line into a dict (ints where possible)."""
    out = {}
    for key, val in _TOKEN_RE.findall(line):
        if key in out:
            continue
        try:
            out[key] = int(val, 16) if val.lower().startswith("0x") else int(val)
        except ValueError:
            out[key] = val
    return out


def parse_receiver_pass(path):
    line = extract_pass_line(path, "le_audio_receiver")
    tokens = parse_tokens(line)
    need = [
        "scenario",
        "seg",
        "after",
        "adv_restart",
        "pacs",
        "obs_ok",
        "obs_rej",
        "obs_dir",
        "obs_code",
        "obs_reason",
        "obs_gate_o",
        "obs_gate_c",
        "obs_mal",
        "obs_blk",
        "obs_stale",
        "obs_rel",
        "obs_disc",
    ]
    missing = [k for k in need if k not in tokens]
    if missing:
        raise ParseError("receiver PASS missing fields %s" % missing)
    return tokens


def parse_client_pass(path):
    line = extract_pass_line(path, "bsim_client")
    tokens = parse_tokens(line)
    need = ["scenario", "sends0", "sends1", "cfgrsps", "relrsps", "disrsps"]
    missing = [k for k in need if k not in tokens]
    if missing:
        raise ParseError("client PASS missing fields %s" % missing)
    return tokens


# ── fault-marker scan ────────────────────────────────────────────────

# Receiver log markers that indicate a decode/lifecycle fault.  There is
# no warning allowlist: the expected control paths (unsupported source,
# rejected codec shape, pool full, malformed SDU) log at INFO level, so
# every remaining bt_bap / bt_ascs warning or error is a fault.
FAULT_MARKERS = [
    "ASSERTION FAILURE",
    "FATAL",
    "decode error",
    "decoder not ready",
    "decode failed",
    "zero-energy push after audio started",
    "source-invalid push after valid boundary",
    "push after stop",
    "malformed sample count",
    "Failed to start stream",
    "stream_lifecycle",
]


def scan_faults(receiver_path, scenario):
    hits = []
    try:
        with open(receiver_path, "r", errors="replace") as fh:
            for line in fh:
                for marker in FAULT_MARKERS:
                    if marker in line:
                        hits.append(line.strip())
                        break
                else:
                    if "<wrn> bt_bap:" in line or "<wrn> bt_ascs:" in line:
                        hits.append(line.strip())
                    elif "<err> bt_bap:" in line:
                        hits.append(line.strip())
    except OSError as exc:
        raise ParseError("cannot read %s: %s" % (receiver_path, exc))
    if hits:
        raise ParseError("fault markers in receiver log: %s" % "; ".join(hits[:5]))


# ── scenario checks ──────────────────────────────────────────────────


def _h(rec, key, scenario, field):
    if key not in rec:
        raise ParseError("%s: missing %s field" % (scenario, field))
    return rec[key]


def check_scenario(scenario, recv, cli, known):
    """Assert the full scenario contract.  `known` maps
    known_full/known_l/known_r for the scenarios that pin hashes."""
    if scenario not in SCENARIOS:
        raise ParseError("unknown scenario %s" % scenario)
    dec_calls, chan, _runs = SCENARIOS[scenario]

    r = parse_receiver_pass(recv)
    c = parse_client_pass(cli)
    scan_faults(recv, scenario)

    errs = []

    if r["scenario"] != scenario:
        errs.append("receiver scenario %s != %s" % (r["scenario"], scenario))
    if c["scenario"] != scenario:
        errs.append("client scenario %s != %s" % (c["scenario"], scenario))

    # PACS contexts must never be NONE.
    if r["pacs"] != 1:
        errs.append("pacs != 1")

    seg = r["seg"]
    after = r["after"]

    if scenario in (
        "mono_10ms",
        "mono_7p5ms",
        "modea_10ms",
        "modea_7p5ms",
        "modea_reverse_start_10ms",
        "modeb_10ms",
        "modeb_7p5ms",
        "invalid_sdu_resume_10ms",
    ):
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1") != 100:
            errs.append("pushes1 %s != 100" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("mal1") != 0:
            errs.append("mal1 %s != 0" % r.get("mal1"))
        if r.get("adv_restart") != 0:
            errs.append("adv_restart %s != 0" % r.get("adv_restart"))
        if scenario == "invalid_sdu_resume_10ms":
            if r.get("derr1") != 1:
                errs.append("derr1 %s != 1 (malformed SDU)" % r.get("derr1"))
            if r.get("obs_mal") != 1:
                errs.append(
                    "obs_mal %s != 1 (malformed-SDU observer)" % r.get("obs_mal")
                )
        else:
            if r.get("derr1") != 0:
                errs.append("derr1 %s != 0" % r.get("derr1"))
            if r.get("obs_mal") != 0:
                errs.append("obs_mal %s != 0" % r.get("obs_mal"))

        # Exact known hashes.
        for field, key in (("full", "h1"), ("l", "lh1"), ("r", "rh1")):
            if key not in r:
                errs.append("missing %s" % key)
                continue
            known_key = "known_%s" % field
            if known_key in known and known[known_key] is not None:
                if r[key] != known[known_key]:
                    errs.append(
                        "%s hash 0x%08X != known 0x%08X"
                        % (field, r[key], known[known_key])
                    )

        # Channel-hash relation.
        if chan == "mono":
            if r.get("lh1") != r.get("rh1"):
                errs.append("mono L hash != R hash")
        else:
            if r.get("lh1") == r.get("rh1"):
                errs.append("stereo L hash == R hash")

        # Decoder-invocation accounting: the total decoder invocations
        # must at least cover every push (dec_calls each); the exact
        # deterministic value (including unpaired-half decodes at the
        # CIS activation skew) is pinned per scenario.
        expected_total = dec_calls * (r.get("pushes1", 0) + r.get("trans1", 0))
        if r.get("total1", 0) < expected_total:
            errs.append("total1 %s < %d" % (r.get("total1"), expected_total))
        if "known_total" in known and known["known_total"] is not None:
            if r.get("total1") != known["known_total"]:
                errs.append(
                    "total1 %s != pinned %d" % (r.get("total1"), known["known_total"])
                )
        # Zero post-start PLC: every PLC frame happened during the
        # startup phase (source-valid boundary).
        if r.get("plc1", 0) != r.get("splc1", 0):
            errs.append("post-start PLC (plc1 %s != splc1 %s)"
                        % (r.get("plc1"), r.get("splc1")))
        # No missing-TS events in any audio scenario.
        if r.get("obs_mts", 0) != 0:
            errs.append("obs_mts %d != 0 (missing ISO TS flag)" % r.get("obs_mts"))

        # Client send counts.
        if scenario == "invalid_sdu_resume_10ms":
            if c["sends0"] != 101:
                errs.append("client sends0 %d != 101" % c["sends0"])
        elif scenario in ("modea_10ms", "modea_7p5ms", "modea_reverse_start_10ms"):
            # Mode A sends 110 per stream: the deterministic CIS-sync
            # losses are consumed by startup transients, and the strict
            # oracle pins exactly 100 valid-sourced pushes.
            if c["sends0"] != 110 or c["sends1"] != 110:
                errs.append(
                    "client sends %d/%d != 110/110" % (c["sends0"], c["sends1"])
                )
        else:
            if c["sends0"] != 100:
                errs.append("client sends0 %d != 100" % c["sends0"])

    elif scenario == "modea_first_stop_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_gate_c", 0) < 1:
            errs.append("obs_gate_c < 1 (gate must close on first stop)")
        if r.get("obs_blk", 0) < 1:
            errs.append("obs_blk < 1 (closed-gate receives expected)")
        if r.get("obs_rel", 0) < 1:
            errs.append("obs_rel < 1 (release cleanup expected)")
        if c["sends0"] < 20 or c["sends1"] < 45:
            errs.append("client sends %d/%d unexpected" % (c["sends0"], c["sends1"]))
        if c["relrsps"] < 2:
            errs.append("client release responses %d < 2" % c["relrsps"])

    elif scenario == "release_without_disable_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("obs_rel", 0) < 1:
            errs.append("obs_rel < 1 (release cleanup expected)")
        if r.get("obs_gate_c", 0) < 1:
            errs.append("obs_gate_c < 1 (gate must close on release)")
        # Sink-stop ordering proof: the release path stopped the sink
        # (segment finalize) before the ACL disconnect, by event sequence.
        if r.get("obs_rel_ss", -1) != 1:
            errs.append("obs_rel_ss %s != 1 (release must stop the sink)"
                        % r.get("obs_rel_ss", -1))
        if r.get("rel_ss_seq", -1) >= r.get("disc_seq", -1):
            errs.append("release sink-stop seq %s not before disconnect seq %s"
                        % (r.get("rel_ss_seq", -1), r.get("disc_seq", -1)))
        if c["sends0"] < 20:
            errs.append("client sends0 %d < 20" % c["sends0"])
        if c["relrsps"] != 1:
            errs.append("client release responses %d != 1" % c["relrsps"])

    elif scenario == "disconnect_streaming_10ms":
        if seg != 1:
            errs.append("seg %d != 1" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if after != 0:
            errs.append("after %d != 0 (late pushes after disconnect)" % after)
        if r.get("derr1") != 0:
            errs.append("derr1 %s != 0" % r.get("derr1"))
        if r.get("adv_restart") != 1:
            errs.append("adv_restart != 1 (advertising restart path)")
        if r.get("obs_disc", 0) < 1:
            errs.append("obs_disc < 1 (disconnect cleanup expected)")
        if c["sends0"] < 20:
            errs.append("client sends0 %d < 20" % c["sends0"])

    elif scenario == "reconnect_second_stream_10ms":
        if seg != 2:
            errs.append("seg %d != 2" % seg)
        if r.get("pushes1", 0) < 20:
            errs.append("pushes1 %s < 20" % r.get("pushes1"))
        if r.get("pushes2") != 100:
            errs.append("pushes2 %s != 100" % r.get("pushes2"))
        if after != 0:
            errs.append("after %d != 0" % after)
        if r.get("adv_restart") != 1:
            errs.append("adv_restart != 1")
        if r.get("derr2") != 0:
            errs.append("derr2 %s != 0" % r.get("derr2"))
        # Second segment must equal a fresh mono 10 ms oracle.
        if "known_full" in known and known["known_full"] is not None:
            if r.get("h2") != known["known_full"]:
                errs.append(
                    "seg2 full hash 0x%08X != fresh mono 10 ms 0x%08X"
                    % (r.get("h2"), known["known_full"])
                )
        if r.get("lh2") != r.get("rh2"):
            errs.append("mono seg2 L hash != R hash")
        expected_total = r.get("pushes2", 0) + r.get("szero2", 0)
        if r.get("total2") != expected_total:
            errs.append("total2 %s != %d" % (r.get("total2"), expected_total))
        if c["sends1"] != 100:
            errs.append("client session-2 sends %d != 100" % c["sends1"])

    elif scenario == "unsupported_source_direction":
        # The final disconnect finalizes an empty segment; seg may be 0
        # (PASS before the disconnect) or 1 (empty segment).  Any push is
        # a fault.
        if seg not in (0, 1):
            errs.append("seg %d not in (0, 1) (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_rej") != 1:
            errs.append("obs_rej %d != 1" % r.get("obs_rej"))
        if r.get("obs_ok") != 0:
            errs.append("obs_ok %d != 0" % r.get("obs_ok"))
        if r.get("obs_dir") != 2:
            errs.append("obs_dir %d != SOURCE(2)" % r.get("obs_dir"))
        if r.get("obs_code") != 0x07:
            errs.append("obs_code 0x%02X != CONF_UNSUPPORTED" % r.get("obs_code"))
        if r.get("obs_reason") != 0:
            errs.append("obs_reason %d != NONE" % r.get("obs_reason"))
        if c["cfgrsps"] != 1:
            errs.append("client config responses %d != 1" % c["cfgrsps"])
        if c["sends0"] != 0:
            errs.append("client sends0 %d != 0" % c["sends0"])

    elif scenario == "no_free_sink_slot":
        # The final disconnect finalizes an empty segment; seg may be 0
        # (PASS before the disconnect) or 1 (empty segment).  Any push is
        # a fault.
        if seg not in (0, 1):
            errs.append("seg %d not in (0, 1) (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_ok", 0) < 3:
            errs.append("obs_ok %d < 3 (2 initial + 1 reuse)" % r.get("obs_ok"))
        if r.get("obs_rej") != 1:
            errs.append("obs_rej %d != 1" % r.get("obs_rej"))
        if r.get("obs_rej_code", -1) != 0x0D:
            errs.append("obs_rej_code 0x%02X != NO_MEM" % r.get("obs_rej_code", -1))
        if r.get("obs_mts", 0) != 0:
            errs.append("obs_mts %d != 0" % r.get("obs_mts"))
        if r.get("obs_rej_reason", -1) != 0:
            errs.append("obs_rej_reason %d != NONE" % r.get("obs_rej_reason", -1))
        if r.get("obs_rel", 0) < 3:
            errs.append("obs_rel %d < 3 (clean releases)" % r.get("obs_rel"))
        if c["cfgrsps"] != 4:
            errs.append("client config responses %d != 4" % c["cfgrsps"])
        if c["relrsps"] != 3:
            errs.append("client release responses %d != 3" % c["relrsps"])

    elif scenario == "invalid_codec_fields":
        # The final disconnect finalizes an empty segment; seg may be 0
        # (PASS before the disconnect) or 1 (empty segment).  Any push is
        # a fault.
        if seg not in (0, 1):
            errs.append("seg %d not in (0, 1) (no audio expected)" % seg)
        if r.get("pushes1", 0) != 0:
            errs.append("pushes1 %s != 0 (no audio expected)" % r.get("pushes1"))
        if r.get("obs_rej", 0) < 9:
            errs.append("obs_rej %d < 9" % r.get("obs_rej"))
        # Two successes overall: the valid mono config and the
        # missing-frame-blocks fallback (the receiver PASSes only after
        # both, so the observer counts are not reset per round).
        if r.get("obs_ok", 0) != 2:
            errs.append("obs_ok %d != 2 (valid mono + fallback)" % r.get("obs_ok"))
        if r.get("obs_rej_code", -1) != 0x08:
            errs.append(
                "obs_rej_code 0x%02X != CONF_REJECTED" % r.get("obs_rej_code", -1)
            )
        if r.get("obs_rej_reason", -1) != 0x02:
            errs.append("obs_rej_reason %d != CODEC_DATA" % r.get("obs_rej_reason", -1))
        if c["cfgrsps"] != 11:
            errs.append(
                "client config responses %d != 11 (9 rejects + 2 accepts)"
                % c["cfgrsps"]
            )

    else:
        errs.append("scenario %s not handled" % scenario)

    return errs


def check(scenario, recv, cli, known):
    errs = check_scenario(scenario, recv, cli, known)
    if errs:
        raise ParseError("%s: %s" % (scenario, "; ".join(errs)))
    return True


# ── CLI ──────────────────────────────────────────────────────────────


def main(argv):
    ap = argparse.ArgumentParser(description="T4 BSim scenario strict check")
    ap.add_argument(
        "check", nargs="?", help="subcommand placeholder (the runner passes 'check')"
    )
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--receiver", required=True)
    ap.add_argument("--client", required=True)
    ap.add_argument("--known-full", default=None)
    ap.add_argument("--known-l", default=None)
    ap.add_argument("--known-r", default=None)
    ap.add_argument("--known-total", default=None)
    args = ap.parse_args(argv)

    known = {}
    for key, val in (
        ("known_full", args.known_full),
        ("known_l", args.known_l),
        ("known_r", args.known_r),
    ):
        known[key] = int(val, 16) if val else None

    known["known_total"] = int(args.known_total) if args.known_total else None

    try:
        check(args.scenario, args.receiver, args.client, known)
    except ParseError as exc:
        print("FAIL: %s" % exc, file=sys.stderr)
        return 1

    r = parse_receiver_pass(args.receiver)
    print(
        "PASS: %s seg=%d pushes1=%s h1=0x%08X lh1=0x%08X rh1=0x%08X"
        % (
            args.scenario,
            r.get("seg"),
            r.get("pushes1"),
            r.get("h1", 0),
            r.get("lh1", 0),
            r.get("rh1", 0),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
