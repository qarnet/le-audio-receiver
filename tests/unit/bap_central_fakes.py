#!/usr/bin/env python3
"""Shared stdlib fakes for the bap_central R9 split suites (not a gate child).

Fake D-Bus type constructors behave like plain Python types, a fake service
module provides an Object base + method decorator, a fake GLib main context
is a no-op iteration, and a fake system bus hands out per-path interface
proxies with a call log and per-method scripting (used to fire async D-Bus
reply/error handlers deterministically).

Deliberately stdlib-only: the R9 module tests must run without dbus-python,
pygobject, or liblc3.
"""


class DBusException(Exception):
    """Stand-in for dbus.exceptions.DBusException."""


class FakeDbusModule:
    """Stand-in for the real ``dbus`` module (late-injected at runtime).

    The R9 modules only touch a narrow surface: type constructors
    (Dictionary/Array/Byte/UInt16/UInt32/String/Boolean), the exceptions
    namespace, ``Interface`` and ``SystemBus``.
    """

    class Dictionary(dict):
        def __init__(self, *args, **kwargs):
            kwargs.pop("signature", None)
            super().__init__(*args)

    class Array(list):
        def __init__(self, *args, **kwargs):
            kwargs.pop("signature", None)
            super().__init__(*args)

    class Byte(int):
        pass

    class UInt16(int):
        pass

    class UInt32(int):
        pass

    class String(str):
        pass

    class Boolean(int):
        def __new__(cls, value):
            return int.__new__(cls, 1 if value else 0)

    class _Exceptions(object):
        DBusException = DBusException

    exceptions = _Exceptions()

    def Interface(self, obj, name):
        """Return (or create) the named interface proxy on ``obj``."""
        return obj.get_iface(name)

    def SystemBus(self):
        return FakeBus()


class FakeIface:
    """Per-path interface proxy: records calls and supports scripting.

    Every attribute access returns a callable that appends
    ``(method, args, kwargs)`` to ``calls``; if a scripted handler was
    registered via ``script(method, fn)`` it is invoked with the same
    arguments and its return value is returned (scripts raise to simulate
    D-Bus errors, or fire reply/error handlers for async methods).
    """

    def __init__(self, path, name):
        self.path = path
        self.name = name
        self.calls = []
        self._script = {}

    def script(self, method, fn):
        self._script[method] = fn

    def calls_for(self, method):
        return [c for c in self.calls if c[0] == method]

    def __getattr__(self, method):
        if method.startswith("_"):
            raise AttributeError(method)

        def call(*args, **kwargs):
            self.calls.append((method, args, kwargs))
            fn = self._script.get(method)
            if fn is not None:
                return fn(*args, **kwargs)
            return None

        return call


class FakeObject:
    """Stand-in for ``bus.get_object(service, path)``."""

    def __init__(self, path, bus):
        self.path = path
        self.bus = bus
        self.ifaces = {}

    def get_iface(self, name):
        if name not in self.ifaces:
            self.ifaces[name] = FakeIface(self.path, name)
        return self.ifaces[name]


class FakeBus:
    """Records signal receivers; hands out FakeObject proxies."""

    def __init__(self):
        self.objects = {}
        self.signal_receivers = []

    def get_object(self, service, path):
        if path not in self.objects:
            self.objects[path] = FakeObject(path, self)
        return self.objects[path]

    def iface(self, path, name):
        return self.get_object("org.bluez", path).get_iface(name)

    def add_signal_receiver(
        self, handler, dbus_interface=None, signal_name=None, **kwargs
    ):
        self.signal_receivers.append((handler, dbus_interface, signal_name, kwargs))

    def remove_signal_receiver(
        self, handler, dbus_interface=None, signal_name=None, **kwargs
    ):
        for i, (h, di, sn, kw) in enumerate(self.signal_receivers):
            if h is handler:
                del self.signal_receivers[i]
                return True
        return False

    def count_signal_receivers(self, signal_name=None):
        n = 0
        for _h, _di, sn, _kw in self.signal_receivers:
            if signal_name is None or sn == signal_name:
                n += 1
        return n


class FakeServiceModule:
    """Stand-in for ``dbus.service``: Object base + method decorator.

    The ``method`` decorator records the interface/signature metadata on
    the function so tests can assert the exact Agent1/MediaEndpoint1
    method surface.
    """

    class Object(object):
        def __init__(self, bus, path):
            self.bus = bus
            self.path = path

    @staticmethod
    def method(iface, in_signature="", out_signature=""):
        def deco(fn):
            fn._r9_iface = iface
            fn._r9_in = in_signature
            fn._r9_out = out_signature
            return fn

        return deco


class FakeMainContext:
    """GLib main context no-op; subclass to fire queued callbacks."""

    def __init__(self):
        self.iterations = 0

    def iteration(self, may_block=False):
        self.iterations += 1
        return False


_MAIN_CONTEXT = FakeMainContext()


def set_fake_context(ctx):
    global _MAIN_CONTEXT
    _MAIN_CONTEXT = ctx


def reset_fake_glib():
    set_fake_context(FakeMainContext())


class FakeGLib:
    class MainContext:
        @staticmethod
        def default():
            return _MAIN_CONTEXT
