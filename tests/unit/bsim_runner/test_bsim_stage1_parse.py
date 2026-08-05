#!/usr/bin/env python3
"""Unit tests for scripts/bsim_stage1_parse.py — strict T4 scenario parser.

Covers PASS-line extraction, token parsing, per-scenario contract checks,
fault-marker scanning with allowlists, and every major failure mode.
No BabbleSim or hardware needed.
"""

import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from bsim_stage1_parse import (  # noqa: E402
    ParseError,
    ScenarioDataError,
    check,
    extract_pass_line,
    known_values,
    load_scenarios,
    main as bsim_main,
    parse_client_pass,
    parse_receiver_pass,
    parse_tokens,
    scan_faults,
)

FAILURES = 0
PASSES = 0


def report(name, ok, detail=""):
    global FAILURES, PASSES
    if ok:
        PASSES += 1
        print("  PASS: %s %s" % (name, detail))
    else:
        FAILURES += 1
        print("  FAIL: %s %s" % (name, detail))


def write_log(root, name, text):
    path = os.path.join(root, name)
    with open(path, "w") as fh:
        fh.write(text)
    return path


def recv_pass(scenario, **over):
    base = (
        "d_00: @00:00:06.295366 INFO: le_audio_receiver: "
        "scenario=%s seg=1 after=0 adv_restart=0 pacs=1 "
        "obs_ok=1 obs_rej=0 obs_dir=0 obs_code=0 obs_reason=0 "
        "obs_gate_o=1 obs_gate_c=0 obs_mal=0 obs_blk=0 obs_stale=0 obs_rel=0 obs_disc=0 "
        "obs_rej_code=0 obs_rej_reason=0 obs_mts=0 obs_rel_ss=0 rel_ss_seq=0 disc_seq=0 "
        "pushes1=100 trans1=8 szero1=8 splc1=7 total1=108 plc1=7 derr1=0 mal1=0 "
        "h1=0x12345678 lh1=0x12345678 rh1=0x12345678 "
        "lemin1=1234 lemax1=5678 remin1=1234 remax1=5678 samples1=960\n" % scenario
    )
    import re

    def repl(m):
        return "%s=%s" % (m.group(1), over[m.group(1)])

    for k in over:
        base = re.sub(r"\b(%s)=\d+" % k, repl, base)
    return base


def cli_pass(scenario, **over):
    base = (
        "d_01: @00:00:06.400000 INFO: bsim_client: "
        "scenario=%s sends0=100 sends1=0 cfgrsps=1 relrsps=0 disrsps=0\n" % scenario
    )
    import re

    def repl(m):
        return "%s=%s" % (m.group(1), over[m.group(1)])

    for k in over:
        base = re.sub(r"\b(%s)=\d+" % k, repl, base)
    return base


def run_check(root, scenario, recv_text, cli_text, known=None, expect_ok=True):
    recv = write_log(root, "receiver.log", recv_text)
    cli = write_log(root, "client.log", cli_text)
    try:
        check(scenario, recv, cli, known or {})
        return True
    except ParseError:
        return False


def test_tokens():
    line = "INFO: le_audio_receiver: scenario=mono_10ms seg=1 h1=0xFE0D4245 after=0"
    t = parse_tokens(line)
    report(
        "tokens",
        t["scenario"] == "mono_10ms"
        and t["seg"] == 1
        and t["h1"] == 0xFE0D4245
        and t["after"] == 0,
        str(t),
    )


def test_extract_missing():
    root = tempfile.mkdtemp()
    p = write_log(root, "x.log", "no pass here\n")
    try:
        extract_pass_line(p, "le_audio_receiver")
        report("extract_missing", False)
    except ParseError:
        report("extract_missing", True)


def test_mono_10ms_ok():
    root = tempfile.mkdtemp()
    known = {"known_full": 0x12345678, "known_l": 0x12345678, "known_r": 0x12345678}
    ok = run_check(
        root, "mono_10ms", recv_pass("mono_10ms"), cli_pass("mono_10ms"), known
    )
    report("mono_10ms ok", ok)


