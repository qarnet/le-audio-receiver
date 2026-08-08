#!/usr/bin/env python3
"""Unit tests for scripts/bap_central_device.py (device-resolution module).

Proves, on stdlib python3 with fake D-Bus/GLib (no live BlueZ):
  - --peer-addr bypass path construction and print;
  - existing Device1 enumeration filtering (adapter prefix, name,
    Paired/Connected flag) and connected-flag propagation;
  - bounded discovery: found / timeout / KeyboardInterrupt;
  - DiscoverySession.close() idempotence — StopDiscovery exactly once,
    signal match removed exactly once, safe from double-run;
  - adapter power-on success/error;
  - resolve_device composition (bypass / enum / discovery) with the exact
    established prints.
"""

import contextlib
import io
import os
import sys
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bap_central_fakes as fakes  # noqa: E402
import bap_central_device as dev  # noqa: E402

HCI_PATH = "/org/bluez/hci0"
PEER = "DB:A6:0C:05:A2:AA"


def capture(fn):
    """Run fn() with stdout redirected; return (result, output)."""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        result = fn()
    return result, buf.getvalue()


class FireOnIteration(fakes.FakeMainContext):
    """GLib context that invokes fn once on the first iteration (simulates
    an InterfacesAdded signal dispatching through the main loop)."""

    def __init__(self, fn):
        super().__init__()
        self._fn = fn
        self.fired = False

    def iteration(self, may_block=False):
        self.iterations += 1
        if not self.fired:
            self.fired = True
            self._fn()
        return False


class RaiseOnIteration(fakes.FakeMainContext):
    """GLib context that raises KeyboardInterrupt on the first iteration."""

    def __init__(self):
        super().__init__()
        self.raised = False

    def iteration(self, may_block=False):
        self.iterations += 1
        if not self.raised:
            self.raised = True
            raise KeyboardInterrupt()
        return False


class TestPeerDevicePath(unittest.TestCase):
    def test_path_construction(self):
        self.assertEqual(
            dev.peer_device_path(HCI_PATH, PEER),
            "/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA",
        )

    def test_prints_bypass_line(self):
        _, out = capture(lambda: dev.peer_device_path(HCI_PATH, PEER))
        self.assertIn(
            "[main] --peer-addr bypass: skipping discovery, target="
            "/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA",
            out,
        )


class TestFindExistingReceiver(unittest.TestCase):
    def _om(self, managed):
        bus = fakes.FakeBus()
        om = bus.iface("/", "org.freedesktop.DBus.ObjectManager")
        om.script("GetManagedObjects", lambda: managed)
        return bus, om

    def test_enum_filters_adapter_and_name(self):
        managed = {
            HCI_PATH + "/dev_AA": {
                "org.bluez.Device1": {
                    "Name": "Some Other Device",
                    "Address": "AA:AA:AA:AA:AA:AA",
                    "Paired": True,
                    "Connected": False,
                }
            },
            HCI_PATH + "/dev_LE": {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "DB:A6:0C:05:A2:AA",
                    "Paired": True,
                    "Connected": True,
                }
            },
            "/org/bluez/hci1/dev_OTHER_ADAPTER": {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "11:22:33:44:55:66",
                    "Paired": False,
                    "Connected": False,
                }
            },
        }
        bus, om = self._om(managed)
        dev_path, already_connected = capture(
            lambda: dev.find_existing_receiver(om, HCI_PATH)
        )[0]
        self.assertEqual(dev_path, HCI_PATH + "/dev_LE")
        self.assertTrue(already_connected)

    def test_enum_prints(self):
        managed = {
            HCI_PATH + "/dev_AA": {
                "org.bluez.Device1": {
                    "Name": "Other",
                    "Address": "AA:AA:AA:AA:AA:AA",
                    "Paired": False,
                    "Connected": False,
                }
            },
            HCI_PATH + "/dev_LE": {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "DB:A6:0C:05:A2:AA",
                    "Paired": True,
                    "Connected": False,
                }
            },
        }
        bus, om = self._om(managed)
        _, out = capture(lambda: dev.find_existing_receiver(om, HCI_PATH))
        self.assertIn("[enum] Existing device: " + HCI_PATH + "/dev_AA", out)
        self.assertIn("name='Other' addr=AA:AA:AA:AA:AA:AA", out)
        self.assertIn("paired=False connected=False", out)
        self.assertIn(
            "[enum] >>> Using existing target: "
            + HCI_PATH
            + "/dev_LE (connected=False)",
            out,
        )

    def test_no_match_returns_none(self):
        managed = {
            HCI_PATH + "/dev_AA": {
                "org.bluez.Device1": {
                    "Name": "Other",
                    "Address": "AA:AA:AA:AA:AA:AA",
                    "Paired": False,
                    "Connected": False,
                }
            },
        }
        bus, om = self._om(managed)
        dev_path, already_connected = capture(
            lambda: dev.find_existing_receiver(om, HCI_PATH)
        )[0]
        self.assertIsNone(dev_path)
        self.assertFalse(already_connected)


