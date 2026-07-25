#!/usr/bin/env python3
"""
BAP central test driver for LE Audio Receiver.

Usage: sudo python3 scripts/bap_central.py [--stereo] [--duration 30] [--freq 1000]

Registers a BAP source endpoint on hci0, pairs + connects to the LE Audio
Receiver peripheral, acquires the MediaTransport, and streams a 1 kHz sine
tone as LC3 (48 kHz / 10 ms / 96 kbps).

--stereo: use stereo Mode B (single ASE, 240-byte SDU)
--duration N: stream for N seconds (default 30)
--freq FREQ: sine frequency (default 1000)
"""

import argparse
import ctypes
import ctypes.util
import math
import os
import struct
import sys
import time

# Force unbuffered stdout so errors in D-Bus callbacks are visible.
try:
    sys.stdout.reconfigure(line_buffering=True)
except Exception:
    pass

# ── Constants ───────────────────────────────────────────────────────────────

ENDPOINT_PATH = "/bap_central/endpoint0"
AGENT_PATH = "/bap_central/agent"

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
        0x01,  # chan count: 1 (mono)
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

DT_US = 10000  # 10 ms
SR_HZ = 48000
FRAME_BYTES = 120  # 96 kbps @ 48k/10ms mono
FRAME_SAMPLES = 480  # 48k * 10ms
LC3_PCM_FORMAT_S16 = 0


# ── liblc3 via ctypes ──────────────────────────────────────────────────────


def _load_liblc3():
    """Load liblc3.so with fallback paths.

    Returns a ctypes.CDLL handle to liblc3.
    """
    # Paths to try, in order.
    NIX_LIBLC3_PATH = (
        "/nix/store/9a1d5s981idlhdhw8dg03bp6arhvrvwn-liblc3-1.1.3/lib/liblc3.so.1"
    )

    # 1. Try system library search first.
    soname = ctypes.util.find_library("lc3")
    if soname:
        try:
            lib = ctypes.CDLL(soname)
            print("[main] liblc3 loaded via find_library:", soname)
            return lib
        except OSError:
            pass

    # 2. Try the known nix-store path.
    if os.path.exists(NIX_LIBLC3_PATH):
        try:
            lib = ctypes.CDLL(NIX_LIBLC3_PATH)
            print("[main] liblc3 loaded via nix-store:", NIX_LIBLC3_PATH)
            return lib
        except OSError:
            pass

    # 3. Scan /nix/store for any liblc3.so.1.
    import glob as _g

    candidates = _g.glob("/nix/store/*-liblc3-*/lib/liblc3.so.1") + _g.glob(
        "/nix/store/*-liblc3*/lib/liblc3.so.1"
    )
    for path in sorted(candidates):
        try:
            lib = ctypes.CDLL(path)
            print("[main] liblc3 loaded via glob scan:", path)
            return lib
        except OSError:
            continue

    raise RuntimeError(
        "liblc3.so.1 not found. Ensure it is installed or update the nix-store path."
    )


# Load liblc3 at import time (stdlib-only, safe).
_liblc3_cdll = _load_liblc3()


class LC3Encoder:
    """LC3 audio encoder (ctypes wrapper around liblc3)."""

    def __init__(self, dt_us=DT_US, sr_hz=SR_HZ, frame_bytes=FRAME_BYTES):
        self.dt_us = dt_us
        self.sr_hz = sr_hz
        self.frame_bytes = frame_bytes
        lib = _liblc3_cdll

        lib.lc3_encoder_size.restype = ctypes.c_uint
        lib.lc3_encoder_size.argtypes = [ctypes.c_int, ctypes.c_int]

        lib.lc3_setup_encoder.restype = ctypes.c_void_p
        lib.lc3_setup_encoder.argtypes = [
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
        ]

        lib.lc3_encode.restype = ctypes.c_int
        lib.lc3_encode.argtypes = [
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
        ]

        size = lib.lc3_encoder_size(dt_us, sr_hz)
        self._mem = (ctypes.c_uint8 * size)()
        self._enc = lib.lc3_setup_encoder(dt_us, sr_hz, 0, self._mem)
        if not self._enc:
            raise RuntimeError("lc3_setup_encoder failed")

    def encode(self, pcm_samples):
        """Encode int16 PCM samples to LC3 frame.

        Args:
            pcm_samples: iterable of int16 samples, FRAME_SAMPLES long.

        Returns:
            bytes of length frame_bytes.
        """
        pcm = (ctypes.c_int16 * len(pcm_samples))(*pcm_samples)
        out = (ctypes.c_uint8 * self.frame_bytes)()
        rc = _liblc3_cdll.lc3_encode(self._enc, 0, pcm, 1, self.frame_bytes, out)
        if rc != 0:
            raise RuntimeError(f"lc3_encode failed: {rc}")
        return bytes(out)