def test_mono_hash_mismatch():
    root = tempfile.mkdtemp()
    known = {"known_full": 0xDEADBEEF, "known_l": 0x12345678, "known_r": 0x12345678}
    ok = run_check(
        root, "mono_10ms", recv_pass("mono_10ms"), cli_pass("mono_10ms"), known
    )
    report("mono hash mismatch rejected", not ok)


def test_mono_lr_equal():
    root = tempfile.mkdtemp()
    # Force L != R: stereo-like record on mono scenario must fail.
    recv = recv_pass("mono_10ms").replace("rh1=0x12345678", "rh1=0xDEADBEEF")
    ok = run_check(root, "mono_10ms", recv, cli_pass("mono_10ms"))
    report("mono L!=R rejected", not ok)


def test_modea_lr_distinct():
    root = tempfile.mkdtemp()
    recv = recv_pass("modea_10ms", pushes1=100, total1=216, szero1=8, obs_gate_o=2)
    recv = recv.replace("lh1=0x12345678", "lh1=0x11111111").replace(
        "rh1=0x12345678", "rh1=0x22222222"
    )
    ok = run_check(
        root, "modea_10ms", recv, cli_pass("modea_10ms", sends0=110, sends1=110), {}
    )
    report("modea L!=R ok", ok)


def test_modea_lr_equal_rejected():
    root = tempfile.mkdtemp()
    recv = recv_pass("modea_10ms", total1=216)
    ok = run_check(
        root, "modea_10ms", recv, cli_pass("modea_10ms", sends0=110, sends1=110), {}
    )
    report("modea L==R rejected", not ok)


def test_total_pin():
    root = tempfile.mkdtemp()
    known = {"known_total": 108}
    ok = run_check(
        root, "mono_10ms", recv_pass("mono_10ms"), cli_pass("mono_10ms"), known
    )
    report("total pin ok", ok)

    known_bad = {"known_total": 200}
    ok = run_check(
        root, "mono_10ms", recv_pass("mono_10ms"), cli_pass("mono_10ms"), known_bad
    )
    report("total pin mismatch rejected", not ok)


def test_post_start_plc():
    root = tempfile.mkdtemp()
    ok = run_check(root, "mono_10ms", recv_pass("mono_10ms"), cli_pass("mono_10ms"))
    report("zero post-start PLC ok", ok)

    # plc1 != splc1 is a fault in every audio scenario.
    recv = recv_pass("mono_10ms", plc1=9)
    ok = run_check(root, "mono_10ms", recv, cli_pass("mono_10ms"))
    report("post-start PLC rejected", not ok)


def test_total_frames_mismatch():
    root = tempfile.mkdtemp()
    # Undercount: total below pushes+startup-zeros is a fault.
    recv = recv_pass("mono_10ms", total1=100)
    ok = run_check(root, "mono_10ms", recv, cli_pass("mono_10ms"))
    report("total frames mismatch rejected", not ok)


def test_invalid_sdu_resume():
    root = tempfile.mkdtemp()
    recv = recv_pass("invalid_sdu_resume_10ms", derr1=1, obs_mal=1)
    ok = run_check(
        root,
        "invalid_sdu_resume_10ms",
        recv,
        cli_pass("invalid_sdu_resume_10ms", sends0=101),
    )
    report("invalid_sdu_resume ok", ok)

    recv_bad = recv_pass("invalid_sdu_resume_10ms", derr1=0, obs_mal=1)
    ok = run_check(
        root,
        "invalid_sdu_resume_10ms",
        recv_bad,
        cli_pass("invalid_sdu_resume_10ms", sends0=101),
    )
    report("invalid_sdu_resume missing decode-error rejected", not ok)


def test_modea_first_stop():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "modea_first_stop_10ms",
        pushes1=25,
        total1=66,
        obs_gate_c=1,
        obs_blk=5,
        obs_rel=2,
        obs_rel_ss=0,
        szero1=8,
        seg=1,
    )
    ok = run_check(
        root,
        "modea_first_stop_10ms",
        recv,
        cli_pass("modea_first_stop_10ms", sends0=25, sends1=45, relrsps=2),
    )
    report("modea_first_stop ok", ok)

    recv_bad = recv_pass(
        "modea_first_stop_10ms",
        pushes1=25,
        total1=66,
        obs_gate_c=0,
        obs_blk=5,
        obs_rel=2,
    )
    ok = run_check(
        root,
        "modea_first_stop_10ms",
        recv_bad,
        cli_pass("modea_first_stop_10ms", sends0=25, sends1=45, relrsps=2),
    )
    report("modea_first_stop missing gate close rejected", not ok)


