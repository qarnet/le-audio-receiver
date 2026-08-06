#!/usr/bin/env python3
"""Unit tests for scripts/bap_central_security.py (R9 security/connect split).

Proves, on stdlib python3 with fake D-Bus/GLib and a fake raw-HCI helper
process (no live BlueZ, no sudo):
  - JustWorks Agent class method surface + registration/unregistration;
  - wait_for_helper_ready (moved from bap_central.py): ready line,
    early exit, deadline expiry (real pipes);
  - exact raw helper argv and hold = duration + 120;
  - fresh RemoveDevice boundary + recreate_proxies after removal;
    preserve-bond keeps the Device1 record;
  - preserve-bond BlueZ Connect: success / policy fail / Connect error /
    timeout / Connected-not-true / disconnect-first;
  - pair skip / fail / success and state reads; read_device_state
    DBusException -> CentralError;
  - helper terminated on every post-spawn failure (gate 1, gate 2, and
    the post-transport read-state failure through the owner);
  - wait_services_resolved success + warning path;
  - disconnect_and_wait success/error;
  - resource cleanup ordering: Device Disconnect BEFORE helper terminate
    via the CLI CentralCleanup owner.
"""

import contextlib
import io
import os
import sys
import time
import unittest

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bap_central_fakes as fakes  # noqa: E402
import bap_central_security as sec  # noqa: E402

from hci_raw_connect import READY_PREFIX  # noqa: E402

DEV_PATH = "/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA"
PEER = "DB:A6:0C:05:A2:AA"


def capture(fn):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        result = fn()
    return result, buf.getvalue()


class FakeRawProc:
    """Fake hci_raw_connect subprocess: real pipe stdout so
    wait_for_helper_ready's select loop works; scripted poll/terminate."""

    def __init__(self, ready_payload=b"", rc=None, stderr_data=b""):
        self._r, self._w = os.pipe()
        if ready_payload:
            os.write(self._w, ready_payload)
            os.close(self._w)
        self.stdout = os.fdopen(self._r, "rb")
        self.stderr = io.BytesIO(stderr_data)
        self._rc = rc
        self.terminated = False
        self.terminate_calls = 0
        self.wait_calls = 0

    def poll(self):
        return self._rc

    def terminate(self):
        self.terminated = True
        self.terminate_calls += 1
        if self._rc is None:
            self._rc = -15
        try:
            os.close(self._w)
        except OSError:
            pass

    def wait(self, timeout=3.0):
        self.wait_calls += 1
        return self._rc


class _StateProps:
    """Scripted Properties interface state used by multiple tests."""

    def __init__(self, **initial):
        self.state = dict(initial)

    def get(self, _iface, prop):
        return self.state.get(prop, False)

    def set(self, _iface, prop, value):
        self.state[prop] = bool(value)


# ── Agent ───────────────────────────────────────────────────────────────