class TestPowerOnAdapter(unittest.TestCase):
    def test_success_prints_and_sets(self):
        bus = fakes.FakeBus()
        props = bus.iface(HCI_PATH, "org.freedesktop.DBus.Properties")
        dev.power_on_adapter(props, fakes.FakeDbusModule())
        calls = props.calls_for("Set")
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1][0], "org.bluez.Adapter1")
        self.assertEqual(calls[0][1][1], "Powered")

    def test_error_raises_central_error(self):
        bus = fakes.FakeBus()
        props = bus.iface(HCI_PATH, "org.freedesktop.DBus.Properties")

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("power failed")

        props.script("Set", boom)
        with self.assertRaises(dev.CentralError):
            capture(lambda: dev.power_on_adapter(props, fakes.FakeDbusModule()))


class TestDiscoverySession(unittest.TestCase):
    def _handler_args(self, path=HCI_PATH + "/dev_LE"):
        return (
            path,
            {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "DB:A6:0C:05:A2:AA",
                    "RSSI": -40,
                }
            },
        )

    def _make(self, bus, adapter, GLib, timeout_s=0.05):
        return dev.DiscoverySession(
            bus, fakes.FakeDbusModule(), GLib, adapter, timeout_s=timeout_s
        )

    def test_found(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        handler_path, handler_ifaces = self._handler_args()
        session = self._make(bus, adapter, fakes.FakeGLib)
        assert len(bus.signal_receivers) == 0

        def fire():
            for h, _di, _sn, _kw in list(bus.signal_receivers):
                h(handler_path, handler_ifaces)

        fakes.set_fake_context(FireOnIteration(fire))
        try:
            result, out = capture(lambda: session.run())
        finally:
            fakes.reset_fake_glib()

        self.assertEqual(result, HCI_PATH + "/dev_LE")
        self.assertIn(
            "[main] Discovery started, waiting for 'LE Audio Receiver'...", out
        )
        self.assertIn("[main]    (or press Ctrl-C to abort)", out)
        self.assertIn("[discovery] Device: " + HCI_PATH + "/dev_LE", out)
        self.assertIn("name='LE Audio Receiver'", out)
        self.assertIn("[discovery] >>> Target found: " + HCI_PATH + "/dev_LE", out)
        # StopDiscovery exactly once, signal match removed.
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 1)
        self.assertEqual(len(adapter.calls_for("StopDiscovery")), 1)
        self.assertEqual(bus.count_signal_receivers(), 0)

    def test_found_ignores_unrelated_devices_first(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        session = self._make(bus, adapter, fakes.FakeGLib)
        other = (
            HCI_PATH + "/dev_OTHER",
            {
                "org.bluez.Device1": {
                    "Name": "Not Us",
                    "Address": "AA:AA:AA:AA:AA:AA",
                    "RSSI": -60,
                }
            },
        )
        target = self._handler_args()

        def fire():
            handlers = [h for h, _di, _sn, _kw in bus.signal_receivers]
            handlers[0](*other)  # ignored (name mismatch) -> not found
            handlers[0](*target)  # found

        fakes.set_fake_context(FireOnIteration(fire))
        try:
            result, out = capture(lambda: session.run())
        finally:
            fakes.reset_fake_glib()
        self.assertEqual(result, HCI_PATH + "/dev_LE")
        self.assertIn("[discovery] Device: " + HCI_PATH + "/dev_OTHER", out)
        self.assertNotIn(">>> Target found: " + HCI_PATH + "/dev_OTHER", out)
        self.assertIn(">>> Target found: " + HCI_PATH + "/dev_LE", out)

    def test_timeout(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        session = self._make(bus, adapter, fakes.FakeGLib, timeout_s=0.05)
        with self.assertRaises(dev.CentralError):
            capture(lambda: session.run())
        # The exact established error message text is preserved.
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(dev.CentralError):
                session2 = self._make(bus, adapter, fakes.FakeGLib, timeout_s=0.05)
                session2.run()
        self.assertIn("[error] LE Audio Receiver not found within 30 s", buf.getvalue())
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 2)
        self.assertEqual(len(adapter.calls_for("StopDiscovery")), 2)

    def test_keyboard_interrupt(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        session = self._make(bus, adapter, fakes.FakeGLib)
        fakes.set_fake_context(RaiseOnIteration())
        try:
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                with self.assertRaises(dev.CentralError):
                    session.run()
        finally:
            fakes.reset_fake_glib()
        self.assertIn("\n[main] Interrupted", buf.getvalue())
        self.assertEqual(len(adapter.calls_for("StopDiscovery")), 1)
        self.assertEqual(bus.count_signal_receivers(), 0)

    def test_close_idempotent_double_run(self):
        """close() is safe from finally: calling it again after run()'s
        internal close is a no-op (StopDiscovery once, signal removed once)."""
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        handler_path, handler_ifaces = self._handler_args()
        session = self._make(bus, adapter, fakes.FakeGLib)

        def fire():
            for h, _di, _sn, _kw in list(bus.signal_receivers):
                h(handler_path, handler_ifaces)

        fakes.set_fake_context(FireOnIteration(fire))
        try:
            session.run()
            session.close()  # explicit second close (owner finally path)
            session.close()
        finally:
            fakes.reset_fake_glib()
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 1)
        self.assertEqual(len(adapter.calls_for("StopDiscovery")), 1)
        self.assertEqual(bus.count_signal_receivers(), 0)


class TestResolveDevice(unittest.TestCase):
    def test_peer_addr_bypass(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        om = bus.iface("/", "org.freedesktop.DBus.ObjectManager")
        result, out = capture(
            lambda: dev.resolve_device(
                bus,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                adapter,
                om,
                HCI_PATH,
                PEER,
                timeout_s=0.05,
            )
        )
        self.assertEqual(result, (HCI_PATH + "/dev_DB_A6_0C_05_A2_AA", False))
        self.assertIn("[main] --peer-addr bypass: skipping discovery", out)
        self.assertIn(
            "[main] Target device: " + HCI_PATH + "/dev_DB_A6_0C_05_A2_AA"
            " (already_connected=False)",
            out,
        )
        # Bypass must never StartDiscovery.
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 0)

    def test_existing_enumeration(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        om = bus.iface("/", "org.freedesktop.DBus.ObjectManager")
        managed = {
            HCI_PATH + "/dev_LE": {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "DB:A6:0C:05:A2:AA",
                    "Paired": True,
                    "Connected": True,
                }
            },
        }
        om.script("GetManagedObjects", lambda: managed)
        result, out = capture(
            lambda: dev.resolve_device(
                bus,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                adapter,
                om,
                HCI_PATH,
                None,
                timeout_s=0.05,
            )
        )
        self.assertEqual(result, (HCI_PATH + "/dev_LE", True))
        self.assertIn(
            "[main] Target device: " + HCI_PATH + "/dev_LE (already_connected=True)",
            out,
        )
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 0)

    def test_discovery_when_no_existing(self):
        bus = fakes.FakeBus()
        adapter = bus.iface(HCI_PATH, "org.bluez.Adapter1")
        om = bus.iface("/", "org.freedesktop.DBus.ObjectManager")
        om.script("GetManagedObjects", lambda: {})
        target_path, target_ifaces = (
            HCI_PATH + "/dev_LE",
            {
                "org.bluez.Device1": {
                    "Name": "LE Audio Receiver",
                    "Address": "DB:A6:0C:05:A2:AA",
                    "RSSI": -40,
                }
            },
        )

        def fire():
            for h, _di, _sn, _kw in list(bus.signal_receivers):
                h(target_path, target_ifaces)

        fakes.set_fake_context(FireOnIteration(fire))
        try:
            result, out = capture(
                lambda: dev.resolve_device(
                    bus,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    adapter,
                    om,
                    HCI_PATH,
                    None,
                    timeout_s=0.05,
                )
            )
        finally:
            fakes.reset_fake_glib()
        self.assertEqual(result, (HCI_PATH + "/dev_LE", False))
        self.assertIn(
            "[main] Target device: " + HCI_PATH + "/dev_LE (already_connected=False)",
            out,
        )
        self.assertEqual(len(adapter.calls_for("StartDiscovery")), 1)
        self.assertEqual(len(adapter.calls_for("StopDiscovery")), 1)


if __name__ == "__main__":
    unittest.main()
