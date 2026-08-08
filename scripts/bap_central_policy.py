#!/usr/bin/env python3
"""Pure decision helpers for bap_central.py --preserve-bond (stdlib only).

Kept import-light (no dbus, no liblc3) so the transport/pairing decision
logic is unit-testable without a BlueZ stack.
"""


def connection_strategy(preserve_bond, already_paired):
    """Decide the connection transport for the --peer-addr path.

    Returns a (verb, detail) tuple:
      ("bluez_connect", ...)  --preserve-bond with an existing host bond:
          use BlueZ Device1.Connect().  Raw-HCI must NOT be launched
          concurrently with BlueZ's own auto-connect for a paired device —
          the single controller initiator rejects the raw-HCI LE Extended
          Create Connection with 0x0d (Limited Resources).
      ("fail", ...)           --preserve-bond and the host bond is missing.
      ("raw_hci", "")         fresh pair: confirmed raw-HCI helper.
    """
    if not preserve_bond:
        return ("raw_hci", "")
    if already_paired:
        return (
            "bluez_connect",
            "--preserve-bond: host bond present, using BlueZ Device1.Connect()",
        )
    return ("fail", "--preserve-bond: host bond missing (device not paired)")


def should_connect(already_connected, strategy):
    """True when BlueZ Device1.Connect() must be invoked: strategy is
    bluez_connect and the device is not already connected."""
    return strategy == "bluez_connect" and not already_connected


def needs_fresh_reconnect(already_connected, strategy):
    """True when the already-connected device must be disconnected first.

    BlueZ runs BAP auto-configuration (SetConfiguration) only for a
    connection it freshly establishes while a source endpoint is
    registered.  An ACL created outside this session (e.g. BlueZ
    auto-connect of a trusted paired device) never triggers it, so the
    preserve-bond flow tears that stale ACL down and reconnects via
    Device1.Connect() to get a fresh connection event.
    """
    return strategy == "bluez_connect" and already_connected


def connect_outcome(reply_ok, error):
    """Decide the outcome of the bounded Device1.Connect() wait.

    "error"   — the async error_handler fired (Connect refused/failed);
    "ok"      — the async reply_handler fired before the deadline;
    "timeout" — neither handler fired inside the bounded wait.
    """
    if error is not None:
        return "error"
    if reply_ok:
        return "ok"
    return "timeout"


def pair_action(preserve_bond, already_paired):
    """Decide the pairing step (Device1.Pair() call).

    --preserve-bond never re-pairs: ("skip", ...) when the host bond
    exists, ("fail", ...) when it is missing.  Fresh mode pairs.
    """
    if not preserve_bond:
        return ("pair", "")
    if already_paired:
        return ("skip", "--preserve-bond: host bond exists, skipping Pair()")
    return ("fail", "--preserve-bond: host bond missing (device not paired)")


def require_secure_state(preserve_bond, paired, connected):
    """Require Paired + Connected before BAP configuration.

    Active only with --preserve-bond.  BlueZ Device1 has no portable
    Encrypted property; successful access/configuration of the encrypted
    PACS/ASCS is the public-boundary security proof.  Returns (ok, reason);
    ok True (reason "") when inactive or all requirements hold.
    """
    if not preserve_bond:
        return (True, "")
    if not connected:
        return (False, "--preserve-bond: device not connected")
    if not paired:
        return (False, "--preserve-bond: device not paired")
    return (True, "")
