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
        self.assertEqual(p.require_secure_state(False, False, False, False), (True, ""))

    def test_all_requirements_hold(self):
        self.assertEqual(p.require_secure_state(True, True, True, True), (True, ""))

    def test_not_connected(self):
        ok, reason = p.require_secure_state(True, True, False, True)
        self.assertFalse(ok)
        self.assertIn("not connected", reason)

    def test_not_paired(self):
        ok, reason = p.require_secure_state(True, False, True, True)
        self.assertFalse(ok)
        self.assertIn("not paired", reason)

    def test_not_encrypted(self):
        ok, reason = p.require_secure_state(True, True, True, False)
        self.assertFalse(ok)
        self.assertIn("not encrypted", reason)


if __name__ == "__main__":
    unittest.main()
