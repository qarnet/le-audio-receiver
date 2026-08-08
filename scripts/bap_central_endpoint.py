#!/usr/bin/env python3
"""BAP source endpoint for bap_central: constants, MediaEndpoint
class factory, registration, deferred async Acquire, fd ownership, and
stream-mode inference.

Stdlib import only; D-Bus is injected late via the class factory.  Every
print and every LC3 config/QoS byte blob is byte-compatible with the
bap_central.py flow.  Fatal paths print their exact message
(primary error to stderr where the established code used stderr) then
raise CentralError.

FD ownership: every acquired fd is owned by exactly one stage.  On
all-or-nothing/acquire-timeout failure acquire_transports closes every
already-taken fd itself before raising; on success the fds transfer to
the CLI cleanup owner's transports stage, which closes each fd exactly
once and clears the record immediately (no double close / reuse).
Release/ClearConfiguration never recursively unregister.
"""

import os
import sys
import time


class CentralError(Exception):
    """Fatal central-driver error (message already printed by the raiser)."""


ENDPOINT_PATH = "/bap_central/endpoint0"

PAC_SOURCE_UUID = "00002bcb-0000-1000-8000-00805f9b34fb"
LC3_CODEC = 0x06

# LC3 LTV blobs (raw byte arrays)
LC3_CAPS = bytes(
    [
        0x03,
        0x01,
        0x80,
        0x00,  # freq: 48k
        0x02,
        0x02,
        0x03,  # duration: 7.5+10ms
        0x02,
        0x03,
        0x01,  # chan count: 1
        0x05,
        0x04,
        0x78,
        0x00,
        0xF0,
        0x00,  # frame len 120..240
    ]
)

# Same as LC3_CAPS but with chan_count=2 for --stereo Mode B source.
LC3_CAPS_STEREO = bytes(
    [
        0x03,
        0x01,
        0x80,
        0x00,  # freq: 48k
        0x02,
        0x02,
        0x03,  # duration: 7.5+10ms
        0x02,
        0x03,
        0x02,  # chan count: 2
        0x05,
        0x04,
        0x78,
        0x00,
        0xF0,
        0x00,  # frame len 120..240
    ]
)

# Config returned from SelectProperties (mono)
LC3_CONFIG_MONO = bytes(
    [
        0x02,
        0x01,
        0x08,  # freq: 48k
        0x02,
        0x02,
        0x01,  # duration: 10ms
        0x03,
        0x04,
        0x78,
        0x00,  # frame len: 120
    ]
)

# Config returned from SelectProperties (stereo Mode B: FL|FR)
LC3_CONFIG_STEREO = bytes(
    [
        0x02,
        0x01,
        0x08,  # freq: 48k
        0x02,
        0x02,
        0x01,  # duration: 10ms
        0x03,
        0x04,
        0x78,
        0x00,  # frame len: 120
        0x05,
        0x03,
        0x03,
        0x00,
        0x00,
        0x00,  # chan alloc: FL|FR
    ]
)

FRAME_BYTES = 120  # 96 kbps @ 48k/10ms mono


# ── Helpers (used inside the D-Bus class) ───────────────────────────────


def _to_plain(d, dbus_mod):
    """Convert a D-Bus-typed dict to plain Python for readable logging."""
    out = {}
    for k, v in d.items():
        k = str(k)
        if isinstance(v, dbus_mod.Array):
            out[k] = bytes(bytearray(v)).hex()
        elif isinstance(v, dbus_mod.Dictionary):
            out[k] = _to_plain(dict(v), dbus_mod)
        elif isinstance(v, dbus_mod.Byte):
            out[k] = int(v)
        else:
            out[k] = v
    return out


def _dict_items(dbus_dict):
    """Iterate (key, value) over a D-Bus Dictionary."""
    if hasattr(dbus_dict, "items"):
        return dbus_dict.items()
    result = []
    for key in dbus_dict:
        result.append((key, dbus_dict[key]))
    return result


# ── D-Bus class factory (requires dbus/dbus_service in scope) ───────────


