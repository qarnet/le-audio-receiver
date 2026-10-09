#!/usr/bin/env python3
"""Valid public BlueZ checkpoint inside PB-053 private guest only."""

import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import sys
import threading
import time

from bluez_guest_acquire import AcquireOwner
from bluez_guest_limits import EventLedger


class Iovec(ctypes.Structure):
    _fields_ = [("iov_base", ctypes.c_void_p), ("iov_len", ctypes.c_size_t)]


class Msghdr(ctypes.Structure):
    _fields_ = [
        ("msg_name", ctypes.c_void_p),
        ("msg_namelen", ctypes.c_uint32),
        ("msg_iov", ctypes.POINTER(Iovec)),
        ("msg_iovlen", ctypes.c_size_t),
        ("msg_control", ctypes.c_void_p),
        ("msg_controllen", ctypes.c_size_t),
        ("msg_flags", ctypes.c_int),
    ]


def receive_packet(fd, capacity, timeout):
    """Receive message flags without decoding a Bluetooth socket address."""
    if capacity <= 0 or timeout < 0:
        raise ValueError("Invalid receive capacity or timeout")
    libc = ctypes.CDLL(None, use_errno=True)
    recvmsg = libc.recvmsg
    recvmsg.argtypes = (ctypes.c_int, ctypes.POINTER(Msghdr), ctypes.c_int)
    recvmsg.restype = ctypes.c_ssize_t
    buffer = ctypes.create_string_buffer(capacity)
    vector = Iovec(ctypes.cast(buffer, ctypes.c_void_p), capacity)
    header = Msghdr(None, 0, ctypes.pointer(vector), 1, None, 0, 0)
    deadline = time.monotonic() + timeout
    while True:
        try:
            ready, _, _ = select.select(
                [fd], [], [], max(0, deadline - time.monotonic())
            )
        except InterruptedError:
            ready = []
        if not ready:
            if time.monotonic() >= deadline:
                raise socket.timeout("timed out")
            continue
        size = recvmsg(fd, ctypes.byref(header), 0)
        if size >= 0:
            return buffer.raw[:size], header.msg_flags
        error = ctypes.get_errno()
        if (
            error in (errno.EINTR, errno.EAGAIN, errno.EWOULDBLOCK)
            and time.monotonic() < deadline
        ):
            continue
        raise OSError(error, os.strerror(error))


def wait_revoked(fd, timeout):
    """Require peer EOF or POLLHUP, rejecting unread data and POLLERR alone."""
    deadline = time.monotonic() + timeout
    watcher = select.poll()
    watcher.register(fd, select.POLLIN | select.POLLHUP | select.POLLERR)
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise socket.timeout("peer transport revocation timed out")
        events = watcher.poll(max(1, int(remaining * 1000)))
        for _, flags in events:
            if flags & select.POLLNVAL:
                raise RuntimeError("POLLNVAL at revocation")
            if flags & select.POLLIN:
                data, msg_flags = receive_packet(
                    fd, 121, max(0, deadline - time.monotonic())
                )
                if data or msg_flags & socket.MSG_TRUNC:
                    raise RuntimeError(
                        f"Unread ISO packet at revocation: {data.hex()}, flags={msg_flags}"
                    )
                if not flags & select.POLLHUP:
                    raise RuntimeError(
                        "Empty ISO packet without peer hangup at revocation"
                    )
                return {
                    "eof": True,
                    "pollhup": True,
                    "poll_flags": flags,
                }
            if flags & select.POLLHUP:
                return {"eof": False, "pollhup": True, "poll_flags": flags}
            if flags & select.POLLERR:
                raise RuntimeError("POLLERR without peer hangup at revocation")


def guard():
    if "pb053_guest=1" not in Path("/proc/cmdline").read_text().split():
        raise RuntimeError("Public child requires pb053_guest=1")
    if not Path("/opt/pb053/runtime.json").is_file():
        raise RuntimeError("Public child requires guest runtime")


def close_owned_lease(lease, closed, result, ledger):
    try:
        if lease["socket"] is not None:
            lease["socket"].close()
        elif lease["fd"] is not None:
            os.close(lease["fd"])
        closed.add(lease["path"])
        ledger.safe_append({"closed": lease["path"], "ok": True})
    except OSError as exc:
        result["cleanup_errors"].append(f"Close {lease['path']}: {exc}")