def test_release_without_disable():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "release_without_disable_10ms",
        pushes1=25,
        total1=33,
        obs_rel=1,
        obs_gate_c=1,
        obs_rel_ss=1,
        rel_ss_seq=5,
        disc_seq=9,
        obs_mts=0,
        obs_disc=1,
    )
    ok = run_check(
        root,
        "release_without_disable_10ms",
        recv,
        cli_pass("release_without_disable_10ms", sends0=25, relrsps=1),
    )
    report("release_without_disable ok", ok)


def test_disconnect_streaming():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "disconnect_streaming_10ms",
        pushes1=25,
        total1=33,
        adv_restart=1,
        obs_disc=1,
        obs_gate_c=1,
        obs_rel=0,
    )
    ok = run_check(
        root,
        "disconnect_streaming_10ms",
        recv,
        cli_pass("disconnect_streaming_10ms", sends0=25),
    )
    report("disconnect_streaming ok", ok)

    recv_bad = recv_pass(
        "disconnect_streaming_10ms", pushes1=25, total1=33, adv_restart=0, obs_disc=1
    )
    ok = run_check(
        root,
        "disconnect_streaming_10ms",
        recv_bad,
        cli_pass("disconnect_streaming_10ms", sends0=25),
    )
    report("disconnect_streaming missing restart rejected", not ok)


def test_reconnect_second_stream():
    root = tempfile.mkdtemp()
    known = {"known_full": 0xABCD1234}
    recv = (
        "d_00: INFO: le_audio_receiver: scenario=reconnect_second_stream_10ms seg=2 "
        "after=0 adv_restart=1 pacs=1 obs_ok=2 obs_rej=0 obs_dir=0 obs_code=0 "
        "obs_reason=0 obs_gate_o=2 obs_gate_c=1 obs_mal=0 obs_blk=0 obs_stale=0 "
        "obs_rel=0 obs_disc=1 "
        "pushes1=25 szero1=8 splc1=7 total1=33 plc1=7 derr1=0 mal1=0 "
        "h1=0x11111111 lh1=0x11111111 rh1=0x11111111 "
        "lemin1=1 lemax1=2 remin1=1 remax1=2 samples1=960 "
        "pushes2=100 trans2=8 szero2=8 splc2=7 total2=108 plc2=7 derr2=0 mal2=0 "
        "h2=0xABCD1234 lh2=0xABCD1234 rh2=0xABCD1234 "
        "lemin2=1 lemax2=2 remin2=1 remax2=2 samples2=960\n"
    )
    ok = run_check(
        root,
        "reconnect_second_stream_10ms",
        recv,
        cli_pass("reconnect_second_stream_10ms", sends0=0, sends1=100),
        known,
    )
    report("reconnect_second_stream ok", ok)

    recv_bad = recv.replace("h2=0xABCD1234", "h2=0xDEADBEEF")
    ok = run_check(
        root,
        "reconnect_second_stream_10ms",
        recv_bad,
        cli_pass("reconnect_second_stream_10ms", sends0=0, sends1=100),
        known,
    )
    report("reconnect seg2 hash mismatch rejected", not ok)


def test_unsupported_source():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "unsupported_source_direction",
        seg=0,
        pushes1=0,
        obs_rej=1,
        obs_dir=2,
        obs_code=7,
        obs_reason=0,
        obs_ok=0,
    )
    ok = run_check(
        root,
        "unsupported_source_direction",
        recv,
        cli_pass("unsupported_source_direction", sends0=0),
    )
    report("unsupported_source ok", ok)

    recv_bad = recv_pass(
        "unsupported_source_direction",
        seg=0,
        obs_rej=1,
        obs_dir=1,
        obs_code=7,
        obs_reason=0,
        obs_ok=0,
    )
    ok = run_check(
        root,
        "unsupported_source_direction",
        recv_bad,
        cli_pass("unsupported_source_direction", sends0=0),
    )
    report("unsupported_source wrong dir rejected", not ok)