# ── Helper functions ────────────────────────────────────────────────────────


def _gen_sine(freq, sr_hz, nsamples, amplitude=10000):
    """Generate *nsamples* of int16 sine wave at *freq* Hz."""
    samples = [
        int(amplitude * math.sin(2 * math.pi * freq * i / sr_hz))
        for i in range(nsamples)
    ]
    return (ctypes.c_int16 * nsamples)(*samples)


# ── D-Bus late import ──────────────────────────────────────────────────────


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


# ── Helpers (used inside D-Bus classes) ────────────────────────────────────


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


# ── D-Bus class factories (require dbus/dbus_service in scope) ─────────────


def _make_endpoint_class(dbus_mod, dbus_service_mod, GLib_mod):
    """Return a BAPSourceEndpoint class bound to the given dbus module."""

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
            self.config_done = False  # True once at least one transport is set

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="", out_signature=""
        )
        def Release(self):
            """Called when the endpoint is unregistered."""
            print("[endpoint] Release")
            for t in self.transports:
                try:
                    os.close(t["fd"])
                except OSError:
                    pass
            self.transports = []
            self.config_done = False

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="o", out_signature=""
        )
        def ClearConfiguration(self, transport):
            """Called when the transport configuration is cleared."""
            print("[endpoint] ClearConfiguration({})".format(transport))
            tp = str(transport)
            for t in self.transports:
                if t["path"] == tp:
                    try:
                        os.close(t["fd"])
                    except OSError:
                        pass
                    self.transports.remove(t)
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
            else:
                # Mono ASE with the requested channel allocation (FL or FR).
                # Build a config with the specific channel alloc LTV.
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
                            # SDU: 120 bytes per frame (96 kbps LC3 @ 48k/10ms)
                            "SDU": dbus_mod.UInt16(120),
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
            except Exception as e:
                print("[endpoint] Returning config: <log failed: {}>".format(e))
            return ret

        @dbus_service_mod.method(
            "org.bluez.MediaEndpoint1", in_signature="oa{sv}", out_signature=""
        )
        def SetConfiguration(self, transport, props):
            """Called when transport is created. Acquire and start streaming.

            May be called multiple times — once per ASE (Mode A: 2 mono ASEs)
            or once for a single stereo ASE (Mode B).
            """
            print("[endpoint] SetConfiguration enter", flush=True)
            p = dict(props)
            tp = str(transport)
            print("[endpoint] SetConfiguration({})".format(tp), flush=True)
            print("[endpoint]  props={}".format(_to_plain(p, dbus_mod)), flush=True)

            # Extract channel allocation from the config caps if present.
            # Wrap in try/except so a parse error doesn't break the flow.
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
            except Exception as e:
                print("[endpoint]  LTV parse error: {}".format(e), flush=True)

            # Mark config_done BEFORE acquiring — Acquire may block, and the
            # main loop needs to know SetConfiguration was called so it can
            # proceed to the streaming phase (which itself waits for all
            # transports to be acquired).
            self.config_done = True
            print(
                "[endpoint] SetConfiguration: config_done=True, acquiring transport...",
                flush=True,
            )
            try:
                self._acquire_transport(tp, channel_alloc)
                print(
                    "[endpoint] SetConfiguration: _acquire OK, transports={}".format(
                        len(self.transports)
                    ),
                    flush=True,
                )
            except Exception as e:
                import traceback

                print(
                    "[endpoint] SetConfiguration: _acquire_transport FAILED: {}".format(
                        e
                    ),
                    file=sys.stderr,
                    flush=True,
                )
                traceback.print_exc(file=sys.stderr)
                sys.stderr.flush()

        def _acquire_transport(self, tp, channel_alloc):
            """Call MediaTransport1.Acquire() and store the ISO socket fd."""
            print(
                "[endpoint] _acquire_transport: calling Acquire() on {}".format(tp),
                flush=True,
            )
            transport_obj = self.bus.get_object("org.bluez", tp)
            transport_iface = dbus_mod.Interface(
                transport_obj, "org.bluez.MediaTransport1"
            )
            # Synchronous Acquire — blocks until ISO CIS is established.
            # May take a few seconds for the controller to set up the CIS.
            # 30s timeout (default is ~25s for dbus-python).
            result = transport_iface.Acquire(timeout=30000)
            print("[endpoint] Acquire returned: {}".format(result), flush=True)
            fd_ufd, read_mtu, write_mtu = result
            # fd may be dbus.types.UnixFd (call .take()) or already an int
            if hasattr(fd_ufd, "take"):
                iso_fd = fd_ufd.take()
            else:
                iso_fd = int(fd_ufd)
            self.transports.append(
                {
                    "path": tp,
                    "fd": iso_fd,
                    "write_mtu": int(write_mtu),
                    "channel_alloc": channel_alloc,
                }
            )
            print(
                "[endpoint] Acquired transport: path={}, fd={}, "
                "read_mtu={}, write_mtu={}, ch_alloc={:#04x}, "
                "total_transports={}".format(
                    tp,
                    iso_fd,
                    read_mtu,
                    write_mtu,
                    channel_alloc,
                    len(self.transports),
                ),
                flush=True,
            )

    return BAPSourceEndpoint