def make_endpoint_class(dbus_mod, dbus_service_mod):
    """Return a BAPSourceEndpoint class bound to the given dbus modules."""

    class BAPSourceEndpoint(dbus_service_mod.Object):
        """org.bluez.MediaEndpoint1 implementation for BAP unicast source."""

        def __init__(self, bus, path, stereo=False):
            super().__init__(bus, path)
            self.bus = bus
            self.path = path
            self.stereo = stereo
            # Multi-transport support: BlueZ may call SetConfiguration once
            # per ASE (Mode A: two mono ASEs, one per channel) or once for
            # a single stereo ASE (Mode B). Track all transports we acquire.
            # Each entry: {"path": str, "fd": int, "write_mtu": int,
            #              "channel_alloc": int (0x01=FL, 0x02=FR, 0x03=FL|FR)}
            self.transports = []
            # Pending transports queued by SetConfiguration — Acquire is
            # deferred to the main loop to avoid reentrant D-Bus deadlock
            # with BlueZ BAP stream creation (the CIS is not created until
            # the SetConfiguration D-Bus reply is received).
            self._pending_transports = []
            self.config_done = False  # True once at least one transport is set

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="", out_signature=""
        )
        def Release(self):
            """Called when the endpoint is unregistered.  Closes every fd
            and clears all records immediately (single owner per fd);
            never recursively unregisters."""
            print("[endpoint] Release")
            for t in self.transports + self._pending_transports:
                if "fd" in t:
                    try:
                        os.close(t["fd"])
                    except OSError:
                        pass
            self.transports = []
            self._pending_transports = []
            self.config_done = False

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="o", out_signature=""
        )
        def ClearConfiguration(self, transport):
            """Called when the transport configuration is cleared."""
            print("[endpoint] ClearConfiguration({})".format(transport))
            tp = str(transport)
            for t in self.transports + self._pending_transports:
                if t["path"] == tp:
                    if "fd" in t:
                        try:
                            os.close(t["fd"])
                        except OSError:
                            pass
                    if t in self.transports:
                        self.transports.remove(t)
                    if t in self._pending_transports:
                        self._pending_transports.remove(t)
                    break

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="a{sv}", out_signature="a{sv}"
        )
        def SelectProperties(self, props):
            """BAP unicast: BlueZ negotiates codec configuration.

            Returns dict with Capabilities, QoS, Metadata.
            """
            p = dict(props)
            caps_raw = p.get("Capabilities")
            caps_log = bytes(bytearray(caps_raw)) if caps_raw is not None else b""
            channels = p.get("ChannelAllocation", 0)
            if hasattr(channels, "__int__"):
                channels = int(channels)

            print(
                "[endpoint] SelectProperties: caps={}".format(
                    caps_log.hex() if caps_log else "N/A"
                )
            )
            print("[endpoint]  ChannelAllocation={:#06x}".format(channels))
            print("[endpoint]  QoS={}".format(p.get("QoS", {})))
            print("[endpoint]  Locations={}".format(p.get("Locations", 0)))

            # Build LC3 config that matches the requested ChannelAllocation.
            # BlueZ may call SelectProperties once per mono ASE (FL=0x01, FR=0x02)
            # for Mode A, or once with FL|FR=0x03 for stereo Mode B.
            # Always include a Channel Allocation LTV so the peripheral knows
            # which channel this ASE carries.
            if channels == 0x03:
                # Stereo Mode B (single ASE, both channels)
                caps = LC3_CONFIG_STEREO
                sdu = 240
            else:
                # Mono ASE with the requested channel allocation (FL or FR).
                # Build a config with the specific channel alloc LTV.
                sdu = 120
                caps = bytes(
                    [
                        0x02,
                        0x01,
                        0x08,  # freq: 48k
                        0x02,
                        0x02,
                        0x01,  # duration: 10ms
                        0x03,
                        0x04,
                        0x78,
                        0x00,  # frame len: 120
                        0x05,
                        0x03,  # LTV len=5, type=0x03 (chan alloc)
                        channels & 0xFF,
                        0x00,
                        0x00,
                        0x00,  # 4-byte LE
                    ]
                )

            ret = dbus_mod.Dictionary(
                {
                    "Capabilities": dbus_mod.Array(
                        [dbus_mod.Byte(b) for b in caps],
                        signature="y",
                    ),
                    "QoS": dbus_mod.Dictionary(
                        {
                            # Framing: 0 = unframed (peripheral supports unframed)
                            "Framing": dbus_mod.Byte(0),
                            # PHY: 0x02 = 2M
                            "PHY": dbus_mod.Byte(0x02),
                            # Interval: 10000 us = 10 ms
                            "Interval": dbus_mod.UInt32(10000),
                            # SDU: 120 (mono) or 240 (stereo Mode B) bytes
                            "SDU": dbus_mod.UInt16(sdu),
                            # Retransmissions: 2 (matches peripheral's pref)
                            "Retransmissions": dbus_mod.Byte(2),
                            # Latency: 10 ms (peripheral prefers 10; must be > 0)
                            "Latency": dbus_mod.UInt16(10),
                            # PresentationDelay: 40000 us (within peripheral's
                            # pd_min=10000, pd_max=80000, pref=40000)
                            "PresentationDelay": dbus_mod.UInt32(40000),
                            # TargetLatency: 1=low, 2=balanced, 3=high.
                            # 0 is INVALID per BAP spec — peripheral rejects it
                            # with "Invalid latency: 0x00" in ASCS codec config.
                            "TargetLatency": dbus_mod.Byte(0x02),
                        },
                        signature="sv",
                    ),
                },
                signature="sv",
            )

            try:
                plain_ret = {}
                for k, v in ret.items():
                    if isinstance(v, dbus_mod.Array):
                        plain_ret[str(k)] = bytes(bytearray(v)).hex()
                    elif isinstance(v, dbus_mod.Dictionary):
                        inner = {}
                        for ik, iv in v.items():
                            inner[str(ik)] = (
                                bytes(bytearray(iv)).hex()
                                if isinstance(iv, dbus_mod.Array)
                                else int(iv)
                                if isinstance(iv, dbus_mod.Byte)
                                else int(iv)
                                if isinstance(iv, dbus_mod.UInt16)
                                else int(iv)
                                if isinstance(iv, dbus_mod.UInt32)
                                else iv
                            )
                        plain_ret[str(k)] = inner
                    else:
                        plain_ret[str(k)] = v
                print("[endpoint] Returning config: {}".format(plain_ret))
            except Exception as e:  # noqa: BLE001
                print("[endpoint] Returning config: <log failed: {}>".format(e))
            return ret

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="oa{sv}", out_signature=""
        )
        def SetConfiguration(self, transport, props):
            """Called when transport is created. Queue for async Acquire.

            May be called multiple times — once per ASE (Mode A: 2 mono ASEs)
            or once for a single stereo ASE (Mode B).

            **Important**: BlueZ creates the CIS only after this D-Bus method
            returns.  Calling MediaTransport1.Acquire() inside this callback
            blocks BlueZ's BAP stream creation → CIS is never established →
            Acquire returns I/O error.  Instead, queue the transport and defer
            Acquire to the main loop.
            """
            print("[endpoint] SetConfiguration enter", flush=True)
            p = dict(props)
            tp = str(transport)
            print("[endpoint] SetConfiguration({})".format(tp), flush=True)
            print("[endpoint]  props={}".format(_to_plain(p, dbus_mod)), flush=True)

            # Extract channel allocation from the config caps if present.
            channel_alloc = 0x03  # default FL|FR
            try:
                caps = p.get("Configuration", [])
                if caps:
                    b = bytes(bytearray(caps))
                    i = 0
                    while i + 1 < len(b):
                        ltv_len = b[i]
                        ltv_type = b[i + 1]
                        if ltv_type == 0x03 and ltv_len == 5 and i + 6 <= len(b):
                            channel_alloc = (
                                b[i + 2]
                                | (b[i + 3] << 8)
                                | (b[i + 4] << 16)
                                | (b[i + 5] << 24)
                            )
                            break
                        i += ltv_len if ltv_len > 0 else 1
                print(
                    "[endpoint]  parsed channel_alloc={:#04x}".format(channel_alloc),
                    flush=True,
                )
            except Exception as e:  # noqa: BLE001
                print("[endpoint]  LTV parse error: {}".format(e), flush=True)

            # Queue the transport for async Acquire in the main loop.
            # Do NOT call Acquire here — that blocks BlueZ from creating
            # the CIS and produces "Input/output error".
            self._pending_transports.append(
                {
                    "path": tp,
                    "channel_alloc": channel_alloc,
                }
            )
            self.config_done = True
            print(
                "[endpoint] SetConfiguration: queued transport for async acquire "
                "(pending={})".format(len(self._pending_transports)),
                flush=True,
            )

    return BAPSourceEndpoint


