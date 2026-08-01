#!/usr/bin/env python3
"""Unit tests for scripts/bsim_stage1_parse.py — strict T4 scenario parser.

Covers PASS-line extraction, token parsing, per-scenario contract checks,
fault-marker scanning with allowlists, and every major failure mode.
No BabbleSim or hardware needed.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

from bsim_stage1_parse import (  # noqa: E402
    ParseError,
    check,
    extract_pass_line,
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
    ok = run_check(root, "modea_10ms", recv, cli_pass("modea_10ms", sends0=110, sends1=110), {})
    report("modea L!=R ok", ok)


def test_modea_lr_equal_rejected():
    root = tempfile.mkdtemp()
    recv = recv_pass("modea_10ms", total1=216)
    ok = run_check(root, "modea_10ms", recv, cli_pass("modea_10ms", sends0=110, sends1=110), {})
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
        obs_rel=1,
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
        obs_rel=1,
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
        "disconnect_streaming_10ms", pushes1=25, total1=33, adv_restart=1, obs_disc=1
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
        "obs_reason=0 obs_gate_o=2 obs_gate_c=0 obs_mal=0 obs_blk=0 obs_stale=0 "
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
    print("=== %d PASS / %d FAIL ===" % (PASSES, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
