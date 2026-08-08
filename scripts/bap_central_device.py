#!/usr/bin/env python3
"""Device resolution for bap_central: adapter power, exact-peer
bypass path, existing Device1 enumeration, and bounded InterfacesAdded
discovery.

Stdlib import only; D-Bus/GLib are injected late (real modules at
runtime, fakes in unit tests).  Every print is byte-compatible with the
bap_central.py flow.

Fatal paths print their exact message then raise CentralError; the CLI
catches it (exit 1) after the finally-registered cleanup owner runs.
"""

import time


class CentralError(Exception):
    """Fatal central-driver error (message already printed by the raiser)."""


def power_on_adapter(adapter_props_iface, dbus_mod):
    """Set Adapter1 Powered=True on the adapter (step 3).

    Prints "[main] Adapter powered on".  A D-Bus failure is fatal
    (it was an uncaught traceback; now a clean [error] line +
    CentralError, exit code 1 preserved).
    """
    try:
        adapter_props_iface.Set("org.bluez.Adapter1", "Powered", dbus_mod.Boolean(True))
    except Exception as e:  # noqa: BLE001
        print("[error] Adapter power-on failed: {}".format(e))
        raise CentralError("adapter power-on failed: {}".format(e)) from e
    print("[main] Adapter powered on")


def peer_device_path(hci_path, peer_addr):
    """--peer-addr bypass: return the exact Device1 path and print the
    bypass line.  already_connected is always False here."""
    dev_path = "{}/dev_{}".format(hci_path, peer_addr.replace(":", "_").upper())
    print("[main] --peer-addr bypass: skipping discovery, target={}".format(dev_path))
    return dev_path


def find_existing_receiver(om_iface, hci_path):
    """Enumerate existing Device1 objects and return the first 'LE Audio
    Receiver' on this adapter.

    Returns (dev_path, already_connected) or (None, False).  Prints the
    exact "[enum] ..." lines.
    """
    managed = om_iface.GetManagedObjects()
    for path, ifaces in managed.items():
        if "org.bluez.Device1" not in ifaces:
            continue
        dev = ifaces["org.bluez.Device1"]
        name = str(dev.get("Name", ""))
        addr = str(dev.get("Address", ""))
        if not path.startswith(hci_path + "/"):
            continue
        print(
            "[enum] Existing device: {} name={!r} addr={} "
            "paired={} connected={}".format(
                path,
                name,
                addr,
                dev.get("Paired", False),
                dev.get("Connected", False),
            )
        )
        if "LE Audio Receiver" in name:
            already_connected = bool(dev.get("Connected", False))
            print(
                "[enum] >>> Using existing target: {} (connected={})".format(
                    path, already_connected
                )
            )
            return (path, already_connected)
    return (None, False)


class DiscoverySession:
    """Bounded InterfacesAdded discovery for 'LE Audio Receiver'.

    Owns the signal match for its lifetime and StopDiscovery exactly once.
    ``run()`` prints the discovery lines and returns the target
    Device1 path; timeout and KeyboardInterrupt are fatal (CentralError
    after printing).  ``close()`` is idempotent and safe from ``finally``:
    StopDiscovery once, remove the signal match once.  ``run()`` closes
    internally on every exit path (preserving the
    post-discovery StopDiscovery).
    """

    def __init__(self, bus, dbus_mod, GLib, adapter_iface, timeout_s=30.0):
        self._bus = bus
        self._dbus = dbus_mod
        self._GLib = GLib
        self._adapter = adapter_iface
        self._timeout_s = timeout_s
        self._target = [None]
        self._found = [False]
        self._handler = None
        self._discovery_started = False
        self._closed = False

    def _on_interfaces_added(self, path, interfaces):
        if self._found[0]:
            return
        if "org.bluez.Device1" not in interfaces:
            return
        dev_iface = interfaces["org.bluez.Device1"]
        name = str(dev_iface.get("Name", ""))
        addr = str(dev_iface.get("Address", ""))
        rssi = dev_iface.get("RSSI", "?")
        print(
            "[discovery] Device: {} name={!r} addr={} RSSI={}".format(
                path, name, addr, rssi
            )
        )
        if "LE Audio Receiver" in name:
            self._target[0] = path
            self._found[0] = True
            print("[discovery] >>> Target found: {}".format(path))

    def run(self):
        """Attach the signal receiver, StartDiscovery, bounded wait."""
        # Store the bound method once so add/remove match by identity
        # (each attribute access would create a fresh bound method).
        self._handler = self._on_interfaces_added
        self._bus.add_signal_receiver(
            self._handler,
            dbus_interface="org.freedesktop.DBus.ObjectManager",
            signal_name="InterfacesAdded",
        )
        try:
            self._adapter.StartDiscovery()
            self._discovery_started = True
            print("[main] Discovery started, waiting for 'LE Audio Receiver'...")
            print("[main]    (or press Ctrl-C to abort)")

            deadline = time.monotonic() + self._timeout_s
            try:
                while not self._found[0] and time.monotonic() < deadline:
                    self._GLib.MainContext.default().iteration(False)
                    time.sleep(0.05)
            except KeyboardInterrupt:
                print("\n[main] Interrupted")
                raise CentralError("discovery interrupted") from None

            if not self._found[0]:
                print("[error] LE Audio Receiver not found within 30 s")
                raise CentralError("discovery timeout") from None
            return self._target[0]
        finally:
            self.close()

    def close(self):
        """Idempotent: StopDiscovery exactly once, signal match removed
        exactly once.  Safe from finally (double-run no-op)."""
        if self._closed:
            return
        self._closed = True
        if self._discovery_started:
            try:
                self._adapter.StopDiscovery()
            except Exception:  # noqa: BLE001
                pass
            self._discovery_started = False
        if self._handler is not None:
            try:
                self._bus.remove_signal_receiver(
                    self._handler,
                    dbus_interface="org.freedesktop.DBus.ObjectManager",
                    signal_name="InterfacesAdded",
                )
            except Exception:  # noqa: BLE001
                pass
            self._handler = None


def resolve_device(
    bus, dbus_mod, GLib, adapter_iface, om_iface, hci_path, peer_addr, timeout_s=30.0
):
    """Locate the target Device1 path (step 4).

    peer_addr bypass -> existing-device enumeration -> discovery.  Prints
    the exact "[main] Target device: ..." line.  Returns
    (dev_path, already_connected).
    """
    dev_path = None
    already_connected = False

    if peer_addr is not None:
        dev_path = peer_device_path(hci_path, peer_addr)
        already_connected = False

    if dev_path is None:
        dev_path, already_connected = find_existing_receiver(om_iface, hci_path)

    if dev_path is None:
        session = DiscoverySession(
            bus, dbus_mod, GLib, adapter_iface, timeout_s=timeout_s
        )
        dev_path = session.run()
        already_connected = False

    print(
        "[main] Target device: {} (already_connected={})".format(
            dev_path, already_connected
        )
    )
    return (dev_path, already_connected)