def test_no_free_sink_slot():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "no_free_sink_slot",
        seg=0,
        pushes1=0,
        obs_ok=3,
        obs_rej=1,
        obs_rej_code=13,
        obs_rej_reason=0,
        obs_rel=3,
    )
    ok = run_check(
        root,
        "no_free_sink_slot",
        recv,
        cli_pass("no_free_sink_slot", sends0=0, cfgrsps=4, relrsps=3),
    )
    report("no_free_sink_slot ok", ok)

    recv_bad = recv_pass(
        "no_free_sink_slot",
        seg=0,
        obs_ok=3,
        obs_rej=1,
        obs_rej_code=9,
        obs_rej_reason=0,
        obs_rel=3,
    )
    ok = run_check(
        root,
        "no_free_sink_slot",
        recv_bad,
        cli_pass("no_free_sink_slot", sends0=0, cfgrsps=4, relrsps=3),
    )
    report("no_free_sink_slot wrong code rejected", not ok)


def test_invalid_codec_fields():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "invalid_codec_fields",
        seg=0,
        pushes1=0,
        obs_rej=9,
        obs_ok=2,
        obs_rej_code=8,
        obs_rej_reason=2,
        obs_rel=2,
        obs_gate_c=0,
        obs_rel_ss=0,
    )
    ok = run_check(
        root,
        "invalid_codec_fields",
        recv,
        cli_pass("invalid_codec_fields", sends0=0, cfgrsps=11),
    )
    report("invalid_codec_fields ok", ok)

    recv_bad = recv_pass(
        "invalid_codec_fields",
        seg=0,
        obs_rej=9,
        obs_ok=2,
        obs_rej_code=8,
        obs_rej_reason=0,
    )
    ok = run_check(
        root,
        "invalid_codec_fields",
        recv_bad,
        cli_pass("invalid_codec_fields", sends0=0, cfgrsps=11),
    )
    report("invalid_codec_fields wrong reason rejected", not ok)


def test_duplicate_release():
    root = tempfile.mkdtemp()
    recv = recv_pass(
        "duplicate_release_10ms",
        pushes1=25,
        total1=33,
        seg=1,
        obs_gate_o=1,
        obs_gate_c=1,
        obs_rel=2,
        obs_rel_ss=1,
        obs_disc=1,
    )
    ok = run_check(
        root,
        "duplicate_release_10ms",
        recv,
        cli_pass("duplicate_release_10ms", sends0=25, cfgrsps=2, relrsps=3),
    )
    report("duplicate_release ok", ok)

    # Duplicate same-slot release must NOT re-clean: cleanup count 3 is a
    # fault (the scenario has exactly two first-time cleanups).
    recv_bad = recv_pass(
        "duplicate_release_10ms",
        pushes1=25,
        total1=33,
        seg=1,
        obs_gate_c=1,
        obs_rel=3,
        obs_rel_ss=1,
        obs_disc=1,
    )
    ok = run_check(
        root,
        "duplicate_release_10ms",
        recv_bad,
        cli_pass("duplicate_release_10ms", sends0=25, cfgrsps=2, relrsps=3),
    )
    report("duplicate_release over-cleanup rejected", not ok)