class TestAgentClass(unittest.TestCase):
    def setUp(self):
        self.dbus = fakes.FakeDbusModule()
        self.svc = fakes.FakeServiceModule()
        self.AgentCls = sec.make_agent_class(self.dbus, self.svc)

    def test_method_surface_metadata(self):
        for name, iface, insig, outsig in [
            ("Release", "org.bluez.Agent1", "", ""),
            ("RequestPinCode", "org.bluez.Agent1", "o", "s"),
            ("RequestPasskey", "org.bluez.Agent1", "o", "u"),
            ("DisplayPinCode", "org.bluez.Agent1", "os", ""),
            ("DisplayPasskey", "org.bluez.Agent1", "ouq", ""),
            ("RequestConfirmation", "org.bluez.Agent1", "ou", ""),
            ("RequestAuthorization", "org.bluez.Agent1", "o", ""),
            ("AuthorizeService", "org.bluez.Agent1", "os", ""),
            ("Cancel", "org.bluez.Agent1", "", ""),
        ]:
            fn = getattr(self.AgentCls, name)
            self.assertEqual(fn._r9_iface, iface, name)
            self.assertEqual(fn._r9_in, insig, name)
            self.assertEqual(fn._r9_out, outsig, name)

    def test_instantiation_and_path(self):
        bus = fakes.FakeBus()
        agent = self.AgentCls(bus, sec.AGENT_PATH)
        self.assertEqual(agent.path, sec.AGENT_PATH)

    def test_request_pin_passkey(self):
        bus = fakes.FakeBus()
        agent = self.AgentCls(bus, sec.AGENT_PATH)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(agent.RequestPinCode(DEV_PATH), "0000")
            self.assertEqual(int(agent.RequestPasskey(DEV_PATH)), 0)
        self.assertIn("RequestPinCode({}) -> '0000'".format(DEV_PATH), out.getvalue())

    def test_accept_methods_print(self):
        bus = fakes.FakeBus()
        agent = self.AgentCls(bus, sec.AGENT_PATH)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            agent.Release()
            agent.DisplayPinCode(DEV_PATH, "123456")
            agent.DisplayPasskey(DEV_PATH, 123456, 0)
            agent.RequestConfirmation(DEV_PATH, 123456)
            agent.RequestAuthorization(DEV_PATH)
            agent.AuthorizeService(DEV_PATH, "0000abcd")
            agent.Cancel()
        text = out.getvalue()
        self.assertIn("[agent] Release", text)
        self.assertIn("[agent] DisplayPinCode: device={}".format(DEV_PATH), text)
        self.assertIn("[agent] DisplayPasskey: device={}".format(DEV_PATH), text)
        self.assertIn("[agent] RequestConfirmation (Just Works)", text)
        self.assertIn(
            "[agent] RequestAuthorization: accepting {}".format(DEV_PATH), text
        )
        self.assertIn("[agent] AuthorizeService: device={}".format(DEV_PATH), text)
        self.assertIn("[agent] Cancel", text)


class TestRegisterUnregisterAgent(unittest.TestCase):
    def test_register_prints_and_calls(self):
        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        AgentCls = sec.make_agent_class(dbus, svc)
        agent, agent_mgr = capture(lambda: sec.register_agent(bus, dbus, AgentCls))[0]
        self.assertEqual(agent.path, sec.AGENT_PATH)
        mgr_iface = bus.iface("/org/bluez", "org.bluez.AgentManager1")
        self.assertEqual(len(mgr_iface.calls_for("RegisterAgent")), 1)
        self.assertEqual(mgr_iface.calls_for("RegisterAgent")[0][1][0], sec.AGENT_PATH)
        self.assertEqual(
            mgr_iface.calls_for("RegisterAgent")[0][1][1], "NoInputNoOutput"
        )
        self.assertEqual(len(mgr_iface.calls_for("RequestDefaultAgent")), 1)

    def test_unregister_prints(self):
        bus = fakes.FakeBus()
        mgr = bus.iface("/org/bluez", "org.bluez.AgentManager1")
        _, out = capture(lambda: sec.unregister_agent(mgr))
        self.assertIn("[cleanup] Agent unregistered", out)
        self.assertEqual(len(mgr.calls_for("UnregisterAgent")), 1)

    def test_unregister_error_tolerated(self):
        bus = fakes.FakeBus()
        mgr = bus.iface("/org/bluez", "org.bluez.AgentManager1")

        def boom(*a, **k):
            raise RuntimeError("gone")

        mgr.script("UnregisterAgent", boom)
        _, out = capture(lambda: sec.unregister_agent(mgr))
        self.assertIn("[cleanup] Agent unregister error: gone", out)


# ── wait_for_helper_ready (moved from bap_central.py) ───────────────────


class TestWaitForHelperReady(unittest.TestCase):
    def _call(self, payload, alive, deadline_s=1.0):
        r, w = os.pipe()
        try:
            if payload:
                os.write(w, payload)
            out = os.fdopen(r, "rb")
            try:
                return sec.wait_for_helper_ready(
                    out, alive, time.monotonic() + deadline_s
                )
            finally:
                out.close()
        finally:
            os.close(w)

    def test_ready_line_detected(self):
        payload = (
            b"noise line\n"
            + b"more noise\n"
            + READY_PREFIX
            + b" peer=x handle=0x0001\n"
        )
        ok, detail, lines = self._call(payload, lambda: True)
        self.assertTrue(ok)
        self.assertTrue(detail.startswith(READY_PREFIX))
        self.assertEqual(len(lines), 3)

    def test_early_exit(self):
        ok, detail, lines = self._call(b"", lambda: False)
        self.assertFalse(ok)
        self.assertEqual(detail, "helper exited before ready")

    def test_deadline_expiry(self):
        ok, detail, lines = self._call(b"", lambda: True, deadline_s=0.1)
        self.assertFalse(ok)
        self.assertEqual(detail, "helper ready-line timeout")

    def test_unrelated_lines_then_ready(self):
        payload = b"line1\nline2\n" + READY_PREFIX + b"\n"
        ok, detail, lines = self._call(payload, lambda: True)
        self.assertTrue(ok)
        self.assertEqual(lines[0], b"line1")
        self.assertEqual(lines[1], b"line2")


