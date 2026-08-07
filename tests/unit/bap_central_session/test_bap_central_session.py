#!/usr/bin/env python3
"""Unit tests for scripts/bap_central_session.py (R9 stream/session split)
plus CLI golden tests for the thin scripts/bap_central.py coordinator.

Proves, on stdlib python3 (fake encoders/writers; REAL liblc3 golden
payload tests skip when the library is unavailable):
  - sine/LC3 output sizes: 120-byte mono, 240-byte stereo_b concat,
    stereo_a two 120-byte frames with FL,FR transport sorting;
  - StreamSession start/encode_frame/run/stop_writer: duration,
    KeyboardInterrupt, writer error, alive-writer force stop, exact
    teardown prints, idempotence;
  - successful teardown exact call/order through the CentralCleanup owner
    (transports -> writer -> endpoint -> agent -> disconnect -> helper);
  - tolerated release/unregister/disconnect/helper errors;
  - idempotent cleanup (run twice);
  - raw helper terminated on a pre-stream failure through the owner;
  - CLI golden: all six flags/defaults/help/description, path constants,
    flpr_hang_gate launcher argv compatibility, --help without dbus/liblc3.
"""

import contextlib
import io
import os
import subprocess
import sys
import unittest
from unittest import mock

sys.path.insert(
    0,
    os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
    ),
)
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bap_central_fakes as fakes  # noqa: E402
import bap_central_session as sess  # noqa: E402
import bap_central_endpoint as ep  # noqa: E402
import bap_central_security as sec  # noqa: E402

DEV_PATH = "/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA"
HCI_PATH = "/org/bluez/hci0"
TP1 = HCI_PATH + "/dev_LE/iso0"
TP2 = HCI_PATH + "/dev_LE/iso1"
PEER = "DB:A6:0C:05:A2:AA"

SCRIPTS_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "scripts"
)


def capture(fn):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        result = fn()
    return result, buf.getvalue()


class FakeEncoder:
    """Deterministic 120-byte encoder; counts instances and calls."""

    instances = 0

    def __init__(self, *a, **k):
        FakeEncoder.instances += 1
        self.calls = 0

    def encode(self, pcm):
        self.calls += 1
        return b"\x5a\xa5" * 60

    @classmethod
    def reset(cls):
        cls.instances = 0


class FakeWriter:
    """Records start/stop/join; scripted join result.  First two
    positional args mirror the real PacedWriter (encode_fn, duration_s)
    so StreamSession can construct it identically."""

    def __init__(
        self,
        encode_fn,
        duration_s,
        join_results=(True, True),
        error=None,
        tail_frames=0,
        frames=5,
    ):
        self.encode_fn = encode_fn
        self.duration_s = duration_s
        self.started = False
        self.stopped = False
        self.join_calls = 0
        self._join_results = list(join_results)
        self.error = error
        self.tail_frames = tail_frames
        self.frames = frames
        self.stopped_by_fd = False

    def start(self):
        self.started = True

    def stop(self):
        self.stopped = True

    def join(self, timeout_s=5.0):
        self.join_calls += 1
        if self._join_results:
            return self._join_results.pop(0)
        return True


def make_transports(chans):
    return [
        {
            "path": TP1 if i == 0 else TP2,
            "fd": 100 + i,
            "write_mtu": 120,
            "channel_alloc": ch,
        }
        for i, ch in enumerate(chans)
    ]


def writer_with(**kwargs):
    """FakeWriter factory: forwards StreamSession's (encode_fn, duration_s)
    positional args and layers scripted kwargs on top."""
    return lambda *a, **k: FakeWriter(*a, **k, **kwargs)


def make_session(
    stream_mode,
    transports=None,
    freq=1000.0,
    duration_s=0.01,
    writer_cls=None,
    encoder_cls=FakeEncoder,
):
    if transports is None:
        transports = make_transports(
            [0x03]
            if stream_mode == "stereo_b"
            else [0x01, 0x02]
            if stream_mode == "stereo_a"
            else [0x01]
        )
    writer_cls = writer_cls or FakeWriter
    return sess.StreamSession(
        transports,
        stream_mode,
        freq,
        duration_s,
        writer_cls=writer_cls,
        encoder_cls=encoder_cls,
    )


