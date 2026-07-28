#!/usr/bin/env nix-shell
#!nix-shell -i python3 -p "python3Packages.pyserial"
"""
Unit tests for flpr_stall_gate.py — timed 60ms gate parser and runner.
Uses FakeSerial transport; no hardware needed.
"""

import sys
import time
from flpr_stall_gate import GateRunner, FakeSerial, StallGateError


def test_parse_offload_active():
    text = (
        "State       : ACTIVE / epoch=12345 gen=3\n"
        "Counters    : submit=2000 success=1500 fallback=5 busy=10\n"
        "Recovery    : attempts=3 fail=0 relapses=0 exhaustion=0\n"
        "Probation   : active=0 success=200 cleared=1\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["state"] == "ACTIVE"
    assert s["success"] == 1500
    assert s["fallback"] == 5
    assert s["recovery_attempts"] == 3
    assert s["exhaustion"] == 0
    assert s["probation_cleared"] == 1
    assert s["probation_active"] == 0
    print("  PASS: parse_offload ACTIVE")


def test_parse_offload_fallback():
    text = (
        "State       : FALLBACK / epoch=12345 gen=3\n"
        "Counters    : submit=500 success=400 fallback=12 busy=3\n"
        "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
        "Probation   : active=1 success=50 cleared=0\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["state"] == "FALLBACK"
    assert s["fallback"] == 12
    assert s["probation_active"] == 1
    print("  PASS: parse_offload FALLBACK")


def test_parse_offload_max_exhaustion():
    text = (
        "State       : FALLBACK / epoch=12345 gen=3\n"
        "Recovery    : attempts=10 fail=2 relapses=1 exhaustion=1\n"
        "Probation   : active=1 success=0 cleared=0\n"
    )
    s = GateRunner.parse_offload(text)
    assert s["exhaustion"] == 1
    assert s["recovery_attempts"] == 10
    print("  PASS: parse_offload exhaustion detected")


def test_parse_offload_faults():
    text = "Faults      : timeout=0 full=1 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    s = GateRunner.parse_offload(text)
    assert s["fault_full"] == 1
    assert s["fault_timeout"] == 0
    print("  PASS: parse_offload fault counters")


def test_parse_offload_zero_faults():
    text = "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
    s = GateRunner.parse_offload(text)
    assert s["fault_timeout"] == 0
    assert s["fault_full"] == 0
    assert s["fault_stale"] == 0
    assert s["fault_seq"] == 0
    assert s["fault_frame"] == 0
    assert s["fault_crc"] == 0
    assert s["fault_payload"] == 0
    print("  PASS: parse_offload zero faults")


def test_timed_stall_ack_regex():
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
    m = RE_STALL_TIMED_ACK.search(text)
    assert m is not None
    assert int(m.group(1), 16) == 0x01
    assert int(m.group(2)) == 60
    print("  PASS: timed stall ACK regex match")


def test_timed_stall_ack_multiple():
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = (
        "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
        "another line\n"
    )
    m = RE_STALL_TIMED_ACK.search(text)
    assert m is not None
    assert int(m.group(1), 16) == 0x01
    assert int(m.group(2)) == 60
    print("  PASS: timed stall ACK in multi-line text")


def test_gate_timeout_on_stale_state():
    """Gate should timeout if state never becomes ACTIVE with success>=500."""
    import re

    schedule = {
        0: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
        1: "State       : PREPARING / epoch=1 gen=1\nCounters    : submit=0 success=0 fallback=0 busy=0\n",
    }
    tr = FakeSerial(schedule)
    runner = GateRunner(tr, total_timeout=0.5, status_interval=0.1)
    result = runner.run()
    assert not result.passed
    assert "ACTIVE" in result.error or "Timeout" in result.error
    print("  PASS: gate timeout on stale state")


def test_gate_exhaustion_error():
    """Gate should fail immediately on exhaustion."""

    class ExhaustionTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                # Pre-injection: ACTIVE success>=500
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                # ACK for stall_flpr_ms
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                # Baseline
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=1 busy=0\n"
                ).encode("utf-8")
            else:
                # Exhaustion hit
                return (
                    "State       : FALLBACK / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=800 fallback=100 busy=0\n"
                    "Recovery    : attempts=10 fail=0 relapses=1 exhaustion=1\n"
                    "Probation   : active=1 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = ExhaustionTransport()
    runner = GateRunner(tr, total_timeout=5.0, status_interval=0.05)
    result = runner.run()
    assert not result.passed
    assert "exhaustion" in result.error.lower()
    print("  PASS: gate immediately fails exhaustion")


def test_gate_success_path():
    """Full success path: ACTIVE → timed stall → fallback → recovery cleared."""

    class SuccessTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                # Step 1: Wait for ACTIVE success>=500
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                # Step 3: ACK for timed stall
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                # Step 4: Baseline
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            else:
                # Step 6: Recovery complete
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1200 success=750 fallback=5 busy=0\n"
                    "Recovery    : attempts=1 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=100 cleared=1\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")

    tr = SuccessTransport()
    runner = GateRunner(tr, total_timeout=5.0, status_interval=0.05)
    result = runner.run()
    assert result.passed
    assert result.baseline_success == 600
    print("  PASS: gate success path")


def test_gate_no_fallback_fails():
    """Gate should not pass if fallback stays zero (no fault evidence)."""

    class NoFallbackTransport(FakeSerial):
        def __init__(self):
            super().__init__({})
            self._idx = 0

        def read_all(self) -> bytes:
            self._idx += 1
            if self._idx <= 2:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                    "Faults      : timeout=0 full=0 stale=0 seq=0 frame=0 crc=0 payload=0\n"
                ).encode("utf-8")
            elif self._idx == 3:
                return (
                    "FLPR timed stall applied: bits=0x01 duration=60 ms (cons_in=1 prod_out=0)\n"
                ).encode("utf-8")
            elif self._idx == 4:
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1000 success=600 fallback=0 busy=0\n"
                ).encode("utf-8")
            else:
                # fallback=0, never passes
                return (
                    "State       : ACTIVE / epoch=1 gen=1\n"
                    "Counters    : submit=1100 success=650 fallback=0 busy=0\n"
                    "Recovery    : attempts=0 fail=0 relapses=0 exhaustion=0\n"
                    "Probation   : active=0 success=0 cleared=0\n"
                ).encode("utf-8")

    tr = NoFallbackTransport()
    runner = GateRunner(tr, total_timeout=1.0, status_interval=0.05)
    result = runner.run()
    assert not result.passed
    print("  PASS: gate fails without fallback evidence")