# ── Registration ────────────────────────────────────────────────────────


def register_endpoint(
    media_iface, bus, dbus_mod, endpoint_cls, path=ENDPOINT_PATH, stereo=False
):
    """RegisterEndpoint with the exact props (UUID/Codec/Caps);
    prints the registration line.  Returns the endpoint instance."""
    endpoint = endpoint_cls(bus, path, stereo=stereo)
    endpoint_caps = LC3_CAPS_STEREO if stereo else LC3_CAPS
    props = dbus_mod.Dictionary(
        {
            "UUID": dbus_mod.String(PAC_SOURCE_UUID),
            "Codec": dbus_mod.Byte(LC3_CODEC),
            "Capabilities": dbus_mod.Array(
                [dbus_mod.Byte(b) for b in endpoint_caps], signature="y"
            ),
        },
        signature="sv",
    )
    media_iface.RegisterEndpoint(path, props)
    print("[main] BAP source endpoint registered at {}".format(path))
    return endpoint


def unregister_endpoint(media_iface, path=ENDPOINT_PATH):
    """Idempotent endpoint unregistration (cleanup tail print preserved)."""
    try:
        media_iface.UnregisterEndpoint(path)
        print("[cleanup] Endpoint unregistered")
    except Exception as e:  # noqa: BLE001
        print("[cleanup] Endpoint unregister error: {}".format(e))