class TestModuleLazyImport(unittest.TestCase):
    def test_no_liblc3_at_import(self):
        # Importing the session module must NOT load liblc3 (stdlib tests).
        # Subprocess so the golden liblc3 tests in this process cannot
        # populate the module cache first.
        code = (
            "import sys; sys.path.insert(0, {scripts!r});"
            "import bap_central_session;"
            "print(bap_central_session._liblc3_cdll is None)"
        ).format(scripts=SCRIPTS_DIR)
        proc = subprocess.run(
            [sys.executable, "-c", code], capture_output=True, text=True
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stdout.strip(), "True")


class TestStreamSessionStart(unittest.TestCase):
    def test_mono_single_encoder(self):
        FakeEncoder.reset()
        session = make_session("mono")
        session.start()
        self.assertEqual(FakeEncoder.instances, 1)
        self.assertTrue(session._writer.started)

    def test_stereo_b_two_encoders(self):
        FakeEncoder.reset()
        session = make_session("stereo_b")
        session.start()
        self.assertEqual(FakeEncoder.instances, 2)

    def test_stereo_a_two_encoders_and_fl_fr_sort(self):
        FakeEncoder.reset()
        # Unsorted channel allocations: FR first, FL second.
        transports = make_transports([0x02, 0x01])
        session = make_session("stereo_a", transports=transports)
        session.start()
        self.assertEqual(FakeEncoder.instances, 2)
        self.assertEqual(session._transports_snapshot[0]["channel_alloc"], 0x01)
        self.assertEqual(session._transports_snapshot[1]["channel_alloc"], 0x02)
        self.assertEqual(session._transports_snapshot[0]["path"], TP2)

    def test_writer_snapshot_is_copied(self):
        transports = make_transports([0x01])
        session = make_session("mono", transports=transports)
        session.start()
        # Teardown empties endpoint.transports; the writer snapshot survives.
        transports.clear()
        self.assertEqual(len(session._transports_snapshot), 1)


class TestEncodeFrame(unittest.TestCase):
    def test_mono_payload(self):
        session = make_session("mono")
        session.start()
        frames = session.encode_frame()
        self.assertEqual(len(frames), 1)
        fd, data = frames[0]
        self.assertEqual(fd, 100)
        self.assertEqual(len(data), 120)

    def test_stereo_b_concat_240(self):
        session = make_session("stereo_b")
        session.start()
        frames = session.encode_frame()
        self.assertEqual(len(frames), 1)
        fd, data = frames[0]
        self.assertEqual(fd, 100)
        self.assertEqual(len(data), 240)

    def test_stereo_a_two_frames(self):
        session = make_session("stereo_a")
        session.start()
        frames = session.encode_frame()
        self.assertEqual(len(frames), 2)
        self.assertEqual([f[0] for f in frames], [100, 101])
        self.assertEqual([len(f[1]) for f in frames], [120, 120])

    def test_encoder_called_per_frame(self):
        session = make_session("mono")
        session.start()
        session.encode_frame()
        session.encode_frame()
        self.assertEqual(session._enc.calls, 2)


class TestRun(unittest.TestCase):
    def test_done_print_and_fps(self):
        session = make_session("mono", duration_s=0.01)
        session.start()
        session._writer.frames = 5
        _, out = capture(lambda: session.run())
        self.assertIn("[main] Done: 5 frames in 0.01 s (500.0 fps)", out)

    def test_keyboard_interrupt_nonfatal(self):
        session = make_session("mono", duration_s=0.01)
        session.start()
        with mock.patch.object(sess.time, "sleep", side_effect=KeyboardInterrupt):
            _, out = capture(lambda: session.run())
        self.assertIn("\n[main] Interrupted during streaming", out)
        # Flow continues: Done line still printed.
        self.assertIn("[main] Done:", out)

    def test_writer_error_reported(self):
        session = make_session(
            "mono",
            duration_s=0.01,
            writer_cls=writer_with(error=RuntimeError("encode boom")),
        )
        session.start()
        _, out = capture(lambda: session.run())
        self.assertIn("[error] SDU writer failed: encode boom", out)
        self.assertIn("[main] Done:", out)


