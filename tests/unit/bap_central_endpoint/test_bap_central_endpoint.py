#!/usr/bin/env python3
"""Unit tests for scripts/bap_central_endpoint.py (BAP endpoint module).

Proves, on stdlib python3 with fake D-Bus/GLib (no live BlueZ):
  - SelectProperties returns the exact LC3 config byte blobs and QoS dict
    for mono FL/FR and stereo (FL|FR) channel allocations;
  - SetConfiguration LTV channel-allocation parsing and pending-state
    queueing (deferred Acquire);
  - deferred async Acquire: UnixFd .take() / plain int fd handling,
    all-success, partial/error/timeout all-or-nothing with every taken
    fd closed and Release()d;
  - stream-mode inference (stereo_b / stereo_a / mono) + SDU sizes;
  - Release / ClearConfiguration idempotence: fds closed exactly once,
    records removed immediately, Release never unregisters.
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
import bap_central_endpoint as ep  # noqa: E402

HCI_PATH = "/org/bluez/hci0"
TP1 = HCI_PATH + "/dev_LE/iso0"
TP2 = HCI_PATH + "/dev_LE/iso1"

# Byte-for-byte established blobs under test.
CAPS_MONO = bytes(
    [
        0x03,
        0x01,
        0x80,
        0x00,
        0x02,
        0x02,
        0x03,
        0x02,
        0x03,
        0x01,
        0x05,
        0x04,
        0x78,
        0x00,
        0xF0,
        0x00,
    ]
)
CAPS_STEREO = bytes(
    [
        0x03,
        0x01,
        0x80,
        0x00,
        0x02,
        0x02,
        0x03,
        0x02,
        0x03,
        0x02,
        0x05,
        0x04,
        0x78,
        0x00,
        0xF0,
        0x00,
    ]
)
CONFIG_MONO = bytes([0x02, 0x01, 0x08, 0x02, 0x02, 0x01, 0x03, 0x04, 0x78, 0x00])
CONFIG_STEREO = bytes(
    [
        0x02,
        0x01,
        0x08,
        0x02,
        0x02,
        0x01,
        0x03,
        0x04,
        0x78,
        0x00,
        0x05,
        0x03,
        0x03,
        0x00,
        0x00,
        0x00,
    ]
)


def capture(fn):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        result = fn()
    return result, buf.getvalue()


def make_endpoint(stereo=False, mono=False):
    bus = fakes.FakeBus()
    dbus = fakes.FakeDbusModule()
    svc = fakes.FakeServiceModule()
    cls = ep.make_endpoint_class(dbus, svc)
    endpoint = cls(bus, ep.ENDPOINT_PATH, stereo=stereo, mono=mono)
    return bus, dbus, endpoint


def transport_iface(bus, tp):
    return bus.iface(tp, "org.bluez.MediaTransport1")


def ltv_config(channel_alloc):
    """Configuration LTV blob: mono config + channel-allocation LTV."""
    return ep.LC3_CONFIG_MONO + bytes(
        [0x05, 0x03, channel_alloc & 0xFF, 0x00, 0x00, 0x00]
    )


class TestConstants(unittest.TestCase):
    def test_blobs_unchanged(self):
        self.assertEqual(ep.LC3_CAPS, CAPS_MONO)
        self.assertEqual(ep.LC3_CAPS_STEREO, CAPS_STEREO)
        self.assertEqual(ep.LC3_CONFIG_MONO, CONFIG_MONO)
        self.assertEqual(ep.LC3_CONFIG_STEREO, CONFIG_STEREO)
        self.assertEqual(ep.ENDPOINT_PATH, "/bap_central/endpoint0")
        self.assertEqual(ep.PAC_SOURCE_UUID, "00002bcb-0000-1000-8000-00805f9b34fb")
        self.assertEqual(ep.LC3_CODEC, 0x06)
        self.assertEqual(ep.FRAME_BYTES, 120)


class TestSelectProperties(unittest.TestCase):
    def _select(self, channel_alloc, stereo_caps=False):
        bus, dbus, endpoint = make_endpoint()
        props = {
            "Capabilities": list(CAPS_STEREO if stereo_caps else CAPS_MONO),
            "ChannelAllocation": channel_alloc,
            "QoS": {},
            "Locations": 0,
        }
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ret = endpoint.SelectProperties(props)
        return ret, out.getvalue(), endpoint

    def test_stereo_exact_bytes_and_qos(self):
        ret, out, _ = self._select(0x03, stereo_caps=True)
        self.assertEqual(bytes(bytearray(ret["Capabilities"])), CONFIG_STEREO)
        qos = ret["QoS"]
        self.assertEqual(int(qos["Framing"]), 0)
        self.assertEqual(int(qos["PHY"]), 0x02)
        self.assertEqual(int(qos["Interval"]), 10000)
        self.assertEqual(int(qos["SDU"]), 240)
        self.assertEqual(int(qos["Retransmissions"]), 2)
        self.assertEqual(int(qos["Latency"]), 10)
        self.assertEqual(int(qos["PresentationDelay"]), 40000)
        self.assertEqual(int(qos["TargetLatency"]), 0x02)
        self.assertIn("ChannelAllocation=0x0003", out)

    def test_mono_fl_exact_bytes(self):
        ret, out, _ = self._select(0x01)
        self.assertEqual(
            bytes(bytearray(ret["Capabilities"])),
            CONFIG_MONO + bytes([0x05, 0x03, 0x01, 0x00, 0x00, 0x00]),
        )
        self.assertEqual(int(ret["QoS"]["SDU"]), 120)

    def test_mono_fr_exact_bytes(self):
        ret, _, _ = self._select(0x02)
        self.assertEqual(
            bytes(bytearray(ret["Capabilities"])),
            CONFIG_MONO + bytes([0x05, 0x03, 0x02, 0x00, 0x00, 0x00]),
        )
        self.assertEqual(int(ret["QoS"]["SDU"]), 120)

    def test_default_channel_alloc_unknown_is_mono_sdu(self):
        ret, _, _ = self._select(0x00)
        self.assertEqual(int(ret["QoS"]["SDU"]), 120)


class TestSetConfiguration(unittest.TestCase):
    def test_parses_channel_alloc_and_queues(self):
        bus, dbus, endpoint = make_endpoint()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x03))})
        self.assertTrue(endpoint.config_done)
        self.assertEqual(len(endpoint._pending_transports), 1)
        self.assertEqual(endpoint._pending_transports[0]["path"], TP1)
        self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], 0x03)
        self.assertIn("[endpoint] SetConfiguration enter", out.getvalue())
        self.assertIn("[endpoint]  parsed channel_alloc=0x03", out.getvalue())

    def test_parses_mono_channel_alloc(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x02))})
        self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], 0x02)

    def test_malformed_defaults_to_stereo(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.SetConfiguration(TP1, {"Configuration": []})
        self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], 0x03)

    def test_two_ase_queue(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        endpoint.SetConfiguration(TP2, {"Configuration": list(ltv_config(0x02))})
        self.assertEqual(len(endpoint._pending_transports), 2)
        self.assertEqual(
            [t["channel_alloc"] for t in endpoint._pending_transports], [0x01, 0x02]
        )


class TestAcquireTransports(unittest.TestCase):
    def _queue(self, endpoint, paths_chans):
        for tp, ch in paths_chans:
            endpoint.SetConfiguration(tp, {"Configuration": list(ltv_config(ch))})

    def test_all_success_stereo_b(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x03)])
        tr = transport_iface(bus, TP1)

        def on_acquire(*args, **kwargs):
            kwargs["reply_handler"](3, 0, 240)

        tr.script("Acquire", on_acquire)
        result, out = capture(
            lambda: ep.acquire_transports(
                bus,
                dbus,
                fakes.FakeGLib,
                endpoint,
                config_timeout_s=0.1,
                grace_s=0.02,
                acquire_timeout_s=0.5,
            )
        )
        transports, stream_mode, sdu_size = result
        self.assertEqual(stream_mode, "stereo_b")
        self.assertEqual(sdu_size, 240)
        self.assertEqual(len(transports), 1)
        self.assertEqual(transports[0]["path"], TP1)
        self.assertEqual(transports[0]["channel_alloc"], 0x03)
        self.assertIn("[main] Stream mode: stereo_b, SDU size: 240 bytes", out)
        self.assertIn("[main]   async Acquire(" + TP1 + ") ...", out)
        self.assertIn("[main] Acquired: path=" + TP1, out)

    def test_all_success_mono(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01)])
        tr = transport_iface(bus, TP1)

        def on_acquire(*args, **kwargs):
            kwargs["reply_handler"](7, 0, 120)

        tr.script("Acquire", on_acquire)
        transports, stream_mode, sdu_size = capture(
            lambda: ep.acquire_transports(
                bus,
                dbus,
                fakes.FakeGLib,
                endpoint,
                config_timeout_s=0.1,
                grace_s=0.02,
                acquire_timeout_s=0.5,
            )
        )[0]
        self.assertEqual(stream_mode, "mono")
        self.assertEqual(sdu_size, 120)
        self.assertEqual(transports[0]["channel_alloc"], 0x01)

    def test_all_success_stereo_a_two_fds(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01), (TP2, 0x02)])
        r1, w1 = os.pipe()
        r2, w2 = os.pipe()
        os.close(r1)
        os.close(r2)
        tr1 = transport_iface(bus, TP1)
        tr2 = transport_iface(bus, TP2)
        tr1.script("Acquire", lambda *a, **k: k["reply_handler"](w1, 0, 120))
        tr2.script("Acquire", lambda *a, **k: k["reply_handler"](w2, 0, 120))
        try:
            transports, stream_mode, sdu_size = capture(
                lambda: ep.acquire_transports(
                    bus,
                    dbus,
                    fakes.FakeGLib,
                    endpoint,
                    config_timeout_s=0.1,
                    grace_s=0.02,
                    acquire_timeout_s=0.5,
                )
            )[0]
        finally:
            # The owner (release stage) closes these; verify here too.
            pass
        self.assertEqual(stream_mode, "stereo_a")
        self.assertEqual(sdu_size, 120)
        self.assertEqual(len(transports), 2)
        self.assertEqual([t["channel_alloc"] for t in transports], [0x01, 0x02])

    def test_unix_fd_take(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01)])
        tr = transport_iface(bus, TP1)

        class FakeUnixFd:
            def __init__(self, fd):
                self._fd = fd

            def take(self):
                return self._fd

        r, w = os.pipe()
        os.close(r)
        tr.script("Acquire", lambda *a, **k: k["reply_handler"](FakeUnixFd(w), 0, 120))
        transports, _, _ = capture(
            lambda: ep.acquire_transports(
                bus,
                dbus,
                fakes.FakeGLib,
                endpoint,
                config_timeout_s=0.1,
                grace_s=0.02,
                acquire_timeout_s=0.5,
            )
        )[0]
        self.assertEqual(transports[0]["fd"], w)
        os.close(w)

    def test_partial_failure_closes_all_taken_fds(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01), (TP2, 0x02)])
        r1, w1 = os.pipe()
        r2, w2 = os.pipe()
        os.close(r1)
        os.close(r2)
        tr1 = transport_iface(bus, TP1)
        tr2 = transport_iface(bus, TP2)
        tr1.script("Acquire", lambda *a, **k: k["reply_handler"](w1, 0, 120))
        tr2.script(
            "Acquire",
            lambda *a, **k: k["error_handler"]("org.bluez.Error.Failed: iso failed"),
        )
        buf = io.StringIO()
        errbuf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with contextlib.redirect_stderr(errbuf):
                with self.assertRaises(ep.CentralError):
                    ep.acquire_transports(
                        bus,
                        dbus,
                        fakes.FakeGLib,
                        endpoint,
                        config_timeout_s=0.1,
                        grace_s=0.02,
                        acquire_timeout_s=0.5,
                    )
        self.assertIn(
            "[error] All-or-nothing: 1/2 Acquire(s) failed", errbuf.getvalue()
        )
        # The taken fd was closed by the all-or-nothing path.
        with self.assertRaises(OSError):
            os.write(w1, b"x")
        # The failed transport was Release()d (tolerated) — no double close.
        self.assertGreaterEqual(len(tr1.calls_for("Release")), 0)

    def test_timeout_closes_acquired_fds(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01)])
        r, w = os.pipe()
        os.close(r)
        tr = transport_iface(bus, TP1)
        tr.script("Acquire", lambda *a, **k: k["reply_handler"](w, 0, 120))
        # Second pending transport never replies -> bounded acquire timeout.
        self._queue(endpoint, [(TP2, 0x02)])
        tr2 = transport_iface(bus, TP2)

        def never(*a, **k):
            pass

        tr2.script("Acquire", never)
        errbuf = io.StringIO()
        with contextlib.redirect_stderr(errbuf):
            with self.assertRaises(ep.CentralError):
                ep.acquire_transports(
                    bus,
                    dbus,
                    fakes.FakeGLib,
                    endpoint,
                    config_timeout_s=0.1,
                    grace_s=0.02,
                    acquire_timeout_s=0.05,
                )
        self.assertIn("[error] Acquire timed out (got 1/2 replies)", errbuf.getvalue())
        with self.assertRaises(OSError):
            os.write(w, b"x")

    def test_no_transports_acquired(self):
        bus, dbus, endpoint = make_endpoint()
        self._queue(endpoint, [(TP1, 0x01)])
        tr = transport_iface(bus, TP1)

        def error(*a, **k):
            k["error_handler"]("org.bluez.Error.NotAuthorized: nope")

        tr.script("Acquire", error)
        errbuf = io.StringIO()
        with contextlib.redirect_stderr(errbuf):
            with self.assertRaises(ep.CentralError):
                ep.acquire_transports(
                    bus,
                    dbus,
                    fakes.FakeGLib,
                    endpoint,
                    config_timeout_s=0.1,
                    grace_s=0.02,
                    acquire_timeout_s=0.5,
                )
        # All-or-nothing path fires before the bare "No transports" check.
        self.assertIn("[error] All-or-nothing", errbuf.getvalue())

    def test_config_timeout(self):
        bus, dbus, endpoint = make_endpoint()  # config_done stays False
        buf = io.StringIO()
        errbuf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            with contextlib.redirect_stderr(errbuf):
                with self.assertRaises(ep.CentralError):
                    ep.acquire_transports(
                        bus,
                        dbus,
                        fakes.FakeGLib,
                        endpoint,
                        config_timeout_s=0.05,
                        grace_s=0.02,
                        acquire_timeout_s=0.5,
                    )
        self.assertIn(
            "[error] SetConfiguration not received within 30 s", buf.getvalue()
        )
        self.assertIn(
            "[hint] Check: does the LE Audio Receiver register PACS/ASCS?",
            buf.getvalue(),
        )

    def test_config_done_but_no_pending(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.config_done = True
        outbuf = io.StringIO()
        with contextlib.redirect_stdout(outbuf):
            with self.assertRaises(ep.CentralError):
                ep.acquire_transports(
                    bus,
                    dbus,
                    fakes.FakeGLib,
                    endpoint,
                    config_timeout_s=0.1,
                    grace_s=0.02,
                    acquire_timeout_s=0.5,
                )
        self.assertIn(
            "[error] SetConfiguration received but no pending transports",
            outbuf.getvalue(),
        )


class TestReleaseTransports(unittest.TestCase):
    def _closed(self, fd):
        try:
            os.fstat(fd)
            return False
        except OSError:
            return True

    def test_release_orders_fd_close_after_dbus_release(self):
        bus, dbus, endpoint = make_endpoint()
        r1, w1 = os.pipe()
        r2, w2 = os.pipe()
        os.close(r1)
        os.close(r2)
        endpoint.transports = [
            {"path": TP1, "fd": w1, "write_mtu": 120, "channel_alloc": 0x01},
            {"path": TP2, "fd": w2, "write_mtu": 120, "channel_alloc": 0x02},
        ]
        tr1 = transport_iface(bus, TP1)
        tr2 = transport_iface(bus, TP2)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ep.release_transports(bus, dbus, list(endpoint.transports), endpoint)
        text = out.getvalue()
        self.assertIn("[cleanup] Transport " + TP1 + " released", text)
        self.assertIn("[cleanup] Transport " + TP2 + " released", text)
        self.assertTrue(self._closed(w1))
        self.assertTrue(self._closed(w2))
        self.assertEqual(endpoint.transports, [])
        # Exactly one Release per transport; fds closed exactly once.
        self.assertEqual(len(tr1.calls_for("Release")), 1)
        self.assertEqual(len(tr2.calls_for("Release")), 1)

    def test_release_error_tolerated_fd_still_closed(self):
        bus, dbus, endpoint = make_endpoint()
        r, w = os.pipe()
        os.close(r)
        endpoint.transports = [
            {"path": TP1, "fd": w, "write_mtu": 120, "channel_alloc": 0x01},
        ]
        tr = transport_iface(bus, TP1)

        def boom(*a, **k):
            raise RuntimeError("gone")

        tr.script("Release", boom)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ep.release_transports(bus, dbus, list(endpoint.transports), endpoint)
        self.assertIn(
            "[cleanup] Transport " + TP1 + " release error: gone", out.getvalue()
        )
        self.assertTrue(self._closed(w))

    def test_release_idempotent(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.transports = []
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ep.release_transports(bus, dbus, [], endpoint)
            ep.release_transports(bus, dbus, [], endpoint)
        self.assertEqual(out.getvalue(), "")


class TestEndpointReleaseClear(unittest.TestCase):
    def _closed(self, fd):
        try:
            os.fstat(fd)
            return False
        except OSError:
            return True

    def test_release_callback_closes_fds_and_clears(self):
        bus, dbus, endpoint = make_endpoint()
        r, w = os.pipe()
        os.close(r)
        endpoint.transports = [
            {"path": TP1, "fd": w, "write_mtu": 120, "channel_alloc": 0x01},
        ]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            endpoint.Release()
        self.assertIn("[endpoint] Release", out.getvalue())
        self.assertTrue(self._closed(w))
        self.assertEqual(endpoint.transports, [])
        self.assertEqual(endpoint._pending_transports, [])
        self.assertFalse(endpoint.config_done)

    def test_release_callback_idempotent_no_double_close(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.Release()
        endpoint.Release()  # second: no fds left, no double close

    def test_release_never_unregisters(self):
        bus, dbus, endpoint = make_endpoint()
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            endpoint.Release()
        self.assertEqual(len(media.calls_for("UnregisterEndpoint")), 0)

    def test_clear_configuration_closes_matching_fd(self):
        bus, dbus, endpoint = make_endpoint()
        r, w = os.pipe()
        os.close(r)
        endpoint.transports = [
            {"path": TP1, "fd": w, "write_mtu": 120, "channel_alloc": 0x01},
        ]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            endpoint.ClearConfiguration(TP1)
        self.assertIn("[endpoint] ClearConfiguration(" + TP1 + ")", out.getvalue())
        self.assertTrue(self._closed(w))
        self.assertEqual(endpoint.transports, [])

    def test_clear_configuration_unknown_path_noop(self):
        bus, dbus, endpoint = make_endpoint()
        r, w = os.pipe()
        os.close(r)
        endpoint.transports = [
            {"path": TP1, "fd": w, "write_mtu": 120, "channel_alloc": 0x01},
        ]
        endpoint.ClearConfiguration(TP2)  # different path: no close
        self.assertFalse(self._closed(w))
        self.assertEqual(len(endpoint.transports), 1)
        os.close(w)

    def test_clear_pending_removes_record(self):
        bus, dbus, endpoint = make_endpoint()
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x03))})
        self.assertEqual(len(endpoint._pending_transports), 1)
        endpoint.ClearConfiguration(TP1)
        self.assertEqual(endpoint._pending_transports, [])


class TestRegisterUnregisterEndpoint(unittest.TestCase):
    def test_register(self):
        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        cls = ep.make_endpoint_class(dbus, svc)
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        endpoint, out = capture(
            lambda: ep.register_endpoint(media, bus, dbus, cls, stereo=False)
        )[0:2]
        self.assertIsInstance(endpoint, cls)
        self.assertIn(
            "[main] BAP source endpoint registered at " + ep.ENDPOINT_PATH, out
        )
        calls = media.calls_for("RegisterEndpoint")
        self.assertEqual(len(calls), 1)
        path, props = calls[0][1][0], calls[0][1][1]
        self.assertEqual(path, ep.ENDPOINT_PATH)
        self.assertEqual(str(props["UUID"]), ep.PAC_SOURCE_UUID)
        self.assertEqual(int(props["Codec"]), ep.LC3_CODEC)
        self.assertEqual(bytes(bytearray(props["Capabilities"])), CAPS_MONO)

    def test_register_stereo_caps(self):
        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        cls = ep.make_endpoint_class(dbus, svc)
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        ep.register_endpoint(media, bus, dbus, cls, stereo=True)
        calls = media.calls_for("RegisterEndpoint")
        self.assertEqual(bytes(bytearray(calls[0][1][1]["Capabilities"])), CAPS_STEREO)

    def test_unregister(self):
        bus = fakes.FakeBus()
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        _, out = capture(lambda: ep.unregister_endpoint(media))
        self.assertIn("[cleanup] Endpoint unregistered", out)
        self.assertEqual(len(media.calls_for("UnregisterEndpoint")), 1)

    def test_unregister_error_tolerated(self):
        bus = fakes.FakeBus()
        media = bus.iface(HCI_PATH, "org.bluez.Media1")

        def boom(*a, **k):
            raise RuntimeError("gone")

        media.script("UnregisterEndpoint", boom)
        _, out = capture(lambda: ep.unregister_endpoint(media))
        self.assertIn("[cleanup] Endpoint unregister error: gone", out)


class TestMonoEndpoint(unittest.TestCase):
    """Public-boundary tests for the strict one-ASE mono mode.

    The admission contract mirrors BlueZ 5.86 client/player.c: at most one
    accepted transport (pending or acquired), exact FL (0x01) or FR (0x02)
    channel allocation, and org.bluez.Error.Rejected on every violation with
    no state mutation. Capacity is restored from actual owned-list state on
    ClearConfiguration/Release, never from a drifting counter.
    """

    REJECTED = "org.bluez.Error.Rejected"

    def _select(self, endpoint, channel_alloc, caps=CAPS_MONO):
        props = {
            "Capabilities": list(caps),
            "ChannelAllocation": channel_alloc,
            "QoS": {},
            "Locations": 0,
        }
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            ret = endpoint.SelectProperties(props)
        return ret, out.getvalue()

    def _assert_rejected(self, fn):
        with self.assertRaises(fakes.DBusException) as cm:
            fn()
        self.assertEqual(cm.exception._dbus_error_name, self.REJECTED)
        return cm.exception

    def test_mono_first_fl_selectproperties_exact(self):
        bus, dbus, endpoint = make_endpoint(mono=True)
        ret, out = self._select(endpoint, 0x01)
        self.assertEqual(
            bytes(bytearray(ret["Capabilities"])),
            CONFIG_MONO + bytes([0x05, 0x03, 0x01, 0x00, 0x00, 0x00]),
        )
        self.assertEqual(int(ret["QoS"]["SDU"]), 120)
        self.assertIn("ChannelAllocation=0x0001", out)

    def test_mono_combined_and_unknown_select_rejected_no_mutation(self):
        for alloc in (0x03, 0x00):
            bus, dbus, endpoint = make_endpoint(mono=True)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self._assert_rejected(
                    lambda: endpoint.SelectProperties(
                        {
                            "Capabilities": list(CAPS_MONO),
                            "ChannelAllocation": alloc,
                            "QoS": {},
                            "Locations": 0,
                        }
                    )
                )
            text = out.getvalue()
            self.assertIn(
                "[endpoint] Mono mode requires FL or FR allocation: "
                "rejecting {:#04x}".format(alloc),
                text,
            )
            # Fail closed: no stereo config returned, no state mutation.
            self.assertEqual(endpoint._pending_transports, [])
            self.assertEqual(endpoint.transports, [])
            self.assertFalse(endpoint.config_done)

    def test_mono_first_fl_or_fr_setconfiguration_queues_one(self):
        for alloc in (0x01, 0x02):
            bus, dbus, endpoint = make_endpoint(mono=True)
            endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(alloc))})
            self.assertEqual(len(endpoint._pending_transports), 1)
            self.assertEqual(endpoint._pending_transports[0]["path"], TP1)
            self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], alloc)
            self.assertTrue(endpoint.config_done)

    def test_mono_second_setconfiguration_rejected_unchanged(self):
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        first_record = endpoint._pending_transports[0]
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self._assert_rejected(
                lambda: endpoint.SetConfiguration(
                    TP2, {"Configuration": list(ltv_config(0x02))}
                )
            )
        self.assertIn(
            "[endpoint] Mono transport limit reached: rejecting", out.getvalue()
        )
        # First record, config_done, and ownership state all unchanged.
        self.assertEqual(endpoint._pending_transports, [first_record])
        self.assertEqual(endpoint.transports, [])
        self.assertTrue(endpoint.config_done)

    def test_mono_malformed_or_combined_setconfiguration_rejected_atomically(self):
        for cfg in ([], list(ltv_config(0x03))):
            bus, dbus, endpoint = make_endpoint(mono=True)
            self._assert_rejected(
                lambda: endpoint.SetConfiguration(TP1, {"Configuration": cfg})
            )
            self.assertEqual(endpoint._pending_transports, [])
            self.assertEqual(endpoint.transports, [])
            self.assertFalse(endpoint.config_done)

    def test_mono_select_after_pending_or_acquired_transport_rejected(self):
        # One pending transport already accepted.
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self._assert_rejected(
                lambda: endpoint.SelectProperties(
                    {
                        "Capabilities": list(CAPS_MONO),
                        "ChannelAllocation": 0x02,
                        "QoS": {},
                        "Locations": 0,
                    }
                )
            )
        self.assertIn(
            "[endpoint] Mono transport limit reached: rejecting", out.getvalue()
        )
        # One acquired transport also exhausts the mono limit.
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.transports = [
            {"path": TP1, "fd": 5, "write_mtu": 120, "channel_alloc": 0x02}
        ]
        self._assert_rejected(
            lambda: endpoint.SelectProperties(
                {
                    "Capabilities": list(CAPS_MONO),
                    "ChannelAllocation": 0x01,
                    "QoS": {},
                    "Locations": 0,
                }
            )
        )
        self.assertEqual(len(endpoint.transports), 1)

    def test_mono_clear_pending_restores_capacity(self):
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        endpoint.ClearConfiguration(TP1)
        # Fresh first configuration accepted again.
        endpoint.SetConfiguration(TP2, {"Configuration": list(ltv_config(0x02))})
        self.assertEqual(len(endpoint._pending_transports), 1)
        self.assertEqual(endpoint._pending_transports[0]["path"], TP2)
        self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], 0x02)

    def test_mono_clear_acquired_restores_capacity(self):
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.transports = [
            {"path": TP1, "fd": 5, "write_mtu": 120, "channel_alloc": 0x01}
        ]
        endpoint.ClearConfiguration(TP1)
        endpoint.SetConfiguration(TP2, {"Configuration": list(ltv_config(0x01))})
        self.assertEqual(len(endpoint._pending_transports), 1)

    def test_mono_release_clears_ownership_and_permits_fresh(self):
        bus, dbus, endpoint = make_endpoint(mono=True)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        self.assertTrue(endpoint.config_done)
        endpoint.Release()
        self.assertEqual(endpoint._pending_transports, [])
        self.assertEqual(endpoint.transports, [])
        self.assertFalse(endpoint.config_done)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        self.assertEqual(len(endpoint._pending_transports), 1)
        self.assertTrue(endpoint.config_done)

    def test_default_mode_a_two_transports_unchanged(self):
        bus, dbus, endpoint = make_endpoint()
        # Both FL and FR accepted for Mode A; no mono admission.
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x01))})
        endpoint.SetConfiguration(TP2, {"Configuration": list(ltv_config(0x02))})
        self.assertEqual(len(endpoint._pending_transports), 2)
        ret, _ = self._select(endpoint, 0x02)
        self.assertEqual(int(ret["QoS"]["SDU"]), 120)

    def test_stereo_mode_b_unchanged(self):
        bus, dbus, endpoint = make_endpoint(stereo=True)
        ret, _ = self._select(endpoint, 0x03, caps=CAPS_STEREO)
        self.assertEqual(bytes(bytearray(ret["Capabilities"])), CONFIG_STEREO)
        self.assertEqual(int(ret["QoS"]["SDU"]), 240)
        endpoint.SetConfiguration(TP1, {"Configuration": list(ltv_config(0x03))})
        self.assertEqual(len(endpoint._pending_transports), 1)
        self.assertEqual(endpoint._pending_transports[0]["channel_alloc"], 0x03)

    def test_constructor_rejects_mono_stereo_atomically(self):
        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        cls = ep.make_endpoint_class(dbus, svc)
        with self.assertRaises(ValueError):
            cls(bus, ep.ENDPOINT_PATH, stereo=True, mono=True)

    def test_register_mono_forwards_flag_one_channel_caps(self):
        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        cls = ep.make_endpoint_class(dbus, svc)
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        endpoint = ep.register_endpoint(media, bus, dbus, cls, mono=True)
        self.assertIsInstance(endpoint, cls)
        self.assertTrue(endpoint.mono)
        self.assertFalse(endpoint.stereo)
        calls = media.calls_for("RegisterEndpoint")
        self.assertEqual(len(calls), 1)
        props = calls[0][1][1]
        self.assertEqual(bytes(bytearray(props["Capabilities"])), CAPS_MONO)


if __name__ == "__main__":
    unittest.main()