def record_ledger_failure(result):
    error = result["events"].error
    if error is not None:
        result["cleanup_errors"].append(f"Event ledger: {error}")
        result["ok"] = False


def device_snapshot(props):
    return {
        "Address": str(props["Address"]),
        "Adapter": str(props["Adapter"]),
        **{
            key: bool(props[key])
            for key in ("Paired", "Bonded", "Connected", "ServicesResolved")
        },
    }


def transport_snapshot(props):
    return {
        "Codec": int(props["Codec"]),
        "Configuration": bytes(props["Configuration"]).hex(),
        "Device": str(props["Device"]),
        "UUID": str(props["UUID"]).lower(),
        "State": str(props["State"]),
    }


def execute(result, state):
    guard()  # No dbus/gi import or bus operation before guest-only guard.
    stimulus = Path("/opt/pb053/stimulus.lc3").read_bytes()
    if (
        len(stimulus) != 15360
        or hashlib.sha256(stimulus).hexdigest()
        != "c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be"
    ):
        raise RuntimeError("LC3 transport stimulus size/hash mismatch")
    import dbus
    import dbus.service
    from dbus.mainloop.glib import DBusGMainLoop, threads_init
    from gi.repository import GLib

    DBusGMainLoop(set_as_default=True)
    threads_init()
    bus = dbus.SystemBus()
    bus.set_exit_on_disconnect(False)
    loop = GLib.MainLoop()
    worker = threading.Thread(target=loop.run, daemon=True)
    worker.start()
    ledger = result["events"]
    acquire_owner = AcquireOwner()

    def begin(obj, interface, method, *args, acquire_role=None):
        detail = {
            "object_path": obj.object_path,
            "interface": interface,
            "method": method,
            "args": repr(args),
        }
        print(json.dumps({"dbus_call": "before", **detail}), flush=True)
        completed = threading.Event()
        outcome = {}
        if acquire_role is not None and (
            interface != "org.bluez.MediaTransport1" or method != "Acquire" or args
        ):
            raise ValueError("Invalid Acquire call")
        token = (
            acquire_owner.register(str(obj.object_path), acquire_role)
            if acquire_role is not None
            else None
        )

        def reply_handler(*values):
            try:
                if token is not None:
                    if len(values) != 3:
                        raise ValueError("Acquire reply must contain fd and two MTUs")
                    unix_fd, read_mtu, write_mtu = values
                    fd = unix_fd.take()
                    try:
                        if any(type(value) is bool for value in (read_mtu, write_mtu)):
                            raise ValueError("Invalid Acquire MTU boolean")
                        read_mtu, write_mtu = int(read_mtu), int(write_mtu)
                    except Exception:
                        os.close(fd)
                        raise
                    acquire_owner.received(token, fd, read_mtu, write_mtu)
                    outcome["reply"] = (read_mtu, write_mtu)
                else:
                    outcome["reply"] = (
                        None
                        if not values
                        else values[0]
                        if len(values) == 1
                        else values
                    )
            except Exception as exc:
                if token is not None:
                    acquire_owner.failed(token, exc)
                outcome["error"] = exc
            finally:
                completed.set()

        def error_handler(error):
            if token is not None:
                acquire_owner.failed(token, error)
            outcome["error"] = error
            completed.set()

        pending = {
            "completed": completed,
            "outcome": outcome,
            "detail": detail,
            "token": token,
        }
        try:
            if token is not None:
                handle = bus.call_async(
                    "org.bluez",
                    str(obj.object_path),
                    interface,
                    method,
                    "",
                    args,
                    reply_handler,
                    error_handler,
                    timeout=10,
                )
                acquire_owner.bind(token, handle)
            else:
                getattr(dbus.Interface(obj, interface), method)(
                    *args,
                    timeout=10,
                    reply_handler=reply_handler,
                    error_handler=error_handler,
                )
        except Exception as exc:
            if token is not None:
                acquire_owner.failed(token, exc)
            print(
                json.dumps(
                    {"dbus_call": "exception", **detail, "exception": repr(exc)}
                ),
                flush=True,
            )
            raise
        return pending

    def wait(pending):
        completed = pending["completed"]
        outcome = pending["outcome"]
        detail = pending["detail"]
        try:
            if not completed.wait(11):
                raise TimeoutError(f"D-Bus call completion timed out: {detail}")
            if "error" in outcome:
                raise outcome["error"]
            reply = outcome["reply"]
        except Exception as exc:
            print(
                json.dumps(
                    {"dbus_call": "exception", **detail, "exception": repr(exc)}
                ),
                flush=True,
            )
            raise
        print(
            json.dumps({"dbus_call": "after", **detail, "result": repr(reply)}),
            flush=True,
        )
        return reply

    def call(obj, interface, method, *args):
        return wait(begin(obj, interface, method, *args))

    def objects():
        return call(
            bus.get_object("org.bluez", "/", introspect=False),
            "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects",
        )

    def properties(path, interface):
        return call(
            bus.get_object("org.bluez", path, introspect=False),
            "org.freedesktop.DBus.Properties",
            "GetAll",
            interface,
        )

    def set_property(path, interface, name, value):
        call(
            bus.get_object("org.bluez", path, introspect=False),
            "org.freedesktop.DBus.Properties",
            "Set",
            interface,
            name,
            value,
        )

    def poll(predicate, seconds=20.0):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            found = predicate()
            if found:
                return found
            time.sleep(0.25)
        raise RuntimeError("Public state deadline exceeded")

    class Agent(dbus.service.Object):
        @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Release(self):
            ledger.append({"callback": "Agent.Release"})

        @dbus.service.method("org.bluez.Agent1", in_signature="", out_signature="")
        def Cancel(self):
            ledger.append({"callback": "Agent.Cancel"})

        @dbus.service.method("org.bluez.Agent1", in_signature="o", out_signature="")
        def RequestAuthorization(self, device):
            ledger.append(
                {"callback": "Agent.RequestAuthorization", "device": str(device)}
            )

        @dbus.service.method("org.bluez.Agent1", in_signature="ou", out_signature="")
        def RequestConfirmation(self, device, passkey):
            ledger.append(
                {
                    "callback": "Agent.RequestConfirmation",
                    "device": str(device),
                    "passkey": int(passkey),
                }
            )

        @dbus.service.method("org.bluez.Agent1", in_signature="os", out_signature="")
        def AuthorizeService(self, device, uuid):
            ledger.append(
                {
                    "callback": "Agent.AuthorizeService",
                    "device": str(device),
                    "uuid": str(uuid),
                }
            )

    def bytes_array(raw):
        return dbus.Array([dbus.Byte(byte) for byte in raw], signature="y")

    codec_configuration = bytes.fromhex(
        "02 01 08 02 02 01 03 04 78 00 05 03 01 00 00 00"
    )

    class Endpoint(dbus.service.Object):
        def __init__(self, connection, path):
            self.path = path
            self.configured = {}
            super().__init__(connection, path)

        @dbus.service.method(
            "org.bluez.MediaEndpoint1", in_signature="", out_signature=""
        )
        def Release(self):
            ledger.append({"callback": "Endpoint.Release", "path": self.path})

        @dbus.service.method(
            "org.bluez.MediaEndpoint1", in_signature="o", out_signature=""
        )
        def ClearConfiguration(self, transport):
            ledger.append(
                {
                    "callback": "Endpoint.ClearConfiguration",
                    "path": self.path,
                    "transport": str(transport),
                }
            )

        @dbus.service.method(
            "org.bluez.MediaEndpoint1", in_signature="oa{sv}", out_signature=""
        )
        def SetConfiguration(self, transport, props):
            self.configured[str(transport)] = props
            ledger.append(
                {
                    "callback": "Endpoint.SetConfiguration",
                    "path": self.path,
                    "transport": str(transport),
                    "properties": repr(props),
                }
            )

        @dbus.service.method(
            "org.bluez.MediaEndpoint1", in_signature="a{sv}", out_signature="a{sv}"
        )
        def SelectProperties(self, props):
            ledger.append(
                {
                    "callback": "Endpoint.SelectProperties",
                    "path": self.path,
                    "properties": str(props),
                }
            )
            return dbus.Dictionary(
                {
                    "Capabilities": bytes_array(codec_configuration),
                    "QoS": dbus.Dictionary(
                        {
                            "Framing": dbus.Byte(0),
                            "PHY": dbus.Byte(2),
                            "Interval": dbus.UInt32(10000),
                            "SDU": dbus.UInt16(120),
                            "Retransmissions": dbus.Byte(5),
                            "Latency": dbus.UInt16(20),
                            "PresentationDelay": dbus.UInt32(40000),
                            "TargetLatency": dbus.Byte(2),
                        },
                        signature="sv",
                    ),
                },
                signature="sv",
            )

    class Advertisement(dbus.service.Object):
        @dbus.service.method(
            "org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}"
        )
        def GetAll(self, interface):
            print(
                json.dumps(
                    {
                        "callback": "Advertisement.GetAll",
                        "phase": "enter",
                        "time_ns": time.monotonic_ns(),
                        "interface": str(interface),
                    }
                ),
                flush=True,
            )
            try:
                if interface != "org.bluez.LEAdvertisement1":
                    raise ValueError("Unexpected advertisement interface")
                return dbus.Dictionary(
                    {
                        "Type": dbus.String("peripheral"),
                        # BlueZ doc/org.bluez.LEAdvertisement.rst:141-147: general discoverable.
                        "Discoverable": dbus.Boolean(True),
                        "ServiceUUIDs": dbus.Array(
                            [dbus.String("00001850-0000-1000-8000-00805f9b34fb")],
                            signature="s",
                        ),
                        "LocalName": dbus.String("PB053 sink"),
                    },
                    signature="sv",
                )
            finally:
                print(
                    json.dumps(
                        {
                            "callback": "Advertisement.GetAll",
                            "phase": "exit",
                            "time_ns": time.monotonic_ns(),
                            "interface": str(interface),
                        }
                    ),
                    flush=True,
                )

        @dbus.service.method(
            "org.bluez.LEAdvertisement1", in_signature="", out_signature=""
        )
        def Release(self):
            ledger.append({"callback": "Advertisement.Release"})

    owned = []
    registered = []
    discovery = False
    peer = None
    reciprocal = None
    leases = []
    transport_paths = []
    delivery_verified = False
    adapter = None
    second = None
    try:
        managed = objects()
        paths = sorted(
            str(p)
            for p, interfaces in managed.items()
            if "org.bluez.Adapter1" in interfaces
        )
        if len(paths) != 2 or any(not p.startswith("/org/bluez/hci") for p in paths):
            raise RuntimeError(f"Expected two guest adapters: {paths}")
        adapter, second = paths
        addresses = [str(properties(p, "org.bluez.Adapter1")["Address"]) for p in paths]
        if addresses[0] == addresses[1]:
            raise RuntimeError("Guest adapter addresses not distinct")
        ledger.append({"adapters": paths, "addresses": addresses})
        initial = objects()
        devices = {
            str(path): dict(interfaces["org.bluez.Device1"])
            for path, interfaces in initial.items()
            if "org.bluez.Device1" in interfaces
        }
        ledger.append(
            {
                "initial_devices": {
                    path: device_snapshot(props) for path, props in devices.items()
                }
            }
        )
        if state == "fresh":
            if any(
                bool(props.get("Paired")) or bool(props.get("Bonded"))
                for props in devices.values()
            ):
                raise RuntimeError("Fresh state contains bonded or paired Device1")
        else:
            prior = json.loads(Path("/opt/pb053/prior.json").read_text())
            if prior.get("addresses") != addresses:
                raise RuntimeError("Retained adapter identity mismatch")
            for path, address in zip(prior["peers"], reversed(addresses)):
                props = devices.get(path)
                if (
                    props is None
                    or str(props.get("Address")) != address
                    or not bool(props.get("Paired"))
                    or not bool(props.get("Bonded"))
                ):
                    raise RuntimeError(f"Retained bond missing: {path}")
        result["cases"]["state_initialization"] = True

        agent = Agent(bus, "/pb053/agent")
        owned.append(agent)
        manager = bus.get_object("org.bluez", "/org/bluez", introspect=False)
        call(
            manager,
            "org.bluez.AgentManager1",
            "RegisterAgent",
            dbus.ObjectPath("/pb053/agent"),
            "NoInputNoOutput",
        )
        registered.append(
            (manager, "org.bluez.AgentManager1", "UnregisterAgent", "/pb053/agent")
        )
        call(
            manager,
            "org.bluez.AgentManager1",
            "RequestDefaultAgent",
            dbus.ObjectPath("/pb053/agent"),
        )

        endpoints = []
        for path, role, uuid in (
            (adapter, "source", "00002bcb-0000-1000-8000-00805f9b34fb"),
            (second, "sink", "00002bc9-0000-1000-8000-00805f9b34fb"),
        ):
            ep_path = "/pb053/" + role
            endpoint = Endpoint(bus, ep_path)
            owned.append(endpoint)
            endpoints.append(endpoint)
            media = bus.get_object("org.bluez", path, introspect=False)
            props = dbus.Dictionary(
                {
                    "UUID": dbus.String(uuid),
                    "Codec": dbus.Byte(6),
                    "Capabilities": bytes_array(
                        bytes.fromhex("03 01 80 00 02 02 02 02 03 01 05 04 78 00 78 00")
                    ),
                    "Locations": dbus.UInt32(1),
                    "Context": dbus.UInt16(0x0005),
                    "SupportedContext": dbus.UInt16(0x0005),
                    "Metadata": bytes_array(bytes.fromhex("03 01 04 00")),
                },
                signature="sv",
            )
            call(
                media,
                "org.bluez.Media1",
                "RegisterEndpoint",
                dbus.ObjectPath(ep_path),
                props,
            )
            registered.append(
                (media, "org.bluez.Media1", "UnregisterEndpoint", ep_path)
            )
            # BlueZ doc/org.bluez.Media.rst:133-139 assigns SupportedUUIDs to Media1.
            actual = properties(path, "org.bluez.Media1")
            uuids = [str(item).lower() for item in actual["SupportedUUIDs"]]
            ledger.append(
                {
                    "endpoint": ep_path,
                    "adapter": path,
                    "uuid": uuid,
                    "supported_uuids": uuids,
                }
            )
            if uuid not in uuids:
                raise RuntimeError(f"Missing public SupportedUUIDs: {uuid}")
        result["cases"]["endpoint_registration"] = True

        for path in paths:
            set_property(
                path,
                "org.bluez.Adapter1",
                "Powered",
                dbus.Boolean(True, variant_level=1),
            )
            set_property(
                path,
                "org.bluez.Adapter1",
                "Pairable",
                dbus.Boolean(True, variant_level=1),
            )
        ad = Advertisement(bus, "/pb053/advertisement")
        owned.append(ad)
        adv_manager = bus.get_object("org.bluez", second, introspect=False)
        call(
            adv_manager,
            "org.bluez.LEAdvertisingManager1",
            "RegisterAdvertisement",
            dbus.ObjectPath("/pb053/advertisement"),
            dbus.Dictionary({}, signature="sv"),
        )
        registered.append(
            (
                adv_manager,
                "org.bluez.LEAdvertisingManager1",
                "UnregisterAdvertisement",
                "/pb053/advertisement",
            )
        )
        first = bus.get_object("org.bluez", adapter, introspect=False)
        call(
            first,
            "org.bluez.Adapter1",
            "SetDiscoveryFilter",
            dbus.Dictionary({"Transport": dbus.String("le")}, signature="sv"),
        )
        call(first, "org.bluez.Adapter1", "StartDiscovery")
        discovery = True

        def device_on(parent, address):
            for p, interfaces in objects().items():
                if (
                    str(p).startswith(parent + "/")
                    and "org.bluez.Device1" in interfaces
                    and str(interfaces["org.bluez.Device1"].get("Address")) == address
                ):
                    return str(p)
            return None

        peer = poll(lambda: device_on(adapter, addresses[1]))
        discovering_before_stop = bool(
            properties(adapter, "org.bluez.Adapter1")["Discovering"]
        )
        if not discovering_before_stop:
            raise RuntimeError("First adapter not Discovering")
        call(first, "org.bluez.Adapter1", "StopDiscovery")
        discovery = False
        discovering_after_stop = bool(
            properties(adapter, "org.bluez.Adapter1")["Discovering"]
        )
        if discovering_after_stop:
            raise RuntimeError("First adapter still Discovering")
        ledger.append(
            {
                "discovered": peer,
                "address": addresses[1],
                "adapter": adapter,
                "discovering_before_stop": discovering_before_stop,
                "discovering_after_stop": discovering_after_stop,
            }
        )
        result["cases"]["discovery"] = True

        remote = bus.get_object("org.bluez", peer, introspect=False)
        operation = "Pair" if state == "fresh" else "Connect"
        call(remote, "org.bluez.Device1", operation)
        required = ("Paired", "Bonded", "Connected", "ServicesResolved")

        def ready_device(path):
            snapshot = dict(properties(path, "org.bluez.Device1"))
            return (
                snapshot if all(bool(snapshot.get(key)) for key in required) else None
            )

        local_snapshot = poll(lambda: ready_device(peer))
        reciprocal = poll(lambda: device_on(second, addresses[0]))
        remote_snapshot = poll(lambda: ready_device(reciprocal))
        ledger.append(
            {
                "paired": peer,
                "reciprocal": reciprocal,
                "local": device_snapshot(local_snapshot),
                "remote": device_snapshot(remote_snapshot),
                "operation": operation,
            }
        )
        result["cases"]["pairing"] = True

        def configured_paths():
            if all(len(endpoint.configured) == 1 for endpoint in endpoints):
                paths = [next(iter(endpoint.configured)) for endpoint in endpoints]
                return paths if len(set(paths)) == 2 else None
            return None

        transport_paths = poll(configured_paths, seconds=30)
        for endpoint, transport_path in zip(endpoints, transport_paths):
            callback_props = endpoint.configured[transport_path]
            transport_props = properties(transport_path, "org.bluez.MediaTransport1")
            ledger.append(
                {
                    "transport": transport_path,
                    "endpoint": endpoint.path,
                    "callback_properties": repr(callback_props),
                    "transport_properties": transport_snapshot(transport_props),
                }
            )
            if (
                int(transport_props["Codec"]) != 6
                or bytes(transport_props["Configuration"]) != codec_configuration
            ):
                raise RuntimeError(f"Invalid public LC3 transport: {transport_path}")
        result["cases"]["endpoint_configuration"] = True

        source_request = begin(
            bus.get_object("org.bluez", transport_paths[0], introspect=False),
            "org.bluez.MediaTransport1",
            "Acquire",
            acquire_role="source",
        )

        def sink_pending():
            if (
                source_request["completed"].is_set()
                and "error" in source_request["outcome"]
            ):
                raise source_request["outcome"]["error"]
            state = str(
                properties(transport_paths[1], "org.bluez.MediaTransport1")["State"]
            )
            ledger.append({"sink_state_before_acquire": state})
            return state == "pending"

        poll(sink_pending, seconds=10)
        sink_request = begin(
            bus.get_object("org.bluez", transport_paths[1], introspect=False),
            "org.bluez.MediaTransport1",
            "Acquire",
            acquire_role="sink",
        )
        requests = [source_request, sink_request]
        acquisition_error = None
        for path, request in zip(transport_paths, requests):
            try:
                wait(request)
                lease = acquire_owner.take(request["token"])
                leases.append(lease)
                lease["socket"] = socket.socket(fileno=lease["fd"])
                lease["fd"] = None
                lease["socket"].settimeout(3)
                ledger.append(
                    {
                        "acquired": path,
                        "read_mtu": lease["read_mtu"],
                        "write_mtu": lease["write_mtu"],
                    }
                )
            except Exception as exc:
                if acquisition_error is None:
                    acquisition_error = exc
        if acquisition_error is not None:
            raise acquisition_error
        source, sink = leases
        if source["write_mtu"] < 120 or sink["read_mtu"] < 120:
            raise RuntimeError(
                f"ISO MTU smaller than frame: {source['write_mtu']}, {sink['read_mtu']}"
            )
        for path in transport_paths:
            poll(
                lambda: (
                    str(properties(path, "org.bluez.MediaTransport1")["State"])
                    == "active"
                ),
                seconds=10,
            )

        deadline = time.monotonic()
        for index in range(16):
            frame = stimulus[index * 120 : (index + 1) * 120]
            time.sleep(max(0, deadline - time.monotonic()))
            try:
                sent = source["socket"].send(frame)
                received, flags = receive_packet(sink["socket"].fileno(), 121, 3)
            except Exception as exc:
                ledger.append(
                    {"failed_frame": index, "expected": frame.hex(), "error": repr(exc)}
                )
                raise
            ledger.append(
                {
                    "frame": index,
                    "sent": sent,
                    "received_len": len(received),
                    "sha256": hashlib.sha256(received).hexdigest(),
                    "received_hex": received.hex(),
                    "flags": flags,
                }
            )
            if (
                sent != 120
                or len(received) != 120
                or flags & socket.MSG_TRUNC
                or received != frame
            ):
                raise RuntimeError(f"ISO frame mismatch at index {index}")
            deadline += 0.010
        try:
            unexpected = receive_packet(sink["socket"].fileno(), 121, 0.05)
        except socket.timeout:
            pass
        else:
            ledger.append({"unexpected_iso": repr(unexpected)})
            raise RuntimeError("Extra ISO message after 16 frames")
        delivery_verified = True
    finally:
        unclaimed = acquire_owner.begin_cleanup()
        for _, path, role in unclaimed:
            try:
                call(
                    bus.get_object("org.bluez", path, introspect=False),
                    "org.bluez.MediaTransport1",
                    "Release",
                )
                ledger.safe_append(
                    {"unclaimed_release": path, "role": role, "ok": True}
                )
            except Exception as exc:
                result["cleanup_errors"].append(f"Unclaimed Release {path}: {exc}")
                ledger.safe_append(
                    {
                        "unclaimed_release": path,
                        "role": role,
                        "ok": False,
                        "error": repr(exc),
                    }
                )
        acquire_owner.finish_cleanup()
        source_lease = next(
            (lease for lease in leases if lease["role"] == "source"), None
        )
        sink_lease = next((lease for lease in leases if lease["role"] == "sink"), None)
        source_released = False
        sink_revoked = False
        closed = set()

        def release(lease):
            call(
                bus.get_object("org.bluez", lease["path"], introspect=False),
                "org.bluez.MediaTransport1",
                "Release",
            )
            ledger.safe_append(
                {"released": lease["path"], "role": lease["role"], "ok": True}
            )

        def close_lease(lease):
            close_owned_lease(lease, closed, result, ledger)

        if source_lease is not None:
            try:
                release(source_lease)
                source_released = True
            except Exception as exc:
                result["cleanup_errors"].append(
                    f"Release {source_lease['path']}: {exc}"
                )
            close_lease(source_lease)

        if sink_lease is not None:
            try:
                if source_released:

                    def sink_idle():
                        current = str(
                            properties(sink_lease["path"], "org.bluez.MediaTransport1")[
                                "State"
                            ]
                        )
                        return current if current == "idle" else None

                    state = poll(
                        sink_idle,
                        seconds=10,
                    )
                    evidence = wait_revoked(sink_lease["socket"].fileno(), 10)
                    ledger.safe_append(
                        {"revoked": sink_lease["path"], "state": state, **evidence}
                    )
                    sink_revoked = True
            except Exception as exc:
                result["cleanup_errors"].append(
                    f"Revocation {sink_lease['path']}: {exc}"
                )
            if not sink_revoked:
                try:
                    release(sink_lease)
                except Exception as exc:
                    result["cleanup_errors"].append(
                        f"Release {sink_lease['path']}: {exc}"
                    )
            close_lease(sink_lease)
        if leases:
            for path in transport_paths:
                try:
                    poll(
                        lambda: (
                            str(properties(path, "org.bluez.MediaTransport1")["State"])
                            != "active"
                        ),
                        seconds=10,
                    )
                    ledger.safe_append({"inactive": path, "ok": True})
                except Exception as exc:
                    result["cleanup_errors"].append(f"Transport inactive {path}: {exc}")
        # Delivery is published only after both Release/close operations succeed.
        result["cases"]["iso_delivery"] = (
            delivery_verified
            and len(leases) == 2
            and source_released
            and sink_revoked
            and len(closed) == 2
            and not result["cleanup_errors"]
        )
        if discovery:
            try:
                call(
                    bus.get_object("org.bluez", adapter, introspect=False),
                    "org.bluez.Adapter1",
                    "StopDiscovery",
                )
                ledger.safe_append({"cleanup": "StopDiscovery", "ok": True})
            except Exception as exc:
                result["cleanup_errors"].append(f"StopDiscovery: {exc}")
        if peer:
            try:
                disconnect_deadline = time.monotonic() + 10
                reply = call(
                    bus.get_object("org.bluez", peer, introspect=False),
                    "org.bluez.Device1",
                    "Disconnect",
                )
                ledger.safe_append(
                    {
                        "disconnect": peer,
                        "reply": repr(reply),
                        "time_ns": time.monotonic_ns(),
                    }
                )
                if reciprocal:
                    disconnect_samples = []

                    def both_disconnected():
                        first = device_snapshot(properties(peer, "org.bluez.Device1"))
                        second = device_snapshot(
                            properties(reciprocal, "org.bluez.Device1")
                        )
                        sample = {
                            "disconnect_states": {peer: first, reciprocal: second},
                            "time_ns": time.monotonic_ns(),
                        }
                        disconnect_samples.append(sample)
                        ledger.safe_append(sample)
                        return not first["Connected"] and not second["Connected"]

                    poll(
                        both_disconnected,
                        seconds=max(0, disconnect_deadline - time.monotonic()),
                    )
                    # Doc/org.bluez.Device.rst:70-71: suppress reciprocal LE
                    # incoming policy only after both public peers disconnected.
                    reply = call(
                        bus.get_object("org.bluez", reciprocal, introspect=False),
                        "org.bluez.Device1",
                        "Disconnect",
                    )
                    ledger.safe_append(
                        {
                            "disconnect": reciprocal,
                            "reply": repr(reply),
                            "time_ns": time.monotonic_ns(),
                        }
                    )
                    if not both_disconnected():
                        raise RuntimeError(
                            "Reciprocal disconnect changed public connection state"
                        )
                    start_ns = time.monotonic_ns()
                    observed_until = time.monotonic() + 0.5
                    if observed_until > disconnect_deadline:
                        raise RuntimeError("Insufficient disconnect observation budget")
                    while time.monotonic() < observed_until:
                        if not both_disconnected():
                            raise RuntimeError(
                                "Peer reconnected during bounded observation"
                            )
                        time.sleep(min(0.05, max(0, observed_until - time.monotonic())))
                    if (
                        time.monotonic() > disconnect_deadline
                        or not both_disconnected()
                    ):
                        raise RuntimeError(
                            "Peer reconnected or observation deadline exceeded"
                        )
                    if time.monotonic() > disconnect_deadline:
                        raise RuntimeError("Disconnect observation deadline exceeded")
                    window_samples = [
                        sample
                        for sample in disconnect_samples
                        if sample["time_ns"] >= start_ns
                    ]
                    ledger.safe_append(
                        {
                            "disconnect_observation": {
                                "peers": [peer, reciprocal],
                                "start_ns": start_ns,
                                "end_ns": window_samples[-1]["time_ns"],
                                "samples": len(window_samples),
                                "all_disconnected": True,
                            }
                        }
                    )
                ledger.safe_append({"cleanup": "peer disconnect", "ok": True})
            except Exception as exc:
                result["cleanup_errors"].append(f"peer disconnect: {exc}")
        for obj, iface, method, path in reversed(registered):
            try:
                call(obj, iface, method, dbus.ObjectPath(path))
                ledger.safe_append({"cleanup": method, "path": path, "ok": True})
            except Exception as exc:
                result["cleanup_errors"].append(f"{method} {path}: {exc}")

        def cleanup_action(action, operation):
            print(json.dumps({"cleanup_action": action, "phase": "before"}), flush=True)
            try:
                operation()
            except Exception as exc:
                result["cleanup_errors"].append(f"{action}: {exc}")
                print(
                    json.dumps(
                        {
                            "cleanup_action": action,
                            "phase": "exception",
                            "exception": repr(exc),
                        }
                    ),
                    flush=True,
                )
            else:
                print(
                    json.dumps({"cleanup_action": action, "phase": "after"}), flush=True
                )

        try:
            for item in reversed(owned):
                path = getattr(item, "__dbus_object_path__", type(item).__name__)
                cleanup_action(f"remove {path}", item.remove_from_connection)
            cleanup_action("bus.close", bus.close)
        finally:
            try:
                cleanup_action("loop.quit", loop.quit)
            finally:
                cleanup_action("worker.join", lambda: worker.join(timeout=5))
                if worker.is_alive():
                    result["cleanup_errors"].append(
                        "GLib loop did not stop within 5 seconds"
                    )
                result["cleanup_errors"].extend(acquire_owner.errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state", choices=("fresh", "retained"), default="fresh")
    args = parser.parse_args()
    result = {
        "schema_version": 2,
        "state": args.state,
        "ok": False,
        "cases": {
            key: False
            for key in (
                "endpoint_registration",
                "discovery",
                "pairing",
                "endpoint_configuration",
                "iso_delivery",
                "state_initialization",
            )
        },
        "events": EventLedger(),
        "cleanup_errors": [],
        "error": None,
    }
    try:
        execute(result, args.state)
    except Exception as exc:
        result["error"] = str(exc)
    record_ledger_failure(result)
    result["ok"] = (
        result["error"] is None
        and not result["cleanup_errors"]
        and all(result["cases"].values())
    )
    print("PB053_PUBLIC_RESULT " + json.dumps(result), flush=True)
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
