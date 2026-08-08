#!/usr/bin/env python3
"""
BAP central test driver for LE Audio Receiver.

Prerequisite: nRF5340DK hci_uart central attached via btattach (see AGENTS.md
"Central setup").  Run this script WITHOUT sudo; only the raw-HCI subprocess
uses sudo internally.

Usage: python3 scripts/bap_central.py [--stereo] [--duration N] [--freq FREQ]

Registers a BAP source endpoint on hci0, pairs + connects to the LE Audio
Receiver peripheral, acquires the MediaTransport, and streams a 1 kHz sine
tone as LC3 (48 kHz / 10 ms / 96 kbps for mono, 192 kbps for stereo).

--stereo: use stereo Mode B (single ASE, 240-byte SDU)
--duration N: stream for N seconds (default 30)
--freq FREQ: sine frequency in Hz (default 1000)

This file is the thin CLI coordinator.  Discovery lives in
bap_central_device.py, agent/pairing/connect strategies in
bap_central_security.py, the BAP endpoint + acquire in
bap_central_endpoint.py, and the LC3 source/writer lifecycle in
bap_central_session.py.  The CentralCleanup owner below is the single
idempotent teardown coordinator (safe from finally): every resource is
registered as it is acquired and released in the fixed successful
teardown order.
"""

import argparse
import sys
import time

import bap_central_policy
import bap_central_device
import bap_central_endpoint
import bap_central_security
import bap_central_session

# Force unbuffered stdout so errors in D-Bus callbacks are visible.
try:
    sys.stdout.reconfigure(line_buffering=True)
except Exception:
    pass


class CentralCleanup:
    """Idempotent ordered cleanup, safe from finally.

    Stages are registered in a FIXED ORDER (the successful teardown
    order); a stage is registered only when its resource was actually
    acquired.  run() walks the fixed order, executes each registered
    stage exactly once (pop), and is itself idempotent — double-run is a
    no-op.  The order preserves the pre-split successful teardown tail:
    transports released (fds closed) -> writer bounded join/force ->
    endpoint unregister -> agent unregister -> Device1 Disconnect ->
    raw-HCI helper terminate.
    """

    ORDER = (
        "discovery",
        "transports",
        "writer",
        "endpoint",
        "agent",
        "disconnect",
        "helper",
    )

    def __init__(self):
        self._stages = {}

    def register(self, stage, fn):
        if stage in self.ORDER:
            self._stages[stage] = fn

    def run(self):
        for stage in self.ORDER:
            fn = self._stages.pop(stage, None)
            if fn is not None:
                fn()


def build_parser():
    parser = argparse.ArgumentParser(
        description="BAP central test driver for LE Audio Receiver."
    )
    parser.add_argument(
        "--stereo", action="store_true", help="Stereo Mode B (single ASE, 240-byte SDU)"
    )
    parser.add_argument(
        "--duration",
        type=int,
        default=30,
        help="Streaming duration in seconds (default: 30)",
    )
    parser.add_argument(
        "--freq",
        type=float,
        default=1000.0,
        help="Sine frequency in Hz (default: 1000)",
    )
    parser.add_argument(
        "--adapter",
        type=str,
        default="hci0",
        help="BlueZ adapter to use (default: hci0)",
    )
    parser.add_argument(
        "--peer-addr",
        type=str,
        default=None,
        help="Peer BLE address (xx:xx:xx:xx:xx:xx). "
        "Skips BlueZ discovery; connects directly via raw HCI. "
        "Required when controller BD_ADDR is all-zero (prevents scanning).",
    )
    parser.add_argument(
        "--preserve-bond",
        action="store_true",
        help="Reconnect with an existing bond: skip BlueZ RemoveDevice and "
        "re-pairing, and require Paired + Connected state before BAP "
        "configuration (T8 reconnect rows).",
    )
    return parser


# ── D-Bus late import ──────────────────────────────────────────────────


def _import_dbus():
    """Import D-Bus bindings and set up the GLib main loop.

    Returns (dbus, dbus_service, GLib) tuple or exits on failure.
    """
    try:
        import dbus as _dbus
        import dbus.service as _dbus_service
        import dbus.mainloop.glib as _dbus_ml_glib
        from gi.repository import GLib as _GLib
    except ImportError as e:
        print("[error] D-Bus bindings not available.")
        print("        Install dbus-python and pygobject:")
        print(
            "        nix-shell -p python3Packages.dbus-python"
            " python3Packages.pygobject3"
        )
        raise SystemExit(1) from e

    _dbus_ml_glib.DBusGMainLoop(set_as_default=True)
    return _dbus, _dbus_service, _GLib


# ── Main flow ───────────────────────────────────────────────────────────