def test_fault_scan():
    root = tempfile.mkdtemp()
    # Scenario 1: any decode fault marker fails.
    log = "d_00: ... INFO: le_audio_receiver: scenario=mono_10ms ...\n<err> bt_bap: LC3 decode error -5\n"
    p = write_log(root, "r.log", log)
    try:
        scan_faults(p, "mono_10ms")
        report("fault scan detects decode error", False)
    except ParseError:
        report("fault scan detects decode error", True)

    # Any bt_bap warning is a fault (no allowlist).
    logw = "d_00: ... <wrn> bt_bap: Source direction unsupported\n"
    pw = write_log(root, "rw.log", logw)
    try:
        scan_faults(pw, "unsupported_source_direction")
        report("bt_bap warning rejected everywhere", False)
    except ParseError:
        report("bt_bap warning rejected everywhere", True)

    # Any bt_ascs warning is a fault (the CONF_REJECTED path logs INFO).
    logr = "d_00: ... <wrn> bt_ascs: Invalid application error code: 9\n"
    pr = write_log(root, "rr.log", logr)
    try:
        scan_faults(pr, "invalid_codec_fields")
        report("ascs rsp warning rejected everywhere", False)
    except ParseError:
        report("ascs rsp warning rejected everywhere", True)

    # The duplicate-release scenario's deliberate server rejection of the
    # second Release PDU (ascs.c: "Invalid operation in state: releasing")
    # is allowed ONLY for that scenario and only for that exact line.
    dup_ok = "d_00: ... <wrn> bt_ascs: Invalid operation in state: releasing\n"
    pd = write_log(root, "dup.log", dup_ok)
    try:
        scan_faults(pd, "duplicate_release_10ms")
        report("duplicate-release server rejection allowlisted", True)
    except ParseError:
        report("duplicate-release server rejection allowlisted", False)

    pdx = write_log(root, "dupx.log", dup_ok)
    try:
        scan_faults(pdx, "mono_10ms")
        report("duplicate-release line still a fault elsewhere", False)
    except ParseError:
        report("duplicate-release line still a fault elsewhere", True)

    # A DIFFERENT bt_ascs warning is still a fault inside the
    # duplicate-release scenario (the allowlist is exact-line only).
    pd2 = write_log(root, "dup2.log", "d_00: ... <wrn> bt_ascs: something else\n")
    try:
        scan_faults(pd2, "duplicate_release_10ms")
        report("other bt_ascs warning still a fault in scenario 17", False)
    except ParseError:
        report("other bt_ascs warning still a fault in scenario 17", True)

    # Any bt_bap error is a fault.
    log14 = "d_00: ... <err> bt_bap: No free sink slot (max 2)\n"
    p14 = write_log(root, "r14.log", log14)
    try:
        scan_faults(p14, "no_free_sink_slot")
        report("bt_bap error rejected everywhere", False)
    except ParseError:
        report("bt_bap error rejected everywhere", True)


def test_client_pass_parse():
    root = tempfile.mkdtemp()
    c = cli_pass("mono_10ms", sends0=100)
    p = write_log(root, "c.log", c)
    t = parse_client_pass(p)
    report(
        "client pass parse",
        t["scenario"] == "mono_10ms" and t["sends0"] == 100 and t["cfgrsps"] == 1,
    )


def test_receiver_pass_parse():
    root = tempfile.mkdtemp()
    p = write_log(root, "r.log", recv_pass("mono_10ms"))
    t = parse_receiver_pass(p)
    report(
        "receiver pass parse",
        t["h1"] == 0x12345678 and t["obs_ok"] == 1 and t["pacs"] == 1,
    )


# ── R3: versioned scenario data (tests/bsim/stage1-scenarios.json) ──────

PRODUCTION_DATA = os.path.join(REPO_ROOT, "tests", "bsim", "stage1-scenarios.json")