class TestStopWriter(unittest.TestCase):
    def test_joined_clean_tail(self):
        session = make_session(
            "mono",
            duration_s=0.01,
            writer_cls=writer_with(tail_frames=3),
        )
        session.start()
        session.run()
        _, out = capture(lambda: session.stop_writer())
        self.assertIn(
            "[cleanup] Teardown tail: 3 frames written after the duration "
            "(release window)",
            out,
        )

    def test_joined_with_error(self):
        session = make_session(
            "mono",
            duration_s=0.01,
            writer_cls=writer_with(error=RuntimeError("late boom")),
        )
        session.start()
        session.run()
        _, out = capture(lambda: session.stop_writer())
        self.assertIn("[cleanup] SDU writer error: late boom", out)

    def test_alive_writer_forced_stop(self):
        session = make_session(
            "mono",
            duration_s=0.01,
            writer_cls=writer_with(join_results=(False, True)),
        )
        session.start()
        session.run()
        _, out = capture(lambda: session.stop_writer())
        self.assertIn("[cleanup] SDU writer still alive after 5 s — forcing stop", out)
        self.assertTrue(session._writer.stopped)
        self.assertEqual(session._writer.join_calls, 2)

    def test_idempotent(self):
        session = make_session("mono", duration_s=0.01)
        session.start()
        session.run()
        capture(lambda: session.stop_writer())
        join_calls = session._writer.join_calls
        capture(lambda: session.stop_writer())
        self.assertEqual(session._writer.join_calls, join_calls)

    def test_no_writer_noop(self):
        session = make_session("mono", duration_s=0.01)
        session.stop_writer()  # never started -> no writer, no crash


# ── Golden LC3 payload sizes with REAL liblc3 (skips without it) ─────────