def main():
    args = build_parser().parse_args()
    hci_path = "/org/bluez/" + args.adapter

    # Extract HCI device index from adapter name (e.g. "hci1" -> 1).
    hci_dev = 0
    if args.adapter.startswith("hci"):
        try:
            hci_dev = int(args.adapter[3:])
        except ValueError:
            pass

    # Late-import D-Bus bindings so --help works without dbus-python.
    _dbus, _dbus_service, _GLib = _import_dbus()

    # Create D-Bus classes (closures capture the real dbus module).
    BAPSourceEndpoint = bap_central_endpoint.make_endpoint_class(_dbus, _dbus_service)
    JustWorksAgent = bap_central_security.make_agent_class(_dbus, _dbus_service)

    # ── Connect to system bus ────────────────────────────────────────
    bus = _dbus.SystemBus()
    print("[main] Connected to D-Bus system bus")

    cleanup = CentralCleanup()
    try:
        # ── 1. Register pairing agent ────────────────────────────────
        agent, agent_mgr = bap_central_security.register_agent(
            bus, _dbus, JustWorksAgent
        )
        cleanup.register(
            "agent", lambda: bap_central_security.unregister_agent(agent_mgr)
        )

        # ── 2. Register BAP source endpoint ──────────────────────────
        media = _dbus.Interface(
            bus.get_object("org.bluez", hci_path), "org.bluez.Media1"
        )
        endpoint = bap_central_endpoint.register_endpoint(
            media, bus, _dbus, BAPSourceEndpoint, stereo=args.stereo
        )
        cleanup.register(
            "endpoint", lambda: bap_central_endpoint.unregister_endpoint(media)
        )

        # ── 3. Power on adapter ──────────────────────────────────────
        adapter_props = _dbus.Interface(
            bus.get_object("org.bluez", hci_path),
            "org.freedesktop.DBus.Properties",
        )
        bap_central_device.power_on_adapter(adapter_props, _dbus)

        # ── 4. Locate target device (existing, peer-addr, or discovery)
        adapter = _dbus.Interface(
            bus.get_object("org.bluez", hci_path), "org.bluez.Adapter1"
        )
        om = _dbus.Interface(
            bus.get_object("org.bluez", "/"),
            "org.freedesktop.DBus.ObjectManager",
        )
        dev_path, already_connected = bap_central_device.resolve_device(
            bus, _dbus, _GLib, adapter, om, hci_path, args.peer_addr
        )

        # ── 5. Raw-HCI connect (kernel accept-list scan path is broken
        # on this hci_usb controller) then Pair over the existing ACL.
        device = _dbus.Interface(
            bus.get_object("org.bluez", dev_path), "org.bluez.Device1"
        )

        # If BlueZ thinks the device is already connected, disconnect first
        # so we get a clean GATT service discovery cycle. BlueZ caches
        # Device1 objects across disconnects, and a stale "connected" flag
        # skips GATT discovery.
        if already_connected:
            print("[main] Disconnecting stale cached connection, reconnecting fresh...")
            try:
                device.Disconnect()
                time.sleep(1.5)  # let receiver settle and restart advertising
            except _dbus.exceptions.DBusException as e:
                print("[main]   Disconnect ignored: {}".format(e))
            already_connected = False

        # ── 6. Get device properties interface ───────────────────────
        dev_props = _dbus.Interface(
            bus.get_object("org.bluez", dev_path),
            "org.freedesktop.DBus.Properties",
        )

        raw_connect = None

        if args.peer_addr is not None:
            # --peer-addr path: persistent raw HCI direct connect + async
            # Pair().  BlueZ scanning is broken on this controller, so the
            # ACL is created directly via raw HCI and the socket stays open
            # for the lifetime of the stream.

            # 5a. Clear any stale BlueZ device before connecting — unless
            # --preserve-bond: the cached Device1 record (with its bond)
            # must survive so the next session reconnects without re-pairing.
            bap_central_security.remove_device(
                adapter, dev_path, _dbus, args.preserve_bond
            )

            if args.preserve_bond:
                # ── Preserve-bond transport: BlueZ Device1.Connect() ──
                # Do NOT launch the raw-HCI helper here: for a paired device
                # BlueZ's own auto-connect owns the controller initiator, and
                # a concurrent raw-HCI LE Extended Create Connection fails
                # with 0x0d (Limited Resources).
                bap_central_security.preserve_bond_connect(
                    device, dev_props, _dbus, _GLib, already_connected
                )
            else:
                # ── Fresh-pair transport: confirmed raw-HCI helper ────
                raw_connect = bap_central_security.RawHciConnect(
                    args.peer_addr, args.duration, hci_dev
                )
                cleanup.register("helper", lambda: raw_connect.terminate(verbose=True))
                raw_connect.spawn()
                raw_connect.wait_ready()
                raw_connect.wait_connected(dev_props, _GLib, _dbus)

            # The transport step succeeded: a link exists (raw-HCI gate 2
            # or BlueZ Connect confirmed).  Register the disconnect stage
            # so every later failure still tears the ACL down cleanly.
            cleanup.register(
                "disconnect",
                lambda: bap_central_security.disconnect_and_wait(
                    device, dev_props, _dbus, _GLib
                ),
            )

            # After RemoveDevice + raw HCI reconnect (fresh mode), recreate
            # proxies from the live bus.  Pre-existing proxies may be stale
            # after RemoveDevice tears down and recreates the D-Bus object.
            device, dev_props = bap_central_security.recreate_proxies(
                bus, _dbus, dev_path
            )

            # 5ab. Read fresh Paired/Connected state (after the transport
            # step above) and decide whether Pair() is required.
            dev_paired, dev_connected = bap_central_security.read_device_state(
                dev_props, _dbus
            )
            action, action_detail = bap_central_policy.pair_action(
                args.preserve_bond, dev_paired
            )
            if action == "fail":
                print("[error] {}".format(action_detail))
                raise bap_central_security.CentralError(
                    "pair action fail: {}".format(action_detail)
                )
            if action == "skip":
                print("[main] {}".format(action_detail))
                pair_skip = True
            else:
                pair_skip = False

            secure_ok, secure_reason = bap_central_policy.require_secure_state(
                args.preserve_bond, dev_paired, dev_connected
            )
            if not secure_ok:
                print("[error] {}".format(secure_reason))
                raise bap_central_security.CentralError(
                    "secure state fail: {}".format(secure_reason)
                )

            # 5b. Set Pairable so bonding can proceed.
            bap_central_security.set_pairable(adapter_props, _dbus)

            # 5c. Trust the device.
            bap_central_security.set_trusted(
                dev_props,
                _dbus,
                "Trusted, async pairing over existing ACL...",
            )

            # 5d. Async Pair() — reply_handler/error_handler so GLib main
            # loop stays serviceable (Agent1 dispatch during pairing).
            bap_central_security.pair_device(device, dev_props, _dbus, _GLib, pair_skip)
        else:
            # Normal discovery path: no raw-HCI preconnect.  BlueZ owns
            # the ACL and bonding transaction.  Async Device.Pair() while
            # disconnected so BlueZ issues MGMT Pair Device before LE
            # Connection Complete, establishing device->bonding before SMP.
            # Do NOT call RemoveDevice here — the Device1 object must exist
            # (just discovered via scan) for Pair() to work.

            # 5a. Set Pairable on the adapter so bonding proceeds.
            bap_central_security.set_pairable(adapter_props, _dbus)

            # 5b. Trust the device.
            bap_central_security.set_trusted(
                dev_props,
                _dbus,
                "Trusted, async pairing (disconnected \u2192 BlueZ ACL)...",
            )

            # 5bc. Preserve-bond gate: with --preserve-bond the host bond
            # must already exist; skip Pair() when it does.
            try:
                dev_paired_norm = bool(dev_props.Get("org.bluez.Device1", "Paired"))
            except _dbus.exceptions.DBusException:
                dev_paired_norm = False
            action_norm, detail_norm = bap_central_policy.pair_action(
                args.preserve_bond, dev_paired_norm
            )
            if action_norm == "fail":
                print("[error] {}".format(detail_norm))
                raise bap_central_security.CentralError(
                    "pair action fail: {}".format(detail_norm)
                )
            pair_skip_norm = action_norm == "skip"
            if pair_skip_norm:
                print("[main] {}".format(detail_norm))

            # 5c. Async Pair() while disconnected — BlueZ creates ACL, runs
            # SMP, and auto-accepts Just Works without Agent1 callback.
            bap_central_security.pair_device(
                device, dev_props, _dbus, _GLib, pair_skip_norm
            )

            print("[main] Waiting for GATT service resolution...")

        # The link exists on every path reaching here (fresh raw gate 2 /
        # BlueZ Connect / discovery Pair).  Register the disconnect stage
        # for the discovery path (peer-addr paths registered it earlier).
        cleanup.register(
            "disconnect",
            lambda: bap_central_security.disconnect_and_wait(
                device, dev_props, _dbus, _GLib
            ),
        )

        bap_central_security.wait_services_resolved(dev_props, _dbus, _GLib)

        # ── 7. Wait for SetConfiguration + deferred async Acquire ────
        transports, stream_mode, sdu_size = bap_central_endpoint.acquire_transports(
            bus, _dbus, _GLib, endpoint
        )
        cleanup.register(
            "transports",
            lambda: bap_central_endpoint.release_transports(
                bus, _dbus, list(endpoint.transports), endpoint
            ),
        )

        # ── 8. Stream LC3 SDUs every 10 ms ───────────────────────────
        print(
            "[main] Streaming {:.0f} Hz sine for {} s...".format(
                args.freq, args.duration
            )
        )
        stream = bap_central_session.StreamSession(
            endpoint.transports, stream_mode, args.freq, args.duration
        )
        cleanup.register("writer", stream.stop_writer)
        stream.start()
        stream.run()

        # ── 9. Cleanup ───────────────────────────────────────────────
        print("[cleanup] Releasing resources...")
        # The finally below runs the CentralCleanup owner: transports
        # released (fds closed) -> writer bounded join/force -> endpoint
        # unregister -> agent unregister -> Device1 Disconnect -> raw-HCI
        # helper terminate, exactly in the pre-split order.
    except (
        bap_central_device.CentralError,
        bap_central_security.CentralError,
        bap_central_endpoint.CentralError,
        bap_central_session.CentralError,
    ):
        # The primary error was already printed by the raising module;
        # the finally owner releases every acquired resource, then exit 1.
        sys.exit(1)
    finally:
        cleanup.run()

    print("[main] Exiting")


if __name__ == "__main__":
    main()
