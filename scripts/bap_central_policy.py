#!/usr/bin/env python3
"""Pure decision helpers for bap_central.py --preserve-bond (stdlib only).

Kept import-light (no dbus, no liblc3) so the reconnect/pairing decision
logic is unit-testable without a BlueZ stack.
"""


def pair_action(preserve_bond, already_paired):
    """Decide the pairing step.

    Returns a (verb, detail) tuple:
      ("pair", "")   default fresh-pair flow (or preserve-bond with an
                     existing host bond is handled by ("skip", ...));
      ("skip", ...)  --preserve-bond and the host bond already exists:
                     do not call Pair() again;
      ("fail", ...)  --preserve-bond and the host bond is missing.
    """
    if not preserve_bond:
        return ("pair", "")
    if already_paired:
        return ("skip", "--preserve-bond: host bond exists, skipping Pair()")
    return ("fail", "--preserve-bond: host bond missing (device not paired)")


def require_secure_state(preserve_bond, paired, connected, encrypted):
    """Require encrypted/paired connected state before BAP configuration.

    Active only with --preserve-bond: the bonded central must be Paired,
    Connected and Encrypted.  Returns (ok, reason); ok True (reason "")
    when inactive or all requirements hold.
    """
    if not preserve_bond:
        return (True, "")
    if not connected:
        return (False, "--preserve-bond: device not connected")
    if not paired:
        return (False, "--preserve-bond: device not paired")
    if not encrypted:
        return (False, "--preserve-bond: link not encrypted")
    return (True, "")