# ── RawHciConnect ───────────────────────────────────────────────────────


class TestRawHciConnect(unittest.TestCase):
    def test_exact_argv_and_hold(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        self.assertEqual(
            conn.argv(),
            [
                "sudo",
                "-n",
                "python3",
                sec.RAW_CONNECT_HELPER,
                PEER,
                "150",
                "--addr-type",
                "public",
                "--peer-addr-type",
                "random",
                "--connect-deadline",
                "30",
                "--device",
                "0",
            ],
        )
        conn2 = sec.RawHciConnect(PEER, 15, 1)
        self.assertEqual(conn2.argv()[5], "135")
        self.assertEqual(conn2.argv()[-1], "1")

    def test_spawn_prints_and_uses_injected(self):
        seen = {}

        def fake_spawn(argv):
            seen["argv"] = argv
            return FakeRawProc()

        conn = sec.RawHciConnect(PEER, 30, 0, spawn=fake_spawn)
        _, out = capture(lambda: conn.spawn())
        self.assertIn("[main] Creating persistent ACL via raw HCI (hold=150s)...", out)
        self.assertEqual(seen["argv"], conn.argv())

    def test_wait_ready_success(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(ready_payload=READY_PREFIX + b" peer=x handle=0x0001\n")
        conn.proc = proc
        _, out = capture(lambda: conn.wait_ready())
        self.assertIn("[main] Raw HCI link confirmed:", out)
        self.assertFalse(proc.terminated)

    def test_wait_ready_early_exit_terminates(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(rc=1, stderr_data=b"fatal\n")
        conn.proc = proc
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                conn.wait_ready()
        self.assertTrue(proc.terminated)
        self.assertIn("[error] helper stderr: fatal\n", buf.getvalue())
        self.assertIn(
            "[error] Raw HCI connect failed: helper exited before ready", buf.getvalue()
        )

    def test_wait_ready_timeout_terminates(self):
        conn = sec.RawHciConnect(PEER, 30, 0, ready_deadline_s=0.05)
        proc = FakeRawProc()  # alive, no data
        conn.proc = proc
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                conn.wait_ready()
        self.assertTrue(proc.terminated)
        self.assertIn(
            "[error] Raw HCI connect failed: helper ready-line timeout", buf.getvalue()
        )

    def test_wait_connected_ok(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: True)  # Connected -> True immediately
        _, out = capture(
            lambda: conn.wait_connected(
                props, fakes.FakeGLib, fakes.FakeDbusModule(), deadline_s=0.2
            )
        )
        self.assertIn("[main] Device1 Connected confirmed", out)

    def test_wait_connected_timeout_terminates(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(ready_payload=READY_PREFIX + b"\n")
        conn.proc = proc
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: False)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                conn.wait_connected(
                    props, fakes.FakeGLib, fakes.FakeDbusModule(), deadline_s=0.05
                )
        self.assertTrue(proc.terminated)
        self.assertIn(
            "[error] Device1 not Connected after confirmed raw HCI link",
            buf.getvalue(),
        )

    def test_terminate_verbose_idempotent(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(ready_payload=READY_PREFIX + b"\n")
        conn.proc = proc
        _, out = capture(lambda: conn.terminate(verbose=True))
        self.assertIn("[cleanup] Raw-HCI helper terminated", out)
        conn.terminate(verbose=True)  # idempotent: no second print/terminate
        self.assertEqual(proc.terminate_calls, 1)
        self.assertEqual(out.count("[cleanup] Raw-HCI helper terminated"), 1)

    def test_terminate_error_tolerated(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(ready_payload=READY_PREFIX + b"\n")

        def bad_wait(timeout=3.0):
            raise RuntimeError("wait boom")

        proc.wait = bad_wait
        conn.proc = proc
        _, out = capture(lambda: conn.terminate(verbose=True))
        self.assertIn("[cleanup] Raw-HCI helper terminate error: wait boom", out)

    def test_terminate_never_errors_when_not_spawned(self):
        conn = sec.RawHciConnect(PEER, 30, 0)
        conn.terminate(verbose=True)  # no proc -> silent no-op
        self.assertIsNone(conn.proc)


# ── RemoveDevice / proxies ──────────────────────────────────────────────


class TestRemoveDevice(unittest.TestCase):
    def test_fresh_removes_and_sleeps(self):
        bus = fakes.FakeBus()
        adapter = bus.iface("/org/bluez/hci0", "org.bluez.Adapter1")
        _, out = capture(
            lambda: sec.remove_device(
                adapter, DEV_PATH, fakes.FakeDbusModule(), preserve_bond=False
            )
        )
        self.assertIn("[main] Removed stale BlueZ device cache", out)
        calls = adapter.calls_for("RemoveDevice")
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][1][0], DEV_PATH)

    def test_fresh_no_cached_device_ok(self):
        bus = fakes.FakeBus()
        adapter = bus.iface("/org/bluez/hci0", "org.bluez.Adapter1")

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("no such device")

        adapter.script("RemoveDevice", boom)
        _, out = capture(
            lambda: sec.remove_device(
                adapter, DEV_PATH, fakes.FakeDbusModule(), preserve_bond=False
            )
        )
        self.assertIn("[main] RemoveDevice: no cached device (ok)", out)

    def test_preserve_bond_keeps_record(self):
        bus = fakes.FakeBus()
        adapter = bus.iface("/org/bluez/hci0", "org.bluez.Adapter1")
        _, out = capture(
            lambda: sec.remove_device(
                adapter, DEV_PATH, fakes.FakeDbusModule(), preserve_bond=True
            )
        )
        self.assertIn(
            "[main] --preserve-bond: keeping existing BlueZ device record", out
        )
        self.assertEqual(len(adapter.calls_for("RemoveDevice")), 0)


class TestRecreateProxies(unittest.TestCase):
    def test_returns_live_proxies(self):
        bus = fakes.FakeBus()
        device, props = sec.recreate_proxies(bus, fakes.FakeDbusModule(), DEV_PATH)
        self.assertEqual(device.path, DEV_PATH)
        self.assertEqual(device.name, "org.bluez.Device1")
        self.assertEqual(props.name, "org.freedesktop.DBus.Properties")
        # A fresh Device1 object exists at the path after removal.
        self.assertIs(
            bus.get_object("org.bluez", DEV_PATH).get_iface("org.bluez.Device1"), device
        )


# ── Preserve-bond BlueZ Connect ─────────────────────────────────────────


class TestPreserveBondConnect(unittest.TestCase):
    def _setup(self, state, connect_fire="ok"):
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: state.get(p, False))

        def on_connect(*args, **kwargs):
            rh = kwargs.get("reply_handler")
            eh = kwargs.get("error_handler")
            if connect_fire == "ok":
                rh()
            elif connect_fire == "error":
                eh("org.bluez.Error.Failed: connect refused")
            # "none": never fire -> timeout

        device.script("Connect", on_connect)
        return bus, device, props

    def test_success_not_connected(self):
        bus, device, props = self._setup({"Paired": True, "Connected": False})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.preserve_bond_connect(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                already_connected=False,
                wait_s=0.5,
                connected_deadline_s=0.2,
            )
        text = out.getvalue()
        self.assertIn(
            "[main] Preserve-bond device state: Paired=True, Connected=False", text
        )
        self.assertIn(
            "[main] --preserve-bond: host bond present, using BlueZ Device1.Connect()",
            text,
        )
        self.assertIn("[main] Device1.Connect() async reply: OK", text)
        self.assertIn(
            "[main] Device1 Connected confirmed (preserve-bond, BlueZ transport)", text
        )
        connect_calls = device.calls_for("Connect")
        self.assertEqual(len(connect_calls), 1)
        self.assertEqual(connect_calls[0][2]["timeout"], 30000)

    def test_policy_fail_when_not_paired(self):
        bus, device, props = self._setup({"Paired": False, "Connected": False})
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                sec.preserve_bond_connect(
                    device,
                    props,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    already_connected=False,
                    wait_s=0.2,
                    connected_deadline_s=0.1,
                )
        self.assertIn(
            "[error] --preserve-bond: host bond missing (device not paired)",
            buf.getvalue(),
        )
        self.assertEqual(len(device.calls_for("Connect")), 0)

    def test_connect_error(self):
        bus, device, props = self._setup(
            {"Paired": True, "Connected": False}, connect_fire="error"
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                sec.preserve_bond_connect(
                    device,
                    props,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    already_connected=False,
                    wait_s=0.5,
                    connected_deadline_s=0.2,
                )
        self.assertIn(
            "[error] Device1.Connect() failed: org.bluez.Error.Failed: connect refused",
            buf.getvalue(),
        )

    def test_connect_timeout(self):
        bus, device, props = self._setup(
            {"Paired": True, "Connected": False}, connect_fire="none"
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                sec.preserve_bond_connect(
                    device,
                    props,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    already_connected=False,
                    wait_s=0.05,
                    connected_deadline_s=0.2,
                )
        self.assertIn("[error] Device1.Connect() timed out (35 s)", buf.getvalue())

    def test_connected_not_true_after_connect(self):
        # Connect replies OK but the Connected property never flips.
        bus, device, props = self._setup(
            {"Paired": True, "Connected": False}, connect_fire="ok"
        )
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                sec.preserve_bond_connect(
                    device,
                    props,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    already_connected=False,
                    wait_s=0.5,
                    connected_deadline_s=0.05,
                )
        self.assertIn(
            "[error] Device1 Connected not true after Connect()", buf.getvalue()
        )

    def test_disconnect_first_then_connect(self):
        state = {"Paired": True, "Connected": True}
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: state.get(p, False))

        def on_disconnect(*a, **k):
            state["Connected"] = False

        def on_connect(*args, **kwargs):
            state["Connected"] = True
            kwargs.get("reply_handler")()

        device.script("Disconnect", on_disconnect)
        device.script("Connect", on_connect)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.preserve_bond_connect(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                already_connected=True,
                wait_s=0.5,
                connected_deadline_s=0.5,
                disc_deadline_s=0.5,
            )
        text = out.getvalue()
        self.assertIn(
            "[main] Device1 already connected; disconnecting and "
            "reconnecting fresh so BlueZ configures BAP",
            text,
        )
        self.assertIn("[main] Device1.Connect() async reply: OK", text)
        self.assertEqual(len(device.calls_for("Disconnect")), 1)
        self.assertEqual(len(device.calls_for("Connect")), 1)

    def test_disconnect_error_tolerated(self):
        state = {"Paired": True, "Connected": True}
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: state.get(p, False))

        def on_disconnect(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("already down")

        device.script("Disconnect", on_disconnect)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.preserve_bond_connect(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                already_connected=True,
                wait_s=0.05,
                connected_deadline_s=0.1,
                disc_deadline_s=0.1,
            )
        self.assertIn("[main]   Disconnect ignored: already down", out.getvalue())


# ── State reads / Pairable / Trusted / Pair / Services ──────────────────


class TestDeviceState(unittest.TestCase):
    def test_read_device_state(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script(
            "Get", lambda i, p: {"Paired": True, "Connected": True}.get(p, False)
        )
        paired, connected = capture(
            lambda: sec.read_device_state(props, fakes.FakeDbusModule())
        )[0]
        self.assertTrue(paired)
        self.assertTrue(connected)

    def test_read_device_state_error_raises(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("gone")

        props.script("Get", boom)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with self.assertRaises(sec.CentralError):
                sec.read_device_state(props, fakes.FakeDbusModule())
        self.assertIn("[main] Could not read device state: gone", buf.getvalue())


class TestPairableTrusted(unittest.TestCase):
    def test_set_pairable(self):
        bus = fakes.FakeBus()
        props = bus.iface("/org/bluez/hci0", "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: True)
        _, out = capture(lambda: sec.set_pairable(props, fakes.FakeDbusModule()))
        self.assertIn("[main] Adapter Pairable=True", out)
        set_calls = props.calls_for("Set")
        self.assertEqual(set_calls[0][1][0], "org.bluez.Adapter1")
        self.assertEqual(set_calls[0][1][1], "Pairable")

    def test_set_pairable_error_nonfatal(self):
        bus = fakes.FakeBus()
        props = bus.iface("/org/bluez/hci0", "org.freedesktop.DBus.Properties")

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("nope")

        props.script("Set", boom)
        _, out = capture(lambda: sec.set_pairable(props, fakes.FakeDbusModule()))
        self.assertIn("[main] Pairable set error: nope", out)

    def test_set_trusted(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        _, out = capture(
            lambda: sec.set_trusted(
                props,
                fakes.FakeDbusModule(),
                "Trusted, async pairing over existing ACL...",
            )
        )
        self.assertIn("[main] Trusted, async pairing over existing ACL...", out)

    def test_set_trusted_error_nonfatal(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("nope")

        props.script("Set", boom)
        _, out = capture(
            lambda: sec.set_trusted(props, fakes.FakeDbusModule(), "Trusted msg")
        )
        self.assertIn("[main] Trust set error: nope", out)


class TestPairDevice(unittest.TestCase):
    def _setup(self, state, fire="ok"):
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: state.get(p, False))

        def on_pair(*args, **kwargs):
            rh = kwargs.get("reply_handler")
            eh = kwargs.get("error_handler")
            if fire == "ok":
                rh()
            elif fire == "error":
                eh("org.bluez.Error.AuthenticationFailed: denied")

        device.script("Pair", on_pair)
        return bus, device, props

    def test_pair_success(self):
        bus, device, props = self._setup({"Paired": True, "Connected": True})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.pair_device(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                pair_skip=False,
                pair_deadline_s=0.5,
            )
        text = out.getvalue()
        self.assertIn("[main] Pair() async reply: OK", text)
        self.assertIn("[main] Pair() async completed OK", text)
        self.assertIn("[main] After Pair: Paired=True, Connected=True", text)
        self.assertEqual(len(device.calls_for("Pair")), 1)

    def test_pair_skip(self):
        bus, device, props = self._setup({"Paired": True, "Connected": True})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.pair_device(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                pair_skip=True,
                pair_deadline_s=0.5,
            )
        text = out.getvalue()
        self.assertIn(
            "[main] Pair() skipped (--preserve-bond, bond already present)", text
        )
        self.assertEqual(len(device.calls_for("Pair")), 0)

    def test_pair_error_not_fatal(self):
        # Pre-split behavior: Pair error is reported, flow continues.
        bus, device, props = self._setup(
            {"Paired": False, "Connected": True}, fire="error"
        )
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.pair_device(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                pair_skip=False,
                pair_deadline_s=0.5,
            )
        text = out.getvalue()
        self.assertIn(
            "[main] Pair() async completed with error: "
            "org.bluez.Error.AuthenticationFailed: denied",
            text,
        )

    def test_pair_timeout_not_fatal(self):
        bus, device, props = self._setup(
            {"Paired": False, "Connected": False}, fire="none"
        )
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            sec.pair_device(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                pair_skip=False,
                pair_deadline_s=0.05,
            )
        self.assertIn("[main] Pair() async timed out (35 s)", out.getvalue())

    def test_state_read_failure_after_pair(self):
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")

        def on_pair(*args, **kwargs):
            kwargs.get("reply_handler")()

        def boom(*a, **k):
            raise fakes.FakeDbusModule.exceptions.DBusException("gone")

        device.script("Pair", on_pair)
        props.script("Get", boom)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            paired, connected = sec.pair_device(
                device,
                props,
                fakes.FakeDbusModule(),
                fakes.FakeGLib,
                pair_skip=False,
                pair_deadline_s=0.5,
            )
        self.assertIn("[main] Could not read device state after Pair", out.getvalue())
        self.assertFalse(paired)
        self.assertFalse(connected)


class TestWaitServicesResolved(unittest.TestCase):
    def test_success(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: True)
        _, out = capture(
            lambda: sec.wait_services_resolved(
                props, fakes.FakeDbusModule(), fakes.FakeGLib, deadline_s=0.2
            )
        )
        self.assertIn("[main] ServicesResolved (link encrypted)", out)

    def test_warning_path(self):
        bus = fakes.FakeBus()
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: False)
        _, out = capture(
            lambda: sec.wait_services_resolved(
                props, fakes.FakeDbusModule(), fakes.FakeGLib, deadline_s=0.05
            )
        )
        self.assertIn("[warn] ServicesResolved not set in 30 s, continuing anyway", out)


class TestDisconnectAndWait(unittest.TestCase):
    def test_success(self):
        state = {"Connected": True}
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        props.script("Get", lambda i, p: state.get(p, False))
        device.script("Disconnect", lambda *a, **k: state.update(Connected=False))
        _, out = capture(
            lambda: sec.disconnect_and_wait(
                device, props, fakes.FakeDbusModule(), fakes.FakeGLib, deadline_s=0.5
            )
        )
        self.assertIn("[cleanup] ACL link disconnected", out)
        self.assertEqual(len(device.calls_for("Disconnect")), 1)

    def test_disconnect_error(self):
        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")

        def boom(*a, **k):
            raise RuntimeError("gone")

        device.script("Disconnect", boom)
        _, out = capture(
            lambda: sec.disconnect_and_wait(
                device, props, fakes.FakeDbusModule(), fakes.FakeGLib, deadline_s=0.1
            )
        )
        self.assertIn("[cleanup] Disconnect error: gone", out)


# ── Resource cleanup ordering through the CLI owner ─────────────────────


class TestCleanupOrdering(unittest.TestCase):
    def test_disconnect_before_helper_terminate(self):
        """After a post-transport failure (read-state CentralError), the
        finally-registered owner runs Device Disconnect BEFORE helper
        terminate — no ACL/fd/helper leak."""
        from bap_central import CentralCleanup  # noqa: E402

        bus = fakes.FakeBus()
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        state = {"Connected": True}
        props.script("Get", lambda i, p: state.get(p, False))
        device.script("Disconnect", lambda *a, **k: state.update(Connected=False))

        conn = sec.RawHciConnect(PEER, 30, 0)
        proc = FakeRawProc(ready_payload=READY_PREFIX + b"\n")
        conn.proc = proc

        order = []
        owner = CentralCleanup()
        owner.register(
            "disconnect",
            lambda: (
                order.append("disconnect"),
                sec.disconnect_and_wait(
                    device,
                    props,
                    fakes.FakeDbusModule(),
                    fakes.FakeGLib,
                    deadline_s=0.5,
                ),
            ),
        )
        owner.register(
            "helper", lambda: (order.append("helper"), conn.terminate(verbose=True))
        )

        try:
            # Post-transport failure: read_device_state raises CentralError.
            props.script(
                "Get",
                lambda i, p: (_ for _ in ()).throw(
                    fakes.FakeDbusModule.exceptions.DBusException("gone")
                ),
            )
            try:
                sec.read_device_state(props, fakes.FakeDbusModule())
                self.fail("expected CentralError")
            except sec.CentralError:
                pass
        finally:
            owner.run()

        self.assertEqual(order, ["disconnect", "helper"])
        self.assertTrue(proc.terminated)

    def test_owner_run_idempotent(self):
        from bap_central import CentralCleanup  # noqa: E402

        ran = []
        owner = CentralCleanup()
        owner.register("helper", lambda: ran.append("helper"))
        owner.run()
        owner.run()
        self.assertEqual(ran, ["helper"])

    def test_owner_fixed_stage_order(self):
        from bap_central import CentralCleanup  # noqa: E402

        order = []
        owner = CentralCleanup()
        # Register out of order; run must use the fixed teardown order.
        owner.register("helper", lambda: order.append("helper"))
        owner.register("agent", lambda: order.append("agent"))
        owner.register("transports", lambda: order.append("transports"))
        owner.register("writer", lambda: order.append("writer"))
        owner.register("endpoint", lambda: order.append("endpoint"))
        owner.register("disconnect", lambda: order.append("disconnect"))
        owner.register("discovery", lambda: order.append("discovery"))
        owner.run()
        self.assertEqual(
            order,
            [
                "discovery",
                "transports",
                "writer",
                "endpoint",
                "agent",
                "disconnect",
                "helper",
            ],
        )


if __name__ == "__main__":
    unittest.main()