def _make_agent_class(dbus_mod, dbus_service_mod, GLib_mod):
    """Return a JustWorksAgent class bound to the given dbus module."""

    class JustWorksAgent(dbus_service_mod.Object):
        """org.bluez.Agent1 that accepts all pairings (Just Works)."""

        def __init__(self, bus, path):
            super().__init__(bus, path)
            self.bus = bus
            self.path = path

        @dbus_service_mod.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Release(self):
            print("[agent] Release")

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="o", out_signature="s"
        )
        def RequestPinCode(self, device):
            print("[agent] RequestPinCode({}) -> '0000'".format(device))
            return "0000"

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="o", out_signature="u"
        )
        def RequestPasskey(self, device):
            print("[agent] RequestPasskey({}) -> 0".format(device))
            return dbus_mod.UInt32(0)

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="ou", out_signature=""
        )
        def DisplayPasskey(self, device, passkey):
            print(
                "[agent] DisplayPasskey: device={}, passkey={}".format(device, passkey)
            )

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="ouq", out_signature=""
        )
        def DisplayPinCode(self, device, pincode, entered):
            print(
                "[agent] DisplayPinCode: device={}, pin={}, entered={}".format(
                    device, pincode, entered
                )
            )

        @dbus_service_mod.method("org.bluez.Agent1", in_signature="o", out_signature="")
        def RequestConfirmation(self, device):
            print(
                "[agent] RequestConfirmation (Just Works): accepting {}".format(device)
            )

        @dbus_service_mod.method("org.bluez.Agent1", in_signature="o", out_signature="")
        def RequestAuthorization(self, device):
            print("[agent] RequestAuthorization: accepting {}".format(device))

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="os", out_signature=""
        )
        def AuthorizeService(self, device, uuid):
            print("[agent] AuthorizeService: device={}, uuid={}".format(device, uuid))

        @dbus_service_mod.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Cancel(self):
            print("[agent] Cancel")

    return JustWorksAgent


# ── Main flow ───────────────────────────────────────────────────────────────


