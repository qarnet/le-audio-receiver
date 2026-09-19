#!/usr/bin/env python3
"""Unit tests for strict Stage 1 BSim parser and runner preflight.

Uses complete receiver/client PASS records. No BabbleSim or hardware needed.
"""

import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "scripts"))

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

from bsim_stage1_parse import (  # noqa: E402
    EXPECTED_SCENARIO_CONTRACTS,
    FNV1A_OFFSET_BASIS,
    ParseError,
    RECEIVER_ORACLE_VALUES,
    SCENARIOS,
    ScenarioDataError,
    TRANSPORT_VALUES,
    _STATEFUL_RECIPES,
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
BSIM_RUNNER = os.path.join(REPO_ROOT, "scripts", "bsim-stage1-run.sh")
PRODUCTION_DATA = os.path.join(REPO_ROOT, "tests", "bsim", "stage1-scenarios.json")


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
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def _recipe_prefix_counts(recipe_id, actions):
    recipe = _STATEFUL_RECIPES[recipe_id]
    remaining = actions
    valid = 0
    plc = 0

    for step in recipe["steps"]:
        consumed = min(remaining, step["count"])
        if step["action"] == "corpus":
            valid += consumed
        else:
            plc += consumed
        remaining -= consumed
        if remaining == 0:
            break
    if remaining != 0:
        raise AssertionError("recipe prefix overrun")
    return valid, plc


def _populate_segment(
    values,
    segment,
    *,
    left_recipe="none",
    right_recipe="none",
    pushes=0,
    transients=0,
    spc=0,
):
    suffix = str(segment)
    active = left_recipe != "none" or right_recipe != "none"
    if active != (pushes > 0):
        raise AssertionError("active segments must have pushes")
    if active:
        l_actions = transients + pushes
        r_actions = transients + pushes
        l_valid, l_plc = _recipe_prefix_counts(left_recipe, l_actions)
        r_valid, r_plc = _recipe_prefix_counts(right_recipe, r_actions)
        l_pre_valid, l_pre_plc = _recipe_prefix_counts(left_recipe, transients)
        r_pre_valid, r_pre_plc = _recipe_prefix_counts(right_recipe, transients)
    else:
        l_actions = l_valid = l_plc = 0
        r_actions = r_valid = r_plc = 0
        l_pre_valid = l_pre_plc = 0
        r_pre_valid = r_pre_plc = 0

    values.update(
        {
            "pushes" + suffix: pushes,
            "trans" + suffix: transients,
            "szero" + suffix: transients,
            "splc" + suffix: max(transients - 1, 0),
            "plc" + suffix: max(transients - 1, 0),
            "total" + suffix: 0,
            "derr" + suffix: 0,
            "mal" + suffix: 0,
            "samples" + suffix: spc * 2 if active else 0,
            "spc" + suffix: spc if active else 0,
            "diff" + suffix: 0,
            "lemin" + suffix: 1 if active else 0,
            "lemax" + suffix: 2 if active else 0,
            "remin" + suffix: 1 if active else 0,
            "remax" + suffix: 2 if active else 0,
            "lrid" + suffix: left_recipe,
            "lact" + suffix: l_actions,
            "lval" + suffix: l_valid,
            "lplc" + suffix: l_plc,
            "lfr" + suffix: l_valid,
            "lsm" + suffix: l_valid * spc,
            "lex" + suffix: l_plc - l_pre_plc,
            "lmax" + suffix: 0,
            "lsse" + suffix: 0,
            "lrms" + suffix: 0,
            "lcorr" + suffix: 32767 if active else 0,
            "lres" + suffix: 0 if active else 1,
            "rrid" + suffix: right_recipe,
            "ract" + suffix: r_actions,
            "rval" + suffix: r_valid,
            "rplc" + suffix: r_plc,
            "rfr" + suffix: r_valid,
            "rsm" + suffix: r_valid * spc,
            "rex" + suffix: r_plc - r_pre_plc,
            "rmax" + suffix: 0,
            "rsse" + suffix: 0,
            "rrms" + suffix: 0,
            "rcorr" + suffix: 32767 if active else 0,
            "rres" + suffix: 0 if active else 1,
        }
    )


def receiver_values(scenario):
    """Complete valid receiver record for one schema-owned scenario."""
    stereo = SCENARIOS[scenario][1] == "stereo"
    total = known_values(scenario).get("total", 0)
    values = {
        "scenario": scenario,
        "seg": len(RECEIVER_ORACLE_VALUES[scenario]),
        "after": 0,
        "adv_restart": 0,
        "pacs": 1,
        "obs_ok": 1,
        "obs_rej": 0,
        "obs_dir": 0,
        "obs_code": 0,
        "obs_reason": 0,
        "obs_gate_o": 1,
        "obs_gate_c": 0,
        "obs_mal": 0,
        "obs_blk": 0,
        "obs_stale": 0,
        "obs_rel": 0,
        "obs_disc": 0,
        "obs_rej_code": 0,
        "obs_rej_reason": 0,
        "obs_mts": 0,
        "obs_rel_ss": 0,
        "rel_ss_seq": 0,
        "disc_seq": 0,
        "limmax": 2048,
        "limrms": 512,
        "limcorr": 32750,
    }

    for segment, oracle in enumerate(RECEIVER_ORACLE_VALUES[scenario], start=1):
        left_recipe, right_recipe, completion = oracle
        left_actions = _STATEFUL_RECIPES[left_recipe]["output_action_count"]
        right_actions = _STATEFUL_RECIPES[right_recipe]["output_action_count"]
        if left_actions != right_actions:
            raise AssertionError("scenario recipes need matching action counts")
        pushes = 100 if completion == "full" else 25
        transients = left_actions - pushes if completion == "full" else 8
        spc = _STATEFUL_RECIPES[left_recipe]["samples_per_frame"]

        _populate_segment(
            values,
            segment,
            left_recipe=left_recipe,
            right_recipe=right_recipe,
            pushes=pushes,
            transients=transients,
            spc=spc,
        )
        values["total" + str(segment)] = total if segment == 1 else transients + pushes
        values["diff" + str(segment)] = 1 if stereo else 0

    for segment in range(len(RECEIVER_ORACLE_VALUES[scenario]) + 1, 3):
        _populate_segment(values, segment)

    if scenario == "modea_one_cis_loss_10ms":
        values["plc1"] = values["splc1"] + 18

    normal_audio = {
        "mono_10ms",
        "mono_7p5ms",
        "modea_10ms",
        "modea_7p5ms",
        "modea_reverse_start_10ms",
        "modeb_10ms",
        "modeb_7p5ms",
        "invalid_sdu_resume_10ms",
        "modea_one_cis_loss_10ms",
    }
    if scenario in normal_audio:
        if scenario == "invalid_sdu_resume_10ms":
            values.update({"derr1": 1, "obs_mal": 1})
        return values

    if scenario == "modea_first_stop_10ms":
        values.update({"obs_gate_c": 1, "obs_blk": 1, "obs_rel": 2})
        return values

    if scenario == "release_without_disable_10ms":
        values.update(
            {
                "obs_gate_c": 1,
                "obs_rel": 1,
                "obs_rel_ss": 1,
                "rel_ss_seq": 5,
                "disc_seq": 9,
                "obs_disc": 1,
            }
        )
        return values

    if scenario == "disconnect_streaming_10ms":
        values.update({"adv_restart": 1, "obs_gate_c": 1, "obs_disc": 1})
        return values

    if scenario == "reconnect_second_stream_10ms":
        values.update(
            {
                "adv_restart": 1,
                "obs_ok": 2,
                "obs_gate_o": 2,
                "obs_gate_c": 1,
                "obs_disc": 1,
            }
        )
        return values

    if scenario == "duplicate_release_10ms":
        values.update({"obs_gate_c": 1, "obs_rel": 2, "obs_rel_ss": 1, "obs_disc": 1})
        return values

    if scenario == "unsupported_source_direction":
        values.update({"obs_ok": 0, "obs_rej": 1, "obs_dir": 2, "obs_code": 7})
    elif scenario == "no_free_sink_slot":
        values.update(
            {
                "obs_ok": 3,
                "obs_rej": 1,
                "obs_rej_code": 13,
                "obs_rel": 3,
            }
        )
    elif scenario == "invalid_codec_fields":
        values.update(
            {
                "obs_ok": 2,
                "obs_rej": 9,
                "obs_rej_code": 8,
                "obs_rej_reason": 2,
                "obs_rel": 2,
            }
        )
    return values


def receiver_pass(scenario, **overrides):
    values = receiver_values(scenario)
    values.update(overrides)
    return (
        "d_00: @00:00:06.295366 INFO: le_audio_receiver: "
        + " ".join("%s=%s" % (key, value) for key, value in values.items())
        + "\n"
    )


def client_values(scenario, **overrides):
    values = {
        "scenario": scenario,
        "sends0": 100,
        "sends1": 0,
        "cfgrsps": 1,
        "relrsps": 0,
        "disrsps": 0,
    }
    if scenario in {
        "modea_10ms",
        "modea_7p5ms",
        "modea_reverse_start_10ms",
        "modea_one_cis_loss_10ms",
    }:
        values.update({"sends0": 110, "sends1": 110})
    elif scenario == "invalid_sdu_resume_10ms":
        values["sends0"] = 101
    elif scenario == "modea_first_stop_10ms":
        values.update({"sends0": 25, "sends1": 45, "relrsps": 2})
    elif scenario == "release_without_disable_10ms":
        values.update({"sends0": 25, "relrsps": 1})
    elif scenario == "disconnect_streaming_10ms":
        values["sends0"] = 25
    elif scenario == "reconnect_second_stream_10ms":
        values.update({"sends0": 25, "sends1": 100})
    elif scenario == "unsupported_source_direction":
        values.update({"sends0": 0, "cfgrsps": 1})
    elif scenario == "no_free_sink_slot":
        values.update({"sends0": 0, "cfgrsps": 4, "relrsps": 3})
    elif scenario == "invalid_codec_fields":
        values.update({"sends0": 0, "cfgrsps": 11})
    elif scenario == "duplicate_release_10ms":
        values.update({"sends0": 25, "cfgrsps": 2, "relrsps": 3})

    values.update(overrides)
    for stream in (0, 1):
        sends_key = "sends%d" % stream
        count_key = "txc%d" % stream
        hash_key = "txh%d" % stream
        if count_key not in overrides:
            values[count_key] = values[sends_key]
        if hash_key not in overrides:
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
    return values


def client_pass(scenario, **overrides):
    values = client_values(scenario, **overrides)
    return (
        "d_01: @00:00:06.400000 INFO: bsim_client: "
        "scenario=%s sends0=%d sends1=%d cfgrsps=%d relrsps=%d disrsps=%d "
        "txc0=%d txh0=0x%08X txc1=%d txh1=0x%08X\n"
        % (
            values["scenario"],
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


def known_for(scenario):
    total = known_values(scenario).get("total")
    return {"known_total": total} if total is not None else {}


def run_check(root, scenario, recv_text, cli_text, known=None):
    recv = write_log(root, "receiver.log", recv_text)
    cli = write_log(root, "client.log", cli_text)
    try:
        check(scenario, recv, cli, known_for(scenario) if known is None else known)
        return True
    except ParseError:
        return False


def remove_token(text, key):
    return re.sub(r"\s%s=[^\s]+" % re.escape(key), "", text)


def test_tokens_and_pass_extraction():
    tokens = parse_tokens(
        "INFO: le_audio_receiver: scenario=mono_10ms lcorr1=-12 txh0=0xFE0D4245"
    )
    report(
        "tokens preserve negative PCM metrics and TX hex",
        tokens["scenario"] == "mono_10ms"
        and tokens["lcorr1"] == -12
        and tokens["txh0"] == 0xFE0D4245,
    )
    root = tempfile.mkdtemp()
    path = write_log(root, "missing.log", "no pass line\n")
    try:
        extract_pass_line(path, "le_audio_receiver")
        report("missing PASS line rejected", False)
    except ParseError:
        report("missing PASS line rejected", True)


def test_complete_production_scenario_records():
    root = tempfile.mkdtemp()
    rejected = []
    for scenario in SCENARIOS:
        if not run_check(
            root, scenario, receiver_pass(scenario), client_pass(scenario)
        ):
            rejected.append(scenario)
    report(
        "complete records pass all 17 scenario contracts", not rejected, repr(rejected)
    )


def test_required_receiver_fields_fail_closed():
    root = tempfile.mkdtemp()
    base = receiver_pass("mono_10ms")
    report(
        "missing compiled PCM limit rejected",
        not run_check(
            root, "mono_10ms", remove_token(base, "limcorr"), client_pass("mono_10ms")
        ),
    )
    report(
        "missing PCM segment field rejected",
        not run_check(
            root, "mono_10ms", remove_token(base, "rres1"), client_pass("mono_10ms")
        ),
    )
    report(
        "non-integer PCM field rejected",
        not run_check(
            root,
            "mono_10ms",
            receiver_pass("mono_10ms", lmax1="oops"),
            client_pass("mono_10ms"),
        ),
    )
    receiver = write_log(root, "receiver.log", receiver_pass("mono_10ms"))
    client = write_log(
        root, "client.log", remove_token(client_pass("mono_10ms"), "txh1")
    )
    try:
        parse_receiver_pass(receiver)
        parse_client_pass(client)
        report("missing client TX field rejected", False)
    except ParseError:
        report("missing client TX field rejected", True)


def test_recipe_pass_fields_and_accounting_fail_closed():
    root = tempfile.mkdtemp()
    mono = "mono_10ms"
    base = receiver_pass(mono)
    for key in (
        "lrid1",
        "rrid1",
        "lact1",
        "lval1",
        "lplc1",
        "ract1",
        "rval1",
        "rplc1",
    ):
        report(
            "missing recipe field %s rejected" % key,
            not run_check(root, mono, remove_token(base, key), client_pass(mono)),
        )

    cases = [
        ("numeric recipe ID", {"lrid1": 1}),
        ("wrong left recipe", {"lrid1": "start8_10ms_r"}),
        ("unknown recipe", {"lrid1": "unknown_recipe"}),
        ("recipe actions", {"lact1": 107}),
        ("recipe valid count", {"lval1": 99}),
        ("recipe PLC count", {"lplc1": 7}),
        ("recipe geometry", {"spc1": 360}),
        ("receiver segment count", {"seg": 2}),
        ("absent segment recipe ID", {"lrid2": "start8_10ms_l"}),
        ("absent segment recipe actions", {"lact2": 1}),
    ]
    for label, overrides in cases:
        report(
            "%s rejected" % label,
            not run_check(
                root, mono, receiver_pass(mono, **overrides), client_pass(mono)
            ),
        )

    report(
        "duplicate PASS token rejected",
        not run_check(
            root,
            mono,
            base.rstrip() + " lact1=108\n",
            client_pass(mono),
        ),
    )
    report(
        "decoded receiver hash field rejected",
        not run_check(
            root, mono, base.rstrip() + " h1=0x12345678\n", client_pass(mono)
        ),
    )
    report(
        "unknown client PASS field rejected",
        not run_check(
            root,
            mono,
            base,
            client_pass(mono).rstrip() + " extra=1\n",
        ),
    )


def test_stateful_recipe_progress_fail_closed():
    root = tempfile.mkdtemp()
    loss = "modea_one_cis_loss_10ms"
    for key, value in (
        ("ract1", 107),
        ("rval1", 81),
        ("rplc1", 25),
        ("rex1", 17),
    ):
        report(
            "one-CIS loss %s exact contract rejected" % key,
            not run_check(
                root, loss, receiver_pass(loss, **{key: value}), client_pass(loss)
            ),
        )

    malformed = "invalid_sdu_resume_10ms"
    for key, value in (
        ("lrid1", "start8_10ms_l"),
        ("lact1", 107),
        ("lval1", 99),
    ):
        report(
            "malformed-resume %s rejected" % key,
            not run_check(
                root,
                malformed,
                receiver_pass(malformed, **{key: value}),
                client_pass(malformed),
            ),
        )

    prefix = "release_without_disable_10ms"
    for key, value in (("lval1", 24), ("lact1", 109)):
        report(
            "prefix recipe %s rejected" % key,
            not run_check(
                root, prefix, receiver_pass(prefix, **{key: value}), client_pass(prefix)
            ),
        )

    reconnect = "reconnect_second_stream_10ms"
    reconnect_values = receiver_values(reconnect)
    report(
        "reconnect segment-2 start7 recipe accepted",
        (
            reconnect_values["lrid1"],
            reconnect_values["rrid1"],
            reconnect_values["lrid2"],
            reconnect_values["rrid2"],
            reconnect_values["lact2"],
            reconnect_values["ract2"],
            reconnect_values["lval2"],
            reconnect_values["rval2"],
            reconnect_values["lplc2"],
            reconnect_values["rplc2"],
        )
        == (
            "start8_10ms_l",
            "start8_10ms_l",
            "start7_10ms_l",
            "start7_10ms_l",
            107,
            107,
            100,
            100,
            7,
            7,
        )
        and run_check(
            root, reconnect, receiver_pass(reconnect), client_pass(reconnect)
        ),
    )
    for key, value in (
        ("lrid2", "start8_10ms_l"),
        ("rrid2", "start8_10ms_l"),
        ("lrid2", "start8_10ms_r"),
        ("lact2", 108),
        ("lval2", 99),
    ):
        report(
            "reconnect fresh segment %s rejected" % key,
            not run_check(
                root,
                reconnect,
                receiver_pass(reconnect, **{key: value}),
                client_pass(reconnect),
            ),
        )

    no_audio = "unsupported_source_direction"
    report(
        "no-audio recipe evidence rejected",
        not run_check(
            root,
            no_audio,
            receiver_pass(no_audio, lrid1="start8_10ms_l"),
            client_pass(no_audio),
        ),
    )


def test_modea_7p5ms_asymmetric_startup_accounting():
    root = tempfile.mkdtemp()
    scenario = "modea_7p5ms"
    values = receiver_values(scenario)
    left_pre = _recipe_prefix_counts("modea_start_7p5ms_l", values["trans1"])
    right_pre = _recipe_prefix_counts("modea_start_7p5ms_r", values["trans1"])
    expected = (
        values["pushes1"] == 100
        and values["trans1"] == 13
        and values["lact1"] == values["ract1"] == 113
        and values["lval1"] == values["rval1"] == 101
        and values["lplc1"] == values["rplc1"] == 12
        and values["lfr1"] == values["rfr1"] == 101
        and values["lex1"] == values["rex1"] == 0
        and values["lsm1"] == values["rsm1"] == 101 * 360
        and left_pre == right_pre == (1, 12)
    )
    report(
        "Mode A 7.5 ms asymmetric startup accounting accepted",
        expected
        and run_check(root, scenario, receiver_pass(scenario), client_pass(scenario)),
    )

    for label, overrides in (
        ("action count", {"lact1": 112}),
        ("startup transient count", {"trans1": 12}),
        ("left startup valid frame", {"lval1": 100}),
        ("right startup PLC frame", {"rplc1": 11}),
        ("left post-boundary exclusion", {"lex1": 1}),
        ("right post-boundary exclusion", {"rex1": 1}),
        ("right compared frame", {"rfr1": 100}),
        ("right compared sample", {"rsm1": 100 * 360}),
    ):
        report(
            "Mode A 7.5 ms %s rejected" % label,
            not run_check(
                root,
                scenario,
                receiver_pass(scenario, **overrides),
                client_pass(scenario),
            ),
        )


def test_pcm_limits_and_metrics_fail_closed():
    root = tempfile.mkdtemp()
    cases = [
        ("receiver max limit mismatch", {"limmax": 2047}),
        ("max error violation", {"lmax1": 2049}),
        ("RMS violation", {"lrms1": 513}),
        ("correlation violation", {"lcorr1": 32749}),
        ("Q15 range violation", {"lcorr1": 32768}),
        ("non-pass evaluation", {"lres1": 1}),
        ("compared frame accounting", {"lfr1": 99}),
        ("compared sample accounting", {"lsm1": 47999}),
        ("excluded frame accounting", {"lex1": 1}),
    ]
    ok = True
    for label, overrides in cases:
        rejected = not run_check(
            root,
            "mono_10ms",
            receiver_pass("mono_10ms", **overrides),
            client_pass("mono_10ms"),
        )
        report(label + " rejected", rejected)
        ok = ok and rejected
    report("numerical PCM records fail closed", ok)


def test_routing_and_loss_coverage_fail_closed():
    root = tempfile.mkdtemp()
    mono_bad = not run_check(
        root, "mono_10ms", receiver_pass("mono_10ms", diff1=1), client_pass("mono_10ms")
    )
    stereo_bad = not run_check(
        root,
        "modea_10ms",
        receiver_pass("modea_10ms", diff1=0),
        client_pass("modea_10ms"),
    )
    negative_stereo = not run_check(
        root,
        "modea_10ms",
        receiver_pass("modea_10ms", diff1=-1),
        client_pass("modea_10ms"),
    )
    loss_bad = not run_check(
        root,
        "modea_one_cis_loss_10ms",
        receiver_pass("modea_one_cis_loss_10ms", rex1=17),
        client_pass("modea_one_cis_loss_10ms"),
    )
    report("mono non-identical routing rejected", mono_bad)
    report("stereo no-distinction routing rejected", stereo_bad)
    report("negative stereo distinction rejected", negative_stereo)
    report("one-CIS-loss exact right exclusion required", loss_bad)


def test_invalid_sdu_and_reconnect_contracts():
    root = tempfile.mkdtemp()
    invalid = "invalid_sdu_resume_10ms"
    bad_sends = not run_check(
        root, invalid, receiver_pass(invalid), client_pass(invalid, sends0=100)
    )
    exact_hash = expected_transport_hash(TRANSPORT_VALUES[invalid][0], 101)
    bad_hash = not run_check(
        root, invalid, receiver_pass(invalid), client_pass(invalid, txh0=exact_hash ^ 1)
    )
    bad_malformed = not run_check(
        root,
        invalid,
        receiver_pass(invalid, derr1=0, obs_mal=0),
        client_pass(invalid),
    )
    reconnect = "reconnect_second_stream_10ms"
    bad_second_total = not run_check(
        root,
        reconnect,
        receiver_pass(reconnect, total2=108),
        client_pass(reconnect),
    )
    report("invalid-SDU send count retained", bad_sends)
    report("invalid-SDU exact TX hash retained", bad_hash)
    report("invalid-SDU malformed/decode evidence retained", bad_malformed)
    report("reconnect fresh second total retained", bad_second_total)


def test_lifecycle_and_known_totals_fail_closed():
    root = tempfile.mkdtemp()
    release_bad = not run_check(
        root,
        "release_without_disable_10ms",
        receiver_pass("release_without_disable_10ms", rel_ss_seq=9, disc_seq=5),
        client_pass("release_without_disable_10ms"),
    )
    duplicate_bad = not run_check(
        root,
        "duplicate_release_10ms",
        receiver_pass("duplicate_release_10ms", obs_rel=3),
        client_pass("duplicate_release_10ms"),
    )
    known_bad = not run_check(
        root,
        "release_without_disable_10ms",
        receiver_pass("release_without_disable_10ms", total1=57),
        client_pass("release_without_disable_10ms"),
    )
    no_audio_bad = not run_check(
        root,
        "unsupported_source_direction",
        receiver_pass("unsupported_source_direction", pushes1=1, lfr1=1, lsm1=480),
        client_pass("unsupported_source_direction"),
    )
    report("release teardown ordering retained", release_bad)
    report("duplicate-release cleanup count retained", duplicate_bad)
    report("declared lifecycle known.total retained", known_bad)
    report("no-audio zero PCM metrics retained", no_audio_bad)


def test_transport_hash_contracts():
    root = tempfile.mkdtemp()
    mono = "mono_10ms"
    good = expected_transport_hash(TRANSPORT_VALUES[mono][0], 100)
    changed_hash = not run_check(
        root, mono, receiver_pass(mono), client_pass(mono, txh0=good ^ 1)
    )
    changed_count = not run_check(
        root, mono, receiver_pass(mono), client_pass(mono, txc0=99)
    )
    wrong_mono = {"stream": 0, "layout": "mono", "fixtures": ["bsim_48k_10ms_120b_r"]}
    wrong_channel = not run_check(
        root,
        mono,
        receiver_pass(mono),
        client_pass(mono, txh0=expected_transport_hash(wrong_mono, 100)),
    )
    no_transport = not run_check(
        root,
        "unsupported_source_direction",
        receiver_pass("unsupported_source_direction"),
        client_pass("unsupported_source_direction", sends0=1),
    )
    report("TX payload/sequence hash mutation rejected", changed_hash)
    report("TX count mismatch rejected", changed_count)
    report("TX fixture channel swap rejected", wrong_channel)
    report("no-transport TX rejected", no_transport)


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


def test_transport_byte_mutations_fail_closed():
    root = tempfile.mkdtemp()
    mono = "mono_10ms"
    frames = _mono_10ms_frames(101)
    expected_hash = expected_transport_hash(TRANSPORT_VALUES[mono][0], 100)
    ordered = [(sequence, frames[sequence]) for sequence in range(100)]
    mutations = {
        "omission": [
            (sequence, frames[sequence])
            for sequence in list(range(20)) + list(range(21, 101))
        ],
        "duplication": [(sequence, frames[sequence]) for sequence in range(50)]
        + [(49, frames[49])]
        + [(sequence, frames[sequence]) for sequence in range(50, 99)],
        "reorder": ordered[:40] + [ordered[41], ordered[40]] + ordered[42:],
        "payload corruption": ordered[:50]
        + [(50, bytes([frames[50][0] ^ 1]) + frames[50][1:])]
        + ordered[51:],
    }
    for label, records in mutations.items():
        observed_hash = _hash_records(records)
        report(
            "TX %s byte/sequence mutation rejected" % label,
            observed_hash != expected_hash
            and not run_check(
                root,
                mono,
                receiver_pass(mono),
                client_pass(mono, txh0=observed_hash),
            ),
        )

    malformed = "invalid_sdu_resume_10ms"
    records = [(sequence, frames[sequence]) for sequence in range(101)]
    records[20] = (20, bytes(0x40 + index for index in range(119)))
    synthetic_hash = _hash_records(records)
    correct_hash = expected_transport_hash(TRANSPORT_VALUES[malformed][0], 101)
    report(
        "malformed transport uses truncated corpus frame",
        synthetic_hash != correct_hash
        and run_check(root, malformed, receiver_pass(malformed), client_pass(malformed))
        and not run_check(
            root,
            malformed,
            receiver_pass(malformed),
            client_pass(malformed, txh0=synthetic_hash),
        ),
    )

    modea = "modea_10ms"
    left, right = TRANSPORT_VALUES[modea]
    report(
        "Mode A transport channel swap rejected",
        not run_check(
            root,
            modea,
            receiver_pass(modea),
            client_pass(
                modea,
                txh0=expected_transport_hash(right, 110),
                txh1=expected_transport_hash(left, 110),
            ),
        ),
    )
    reverse_modeb = {
        "stream": 0,
        "layout": "stereo-concat",
        "fixtures": ["bsim_48k_10ms_120b_r", "bsim_48k_10ms_120b_l"],
    }
    report(
        "Mode B concatenation reversal rejected",
        not run_check(
            root,
            "modeb_10ms",
            receiver_pass("modeb_10ms"),
            client_pass("modeb_10ms", txh0=expected_transport_hash(reverse_modeb, 100)),
        ),
    )
    report(
        "no-transport zero TX accepted",
        run_check(
            root,
            "unsupported_source_direction",
            receiver_pass("unsupported_source_direction"),
            client_pass("unsupported_source_direction"),
        ),
    )


def test_fault_scan_allowlist():
    root = tempfile.mkdtemp()
    warning = (
        "d_00: \x1b[1;33m<wrn> bt_ascs: Invalid operation in state: releasing\x1b[0m\n"
    )
    accepted = run_check(
        root,
        "duplicate_release_10ms",
        receiver_pass("duplicate_release_10ms") + warning,
        client_pass("duplicate_release_10ms"),
    )
    outside = not run_check(
        root,
        "mono_10ms",
        receiver_pass("mono_10ms") + warning,
        client_pass("mono_10ms"),
    )
    error = not run_check(
        root,
        "duplicate_release_10ms",
        receiver_pass("duplicate_release_10ms") + "d_00: <err> bt_ascs: unexpected\n",
        client_pass("duplicate_release_10ms"),
    )
    report("scenario-17 exact warning allowlisted", accepted)
    report("scenario-17 warning rejected elsewhere", outside)
    report("all other warnings/errors rejected", error)
    fault_cases = (
        (
            "receiver semantic fault",
            "mono_10ms",
            receiver_pass("mono_10ms") + "d_00: FATAL receiver failure\n",
            client_pass("mono_10ms"),
        ),
        (
            "receiver warning",
            "mono_10ms",
            receiver_pass("mono_10ms") + "d_00: <wrn> bt_hci_core: unexpected\n",
            client_pass("mono_10ms"),
        ),
        (
            "receiver error",
            "mono_10ms",
            receiver_pass("mono_10ms") + "d_00: <err> bt_gatt: unexpected\n",
            client_pass("mono_10ms"),
        ),
        (
            "client warning",
            "mono_10ms",
            receiver_pass("mono_10ms"),
            client_pass("mono_10ms") + "d_01: <wrn> bsim_tx: unexpected\n",
        ),
        (
            "client error",
            "mono_10ms",
            receiver_pass("mono_10ms"),
            client_pass("mono_10ms") + "d_01: <err> bsim_tx: unexpected\n",
        ),
        (
            "scenario-17 client warning",
            "duplicate_release_10ms",
            receiver_pass("duplicate_release_10ms"),
            client_pass("duplicate_release_10ms") + warning,
        ),
        (
            "scenario-17 warning text mutation",
            "duplicate_release_10ms",
            receiver_pass("duplicate_release_10ms") + warning.rstrip() + " extra\n",
            client_pass("duplicate_release_10ms"),
        ),
    )
    for label, scenario, receiver, client in fault_cases:
        report(
            "%s rejected" % label,
            not run_check(root, scenario, receiver, client),
        )


def _production_payload():
    with open(PRODUCTION_DATA, "r", encoding="utf-8") as fh:
        return json.load(fh)


def _write_scenario_file(root, payload):
    path = os.path.join(root, "scenarios.json")
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh)
    return path


def test_schema_three_and_matrix_contract():
    data = load_scenarios()
    scenarios = data["scenarios"]
    report("production schema 3", data["schema_version"] == 3)
    report(
        "production fixed 17/26 matrix",
        len(scenarios) == 17
        and sum(entry["runs"] for entry in scenarios) == 26
        and {entry["name"] for entry in scenarios} == set(EXPECTED_SCENARIO_CONTRACTS),
    )
    report(
        "decoded PCM known hashes removed",
        all(set((entry.get("known") or {})) <= {"total"} for entry in scenarios),
    )
    reconnect = next(
        entry for entry in scenarios if entry["name"] == "reconnect_second_stream_10ms"
    )
    report(
        "production reconnect segment-2 recipe mapping",
        [
            (entry["left_recipe"], entry["right_recipe"], entry["completion"])
            for entry in reconnect["receiver_oracle"]
        ]
        == [
            ("start8_10ms_l", "start8_10ms_l", "prefix"),
            ("start7_10ms_l", "start7_10ms_l", "full"),
        ],
    )

    root = tempfile.mkdtemp()
    cases = []
    payload = _production_payload()
    payload["scenarios"][0]["known"]["full"] = "0x12345678"
    cases.append(("decoded known hash", payload))
    payload = _production_payload()
    payload["scenarios"][0]["runs"] = 1
    cases.append(("changed run count", payload))
    payload = _production_payload()
    payload["scenarios"].pop()
    cases.append(("missing scenario", payload))
    payload = _production_payload()
    payload["scenarios"][0]["channel_mode"] = "stereo"
    cases.append(("changed channel mode", payload))
    payload = _production_payload()
    payload["scenarios"][0]["known"]["total"] = 109
    cases.append(("changed known total", payload))
    payload = _production_payload()
    payload["scenarios"][0]["transport"][0]["fixtures"] = ["bsim_48k_10ms_120b_r"]
    cases.append(("changed transport fixture", payload))
    payload = _production_payload()
    payload["scenarios"][0].pop("receiver_oracle")
    cases.append(("missing receiver oracle", payload))
    payload = _production_payload()
    payload["scenarios"][0]["receiver_oracle"][0]["extra"] = True
    cases.append(("unknown receiver oracle field", payload))
    payload = _production_payload()
    payload["scenarios"][0]["receiver_oracle"][0]["left_recipe"] = "unknown_recipe"
    cases.append(("unknown receiver recipe", payload))
    payload = _production_payload()
    payload["scenarios"][0]["receiver_oracle"][0]["completion"] = "partial"
    cases.append(("unknown receiver completion", payload))
    payload = _production_payload()
    payload["scenarios"][0]["receiver_oracle"][0]["right_recipe"] = "start8_10ms_r"
    cases.append(("changed receiver mapping", payload))
    payload = _production_payload()
    reconnect = next(
        entry
        for entry in payload["scenarios"]
        if entry["name"] == "reconnect_second_stream_10ms"
    )
    reconnect["receiver_oracle"][1]["left_recipe"] = "start8_10ms_l"
    cases.append(("reconnect segment-2 left start8 recipe", payload))
    payload = _production_payload()
    reconnect = next(
        entry
        for entry in payload["scenarios"]
        if entry["name"] == "reconnect_second_stream_10ms"
    )
    reconnect["receiver_oracle"][1]["right_recipe"] = "start8_10ms_l"
    cases.append(("reconnect segment-2 right start8 recipe", payload))
    payload = _production_payload()
    payload["scenarios"][0]["receiver_oracle"].append(
        dict(payload["scenarios"][0]["receiver_oracle"][0])
    )
    cases.append(("extra receiver segment", payload))
    payload = _production_payload()
    no_audio = next(
        entry
        for entry in payload["scenarios"]
        if entry["name"] == "unsupported_source_direction"
    )
    no_audio["receiver_oracle"] = [
        {
            "left_recipe": "start8_10ms_l",
            "right_recipe": "start8_10ms_l",
            "completion": "full",
        }
    ]
    cases.append(("no-audio receiver segment", payload))
    payload = _production_payload()
    malformed = next(
        entry
        for entry in payload["scenarios"]
        if entry["name"] == "invalid_sdu_resume_10ms"
    )
    malformed["transport"][0]["malformed_at"] = 19
    cases.append(("changed malformed index", payload))
    payload = _production_payload()
    modea = next(
        entry for entry in payload["scenarios"] if entry["name"] == "modea_10ms"
    )
    modea["transport"].reverse()
    cases.append(("transport order mutation", payload))
    payload = _production_payload()
    payload["unexpected"] = True
    cases.append(("unknown top-level field", payload))
    payload = _production_payload()
    payload["scenarios"][0]["unexpected"] = True
    cases.append(("unknown scenario field", payload))
    payload = _production_payload()
    payload["scenarios"][0]["known"] = None
    cases.append(("null known object", payload))
    for label, payload in cases:
        try:
            load_scenarios(_write_scenario_file(root, payload))
            report("schema %s rejected" % label, False)
        except ScenarioDataError:
            report("schema %s rejected" % label, True)

    invalid_path = write_log(root, "invalid.json", "{not valid json")
    missing_path = os.path.join(root, "missing.json")
    malformed_root_path = write_log(root, "bad-root.json", "[]")
    for label, path in (
        ("missing file", missing_path),
        ("invalid JSON", invalid_path),
        ("invalid root", malformed_root_path),
    ):
        try:
            load_scenarios(path)
            report("schema %s rejected" % label, False)
        except ScenarioDataError:
            report("schema %s rejected" % label, True)


def _run_cli(args):
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = bsim_main(args)
        except SystemExit as exc:
            code = exc.code
    return code, out.getvalue() + err.getvalue()


def test_cli_only_exposes_known_total_and_numerical_metrics():
    root = tempfile.mkdtemp()
    recv = write_log(root, "receiver.log", receiver_pass("mono_10ms"))
    client = write_log(root, "client.log", client_pass("mono_10ms"))
    base = ["check", "--scenario", "mono_10ms", "--receiver", recv, "--client", client]
    code, output = _run_cli(base)
    report("CLI file known.total passes", code == 0)
    report(
        "CLI prints numerical metrics, not decoded hashes",
        "lmax1=" in output and " h1=" not in output,
    )
    code, _ = _run_cli(base + ["--known-total", "200"])
    report("CLI known-total override rejects mismatch", code != 0)
    code, _ = _run_cli(base + ["--known-full", "0x12345678"])
    report("CLI decoded hash option removed", code != 0)

    changed = write_log(
        root, "changed-total.log", receiver_pass("mono_10ms", total1=109)
    )
    changed_base = [
        "check",
        "--scenario",
        "mono_10ms",
        "--receiver",
        changed,
        "--client",
        client,
    ]
    code, _ = _run_cli(changed_base)
    report("CLI versioned known.total rejects changed total", code != 0)
    code, _ = _run_cli(changed_base + ["--no-known"])
    report("CLI no-known disables only known.total pin", code == 0)
    code, _ = _run_cli(changed_base + ["--known-total", "109"])
    report("CLI explicit known.total overrides file pin", code == 0)


def _run_bsim_runner(env_extra):
    env = dict(os.environ)
    env.pop("ZEPHYR_BASE", None)
    env.update(env_extra)
    proc = subprocess.run(
        ["bash", BSIM_RUNNER], capture_output=True, text=True, env=env, timeout=120
    )
    return proc.returncode, proc.stdout + proc.stderr


def test_runner_preflight_contracts():
    rc, output = _run_bsim_runner({"BSIM_BASELINE": "1"})
    report(
        "BSIM_BASELINE rejected before toolchain setup",
        rc != 0
        and "BSIM_BASELINE=1 was removed" in output
        and "ZEPHYR_BASE" not in output,
    )
    rc, output = _run_bsim_runner({"BSIM_LOG_ROOT": "relative-dir"})
    report(
        "relative BSIM_LOG_ROOT rejected before toolchain setup",
        rc != 0
        and "BSIM_LOG_ROOT must be an absolute path" in output
        and "ZEPHYR_BASE" not in output,
    )


def test_runner_log_root_ownership_preflight():
    for label, path in (("root", "/"), ("home", os.path.expanduser("~"))):
        rc, output = _run_bsim_runner({"BSIM_LOG_ROOT": path})
        report(
            "BSIM_LOG_ROOT %s rejected before toolchain setup" % label,
            rc != 0 and "BSIM_LOG_ROOT" in output and "ZEPHYR_BASE" not in output,
        )

    for label, path in (
        ("repository", REPO_ROOT),
        ("repository child", os.path.join(REPO_ROOT, "docs")),
    ):
        rc, output = _run_bsim_runner({"BSIM_LOG_ROOT": path})
        report(
            "BSIM_LOG_ROOT %s rejected" % label,
            rc != 0 and "BSIM_LOG_ROOT" in output and "ZEPHYR_BASE" not in output,
        )

    with tempfile.TemporaryDirectory() as root:
        nonempty = os.path.join(root, "nonempty")
        os.makedirs(nonempty)
        marker = os.path.join(nonempty, "keep.txt")
        with open(marker, "w", encoding="utf-8") as fh:
            fh.write("keep")
        rc, output = _run_bsim_runner({"BSIM_LOG_ROOT": nonempty})
        report(
            "nonempty caller log root rejected without deletion",
            rc != 0
            and "BSIM_LOG_ROOT must be an empty directory" in output
            and os.path.exists(marker),
        )

        as_file = os.path.join(root, "not-a-directory")
        with open(as_file, "w", encoding="utf-8") as fh:
            fh.write("x")
        rc, output = _run_bsim_runner({"BSIM_LOG_ROOT": as_file})
        report(
            "file caller log root rejected",
            rc != 0 and "BSIM_LOG_ROOT is not a directory" in output,
        )

        filtered_path = [
            path
            for path in os.environ.get("PATH", "").split(os.pathsep)
            if not os.path.isfile(os.path.join(path, "nrfutil"))
        ]
        if any(os.path.isfile(os.path.join(path, "nrfutil")) for path in filtered_path):
            report(
                "valid caller log root reaches nrfutil preflight",
                False,
                "nrfutil remains",
            )
            return

        fake_zephyr = os.path.join(root, "fake-zephyr")
        os.makedirs(fake_zephyr)
        bsim_bin = os.path.join(root, "tools", "bsim", "bin")
        os.makedirs(bsim_bin)
        phy = os.path.join(bsim_bin, "bs_2G4_phy_v1")
        with open(phy, "w", encoding="utf-8") as fh:
            fh.write("#!/usr/bin/env bash\nexit 0\n")
        os.chmod(phy, 0o755)
        log_root = os.path.join(root, "caller-logs")
        rc, output = _run_bsim_runner(
            {
                "PATH": os.pathsep.join(filtered_path),
                "ZEPHYR_BASE": fake_zephyr,
                "BSIM_OUT_PATH": "",
                "BSIM_LOG_ROOT": log_root,
            }
        )
        report(
            "valid caller log root preserved through nrfutil preflight",
            rc != 0
            and "nrfutil not in PATH" in output
            and os.path.isdir(log_root)
            and not os.listdir(log_root),
        )


def main():
    print("=== bsim_stage1_parse unit tests ===")
    test_tokens_and_pass_extraction()
    test_complete_production_scenario_records()
    test_required_receiver_fields_fail_closed()
    test_recipe_pass_fields_and_accounting_fail_closed()
    test_stateful_recipe_progress_fail_closed()
    test_modea_7p5ms_asymmetric_startup_accounting()
    test_pcm_limits_and_metrics_fail_closed()
    test_routing_and_loss_coverage_fail_closed()
    test_invalid_sdu_and_reconnect_contracts()
    test_lifecycle_and_known_totals_fail_closed()
    test_transport_hash_contracts()
    test_transport_byte_mutations_fail_closed()
    test_fault_scan_allowlist()
    test_schema_three_and_matrix_contract()
    test_cli_only_exposes_known_total_and_numerical_metrics()
    test_runner_preflight_contracts()
    test_runner_log_root_ownership_preflight()
    print("=== %d PASS / %d FAIL ===" % (PASSES, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
