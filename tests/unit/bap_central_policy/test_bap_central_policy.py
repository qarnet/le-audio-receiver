#!/usr/bin/env python3
"""Unit tests for scripts/bap_central_policy.py (pure decision helpers)."""

import os
import sys
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)

import bap_central_policy as p  # noqa: E402


class TestConnectionStrategy(unittest.TestCase):
    def test_fresh_uses_raw_hci(self):
        self.assertEqual(p.connection_strategy(False, False)[0], "raw_hci")
        self.assertEqual(p.connection_strategy(False, True)[0], "raw_hci")

    def test_preserve_uses_bluez_connect_when_paired(self):
        verb, detail = p.connection_strategy(True, True)
        self.assertEqual(verb, "bluez_connect")
        self.assertIn("Device1.Connect()", detail)

    def test_preserve_fails_without_bond(self):
        verb, detail = p.connection_strategy(True, False)
        self.assertEqual(verb, "fail")
        self.assertIn("bond missing", detail)


class TestShouldConnect(unittest.TestCase):
    def test_already_connected_skips_connect(self):
        self.assertFalse(p.should_connect(True, "bluez_connect"))

    def test_not_connected_connects(self):
        self.assertTrue(p.should_connect(False, "bluez_connect"))

    def test_raw_hci_never_connects_via_bluez(self):
        self.assertFalse(p.should_connect(False, "raw_hci"))


class TestConnectOutcome(unittest.TestCase):
    def test_reply_ok(self):
        self.assertEqual(p.connect_outcome(True, None), "ok")

    def test_error_reply_wins_over_ok(self):
        # A Connect failure that races the reply handler must read as error.
        self.assertEqual(p.connect_outcome(True, "org.bluez.Error.Failed"), "error")

    def test_timeout_when_no_reply(self):
        self.assertEqual(p.connect_outcome(False, None), "timeout")


class TestPairAction(unittest.TestCase):
    def test_default_pairs(self):
        self.assertEqual(p.pair_action(False, False)[0], "pair")
        self.assertEqual(p.pair_action(False, True)[0], "pair")

    def test_preserve_bond_skips_when_paired(self):
        verb, detail = p.pair_action(True, True)
        self.assertEqual(verb, "skip")
        self.assertIn("skipping Pair()", detail)

    def test_preserve_bond_rejects_missing_bond(self):
        verb, detail = p.pair_action(True, False)
        self.assertEqual(verb, "fail")
        self.assertIn("bond missing", detail)


class TestRequireSecureState(unittest.TestCase):
    def test_inactive_always_ok(self):
        self.assertEqual(p.require_secure_state(False, False, False), (True, ""))

    def test_all_requirements_hold(self):
        self.assertEqual(p.require_secure_state(True, True, True), (True, ""))

    def test_not_connected(self):
        ok, reason = p.require_secure_state(True, True, False)
        self.assertFalse(ok)
        self.assertIn("not connected", reason)

    def test_not_paired(self):
        ok, reason = p.require_secure_state(True, False, True)
        self.assertFalse(ok)
        self.assertIn("not paired", reason)

    def test_no_encrypted_dependency(self):
        # BlueZ Device1 has no portable Encrypted property; Paired+Connected
        # plus encrypted PACS/ASCS access is the security proof.
        self.assertEqual(p.require_secure_state(True, True, True), (True, ""))


if __name__ == "__main__":
    unittest.main()