def main():
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
    args = parser.parse_args()
    hci_path = "/org/bluez/" + args.adapter

    # Late-import D-Bus bindings so --help works without dbus-python.
    _dbus, _dbus_service, _GLib = _import_dbus()

    # Create D-Bus classes (closure captures the real dbus module).
    BAPSourceEndpoint = _make_endpoint_class(_dbus, _dbus_service, _GLib)
    JustWorksAgent = _make_agent_class(_dbus, _dbus_service, _GLib)

    # ── Connect to system bus ────────────────────────────────────────────
    bus = _dbus.SystemBus()
    print("[main] Connected to D-Bus system bus")

    # ── 1. Register pairing agent ────────────────────────────────────────
    agent = JustWorksAgent(bus, AGENT_PATH)
    agent_mgr = _dbus.Interface(
        bus.get_object("org.bluez", "/org/bluez"), "org.bluez.AgentManager1"
    )
    agent_mgr.RegisterAgent(AGENT_PATH, "DisplayYesNo")
    agent_mgr.RequestDefaultAgent(AGENT_PATH)
    print("[main] Agent registered at {}".format(AGENT_PATH))

    # ── 2. Register BAP source endpoint ──────────────────────────────────
    media = _dbus.Interface(bus.get_object("org.bluez", hci_path), "org.bluez.Media1")
    endpoint = BAPSourceEndpoint(bus, ENDPOINT_PATH, stereo=args.stereo)
    props = _dbus.Dictionary(
        {
            "UUID": _dbus.String(PAC_SOURCE_UUID),
            "Codec": _dbus.Byte(LC3_CODEC),
            "Capabilities": _dbus.Array(
                [_dbus.Byte(b) for b in LC3_CAPS], signature="y"
            ),
        },
        signature="sv",
    )
    media.RegisterEndpoint(ENDPOINT_PATH, props)
    print("[main] BAP source endpoint registered at {}".format(ENDPOINT_PATH))

    # ── 3. Power on adapter ──────────────────────────────────────────────
    adapter_props = _dbus.Interface(
        bus.get_object("org.bluez", hci_path), "org.freedesktop.DBus.Properties"
    )
    adapter_props.Set("org.bluez.Adapter1", "Powered", _dbus.Boolean(True))
    print("[main] Adapter powered on")

    # ── 4. Locate target device (existing or via discovery) ──────────────
    adapter = _dbus.Interface(
        bus.get_object("org.bluez", hci_path), "org.bluez.Adapter1"
    )

    # 4a. First, enumerate existing devices (cached/paired/connected).
    dev_path = None
    already_connected = False
    om = _dbus.Interface(
        bus.get_object("org.bluez", "/"),
        "org.freedesktop.DBus.ObjectManager",
    )
    managed = om.GetManagedObjects()
    for path, ifaces in managed.items():
        if "org.bluez.Device1" not in ifaces:
            continue
        dev = ifaces["org.bluez.Device1"]
        name = str(dev.get("Name", ""))
        addr = str(dev.get("Address", ""))
        # Only look at devices on our adapter (hci0)
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
            dev_path = path
            already_connected = bool(dev.get("Connected", False))
            print(
                "[enum] >>> Using existing target: {} (connected={})".format(
                    path, already_connected
                )
            )
            break

    # 4b. If not in existing devices, start discovery.
    if dev_path is None:
        target_device_path = [None]
        device_found = [False]

        def on_interfaces_added(path, interfaces):
            if device_found[0]:
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
                target_device_path[0] = path
                device_found[0] = True
                print("[discovery] >>> Target found: {}".format(path))

        bus.add_signal_receiver(
            on_interfaces_added,
            dbus_interface="org.freedesktop.DBus.ObjectManager",
            signal_name="InterfacesAdded",
        )

        adapter.StartDiscovery()
        print("[main] Discovery started, waiting for 'LE Audio Receiver'...")
        print("[main]    (or press Ctrl-C to abort)")

        # Wait up to 30 s for discovery.
        deadline = time.monotonic() + 30
        try:
            while not device_found[0] and time.monotonic() < deadline:
                _GLib.MainContext.default().iteration(False)
                time.sleep(0.05)
        except KeyboardInterrupt:
            print("\n[main] Interrupted")
            adapter.StopDiscovery()
            sys.exit(1)

        if not device_found[0]:
            print("[error] LE Audio Receiver not found within 30 s")
            adapter.StopDiscovery()
            sys.exit(1)

        adapter.StopDiscovery()
        dev_path = target_device_path[0]
        already_connected = False

    print(
        "[main] Target device: {} (already_connected={})".format(
            dev_path, already_connected
        )
    )

    # ── 5. Pair + Connect (always fresh) ─────────────────────────────────
    device = _dbus.Interface(bus.get_object("org.bluez", dev_path), "org.bluez.Device1")

    # If BlueZ thinks the device is already connected, disconnect first so we
    # get a clean GATT service discovery cycle. BlueZ caches Device1 objects
    # across disconnects, and a stale "connected" flag skips GATT discovery.
    if already_connected:
        print("[main] Disconnecting stale cached connection, reconnecting fresh...")
        try:
            device.Disconnect()
            time.sleep(1.5)  # let receiver settle and restart advertising
        except _dbus.exceptions.DBusException as e:
            print("[main]   Disconnect ignored: {}".format(e))

    try:
        device.Pair(timeout=60000)
        print("[main] Paired")
    except _dbus.exceptions.DBusException as e:
        err_name = e.get_dbus_name()
        if err_name and "AlreadyExists" in err_name:
            print("[main] Already paired")
        elif err_name and "org.bluez.Error.AlreadyExists" in str(e):
            print("[main] Already paired (ignored)")
        else:
            print("[error] Pairing failed: {}".format(e))
            raise

    # ── 6. Trust + Connect ───────────────────────────────────────────────
    dev_props = _dbus.Interface(
        bus.get_object("org.bluez", dev_path), "org.freedesktop.DBus.Properties"
    )
    dev_props.Set("org.bluez.Device1", "Trusted", _dbus.Boolean(True))
    print("[main] Trusted, connecting...")
    device.Connect()
    print("[main] Connected")

    # ── 7. Wait for SetConfiguration callback(s) ─────────────────────────
    print("[main] Waiting for SetConfiguration (auto-config by BlueZ)...")
    deadline = time.monotonic() + 30
    while not endpoint.config_done and time.monotonic() < deadline:
        _GLib.MainContext.default().iteration(False)
        time.sleep(0.05)

    if not endpoint.config_done or not endpoint.transports:
        print("[error] SetConfiguration not received within 30 s")
        print("[hint] Check: does the LE Audio Receiver register PACS/ASCS?")
        print("[hint] Try manual fallback: bluetoothctl menu endpoint")
        sys.exit(1)

    # Give BlueZ a moment in case more SetConfiguration calls are coming
    # (Mode A: two mono ASEs — BlueZ may call SetConfiguration for the second
    # ASE shortly after the first).
    if len(endpoint.transports) == 1:
        print("[main] Got 1 transport, waiting 2s for a possible second ASE...")
        deadline2 = time.monotonic() + 2
        while time.monotonic() < deadline2:
            _GLib.MainContext.default().iteration(False)
            time.sleep(0.05)

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

    # ── 8. Stream LC3 SDUs every 10 ms ───────────────────────────────────
    print(
        "[main] Streaming {:.0f} Hz sine for {} s...".format(args.freq, args.duration)
    )

    # Set up encoder(s) based on stream mode
    if stream_mode == "stereo_b":
        enc_L = LC3Encoder()
        enc_R = LC3Encoder()
    elif stream_mode == "stereo_a":
        # Two mono encoders — one per transport
        enc_L = LC3Encoder()
        enc_R = LC3Encoder()
        # Sort transports: FL (0x01) first, FR (0x02) second
        endpoint.transports.sort(key=lambda t: t["channel_alloc"])
    else:  # mono
        enc = LC3Encoder()

    pcm_L = _gen_sine(args.freq, SR_HZ, FRAME_SAMPLES, amplitude=10000)
    pcm_R = _gen_sine(args.freq, SR_HZ, FRAME_SAMPLES, amplitude=10000)

    start = time.monotonic()
    next_ts = start
    frame_count = 0
    try:
        while time.monotonic() - start < args.duration:
            if stream_mode == "stereo_b":
                frame_L = enc_L.encode(pcm_L)
                frame_R = enc_R.encode(pcm_R)
                sdu = frame_L + frame_R
                try:
                    os.write(endpoint.transports[0]["fd"], sdu)
                except OSError as e:
                    print("[error] os.write failed: {}".format(e))
                    break
            elif stream_mode == "stereo_a":
                frame_L = enc_L.encode(pcm_L)
                frame_R = enc_R.encode(pcm_R)
                try:
                    os.write(endpoint.transports[0]["fd"], frame_L)
                    os.write(endpoint.transports[1]["fd"], frame_R)
                except OSError as e:
                    print("[error] os.write failed: {}".format(e))
                    break
            else:  # mono
                sdu = enc.encode(pcm_L)
                try:
                    os.write(endpoint.transports[0]["fd"], sdu)
                except OSError as e:
                    print("[error] os.write failed: {}".format(e))
                    break

            frame_count += 1

            # Pace: 10 ms cadence
            next_ts += 0.010
            sleep_for = next_ts - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)
            else:
                next_ts = time.monotonic()
    except KeyboardInterrupt:
        print("\n[main] Interrupted during streaming")

    elapsed = time.monotonic() - start
    if frame_count > 0:
        fps = frame_count / elapsed if elapsed > 0 else 0
        print(
            "[main] Done: {} frames in {:.2f} s ({:.1f} fps)".format(
                frame_count, elapsed, fps
            )
        )

    # ── 9. Cleanup ───────────────────────────────────────────────────────
    print("[cleanup] Releasing resources...")

    for t in endpoint.transports:
        try:
            transport_obj = bus.get_object("org.bluez", t["path"])
            transport_iface = _dbus.Interface(
                transport_obj, "org.bluez.MediaTransport1"
            )
            transport_iface.Release()
            print("[cleanup] Transport {} released".format(t["path"]))
        except Exception as e:
            print("[cleanup] Transport {} release error: {}".format(t["path"], e))
        try:
            os.close(t["fd"])
        except OSError:
            pass
    endpoint.transports = []

    try:
        media.UnregisterEndpoint(ENDPOINT_PATH)
        print("[cleanup] Endpoint unregistered")
    except Exception as e:
        print("[cleanup] Endpoint unregister error: {}".format(e))

    try:
        agent_mgr.UnregisterAgent(AGENT_PATH)
        print("[cleanup] Agent unregistered")
    except Exception as e:
        print("[cleanup] Agent unregister error: {}".format(e))

    print("[main] Exiting")


if __name__ == "__main__":
    main()