# ── Acquire orchestration (deferred, all-or-nothing) ────────────────────


def acquire_transports(
    bus,
    dbus_mod,
    GLib,
    endpoint,
    config_timeout_s=30.0,
    grace_s=2.0,
    acquire_timeout_s=35.0,
):
    """Wait for SetConfiguration, apply the second-ASE grace, run the
    deferred async Acquire, enforce all-or-nothing, infer stream mode.

    On success populates endpoint.transports, prints the exact
    totals/mode lines, and returns (transports, stream_mode, sdu_size).
    Every failure path prints the exact error lines (stderr
    where the established code used stderr), closes every already-taken fd,
    and raises CentralError.
    """
    # ── 7. Wait for SetConfiguration callback(s) ─────────────────────
    print("[main] Waiting for SetConfiguration (auto-config by BlueZ)...")
    deadline = time.monotonic() + config_timeout_s
    while not endpoint.config_done and time.monotonic() < deadline:
        GLib.MainContext.default().iteration(False)
        time.sleep(0.05)

    if not endpoint.config_done:
        print("[error] SetConfiguration not received within 30 s")
        print("[hint] Check: does the LE Audio Receiver register PACS/ASCS?")
        print("[hint] Try manual fallback: bluetoothctl menu endpoint")
        raise CentralError("SetConfiguration timeout") from None

    if not endpoint._pending_transports:
        print("[error] SetConfiguration received but no pending transports")
        raise CentralError("no pending transports") from None

    # Give BlueZ a moment in case more SetConfiguration calls are coming
    # (Mode A: two mono ASEs — BlueZ may call SetConfiguration for the second
    # ASE shortly after the first).
    print("[main] Pending transports: {}".format(len(endpoint._pending_transports)))
    if len(endpoint._pending_transports) == 1:
        print("[main] Got 1 transport, waiting 2s for a possible second ASE...")
        deadline2 = time.monotonic() + grace_s
        while time.monotonic() < deadline2:
            GLib.MainContext.default().iteration(False)
            time.sleep(0.05)

    # ── 7b. Acquire transports ASYNCHRONOUSLY (outside SetConfiguration) ──
    n_pending = len(endpoint._pending_transports)
    print("[main] Acquiring {} transport(s) asynchronously...".format(n_pending))

    # Shared state for async Acquire results.
    acquired = []  # successful acquires
    acquire_errors = []  # (path, error) for failures
    acquire_done = [False]

    def _on_acquire_ok(fd_ufd, read_mtu, write_mtu, tp, channel_alloc):
        """Callback: Acquire reply received.

        dbus-python passes each D-Bus OUT arg as a positional argument,
        so MediaTransport1.Acquire() returns (UnixFd, uint16, uint16)
        → callback receives 3 args: (fd_ufd, read_mtu, write_mtu).
        """
        if hasattr(fd_ufd, "take"):
            iso_fd = fd_ufd.take()
        else:
            iso_fd = int(fd_ufd)
        rec = {
            "path": tp,
            "fd": iso_fd,
            "write_mtu": int(write_mtu),
            "channel_alloc": channel_alloc,
        }
        acquired.append(rec)
        print(
            "[main] Acquired: path={}, fd={}, write_mtu={}, ch_alloc={:#04x}"
            " ({}/{})".format(
                tp,
                iso_fd,
                write_mtu,
                channel_alloc,
                len(acquired),
                n_pending,
            ),
            flush=True,
        )
        if len(acquired) + len(acquire_errors) >= n_pending:
            acquire_done[0] = True

    def _on_acquire_err(error, tp):
        """Callback: Acquire error."""
        err_name = getattr(error, "get_dbus_name", lambda: str(error))()
        err_msg = str(error)
        print(
            "[error] Acquire({}) failed: {} - {}".format(tp, err_name, err_msg),
            file=sys.stderr,
            flush=True,
        )
        acquire_errors.append((tp, error))
        if len(acquired) + len(acquire_errors) >= n_pending:
            acquire_done[0] = True

    for pt in endpoint._pending_transports:
        tp = pt["path"]
        transport_obj = bus.get_object("org.bluez", tp)
        transport_iface = dbus_mod.Interface(transport_obj, "org.bluez.MediaTransport1")
        print("[main]   async Acquire({}) ...".format(tp), flush=True)
        transport_iface.Acquire(
            reply_handler=lambda fd, rm, wm, tp=tp, ca=pt["channel_alloc"]: (
                _on_acquire_ok(fd, rm, wm, tp, ca)
            ),
            error_handler=lambda e, tp=tp: _on_acquire_err(e, tp),
            timeout=30000,
        )

    # Service GLib while Acquire replies arrive.
    acquire_deadline = time.monotonic() + acquire_timeout_s  # 30 s + margin
    while not acquire_done[0] and time.monotonic() < acquire_deadline:
        GLib.MainContext.default().iteration(False)
        time.sleep(0.05)

    if not acquire_done[0]:
        print(
            "[error] Acquire timed out (got {}/{} replies)".format(
                len(acquired), n_pending
            ),
            file=sys.stderr,
        )
        # Close any already-acquired fds before exit.
        for rec in acquired:
            try:
                os.close(rec["fd"])
            except OSError:
                pass
        for e in acquire_errors:
            print("[error]   {}: {}".format(e[0], e[1]), file=sys.stderr)
        raise CentralError("Acquire timed out") from None

    if acquire_errors or len(acquired) != n_pending:
        # All-or-nothing: if any required Acquire failed, do not stream a
        # partial Mode A setup. Close every acquired fd and exit.
        print(
            "[error] All-or-nothing: {}/{} Acquire(s) failed, closing"
            " all acquired fds".format(n_pending - len(acquired), n_pending),
            file=sys.stderr,
        )
        for tp, e in acquire_errors:
            print("[error]   {}: {}".format(tp, e), file=sys.stderr)
        for rec in acquired:
            try:
                os.close(rec["fd"])
            except OSError:
                pass
            try:
                transport_obj = bus.get_object("org.bluez", rec["path"])
                transport_iface = dbus_mod.Interface(
                    transport_obj, "org.bluez.MediaTransport1"
                )
                transport_iface.Release()
            except Exception as rel_e:  # noqa: BLE001
                print(
                    "[error] Release({}) failed: {}".format(rec["path"], rel_e),
                    file=sys.stderr,
                )
        raise CentralError("all-or-nothing acquire failed") from None

    if not acquired:
        print("[error] No transports acquired", file=sys.stderr)
        raise CentralError("no transports acquired") from None

    # Populate endpoint.transports from acquired results.
    endpoint.transports = acquired

    n_transports = len(endpoint.transports)
    print("[main] Total transports: {}".format(n_transports))
    for t in endpoint.transports:
        print(
            "[main]   fd={} ch_alloc={:#04x} write_mtu={}".format(
                t["fd"], t["channel_alloc"], t["write_mtu"]
            )
        )

    # Determine streaming mode based on number of transports
    # 1 transport with ch_alloc=0x03 = Mode B stereo (240-byte SDU)
    # 1 transport with ch_alloc=0x01 or 0x02 = Mono (120-byte SDU)
    # 2 transports = Mode A (two mono ASEs, 120 bytes each)
    if n_transports == 1 and endpoint.transports[0]["channel_alloc"] == 0x03:
        stream_mode = "stereo_b"
        sdu_size = FRAME_BYTES * 2
    elif n_transports == 2:
        stream_mode = "stereo_a"
        sdu_size = FRAME_BYTES  # per transport
    else:
        stream_mode = "mono"
        sdu_size = FRAME_BYTES
    print("[main] Stream mode: {}, SDU size: {} bytes".format(stream_mode, sdu_size))

    return (endpoint.transports, stream_mode, sdu_size)


def release_transports(bus, dbus_mod, transports, endpoint):
    """Release each MediaTransport, then close its fd; clear the endpoint
    record.  Idempotent; every fd owned exactly once (records marked
    closed immediately).  Preserves the exact [cleanup] prints."""
    for t in transports:
        try:
            transport_obj = bus.get_object("org.bluez", t["path"])
            transport_iface = dbus_mod.Interface(
                transport_obj, "org.bluez.MediaTransport1"
            )
            transport_iface.Release()
            print("[cleanup] Transport {} released".format(t["path"]))
        except Exception as e:  # noqa: BLE001
            print("[cleanup] Transport {} release error: {}".format(t["path"], e))
        try:
            os.close(t["fd"])
        except OSError:
            pass
    endpoint.transports = []