def test_production_pins_load_unchanged():
    """All 17 production pins load from the versioned file, unchanged."""
    data = load_scenarios()
    scenarios = data["scenarios"]
    report("production pin count", len(scenarios) == 17)

    # Every scenario has the full metadata contract.
    ok = True
    for s in scenarios:
        ok = ok and isinstance(s["name"], str) and s["name"]
        ok = ok and isinstance(s["runs"], int) and s["runs"] >= 1
        ok = ok and isinstance(s["dec_calls"], int) and s["dec_calls"] >= 1
        ok = ok and s["channel_mode"] in ("mono", "stereo")
    report("production scenario metadata shape", ok)

    # Exact pinned values from the pre-R3 shell tables (no repinning).
    expected = {
        "mono_10ms": {
            "full": 0x22AB5C0D,
            "l": 0x32777D65,
            "r": 0x32777D65,
            "total": 108,
        },
        "mono_7p5ms": {
            "full": 0x01A3EB05,
            "l": 0x30F0308C,
            "r": 0x30F0308C,
            "total": 111,
        },
        "modea_10ms": {
            "full": 0xBAE24F7E,
            "l": 0x32777D65,
            "r": 0xD3EE3722,
            "total": 216,
        },
        "modea_7p5ms": {
            "full": 0x2D95D15C,
            "l": 0xE1D60E7B,
            "r": 0xA219B61E,
            "total": 226,
        },
        "modea_reverse_start_10ms": {
            "full": 0xBAE24F7E,
            "l": 0x32777D65,
            "r": 0xD3EE3722,
            "total": 216,
        },
        "modeb_10ms": {
            "full": 0xBAE24F7E,
            "l": 0x32777D65,
            "r": 0xD3EE3722,
            "total": 216,
        },
        "modeb_7p5ms": {
            "full": 0xFF82CADB,
            "l": 0x30F0308C,
            "r": 0x129591EE,
            "total": 222,
        },
        "invalid_sdu_resume_10ms": {"full": 0x0C61918D, "total": 108},
        "modea_one_cis_loss_10ms": {
            "full": 0x30D6BAF0,
            "l": 0x32777D65,
            "r": 0x9859F1D8,
            "total": 216,
        },
        "modea_first_stop_10ms": {"total": 86},
        "release_without_disable_10ms": {"total": 56},
        "disconnect_streaming_10ms": {"total": 63},
        "reconnect_second_stream_10ms": {"full": 0x22AB5C0D, "total": 63},
    }
    pins_ok = True
    for name, want in expected.items():
        got = known_values(name)
        if got != want:
            pins_ok = False
            report("pin %s" % name, False, "got %r want %r" % (got, want))
    report("production pins unchanged (no repinning)", pins_ok)

    unpinned = {
        "unsupported_source_direction",
        "no_free_sink_slot",
        "invalid_codec_fields",
    }
    unpinned_ok = all(known_values(n) == {} for n in unpinned)
    report("unpinned scenarios stay unpinned", unpinned_ok)


def _write_scenario_file(root, payload):
    path = os.path.join(root, "scenarios.json")
    with open(path, "w", encoding="utf-8") as fh:
        if isinstance(payload, str):
            fh.write(payload)
        else:
            json.dump(payload, fh)
    return path


def _valid_scenario(name="scn", runs=1, dec_calls=1, channel_mode="mono", known=None):
    entry = {
        "name": name,
        "runs": runs,
        "dec_calls": dec_calls,
        "channel_mode": channel_mode,
    }
    if known:
        entry["known"] = known
    return entry


def test_schema_missing_file():
    root = tempfile.mkdtemp()
    try:
        load_scenarios(os.path.join(root, "nope.json"))
        report("schema missing file rejected", False)
    except ScenarioDataError:
        report("schema missing file rejected", True)


def test_schema_invalid_json():
    root = tempfile.mkdtemp()
    p = _write_scenario_file(root, "{not json")
    try:
        load_scenarios(p)
        report("schema invalid json rejected", False)
    except ScenarioDataError:
        report("schema invalid json rejected", True)


def test_schema_shape_errors():
    root = tempfile.mkdtemp()
    cases = [
        ("missing schema_version", {"scenarios": [_valid_scenario()]}),
        ("empty scenarios", {"schema_version": 1, "scenarios": []}),
        ("missing scenarios key", {"schema_version": 1}),
    ]
    for label, payload in cases:
        p = _write_scenario_file(root, payload)
        try:
            load_scenarios(p)
            report("schema %s rejected" % label, False)
        except ScenarioDataError:
            report("schema %s rejected" % label, True)


