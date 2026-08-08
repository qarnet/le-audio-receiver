#!/usr/bin/env python3
"""Security/connect orchestration for bap_central: JustWorks agent,
pairing, raw-HCI fresh-connect strategy, and BlueZ preserve-bond connect
strategy.

Stdlib import (bap_central_policy + hci_raw_connect only); D-Bus is
injected late via factories.  Every print is byte-compatible with the
bap_central.py flow.  Fatal paths print their exact message
then raise CentralError; the CLI catches it (exit 1) after the
finally-registered cleanup owner runs (the raw helper is terminated by
the owner on every post-spawn failure).
"""

import os
import select
import subprocess
import sys
import time

import bap_central_policy

from hci_raw_connect import READY_PREFIX  # noqa: E402  (single source)


class CentralError(Exception):
    """Fatal central-driver error (message already printed by the raiser)."""


AGENT_PATH = "/bap_central/agent"

# Raw-HCI direct-connect helper (kernel accept-list scan path is broken on
# the nRF5340 hci_usb controller; see scripts/hci_raw_connect.py).
RAW_CONNECT_HELPER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "hci_raw_connect.py"
)


# ── JustWorks Agent ─────────────────────────────────────────────────────


def make_agent_class(dbus_mod, dbus_service_mod):
    """Return a JustWorksAgent class bound to the given dbus modules."""

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
            "org.bluez.Agent1", in_signature="os", out_signature=""
        )
        def DisplayPinCode(self, device, pincode):
            print("[agent] DisplayPinCode: device={}, pin={}".format(device, pincode))

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="ouq", out_signature=""
        )
        def DisplayPasskey(self, device, passkey, entered):
            print(
                "[agent] DisplayPasskey: device={}, passkey={}, entered={}".format(
                    device, passkey, entered
                )
            )

        @dbus_service_mod.method(
            "org.bluez.Agent1", in_signature="ou", out_signature=""
        )
        def RequestConfirmation(self, device, passkey):
            print(
                "[agent] RequestConfirmation (Just Works): accepting {}, passkey={}".format(
                    device, passkey
                )
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


def register_agent(bus, dbus_mod, agent_cls, path=AGENT_PATH):
    """Register the NINO agent and request it as default (step 1).

    Returns (agent, agent_mgr).
    """
    agent = agent_cls(bus, path)
    agent_mgr = dbus_mod.Interface(
        bus.get_object("org.bluez", "/org/bluez"), "org.bluez.AgentManager1"
    )
    agent_mgr.RegisterAgent(path, "NoInputNoOutput")
    agent_mgr.RequestDefaultAgent(path)
    print("[main] Agent registered at {}".format(path))
    return (agent, agent_mgr)


def unregister_agent(agent_mgr, path=AGENT_PATH):
    """Idempotent agent unregistration (cleanup tail print preserved)."""
    try:
        agent_mgr.UnregisterAgent(path)
        print("[cleanup] Agent unregistered")
    except Exception as e:  # noqa: BLE001
        print("[cleanup] Agent unregister error: {}".format(e))


# ── Raw-HCI helper ready-line gate (moved verbatim) ─────────────────────


def wait_for_helper_ready(out, is_alive, deadline, poll_s=0.05):
    """Wait for the raw-HCI helper's confirmed-connect ready line.

    Reads helper stdout lines until a line starts with READY_PREFIX, the
    helper exits, or the deadline passes.  Returns
    (ok, detail, lines) where lines is the list of helper stdout lines seen.
    """
    lines = []
    buffer = b""
    out_fd = out.fileno()

    while time.monotonic() < deadline:
        if not is_alive():
            return (False, "helper exited before ready", lines)

        timeout = deadline - time.monotonic()
        if timeout <= 0:
            break

        r, _, _ = select.select([out_fd], [], [], min(poll_s, timeout))
        if not r:
            continue

        chunk = os.read(out_fd, 4096)
        if chunk == b"":
            if buffer:
                line = buffer.rstrip(b"\r\n")
                if line:
                    lines.append(line)
                    print(
                        "[helper] {}".format(line.decode(errors="replace")), flush=True
                    )
                    if line.startswith(READY_PREFIX):
                        return (True, line, lines)
            return (False, "helper stdout closed", lines)

        buffer += chunk
        while True:
            idx = buffer.find(b"\n")
            if idx < 0:
                break
            line = buffer[:idx]
            buffer = buffer[idx + 1 :]
            line = line.rstrip(b"\r")
            if line:
                lines.append(line)
                print("[helper] {}".format(line.decode(errors="replace")), flush=True)
                if line.startswith(READY_PREFIX):
                    return (True, line, lines)

    if buffer:
        line = buffer.rstrip(b"\r")
        if line:
            lines.append(line)
            print("[helper] {}".format(line.decode(errors="replace")), flush=True)
            if line.startswith(READY_PREFIX):
                return (True, line, lines)

    return (False, "helper ready-line timeout", lines)


# ── Raw-HCI fresh-connect strategy ──────────────────────────────────────


class RawHciConnect:
    """Owns the raw-HCI helper process for the fresh --peer-addr path.

    Spawns hci_raw_connect.py with the exact argv, waits for the
    machine-readable ready line (gate 1), then the BlueZ Device1 Connected
    property (gate 2).  Every post-spawn failure terminates the helper
    silently and raises CentralError.  ``terminate(verbose=True)`` is the
    idempotent cleanup-tail version (prints the [cleanup] line).
    """

    def __init__(
        self,
        peer_addr,
        duration_s,
        hci_dev,
        spawn=None,
        ready_deadline_s=40.0,
        hold_add_s=120,
    ):
        self.peer_addr = peer_addr
        self.duration_s = duration_s
        self.hci_dev = hci_dev
        self._spawn_fn = spawn
        self._ready_deadline_s = ready_deadline_s
        self._hold_add_s = hold_add_s
        self.proc = None
        self._terminated = False

    def argv(self):
        """Exact helper argv (sudo boundary preserved)."""
        hold_secs = int(self.duration_s) + int(self._hold_add_s)
        return [
            "sudo",
            "-n",
            "python3",
            RAW_CONNECT_HELPER,
            self.peer_addr,
            str(hold_secs),
            "--addr-type",
            "public",
            "--peer-addr-type",
            "random",
            "--connect-deadline",
            "30",
            "--device",
            str(self.hci_dev),
        ]

    def spawn(self):
        """Popen the helper (exact command + print)."""
        print(
            "[main] Creating persistent ACL via raw HCI (hold={:.0f}s)...".format(
                self.duration_s + self._hold_add_s
            )
        )
        if self._spawn_fn is not None:
            self.proc = self._spawn_fn(self.argv())
        else:
            self.proc = subprocess.Popen(
                self.argv(),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

    def wait_ready(self):
        """Gate 1: wait for the confirmed-connect ready line (bounded)."""
        assert self.proc is not None
        raw_connect_stdout = self.proc.stdout
        helper_deadline = time.monotonic() + self._ready_deadline_s
        ready, detail, helper_lines = wait_for_helper_ready(
            raw_connect_stdout,
            lambda: self.proc.poll() is None,
            helper_deadline,
        )
        if not ready:
            # Drain any remaining helper stdout (e.g. an HCI_CONNECT_FAIL
            # line) so the failure reason is not lost.
            if self.proc.poll() is not None:
                try:
                    rest = raw_connect_stdout.read(4096)
                except Exception:  # noqa: BLE001
                    rest = b""
                for rl in rest.split(b"\n"):
                    rl = rl.strip()
                    if rl:
                        helper_lines.append(rl)
                        print(
                            "[helper] {}".format(rl.decode(errors="replace")),
                            flush=True,
                        )
            if self.proc.poll() is not None and self.proc.stderr is not None:
                try:
                    err = self.proc.stderr.read(4096).decode(errors="replace")
                except Exception:  # noqa: BLE001
                    err = ""
                if err:
                    print("[error] helper stderr: {}".format(err[:2000]))
            if helper_lines:
                print("[error] helper stdout tail: {}".format(helper_lines[-3:]))
            print("[error] Raw HCI connect failed: {}".format(detail))
            self.terminate()
            raise CentralError("raw hci connect failed: {}".format(detail)) from None
        print("[main] Raw HCI link confirmed: {}".format(detail))

    def wait_connected(self, dev_props_iface, GLib, dbus_mod, deadline_s=10.0):
        """Gate 2: BlueZ must observe the link (Device1 Connected)."""
        conn_deadline = time.monotonic() + deadline_s
        connected = False
        while time.monotonic() < conn_deadline:
            try:
                if bool(dev_props_iface.Get("org.bluez.Device1", "Connected")):
                    connected = True
                    break
            except dbus_mod.exceptions.DBusException:
                pass
            GLib.MainContext.default().iteration(False)
            time.sleep(0.1)
        if not connected:
            print("[error] Device1 not Connected after confirmed raw HCI link")
            self.terminate()
            raise CentralError("raw hci connected gate failed") from None
        print("[main] Device1 Connected confirmed")

    def terminate(self, verbose=False):
        """Idempotent helper termination with a guaranteed-reaped guarantee.

        SIGTERM + bounded wait; on timeout escalate to SIGKILL + bounded
        wait.  No SIGKILL/host-loss cleanup is ever claimed: the [cleanup]
        line is printed only when the process was actually reaped, and a
        process that still cannot be reaped surfaces the failure on stderr
        regardless of verbose.
        """
        if self._terminated:
            return
        self._terminated = True
        if self.proc is None:
            return
        try:
            try:
                self.proc.terminate()
            except ProcessLookupError:
                # Already exited and reaped — nothing left to terminate.
                pass
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                # SIGTERM did not take: escalate, then wait again.
                try:
                    self.proc.kill()
                except ProcessLookupError:
                    pass
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    print(
                        "[error] Raw-HCI helper did not exit after SIGKILL — "
                        "helper may survive",
                        file=sys.stderr,
                        flush=True,
                    )
                    return
        except Exception as e:  # noqa: BLE001
            # Unreapable or unexpected failure: surface it.
            print(
                "[error] Raw-HCI helper termination failed: {}".format(e),
                file=sys.stderr,
                flush=True,
            )
            return
        if verbose:
            print("[cleanup] Raw-HCI helper terminated")


# ── RemoveDevice / proxy recreation ─────────────────────────────────────


def remove_device(adapter_iface, dev_path, dbus_mod, preserve_bond):
    """Clear the stale BlueZ device cache — fresh path only.

    --preserve-bond keeps the Device1 record (the bond must survive);
    fresh mode RemoveDevice + settle.  Prints are exact.
    """
    if preserve_bond:
        print("[main] --preserve-bond: keeping existing BlueZ device record")
        return
    try:
        adapter_iface.RemoveDevice(dev_path)
        print("[main] Removed stale BlueZ device cache")
        time.sleep(0.5)
    except dbus_mod.exceptions.DBusException:
        # No cached device — expected on first run.
        print("[main] RemoveDevice: no cached device (ok)")


def recreate_proxies(bus, dbus_mod, dev_path):
    """Rebuild Device1 + Properties proxies from the live bus after
    RemoveDevice + raw HCI reconnect."""
    device = dbus_mod.Interface(
        bus.get_object("org.bluez", dev_path), "org.bluez.Device1"
    )
    dev_props = dbus_mod.Interface(
        bus.get_object("org.bluez", dev_path),
        "org.freedesktop.DBus.Properties",
    )
    return (device, dev_props)


# ── Preserve-bond BlueZ Connect strategy ────────────────────────────────


def preserve_bond_connect(
    device_iface,
    dev_props_iface,
    dbus_mod,
    GLib,
    already_connected,
    connect_timeout_ms=30000,
    wait_s=35.0,
    connected_deadline_s=15.0,
    disc_deadline_s=10.0,
):
    """BlueZ Device1.Connect() transport for --preserve-bond (paired).

    Policy-driven (bap_central_policy): strategy fail / disconnect-first
    on a stale BlueZ connection / async Connect with bounded wait /
    Connected-property gate.  Prints and acceptance policy are
    exact; every fatal path prints then raises CentralError.  The raw-HCI
    helper is NEVER launched here (BlueZ owns the initiator).
    """
    try:
        dev_paired = bool(dev_props_iface.Get("org.bluez.Device1", "Paired"))
        dev_connected = bool(dev_props_iface.Get("org.bluez.Device1", "Connected"))
    except dbus_mod.exceptions.DBusException as e:
        print("[error] Could not read Device1 state: {}".format(e))
        raise CentralError("preserve-bond state read failed: {}".format(e)) from e
    print(
        "[main] Preserve-bond device state: Paired={}, Connected={}".format(
            dev_paired, dev_connected
        )
    )

    strategy, strategy_detail = bap_central_policy.connection_strategy(True, dev_paired)
    if strategy == "fail":
        print("[error] {}".format(strategy_detail))
        raise CentralError("preserve-bond strategy fail: {}".format(strategy_detail))
    print("[main] {}".format(strategy_detail))

    if bap_central_policy.needs_fresh_reconnect(dev_connected, strategy):
        # Already connected (e.g. BlueZ auto-connected a trusted paired
        # device): BlueZ runs BAP auto-configuration only for a connection
        # it freshly establishes, so tear this stale ACL down first.
        print(
            "[main] Device1 already connected; disconnecting and "
            "reconnecting fresh so BlueZ configures BAP"
        )
        try:
            device_iface.Disconnect()
        except dbus_mod.exceptions.DBusException as e:
            print("[main]   Disconnect ignored: {}".format(e))
        disc_deadline = time.monotonic() + disc_deadline_s
        while time.monotonic() < disc_deadline:
            try:
                if not bool(dev_props_iface.Get("org.bluez.Device1", "Connected")):
                    break
            except dbus_mod.exceptions.DBusException:
                break
            GLib.MainContext.default().iteration(False)
            time.sleep(0.1)

    # Fresh mode (raw_hci strategy) needs no BlueZ Connect() — the
    # raw-HCI helper creates the ACL.
    if bap_central_policy.should_connect(dev_connected, strategy) or (
        bap_central_policy.needs_fresh_reconnect(dev_connected, strategy)
    ):
        conn_ok = [False]
        conn_err = [None]

        def _on_connect_ok():
            conn_ok[0] = True
            print("[main] Device1.Connect() async reply: OK")

        def _on_connect_err(error):
            conn_err[0] = error
            print("[main] Device1.Connect() async error: {}".format(error))

        device_iface.Connect(
            reply_handler=_on_connect_ok,
            error_handler=_on_connect_err,
            timeout=connect_timeout_ms,
        )
        conn_deadline = time.monotonic() + wait_s
        while (
            not conn_ok[0] and conn_err[0] is None and time.monotonic() < conn_deadline
        ):
            GLib.MainContext.default().iteration(False)
            time.sleep(0.05)
        outcome = bap_central_policy.connect_outcome(conn_ok[0], conn_err[0])
        if outcome == "error":
            print("[error] Device1.Connect() failed: {}".format(conn_err[0]))
            raise CentralError("preserve-bond connect failed: {}".format(conn_err[0]))
        if outcome == "timeout":
            print("[error] Device1.Connect() timed out (35 s)")
            raise CentralError("preserve-bond connect timed out") from None

        # Wait for the exact Device1 Connected property (bounded).
        cdeadline = time.monotonic() + connected_deadline_s
        connected_pb = False
        while time.monotonic() < cdeadline:
            try:
                if bool(dev_props_iface.Get("org.bluez.Device1", "Connected")):
                    connected_pb = True
                    break
            except dbus_mod.exceptions.DBusException:
                pass
            GLib.MainContext.default().iteration(False)
            time.sleep(0.1)
        if not connected_pb:
            print("[error] Device1 Connected not true after Connect()")
            raise CentralError("preserve-bond connected gate failed") from None
        print("[main] Device1 Connected confirmed (preserve-bond, BlueZ transport)")


# ── State reads / Pairable / Trusted / Pair / Services ──────────────────


def read_device_state(dev_props_iface, dbus_mod):
    """Read fresh Paired/Connected.  Fatal on D-Bus error (owner cleans
    the helper via finally)."""
    try:
        dev_paired = bool(dev_props_iface.Get("org.bluez.Device1", "Paired"))
        dev_connected = bool(dev_props_iface.Get("org.bluez.Device1", "Connected"))
    except dbus_mod.exceptions.DBusException as e:
        print("[main] Could not read device state: {}".format(e))
        raise CentralError("device state read failed: {}".format(e)) from e
    print(
        "[main] Device state: Paired={}, Connected={}".format(dev_paired, dev_connected)
    )
    return (dev_paired, dev_connected)


def set_pairable(adapter_props_iface, dbus_mod):
    """Set Adapter1 Pairable so bonding can proceed (non-fatal)."""
    try:
        adapter_props_iface.Set(
            "org.bluez.Adapter1", "Pairable", dbus_mod.Boolean(True)
        )
        pairable = bool(adapter_props_iface.Get("org.bluez.Adapter1", "Pairable"))
        print("[main] Adapter Pairable={}".format(pairable))
    except dbus_mod.exceptions.DBusException as e:
        print("[main] Pairable set error: {}".format(e))


def set_trusted(dev_props_iface, dbus_mod, trust_msg):
    """Trust the device (non-fatal).  trust_msg is the exact
    post-Trust print for the active pairing path."""
    try:
        dev_props_iface.Set("org.bluez.Device1", "Trusted", dbus_mod.Boolean(True))
        print("[main] {}".format(trust_msg))
    except dbus_mod.exceptions.DBusException as e:
        print("[main] Trust set error: {}".format(e))


def pair_device(
    device_iface, dev_props_iface, dbus_mod, GLib, pair_skip, pair_deadline_s=35.0
):
    """Async Pair() over the existing link (GLib keeps dispatching the
    Agent1 callbacks).  Pair skip/fail/timeout acceptance policy is the
    behavior: NONE of these are fatal.  Returns (paired,
    connected) read back after pairing."""
    pair_result = [None]
    pair_error = [None]
    pair_done = [False]

    def _on_pair_ok():
        pair_result[0] = True
        pair_done[0] = True
        print("[main] Pair() async reply: OK")

    def _on_pair_err(error):
        pair_error[0] = error
        pair_done[0] = True
        print("[main] Pair() async error: {}".format(error))

    if pair_skip:
        pair_result[0] = True
        pair_done[0] = True
        print("[main] Pair() skipped (--preserve-bond, bond already present)")
    else:
        device_iface.Pair(
            reply_handler=_on_pair_ok,
            error_handler=_on_pair_err,
            timeout=30000,
        )
    # Iterate GLib: Agent1 RequestAuthorization dispatches here.
    pair_deadline = time.monotonic() + pair_deadline_s
    while not pair_done[0] and time.monotonic() < pair_deadline:
        GLib.MainContext.default().iteration(False)
        time.sleep(0.05)

    if pair_error[0] is not None:
        print("[main] Pair() async completed with error: {}".format(pair_error[0]))
    elif pair_result[0]:
        print("[main] Pair() async completed OK")
    else:
        print("[main] Pair() async timed out (35 s)")

    # 5e. Check resulting state.
    paired = False
    connected2 = False
    try:
        paired = bool(dev_props_iface.Get("org.bluez.Device1", "Paired"))
        connected2 = bool(dev_props_iface.Get("org.bluez.Device1", "Connected"))
        print("[main] After Pair: Paired={}, Connected={}".format(paired, connected2))
    except dbus_mod.exceptions.DBusException:
        print("[main] Could not read device state after Pair")
    return (paired, connected2)


def wait_services_resolved(dev_props_iface, dbus_mod, GLib, deadline_s=30.0):
    """Bounded ServicesResolved wait.  Pre-split acceptance: NOT set is a
    warning, flow continues (never tightened)."""
    sr_deadline = time.monotonic() + deadline_s
    services_resolved = False
    while time.monotonic() < sr_deadline:
        try:
            if bool(dev_props_iface.Get("org.bluez.Device1", "ServicesResolved")):
                services_resolved = True
                break
        except dbus_mod.exceptions.DBusException:
            pass
        GLib.MainContext.default().iteration(False)
        time.sleep(0.1)

    if services_resolved:
        print("[main] ServicesResolved (link encrypted)")
    else:
        print("[warn] ServicesResolved not set in 30 s, continuing anyway")


def disconnect_and_wait(device_iface, dev_props_iface, dbus_mod, GLib, deadline_s=5.0):
    """Cleanup Disconnect: graceful BlueZ HCI disconnect + bounded
    Connected poll.  Tolerates every error (cleanup tail prints)."""
    try:
        device_iface.Disconnect()
        print("[cleanup] ACL link disconnected")
        disc_deadline = time.monotonic() + deadline_s
        while time.monotonic() < disc_deadline:
            try:
                if not bool(dev_props_iface.Get("org.bluez.Device1", "Connected")):
                    break
            except dbus_mod.exceptions.DBusException:
                break
            GLib.MainContext.default().iteration(False)
            time.sleep(0.1)
    except Exception as e:  # noqa: BLE001
        print("[cleanup] Disconnect error: {}".format(e))