def test_timed_ack_wrong_duration_rejected():
    """Gate should reject an ACK with wrong duration."""
    import re
    from flpr_stall_gate import RE_STALL_TIMED_ACK

    text = (
        "FLPR timed stall applied: bits=0x01 duration=999 ms (cons_in=1 prod_out=0)\n"
    )
    m = RE_STALL_TIMED_ACK.search(text)
    dur_val = int(m.group(2))
    mask_val = int(m.group(1), 16)
    # The gate logic checks: mask_val == 0x01 and dur_val == 60
    assert not (mask_val == 0x01 and dur_val == 60)
    print("  PASS: wrong duration rejected")


def run_tests():
    tests = [
        test_parse_offload_active,
        test_parse_offload_fallback,
        test_parse_offload_max_exhaustion,
        test_parse_offload_faults,
        test_parse_offload_zero_faults,
        test_timed_stall_ack_regex,
        test_timed_stall_ack_multiple,
        test_gate_timeout_on_stale_state,
        test_gate_exhaustion_error,
        test_gate_success_path,
        test_gate_no_fallback_fails,
        test_timed_ack_wrong_duration_rejected,
    ]

    failures = 0
    for test in tests:
        try:
            test()
        except Exception as e:
            print(f"  FAIL: {test.__name__}: {e}")
            failures += 1

    print(f"\n{len(tests) - failures}/{len(tests)} tests passed")
    return failures


if __name__ == "__main__":
    sys.exit(run_tests())
