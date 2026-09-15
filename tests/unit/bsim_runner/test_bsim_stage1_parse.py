#!/usr/bin/env python3
"""Unit tests for scripts/bsim_stage1_parse.py — strict T4 scenario parser.

Covers PASS-line extraction, token parsing, per-scenario contract checks,
fault-marker scanning with allowlists, and every major failure mode.
No BabbleSim or hardware needed.
"""

import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from bsim_stage1_parse import (  # noqa: E402
    FNV1A_OFFSET_BASIS,
    ParseError,
    ScenarioDataError,
    TRANSPORT_VALUES,
    check,
    expected_transport_hash,
    extract_pass_line,
    known_values,
    load_scenarios,
    main as bsim_main,
    parse_client_pass,
    parse_receiver_pass,
    parse_tokens,
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
    values = {
        "sends0": 100,
        "sends1": 0,
        "cfgrsps": 1,
        "relrsps": 0,
        "disrsps": 0,
    }
    values.update(over)
    for stream in (0, 1):
        count_key = "txc%d" % stream
        hash_key = "txh%d" % stream
        sends_key = "sends%d" % stream
        if count_key not in values:
            values[count_key] = values[sends_key]
        if hash_key not in values:
            transport = next(
                (
                    entry
                    for entry in TRANSPORT_VALUES[scenario]
                    if entry["stream"] == stream
                ),
                None,
            )
            values[hash_key] = (
                expected_transport_hash(transport, values[count_key])
                if transport is not None
                else FNV1A_OFFSET_BASIS
            )
    return (
        "d_01: @00:00:06.400000 INFO: bsim_client: "
        "scenario=%s sends0=%d sends1=%d cfgrsps=%d relrsps=%d disrsps=%d "
        "txc0=%d txh0=0x%08X txc1=%d txh1=0x%08X\n"
        % (
            scenario,
            values["sends0"],
            values["sends1"],
            values["cfgrsps"],
            values["relrsps"],
            values["disrsps"],
            values["txc0"],
            values["txh0"],
            values["txc1"],
            values["txh1"],
        )
    )


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
    scenario = "invalid_sdu_resume_10ms"
    pinned = known_values(scenario)
    known = {"known_total": pinned.get("total")}
    recv = recv_pass(scenario, derr1=1, obs_mal=1)
    ok = run_check(
        root,
        scenario,
        recv,
        cli_pass(scenario, sends0=101),
        known,
    )
    report(
        "invalid_sdu_resume has no full pin and keeps total 108",
        pinned == {"total": 108},
    )
    report("invalid_sdu_resume exact contract", ok)

    transport = TRANSPORT_VALUES[scenario][0]
    exact_hash = expected_transport_hash(transport, 101)
    cases = [
        (
            "invalid_sdu_resume send count rejected",
            recv,
            cli_pass(scenario, sends0=100),
        ),
        (
            "invalid_sdu_resume TX hash rejected",
            recv,
            cli_pass(scenario, sends0=101, txh0=exact_hash ^ 1),
        ),
        (
            "invalid_sdu_resume malformed observer and decode error required",
            recv_pass(scenario, derr1=0, obs_mal=0),
            cli_pass(scenario, sends0=101),
        ),
        (
            "invalid_sdu_resume resumed pushes required",
            recv_pass(scenario, pushes1=99, derr1=1, obs_mal=1),
            cli_pass(scenario, sends0=101),
        ),
        (
            "invalid_sdu_resume lifecycle checks retained",
            recv_pass(scenario, derr1=1, obs_mal=1, obs_gate_c=1),
            cli_pass(scenario, sends0=101),
        ),
    ]
    for label, bad_recv, bad_cli in cases:
        report(label, not run_check(root, scenario, bad_recv, bad_cli, known))


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
        cli_pass("reconnect_second_stream_10ms", sends0=25, sends1=100),
        known,
    )
    session0, session1 = TRANSPORT_VALUES["reconnect_second_stream_10ms"]
    fresh_mono = expected_transport_hash(TRANSPORT_VALUES["mono_10ms"][0], 100)
    reconnect_hashes_ok = (
        expected_transport_hash(session0, 25) != FNV1A_OFFSET_BASIS
        and expected_transport_hash(session1, 100) == fresh_mono
    )
    report(
        "reconnect retained stream 0 and fresh stream 1 hashes",
        ok and reconnect_hashes_ok,
    )

    recv_bad = recv.replace("h2=0xABCD1234", "h2=0xDEADBEEF")
    ok = run_check(
        root,
        "reconnect_second_stream_10ms",
        recv_bad,
        cli_pass("reconnect_second_stream_10ms", sends0=25, sends1=100),
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
    mono_recv = recv_pass("mono_10ms")
    mono_cli = cli_pass("mono_10ms")

    def rejected(label, scenario, receiver, client):
        report(label, not run_check(root, scenario, receiver, client))

    rejected(
        "receiver semantic fault marker rejected",
        "mono_10ms",
        mono_recv + "d_00: FATAL receiver failure\n",
        mono_cli,
    )
    rejected(
        "receiver ANSI warning rejected",
        "mono_10ms",
        mono_recv + "\x1b[1;33m<wrn> bt_hci_core: unexpected warning\x1b[0m\n",
        mono_cli,
    )
    rejected(
        "receiver ANSI error rejected",
        "mono_10ms",
        mono_recv + "\x1b[1;31m<err> bt_gatt: unexpected error\x1b[0m\n",
        mono_cli,
    )
    rejected(
        "client ANSI warning rejected",
        "mono_10ms",
        mono_recv,
        mono_cli + "\x1b[1;33m<wrn> bt_hci_core: unexpected warning\x1b[0m\n",
    )
    rejected(
        "client ANSI error rejected",
        "mono_10ms",
        mono_recv,
        mono_cli + "\x1b[1;31m<err> bsim_tx: unexpected error\x1b[0m\n",
    )

    duplicate_recv = recv_pass(
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
    duplicate_cli = cli_pass("duplicate_release_10ms", sends0=25, cfgrsps=2, relrsps=3)
    exact_warning = (
        "d_00: \x1b[1;33m<wrn> bt_ascs: Invalid operation in state: releasing\x1b[0m\n"
    )
    report(
        "duplicate-release receiver warning allowlisted",
        run_check(
            root,
            "duplicate_release_10ms",
            duplicate_recv + exact_warning,
            duplicate_cli,
        ),
    )
    rejected(
        "duplicate-release warning rejected outside scenario 17",
        "mono_10ms",
        mono_recv + exact_warning,
        mono_cli,
    )
    rejected(
        "duplicate-release warning rejected in client log",
        "duplicate_release_10ms",
        duplicate_recv,
        duplicate_cli + exact_warning,
    )
    rejected(
        "duplicate-release warning requires exact text",
        "duplicate_release_10ms",
        duplicate_recv + exact_warning.rstrip() + " extra\n",
        duplicate_cli,
    )
    rejected(
        "duplicate-release error remains rejected",
        "duplicate_release_10ms",
        duplicate_recv
        + "d_00: \x1b[1;31m<err> bt_ascs: Invalid operation in state: releasing\x1b[0m\n",
        duplicate_cli,
    )


def test_client_pass_parse():
    root = tempfile.mkdtemp()
    c = cli_pass("mono_10ms", sends0=100)
    p = write_log(root, "c.log", c)
    t = parse_client_pass(p)
    report(
        "client pass parse",
        t["scenario"] == "mono_10ms"
        and t["sends0"] == 100
        and t["cfgrsps"] == 1
        and t["txc0"] == 100
        and t["txh0"] == expected_transport_hash(TRANSPORT_VALUES["mono_10ms"][0], 100)
        and t["txc1"] == 0
        and t["txh1"] == FNV1A_OFFSET_BASIS,
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
    """Schema 2 loads all production scenarios and preserves receiver pins."""
    data = load_scenarios()
    scenarios = data["scenarios"]
    report("production schema 2", data["schema_version"] == 2)
    report("production pin count", len(scenarios) == 17)

    # Every scenario has the full metadata contract.
    ok = True
    for s in scenarios:
        ok = ok and isinstance(s["name"], str) and s["name"]
        ok = ok and isinstance(s["runs"], int) and s["runs"] >= 1
        ok = ok and isinstance(s["dec_calls"], int) and s["dec_calls"] >= 1
        ok = ok and s["channel_mode"] in ("mono", "stereo")
        ok = ok and isinstance(s["transport"], list) and len(s["transport"]) <= 2
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
        "invalid_sdu_resume_10ms": {"total": 108},
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
        "duplicate_release_10ms": {"total": 56},
    }
    pins_ok = True
    for name, want in expected.items():
        got = known_values(name)
        if got != want:
            pins_ok = False
            report("pin %s" % name, False, "got %r want %r" % (got, want))
    report("production pins unchanged (no repinning)", pins_ok)

    malformed = _scenario(data, "invalid_sdu_resume_10ms")
    report(
        "malformed scenario omits only full pin",
        malformed.get("known") == {"total": 108},
    )

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
        "transport": [],
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
        ("schema 1", {"schema_version": 1, "scenarios": [_valid_scenario()]}),
        ("empty scenarios", {"schema_version": 2, "scenarios": []}),
        ("missing scenarios key", {"schema_version": 2}),
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
            "schema_version": 2,
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
        {"schema_version": 2, "scenarios": [_valid_scenario(channel_mode="jazz")]},
    )
    try:
        load_scenarios(bad_chan)
        report("schema bad channel_mode rejected", False)
    except ScenarioDataError:
        report("schema bad channel_mode rejected", True)

    bad_runs = _write_scenario_file(
        root, {"schema_version": 2, "scenarios": [_valid_scenario(runs=0)]}
    )
    try:
        load_scenarios(bad_runs)
        report("schema non-positive runs rejected", False)
    except ScenarioDataError:
        report("schema non-positive runs rejected", True)

    bad_known = _write_scenario_file(
        root,
        {"schema_version": 2, "scenarios": [_valid_scenario(known={"full": "nope"})]},
    )
    try:
        load_scenarios(bad_known)
        report("schema bad known hex rejected", False)
    except ScenarioDataError:
        report("schema bad known hex rejected", True)

    unknown_key = _write_scenario_file(
        root,
        {"schema_version": 2, "scenarios": [_valid_scenario(known={"floof": "0x1"})]},
    )
    try:
        load_scenarios(unknown_key)
        report("schema unknown known key rejected", False)
    except ScenarioDataError:
        report("schema unknown known key rejected", True)

    missing_name = _write_scenario_file(
        root,
        {
            "schema_version": 2,
            "scenarios": [
                {"runs": 1, "dec_calls": 1, "channel_mode": "mono", "transport": []}
            ],
        },
    )
    try:
        load_scenarios(missing_name)
        report("schema missing name rejected", False)
    except ScenarioDataError:
        report("schema missing name rejected", True)


def _production_payload():
    with open(PRODUCTION_DATA, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _scenario(payload, name):
    return next(entry for entry in payload["scenarios"] if entry["name"] == name)


def test_transport_schema_errors():
    root = tempfile.mkdtemp()
    cases = []

    payload = _production_payload()
    _scenario(payload, "mono_10ms").pop("transport")
    cases.append(("missing transport", payload))

    payload = _production_payload()
    _scenario(payload, "mono_10ms")["transport"][0]["extra"] = True
    cases.append(("unknown transport key", payload))

    payload = _production_payload()
    entry = _scenario(payload, "modea_10ms")["transport"][0]
    _scenario(payload, "modea_10ms")["transport"].append(json.loads(json.dumps(entry)))
    cases.append(("duplicate stream", payload))

    payload = _production_payload()
    _scenario(payload, "mono_10ms")["transport"][0]["fixtures"] = ["not-a-fixture"]
    cases.append(("unknown fixture", payload))

    payload = _production_payload()
    _scenario(payload, "mono_10ms")["transport"][0]["layout"] = "interleaved"
    cases.append(("bad layout", payload))

    payload = _production_payload()
    _scenario(payload, "mono_10ms")["transport"][0]["fixtures"] = []
    cases.append(("wrong mono fixture count", payload))

    payload = _production_payload()
    _scenario(payload, "modeb_10ms")["transport"][0]["fixtures"].reverse()
    cases.append(("Mode B fixture reversal", payload))

    payload = _production_payload()
    _scenario(payload, "invalid_sdu_resume_10ms")["transport"][0]["malformed_at"] = 128
    cases.append(("invalid malformed_at", payload))

    ok = True
    for label, payload in cases:
        path = _write_scenario_file(root, payload)
        try:
            load_scenarios(path)
            ok = False
            report("transport schema %s rejected" % label, False)
        except ScenarioDataError:
            report("transport schema %s rejected" % label, True)
    report("transport schema errors rejected", ok)


def test_transport_pass_fields_and_counts():
    root = tempfile.mkdtemp()
    recv = recv_pass("mono_10ms")
    valid = cli_pass("mono_10ms")
    missing = valid.replace(" txh1=0x%08X" % FNV1A_OFFSET_BASIS, "")
    missing_ok = not run_check(root, "mono_10ms", recv, missing)
    report("missing TX PASS field rejected", missing_ok)

    mismatch = cli_pass("mono_10ms", txc0=99)
    mismatch_ok = not run_check(root, "mono_10ms", recv, mismatch)
    report("TX count differing from sends rejected", mismatch_ok)

    good_hash = expected_transport_hash(TRANSPORT_VALUES["mono_10ms"][0], 100)
    changed = cli_pass("mono_10ms", txh0=good_hash ^ 1)
    hash_ok = not run_check(root, "mono_10ms", recv, changed)
    report("TX hash bit change rejected", hash_ok)


def _stereo_receiver(scenario):
    recv = recv_pass(scenario, total1=216)
    return recv.replace("lh1=0x12345678", "lh1=0x11111111").replace(
        "rh1=0x12345678", "rh1=0x22222222"
    )


def test_transport_channel_layout_hash_rejections():
    root = tempfile.mkdtemp()

    wrong_mono = {"stream": 0, "layout": "mono", "fixtures": ["bsim_48k_10ms_120b_r"]}
    mono_ok = not run_check(
        root,
        "mono_10ms",
        recv_pass("mono_10ms"),
        cli_pass("mono_10ms", txh0=expected_transport_hash(wrong_mono, 100)),
    )
    report("mono wrong-channel TX hash rejected", mono_ok)

    modea_left, modea_right = TRANSPORT_VALUES["modea_10ms"]
    modea_ok = not run_check(
        root,
        "modea_10ms",
        _stereo_receiver("modea_10ms"),
        cli_pass(
            "modea_10ms",
            sends0=110,
            sends1=110,
            txh0=expected_transport_hash(modea_right, 110),
            txh1=expected_transport_hash(modea_left, 110),
        ),
    )
    report("Mode A channel swap TX hashes rejected", modea_ok)

    reverse_modeb = {
        "stream": 0,
        "layout": "stereo-concat",
        "fixtures": ["bsim_48k_10ms_120b_r", "bsim_48k_10ms_120b_l"],
    }
    modeb_ok = not run_check(
        root,
        "modeb_10ms",
        _stereo_receiver("modeb_10ms"),
        cli_pass("modeb_10ms", txh0=expected_transport_hash(reverse_modeb, 100)),
    )
    report("Mode B concatenation reversal rejected", modeb_ok)


def _hash_records(records):
    hash_value = FNV1A_OFFSET_BASIS
    for sequence, payload in records:
        for byte in sequence.to_bytes(4, "little") + payload:
            hash_value = ((hash_value ^ byte) * 0x01000193) & 0xFFFFFFFF
    return hash_value


def _mono_10ms_frames(count):
    path = os.path.join(
        REPO_ROOT, "tests", "fixtures", "lc3", "bsim_48k_10ms_120b_l.lc3"
    )
    with open(path, "rb") as fh:
        raw = fh.read()
    return [raw[index * 120 : (index + 1) * 120] for index in range(count)]


def test_transport_sequence_and_payload_mutations():
    root = tempfile.mkdtemp()
    frames = _mono_10ms_frames(101)
    expected_hash = expected_transport_hash(TRANSPORT_VALUES["mono_10ms"][0], 100)
    ordered = [(sequence, frames[sequence]) for sequence in range(100)]
    omitted = [
        (sequence, frames[sequence])
        for sequence in list(range(20)) + list(range(21, 101))
    ]
    duplicated = (
        [(sequence, frames[sequence]) for sequence in range(50)]
        + [(49, frames[49])]
        + [(sequence, frames[sequence]) for sequence in range(50, 99)]
    )
    reordered = list(ordered)
    reordered[40], reordered[41] = reordered[41], reordered[40]
    corrupted = list(ordered)
    payload = bytearray(corrupted[50][1])
    payload[0] ^= 1
    corrupted[50] = (50, bytes(payload))
    mutations = {
        "omission": omitted,
        "duplication": duplicated,
        "reorder": reordered,
        "payload corruption": corrupted,
    }

    ok = True
    for label, records in mutations.items():
        observed_hash = _hash_records(records)
        rejected = not run_check(
            root,
            "mono_10ms",
            recv_pass("mono_10ms"),
            cli_pass("mono_10ms", txh0=observed_hash),
        )
        mutation_ok = observed_hash != expected_hash and rejected
        ok = ok and mutation_ok
        report("TX %s hash rejected" % label, mutation_ok)
    report("TX sequence and payload mutations differ from expected", ok)


def test_malformed_fixture_transport_hash():
    root = tempfile.mkdtemp()
    frames = _mono_10ms_frames(101)
    records = [(sequence, frames[sequence]) for sequence in range(101)]
    records[20] = (20, bytes(0x40 + index for index in range(119)))
    synthetic_hash = _hash_records(records)
    correct_hash = expected_transport_hash(
        TRANSPORT_VALUES["invalid_sdu_resume_10ms"][0], 101
    )
    recv = recv_pass("invalid_sdu_resume_10ms", derr1=1, obs_mal=1)
    truncated_ok = run_check(
        root,
        "invalid_sdu_resume_10ms",
        recv,
        cli_pass("invalid_sdu_resume_10ms", sends0=101),
    )
    synthetic_rejected = not run_check(
        root,
        "invalid_sdu_resume_10ms",
        recv,
        cli_pass("invalid_sdu_resume_10ms", sends0=101, txh0=synthetic_hash),
    )
    report(
        "malformed frame uses truncated corpus bytes",
        truncated_ok and synthetic_hash != correct_hash and synthetic_rejected,
    )


def test_no_transport_requires_zero_result():
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
    zero_ok = run_check(
        root,
        "unsupported_source_direction",
        recv,
        cli_pass("unsupported_source_direction", sends0=0),
    )
    nonzero_rejected = not run_check(
        root,
        "unsupported_source_direction",
        recv,
        cli_pass("unsupported_source_direction", sends0=1),
    )
    report(
        "no-transport scenarios require zero TX result", zero_ok and nonzero_rejected
    )


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


# ── BSIM_LOG_ROOT output-root behavior (shell runner) ────────────────────

BSIM_RUNNER = os.path.join(REPO_ROOT, "scripts", "bsim-stage1-run.sh")


def _run_bsim_runner(env_extra):
    env = dict(os.environ)
    env.pop("ZEPHYR_BASE", None)
    env.update(env_extra)
    proc = subprocess.run(
        ["bash", BSIM_RUNNER],
        capture_output=True,
        text=True,
        env=env,
        timeout=120,
    )
    return proc.returncode, proc.stdout + proc.stderr


def test_bsim_log_root_relative_rejected_early():
    rc, out = _run_bsim_runner({"BSIM_LOG_ROOT": "relative-dir"})
    report(
        "bsim log root relative rejected early",
        rc != 0
        and "BSIM_LOG_ROOT must be an absolute path" in out
        and "ZEPHYR_BASE" not in out,
    )


def test_bsim_log_root_root_and_home_rejected_early():
    ok = True
    for bad in ("/", os.path.expanduser("~")):
        rc, out = _run_bsim_runner({"BSIM_LOG_ROOT": bad})
        ok = ok and rc != 0 and "BSIM_LOG_ROOT" in out and "ZEPHYR_BASE" not in out
    report("bsim log root root/home rejected early", ok)


def test_bsim_log_root_repo_paths_rejected():
    ok = True
    for bad in (REPO_ROOT, os.path.join(REPO_ROOT, "docs")):
        rc, out = _run_bsim_runner({"BSIM_LOG_ROOT": bad})
        ok = ok and rc != 0 and "BSIM_LOG_ROOT" in out and "ZEPHYR_BASE" not in out
    report("bsim log root repo paths rejected early", ok)


def test_bsim_log_root_nonempty_and_file_rejected_preserving_output():
    root = tempfile.mkdtemp()
    nonempty = os.path.join(root, "nonempty")
    os.makedirs(nonempty)
    marker = os.path.join(nonempty, "keep.txt")
    with open(marker, "w") as fh:
        fh.write("keep")
    rc, out = _run_bsim_runner({"BSIM_LOG_ROOT": nonempty})
    ok = rc != 0 and "BSIM_LOG_ROOT must be an empty directory" in out
    ok = ok and os.path.exists(marker)
    report("bsim log root nonempty rejected, caller output preserved", ok)

    as_file = os.path.join(root, "afile")
    with open(as_file, "w") as fh:
        fh.write("x")
    rc, out = _run_bsim_runner({"BSIM_LOG_ROOT": as_file})
    report(
        "bsim log root existing file rejected",
        rc != 0 and "BSIM_LOG_ROOT is not a directory" in out,
    )


def test_bsim_log_root_valid_accepted_then_stops_at_nrfutil():
    # Deterministic stop after validation: ZEPHYR_BASE points at a fake
    # tree carrying the PHY binary so bsim-env.sh passes, and nrfutil is
    # stripped from PATH so the toolchain check is the next failure.
    path = [
        p
        for p in os.environ.get("PATH", "").split(os.pathsep)
        if not os.path.isfile(os.path.join(p, "nrfutil"))
    ]
    if any(os.path.isfile(os.path.join(p, "nrfutil")) for p in path):
        report(
            "bsim log root valid accepted then stops at nrfutil",
            False,
            "nrfutil still present on filtered PATH",
        )
        return
    with tempfile.TemporaryDirectory() as root:
        fake_zephyr = os.path.join(root, "fake-zephyr")
        os.makedirs(fake_zephyr)
        bsim_bin = os.path.join(root, "tools", "bsim", "bin")
        os.makedirs(bsim_bin)
        phy = os.path.join(bsim_bin, "bs_2G4_phy_v1")
        with open(phy, "w") as fh:
            fh.write("#!/usr/bin/env bash\nexit 0\n")
        os.chmod(phy, 0o755)
        log_root = os.path.join(root, "bsim-logs")
        rc, out = _run_bsim_runner(
            {
                "PATH": os.pathsep.join(path),
                "ZEPHYR_BASE": fake_zephyr,
                "BSIM_LOG_ROOT": log_root,
            }
        )
        ok = rc != 0 and "nrfutil not in PATH" in out
        ok = ok and "BSIM_LOG_ROOT must" not in out
        ok = ok and os.path.isdir(log_root) and os.listdir(log_root) == []
        report("bsim log root valid accepted then stops at nrfutil", ok)


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
    test_transport_schema_errors()
    test_transport_pass_fields_and_counts()
    test_transport_channel_layout_hash_rejections()
    test_transport_sequence_and_payload_mutations()
    test_malformed_fixture_transport_hash()
    test_no_transport_requires_zero_result()
    test_cli_known_precedence()
    test_bsim_log_root_relative_rejected_early()
    test_bsim_log_root_root_and_home_rejected_early()
    test_bsim_log_root_repo_paths_rejected()
    test_bsim_log_root_nonempty_and_file_rejected_preserving_output()
    test_bsim_log_root_valid_accepted_then_stops_at_nrfutil()
    print("=== %d PASS / %d FAIL ===" % (PASSES, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