class TestGoldenLiblc3(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.lib = sess.load_liblc3()
            cls.lib_error = None
        except Exception as exc:  # noqa: BLE001
            cls.lib = None
            cls.lib_error = str(exc)

    def _require(self):
        if self.lib is None:
            self.skipTest("liblc3 unavailable: {}".format(self.lib_error))

    def test_mono_120_bytes(self):
        self._require()
        enc = sess.LC3Encoder()
        pcm = sess.gen_sine(1000.0, sess.SR_HZ, sess.FRAME_SAMPLES)
        frame = enc.encode(pcm)
        self.assertEqual(len(frame), 120)

    def test_stereo_b_concat_240(self):
        self._require()
        enc_l = sess.LC3Encoder()
        enc_r = sess.LC3Encoder()
        pcm = sess.gen_sine(1000.0, sess.SR_HZ, sess.FRAME_SAMPLES)
        sdu = enc_l.encode(pcm) + enc_r.encode(pcm)
        self.assertEqual(len(sdu), 240)
        # Identical sine -> identical halves (L == R routing).
        self.assertEqual(sdu[:120], sdu[120:])

    def test_stereo_a_two_120_frames(self):
        self._require()
        enc_l = sess.LC3Encoder()
        enc_r = sess.LC3Encoder()
        pcm = sess.gen_sine(1000.0, sess.SR_HZ, sess.FRAME_SAMPLES)
        self.assertEqual(len(enc_l.encode(pcm)), 120)
        self.assertEqual(len(enc_r.encode(pcm)), 120)

    def test_sine_length(self):
        pcm = sess.gen_sine(1000.0, sess.SR_HZ, sess.FRAME_SAMPLES)
        self.assertEqual(len(pcm), 480)


# ── Teardown ordering through the CLI CentralCleanup owner ──────────────


class TestCleanupSequence(unittest.TestCase):
    def test_successful_teardown_exact_order_and_prints(self):
        import bap_central as bc

        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        order = []

        # Endpoint + transports with real fds.
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        EndpointCls = ep.make_endpoint_class(dbus, svc)
        endpoint = EndpointCls(bus, ep.ENDPOINT_PATH, stereo=False)
        r1, w1 = os.pipe()
        r2, w2 = os.pipe()
        os.close(r1)
        os.close(r2)
        endpoint.transports = [
            {"path": TP1, "fd": w1, "write_mtu": 120, "channel_alloc": 0x01},
            {"path": TP2, "fd": w2, "write_mtu": 120, "channel_alloc": 0x02},
        ]
        tr1 = bus.iface(TP1, "org.bluez.MediaTransport1")
        tr2 = bus.iface(TP2, "org.bluez.MediaTransport1")

        # Agent.
        mgr = bus.iface("/org/bluez", "org.bluez.AgentManager1")

        # Device disconnect + helper.
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        props = bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties")
        state = {"Connected": True}
        props.script("Get", lambda i, p: state.get(p, False))
        device.script("Disconnect", lambda *a, **k: state.update(Connected=False))
        conn = sec.RawHciConnect(PEER, 30, 0)
        conn.proc = mock.Mock()
        conn.proc.terminate.side_effect = None

        # Writer.
        session = make_session("mono")
        session.start()
        session.run()
        session._writer.tail_frames = 2

        owner = bc.CentralCleanup()
        owner.register("discovery", lambda: order.append("discovery"))
        owner.register(
            "transports",
            lambda: (
                order.append("transports"),
                ep.release_transports(bus, dbus, list(endpoint.transports), endpoint),
            ),
        )
        owner.register(
            "writer", lambda: (order.append("writer"), session.stop_writer())
        )
        owner.register(
            "endpoint",
            lambda: (order.append("endpoint"), ep.unregister_endpoint(media)),
        )
        owner.register(
            "agent", lambda: (order.append("agent"), sec.unregister_agent(mgr))
        )
        owner.register(
            "disconnect",
            lambda: (
                order.append("disconnect"),
                sec.disconnect_and_wait(
                    device, props, dbus, fakes.FakeGLib, deadline_s=0.5
                ),
            ),
        )
        owner.register(
            "helper", lambda: (order.append("helper"), conn.terminate(verbose=True))
        )

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
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
        text = out.getvalue()
        self.assertIn("[cleanup] Transport " + TP1 + " released", text)
        self.assertIn("[cleanup] Transport " + TP2 + " released", text)
        self.assertIn(
            "[cleanup] Teardown tail: 2 frames written after the duration "
            "(release window)",
            text,
        )
        self.assertIn("[cleanup] Endpoint unregistered", text)
        self.assertIn("[cleanup] Agent unregistered", text)
        self.assertIn("[cleanup] ACL link disconnected", text)
        self.assertIn("[cleanup] Raw-HCI helper terminated", text)
        # Fds closed exactly once by the transports stage.
        for fd in (w1, w2):
            with self.assertRaises(OSError):
                os.write(fd, b"x")
        self.assertEqual(endpoint.transports, [])
        # Transport D-Bus Release ran before UnregisterEndpoint.
        self.assertGreaterEqual(len(tr1.calls_for("Release")), 1)
        self.assertEqual(len(media.calls_for("UnregisterEndpoint")), 1)
        conn.proc.terminate.assert_called_once()

    def test_cleanup_idempotent_run_twice(self):
        import bap_central as bc

        ran = []
        owner = bc.CentralCleanup()
        owner.register("agent", lambda: ran.append("agent"))
        owner.register("helper", lambda: ran.append("helper"))
        owner.run()
        owner.run()
        self.assertEqual(ran, ["agent", "helper"])

    def test_tolerated_cleanup_errors(self):
        import bap_central as bc

        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        EndpointCls = ep.make_endpoint_class(dbus, svc)
        endpoint = EndpointCls(bus, ep.ENDPOINT_PATH, stereo=False)
        r, w = os.pipe()
        os.close(r)
        endpoint.transports = [
            {"path": TP1, "fd": w, "write_mtu": 120, "channel_alloc": 0x01},
        ]
        tr = bus.iface(TP1, "org.bluez.MediaTransport1")
        tr.script(
            "Release", lambda *a, **k: (_ for _ in ()).throw(RuntimeError("rel boom"))
        )
        media.script(
            "UnregisterEndpoint",
            lambda *a, **k: (_ for _ in ()).throw(RuntimeError("unreg boom")),
        )
        mgr = bus.iface("/org/bluez", "org.bluez.AgentManager1")
        mgr.script(
            "UnregisterAgent",
            lambda *a, **k: (_ for _ in ()).throw(RuntimeError("agent boom")),
        )
        device = bus.iface(DEV_PATH, "org.bluez.Device1")
        device.script(
            "Disconnect",
            lambda *a, **k: (_ for _ in ()).throw(RuntimeError("disc boom")),
        )
        conn = sec.RawHciConnect(PEER, 30, 0)
        conn.proc = mock.Mock()
        conn.proc.wait.side_effect = RuntimeError("wait boom")

        session = make_session("mono")
        session.start()
        session.run()

        owner = bc.CentralCleanup()
        owner.register(
            "transports",
            lambda: ep.release_transports(
                bus, dbus, list(endpoint.transports), endpoint
            ),
        )
        owner.register("writer", session.stop_writer)
        owner.register("endpoint", lambda: ep.unregister_endpoint(media))
        owner.register("agent", lambda: sec.unregister_agent(mgr))
        owner.register(
            "disconnect",
            lambda: sec.disconnect_and_wait(
                device,
                bus.iface(DEV_PATH, "org.freedesktop.DBus.Properties"),
                dbus,
                fakes.FakeGLib,
                deadline_s=0.1,
            ),
        )
        owner.register("helper", lambda: conn.terminate(verbose=True))

        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            owner.run()  # must not raise; every error is printed + tolerated
        text = out.getvalue()
        self.assertIn("[cleanup] Transport " + TP1 + " release error: rel boom", text)
        self.assertIn("[cleanup] Endpoint unregister error: unreg boom", text)
        self.assertIn("[cleanup] Agent unregister error: agent boom", text)
        self.assertIn("[cleanup] Disconnect error: disc boom", text)
        # The raw-HCI helper termination failure now surfaces on stderr
        # (guaranteed-reaped guarantee); it is never claimed as cleaned up.
        self.assertNotIn("[cleanup] Raw-HCI helper terminated", text)
        self.assertIn(
            "[error] Raw-HCI helper termination failed: wait boom", err.getvalue()
        )
        with self.assertRaises(OSError):
            os.write(w, b"x")  # fd still closed despite Release error

    def test_helper_terminated_on_pre_stream_failure(self):
        """CentralError from acquire (before any transport) must still
        release endpoint/agent and terminate the helper via finally."""
        import bap_central as bc

        bus = fakes.FakeBus()
        dbus = fakes.FakeDbusModule()
        svc = fakes.FakeServiceModule()
        media = bus.iface(HCI_PATH, "org.bluez.Media1")
        EndpointCls = ep.make_endpoint_class(dbus, svc)
        endpoint = EndpointCls(bus, ep.ENDPOINT_PATH, stereo=False)
        mgr = bus.iface("/org/bluez", "org.bluez.AgentManager1")
        conn = sec.RawHciConnect(PEER, 30, 0)
        conn.proc = mock.Mock()

        owner = bc.CentralCleanup()
        owner.register("endpoint", lambda: ep.unregister_endpoint(media))
        owner.register("agent", lambda: sec.unregister_agent(mgr))
        owner.register("helper", lambda: conn.terminate(verbose=True))

        try:
            # Pre-stream failure: endpoint config never arrives.
            try:
                ep.acquire_transports(
                    bus,
                    dbus,
                    fakes.FakeGLib,
                    endpoint,
                    config_timeout_s=0.05,
                    grace_s=0.02,
                    acquire_timeout_s=0.5,
                )
                self.fail("expected CentralError")
            except ep.CentralError:
                pass
        finally:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                owner.run()
        text = out.getvalue()
        self.assertIn("[cleanup] Endpoint unregistered", text)
        self.assertIn("[cleanup] Agent unregistered", text)
        self.assertIn("[cleanup] Raw-HCI helper terminated", text)
        conn.proc.terminate.assert_called_once()
        self.assertEqual(len(media.calls_for("UnregisterEndpoint")), 1)


# ── CLI golden tests ─────────────────────────────────────────────────────


class TestCliGolden(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import bap_central as bc

        cls.bc = bc
        cls.parser = bc.build_parser()

    def test_defaults(self):
        args = self.parser.parse_args([])
        self.assertFalse(args.stereo)
        self.assertEqual(args.duration, 30)
        self.assertEqual(args.freq, 1000.0)
        self.assertEqual(args.adapter, "hci0")
        self.assertIsNone(args.peer_addr)
        self.assertFalse(args.preserve_bond)

    def test_all_flags_parse(self):
        args = self.parser.parse_args(
            [
                "--stereo",
                "--duration",
                "15",
                "--freq",
                "440",
                "--adapter",
                "hci1",
                "--peer-addr",
                PEER,
                "--preserve-bond",
            ]
        )
        self.assertTrue(args.stereo)
        self.assertEqual(args.duration, 15)
        self.assertEqual(args.freq, 440.0)
        self.assertEqual(args.adapter, "hci1")
        self.assertEqual(args.peer_addr, PEER)
        self.assertTrue(args.preserve_bond)

    def test_help_mentions_all_flags(self):
        help_text = self.parser.format_help()
        for flag in (
            "--stereo",
            "--duration",
            "--freq",
            "--adapter",
            "--peer-addr",
            "--preserve-bond",
        ):
            self.assertIn(flag, help_text)
        self.assertIn("BAP central test driver for LE Audio Receiver.", help_text)

    def test_path_constants(self):
        self.assertEqual(ep.ENDPOINT_PATH, "/bap_central/endpoint0")
        self.assertEqual(sec.AGENT_PATH, "/bap_central/agent")
        self.assertTrue(sec.RAW_CONNECT_HELPER.endswith("hci_raw_connect.py"))
        self.assertTrue(os.path.isabs(sec.RAW_CONNECT_HELPER))

    def test_flpr_hang_gate_argv_compatibility(self):
        from flpr_hang_gate import launch_bap_central

        with mock.patch("flpr_hang_gate.subprocess.Popen") as popen:
            popen.return_value = mock.Mock()
            launch_bap_central(30, True, PEER)
            argv = popen.call_args[0][0]
        self.assertEqual(argv[0], "python3")
        self.assertTrue(argv[1].endswith("scripts/bap_central.py"))
        # The exact argv the hang gate builds must parse on the new CLI.
        args = self.parser.parse_args(argv[2:])
        self.assertEqual(args.duration, 30)
        self.assertTrue(args.stereo)
        self.assertEqual(args.peer_addr, PEER)

    def test_help_works_without_dbus_or_liblc3(self):
        """--help must succeed in a subprocess with only stdlib: importing
        bap_central must not import dbus or load liblc3."""
        code = (
            "import sys; sys.path.insert(0, {scripts!r});"
            "import bap_central;"
            "import bap_central_session;"
            "print('dbus' in sys.modules);"
            "print(bap_central_session._liblc3_cdll is None);"
            "print(bap_central.build_parser().format_help())"
        ).format(scripts=SCRIPTS_DIR)
        proc = subprocess.run(
            [sys.executable, "-c", code],
            capture_output=True,
            text=True,
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        lines = proc.stdout.splitlines()
        self.assertEqual(lines[0], "False")  # dbus never imported
        self.assertEqual(lines[1], "True")  # liblc3 never loaded
        self.assertIn("--preserve-bond", proc.stdout)


if __name__ == "__main__":
    unittest.main()