def test_schema_entry_errors():
    root = tempfile.mkdtemp()
    dup = _write_scenario_file(
        root,
        {
            "schema_version": 1,
            "scenarios": [_valid_scenario("dup"), _valid_scenario("dup")],
        },
    )
    try:
        load_scenarios(dup)
        report("schema duplicate name rejected", False)
    except ScenarioDataError:
        report("schema duplicate name rejected", True)

    bad_chan = _write_scenario_file(
        root,
        {"schema_version": 1, "scenarios": [_valid_scenario(channel_mode="jazz")]},
    )
    try:
        load_scenarios(bad_chan)
        report("schema bad channel_mode rejected", False)
    except ScenarioDataError:
        report("schema bad channel_mode rejected", True)

    bad_runs = _write_scenario_file(
        root, {"schema_version": 1, "scenarios": [_valid_scenario(runs=0)]}
    )
    try:
        load_scenarios(bad_runs)
        report("schema non-positive runs rejected", False)
    except ScenarioDataError:
        report("schema non-positive runs rejected", True)

    bad_known = _write_scenario_file(
        root,
        {"schema_version": 1, "scenarios": [_valid_scenario(known={"full": "nope"})]},
    )
    try:
        load_scenarios(bad_known)
        report("schema bad known hex rejected", False)
    except ScenarioDataError:
        report("schema bad known hex rejected", True)

    unknown_key = _write_scenario_file(
        root,
        {"schema_version": 1, "scenarios": [_valid_scenario(known={"floof": "0x1"})]},
    )
    try:
        load_scenarios(unknown_key)
        report("schema unknown known key rejected", False)
    except ScenarioDataError:
        report("schema unknown known key rejected", True)

    missing_name = _write_scenario_file(
        root,
        {
            "schema_version": 1,
            "scenarios": [{"runs": 1, "dec_calls": 1, "channel_mode": "mono"}],
        },
    )
    try:
        load_scenarios(missing_name)
        report("schema missing name rejected", False)
    except ScenarioDataError:
        report("schema missing name rejected", True)


def _run_cli(args):
    import contextlib
    import io

    # Capture both streams: expected-negative CLI runs print FAIL to
    # stderr, which must not leak into the canonical gate log.
    buf = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(err):
        code = bsim_main(args)
    return code, buf.getvalue() + err.getvalue()


def test_cli_known_precedence():
    """CLI precedence: --no-known > explicit --known-* > file defaults."""
    root = tempfile.mkdtemp()
    # mono_10ms with the REAL pinned hashes (from the versioned file).
    # Replace lh1/rh1 before h1: "h1=..." is a substring of "lh1=...".
    recv = write_log(
        root,
        "receiver.log",
        recv_pass("mono_10ms")
        .replace("lh1=0x12345678", "lh1=0x32777D65")
        .replace("rh1=0x12345678", "rh1=0x32777D65")
        .replace("h1=0x12345678", "h1=0x22AB5C0D"),
    )
    cli = write_log(root, "client.log", cli_pass("mono_10ms"))
    base = ["check", "--scenario", "mono_10ms", "--receiver", recv, "--client", cli]

    code, _ = _run_cli(base)
    report("cli file-default pins pass", code == 0)

    code, _ = _run_cli(base + ["--known-full", "0xDEADBEEF"])
    report("cli explicit override wins (mismatch fails)", code != 0)

    code, _ = _run_cli(base + ["--no-known"])
    report("cli --no-known disables pins", code == 0)

    # Explicit total override still applies when no --no-known.
    code, _ = _run_cli(base + ["--known-total", "200"])
    report("cli --known-total override fails on mismatch", code != 0)


def main():
    print("=== bsim_stage1_parse unit tests ===")
    test_tokens()
    test_extract_missing()
    test_mono_10ms_ok()
    test_mono_hash_mismatch()
    test_mono_lr_equal()
    test_modea_lr_distinct()
    test_modea_lr_equal_rejected()
    test_total_frames_mismatch()
    test_total_pin()
    test_post_start_plc()
    test_invalid_sdu_resume()
    test_modea_first_stop()
    test_release_without_disable()
    test_disconnect_streaming()
    test_reconnect_second_stream()
    test_unsupported_source()
    test_no_free_sink_slot()
    test_invalid_codec_fields()
    test_fault_scan()
    test_client_pass_parse()
    test_receiver_pass_parse()
    test_production_pins_load_unchanged()
    test_schema_missing_file()
    test_schema_invalid_json()
    test_schema_shape_errors()
    test_schema_entry_errors()
    test_cli_known_precedence()
    print("=== %d PASS / %d FAIL ===" % (PASSES, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
