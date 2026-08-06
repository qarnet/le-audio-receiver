# R9 handoff — host central orchestration split

Start commit: `fe5213b` (R8 docs acceptance; worktree clean).  Branch:
`handoff/workstation-transfer`.  This document captures the exact decided
shape before implementation; the implementation must match it and the
results document (`docs/development/refactor-r9-results.md`) must record
what actually happened.

## Goal / invariant

Split `scripts/bap_central.py` (~1770 lines) into four module-owned
components without changing: CLI flags/help/defaults; D-Bus object paths
(`/bap_central/endpoint0`, `/bap_central/agent`); registration / connect /
pair / security flow; LC3 config/payload bytes; 10 ms pacing; successful
teardown tail/order/output; sudo boundary (`sudo -n python3
hci_raw_connect.py ...`); exit codes; or hardware-gate compatibility.
`bap_central_policy.py` and `bap_central_writer.py` remain unchanged
useful boundaries.  `hci_raw_connect.py` behavior unchanged.

**No function owns discovery + security + endpoint + stream + teardown.**
The CLI (`bap_central.py`) remains a thin argparse + dependency-wiring +
main-coordinator; each domain block lives in exactly one new module.

## Error / exit design (resolved)

`sys.exit` inside modules is forbidden (it would skip `finally` cleanup).
One narrow exception type in each module:

```python
class CentralError(Exception):
    """Fatal central-driver error.  The message was ALREADY printed by
    the raising module with its exact `[error]`/`[main]` prefix; the CLI
    catches CentralError and exits 1 without printing anything further.
    Resource cleanup runs first via the finally-registered owner."""
```

Rules:

1. Modules print the exact pre-split message lines, then
   `raise CentralError(reason)`; the CLI catches and `sys.exit(1)` after
   the `finally` owner cleanup.  Exit code 1 and the primary printed
   message are preserved exactly.
2. The CLI keeps only three exit paths: `_import_dbus()` failure
   (`SystemExit(1)` — unchanged, before any resource acquisition),
   `CentralError` catch → `sys.exit(1)`, and normal end → `[main] Exiting`
   + exit 0.  KeyboardInterrupt during the streaming sleep is NOT fatal
   (print `\n[main] Interrupted during streaming`, continue teardown,
   exit 0 — unchanged); KeyboardInterrupt during discovery is fatal
   (raised inside `DiscoverySession` as CentralError after printing
   `\n[main] Interrupted`).
3. On early fatal paths, cleanup diagnostics are emitted ONLY for
   resources actually acquired (owner stage registered only on
   acquisition).  The primary `[error]` line is printed before the
   CentralError; the cleanup tail may add its own `[cleanup] ...` lines
   after it.
4. Successful-path stdout is byte-identical to pre-split (pinned by CLI
   golden tests).
5. Existing pairing/services acceptance policy, Pairable/Trusted
   persistence, second-ASE timing (2 s grace), raw-vs-BlueZ strategy,
   and services-resolved warning (continue anyway) are NOT changed.

## Resource ownership (resolved)

`CentralCleanup` lives in `bap_central.py` (CLI-owned coordinator):

```python
class CentralCleanup:
    """Idempotent ordered cleanup, safe from finally.  Stages are
    registered in a FIXED ORDER (the successful teardown order); a stage
    is registered only when its resource was actually acquired.  run()
    walks the fixed order, executes each registered stage once, and is
    itself idempotent."""
    ORDER = ("discovery", "transports", "writer", "endpoint",
             "agent", "disconnect", "helper")
    def register(self, stage, fn) ...   # no-op for unknown stage
    def run(self) ...                   # executes registered stages in ORDER
```

Fixed order (matches the pre-split successful teardown tail):

1. **discovery** — `DiscoverySession.close()` (StopDiscovery exactly
   once, remove signal match exactly once; normally already closed when
   resolution succeeded — idempotent no-op).
2. **transports** — endpoint `release_transports()`: per transport
   `MediaTransport1.Release()` (+ exact print), then close fd; clear
   `endpoint.transports` and mark every fd closed immediately (single
   owner per fd, no double close/reuse).
3. **writer** — session `StreamSession.stop_writer()`: bounded
   `join(5 s)`; if still alive print
   `[cleanup] SDU writer still alive after 5 s — forcing stop`,
   `stop()`, `join(1 s)`; else the writer-error /
   `[cleanup] Teardown tail: ...` prints (exact pre-split logic).
4. **endpoint** — `unregister_endpoint()`: `[cleanup] Endpoint
   unregistered` / `[cleanup] Endpoint unregister error: {}`.
5. **agent** — `unregister_agent()`: `[cleanup] Agent unregistered` /
   `[cleanup] Agent unregister error: {}`.
6. **disconnect** — registered only AFTER the transport step succeeded
   (raw ready-line gate OR BlueZ Connect confirmed): `Device1.Disconnect()`
   → `[cleanup] ACL link disconnected` + 5 s Connected poll (silent) /
   `[cleanup] Disconnect error: {}`.  Never registered when no link can
   exist (pre-transport failures) — this satisfies "Device Disconnect
   (if Device exists/connected) then helper terminate".
7. **helper** — `RawHciConnect.terminate(verbose=True)`:
   `[cleanup] Raw-HCI helper terminated` / `[cleanup] Raw-HCI helper
   terminate error: {}` (idempotent; error paths inside the module kill
   the helper silently with `terminate(verbose=False)`).

Every fd has exactly one owner: `acquire_transports` closes all
partially-acquired fds itself before raising (all-or-nothing); fully
acquired fds are owned by the transports stage (2).  The writer thread
reads only its snapshot `list(endpoint.transports)` taken at
`StreamSession.start()` — never the live list — so teardown cannot race
a mid-frame read.

## Module APIs (resolved)

### `scripts/bap_central_device.py` — adapter power + device resolution

Stdlib import only; D-Bus/GLib injected late (real modules at runtime,
fakes in tests).  All prints byte-identical to the pre-split flow.

```python
def power_on_adapter(adapter_props_iface, dbus_mod) -> None
    # Adapter1 Powered=True; prints "[main] Adapter powered on";
    # CentralError on failure (message printed).

def peer_device_path(hci_path, peer_addr) -> str
    # "--peer-addr" bypass: returns "{hci_path}/dev_{addr.upper()}",
    # prints "[main] --peer-addr bypass: skipping discovery, target=...".

def find_existing_receiver(om_iface, hci_path) -> (dev_path|None, already_connected)
    # ObjectManager GetManagedObjects; prints "[enum] ..." lines and
    # "[enum] >>> Using existing target: ..."; name filter "LE Audio
    # Receiver"; path prefix hci_path + "/".

class DiscoverySession:
    # Bounded InterfacesAdded discovery; owns the signal match for its
    # lifetime and StopDiscovery exactly once.
    def __init__(self, bus, dbus_mod, GLib, adapter_iface, timeout_s=30.0): ...
    def run(self) -> str          # prints "[main] Discovery started..." +
                                  # "(or press Ctrl-C to abort)" + "[discovery] ..."
                                  # lines; returns dev_path on found.
                                  # Timeout: prints "[error] LE Audio Receiver not
                                  # found within 30 s" -> CentralError.
                                  # KeyboardInterrupt: prints "\n[main] Interrupted"
                                  # -> CentralError.  Calls close() internally on
                                  # every exit path (preserves the post-discovery
                                  # StopDiscovery).
    def close(self) -> None       # idempotent: StopDiscovery once, remove
                                  # signal match once; safe from finally.

def resolve_device(bus, dbus_mod, GLib, adapter_iface, om_iface, hci_path,
                   peer_addr, timeout_s=30.0) -> (dev_path, already_connected)
    # Top-level: peer bypass -> existing enumeration -> discovery.
    # Prints "[main] Target device: ... (already_connected=...)".
```

### `scripts/bap_central_security.py` — agent, pairing, connect strategies

Stdlib import (policy + hci_raw_connect only); dbus via factory.

```python
AGENT_PATH = "/bap_central/agent"          # moved verbatim
RAW_CONNECT_HELPER = ...                   # moved verbatim (hci_raw_connect.py path)

def make_agent_class(dbus_mod, dbus_service_mod) -> cls
    # JustWorksAgent (org.bluez.Agent1, NoInputNoOutput) — method surface
    # and prints moved verbatim (Release/RequestPinCode/RequestPasskey/
    # DisplayPinCode/DisplayPasskey/RequestConfirmation/
    # RequestAuthorization/AuthorizeService/Cancel).

def register_agent(bus, dbus_mod, path=AGENT_PATH) -> (agent, agent_mgr)
    # RegisterAgent(path, "NoInputNoOutput") + RequestDefaultAgent(path);
    # prints "[main] Agent registered at {path}".

def unregister_agent(agent_mgr, path=AGENT_PATH) -> None
    # idempotent; "[cleanup] Agent unregistered" / "[cleanup] Agent
    # unregister error: {}".

def wait_for_helper_ready(out, is_alive, deadline, poll_s=0.05)
    # MOVED VERBATIM from bap_central.py (ready-line gate).  Imports
    # READY_PREFIX from hci_raw_connect (single source of truth).

class RawHciConnect:
    # Fresh --peer-addr transport.  EXACT pre-split argv:
    #   ["sudo", "-n", "python3", RAW_CONNECT_HELPER, addr,
    #    str(hold_secs), "--addr-type", "public", "--peer-addr-type",
    #    "random", "--connect-deadline", "30", "--device", str(hci_dev)]
    #   with hold_secs = duration_s + 120.
    def __init__(self, peer_addr, duration_s, hci_dev, spawn=None, READY_DEADLINE_S=40.0): ...
    def spawn(self) -> None
        # Popen(stdout=PIPE, stderr=PIPE); prints "[main] Creating
        # persistent ACL via raw HCI (hold=...s)...".
    def wait_ready(self) -> None
        # Gate 1: wait_for_helper_ready on the exact pre-split stdout
        # drain/stderr/terminate sequence; prints "[main] Raw HCI link
        # confirmed: {detail}" on success; on failure prints the exact
        # "[error] helper stderr/stdout tail/Raw HCI connect failed"
        # lines, terminates the helper silently, raises CentralError.
    def wait_connected(self, dev_props_iface, GLib, deadline_s=10.0) -> None
        # Gate 2: poll Device1 Connected; prints "[main] Device1
        # Connected confirmed"; on failure prints "[error] Device1 not
        # Connected after confirmed raw HCI link", terminates helper
        # silently, raises CentralError.
    def terminate(self, verbose=False) -> None
        # idempotent; terminate + wait(3 s); verbose prints the
        # "[cleanup] Raw-HCI helper terminated" line (successful tail).

def remove_device(adapter_iface, dev_path, dbus_mod, preserve_bond) -> None
    # Only the fresh path (preserve_bond -> keep record, print
    # "[main] --preserve-bond: keeping existing BlueZ device record").
    # Fresh: RemoveDevice + 0.5 s sleep, prints "[main] Removed stale
    # BlueZ device cache" or "[main] RemoveDevice: no cached device (ok)".

def preserve_bond_connect(device_iface, dev_props_iface, dbus_mod, GLib,
                          already_connected) -> None
    # Policy-driven BlueZ Device1.Connect(): read Paired/Connected (print
    # "[main] Preserve-bond device state: ..."); connection_strategy /
    # "fail" -> print "[error] ..." CentralError; needs_fresh_reconnect ->
    # Disconnect + 10 s Connected poll (print "[main] Device1 already
    # connected; disconnecting and reconnecting fresh so BlueZ configures
    # BAP"); should_connect/needs_fresh_reconnect -> async Connect with
    # 30 s timeout + bounded 35 s wait + connect_outcome ("error"/"timeout"
    # -> exact "[error]" prints + CentralError) + 15 s Connected-property
    # gate ("[error] Device1 Connected not true after Connect()" ->
    # CentralError); success prints "[main] Device1 Connected confirmed
    # (preserve-bond, BlueZ transport)".  Helper is never spawned here.

def recreate_proxies(bus, dbus_mod, dev_path) -> (device_iface, dev_props_iface)
    # After RemoveDevice + raw reconnect: rebuild Device1 + Properties
    # proxies from the live bus (pre-split lines 1179-1188).

def read_device_state(dev_props_iface, dbus_mod) -> (paired, connected)
    # Prints "[main] Device state: Paired={}, Connected={}"; on
    # DBusException prints "[main] Could not read device state: {}" and
    # raises CentralError (owner cleans the helper).

def set_pairable(adapter_props_iface, dbus_mod) -> None
    # Pairable=True + readback; prints "[main] Adapter Pairable={}" or
    # "[main] Pairable set error: {}" (non-fatal, unchanged).

def set_trusted(dev_props_iface, dbus_mod, trust_msg) -> None
    # Trusted=True; prints "[main] {trust_msg}" or "[main] Trust set
    # error: {}" (non-fatal, unchanged).  trust_msg is the exact
    # pre-split string: peer-addr path "Trusted, async pairing over
    # existing ACL...", discovery path "Trusted, async pairing
    # (disconnected \u2192 BlueZ ACL)...".

def pair_device(device_iface, dev_props_iface, dbus_mod, GLib, pair_skip,
                pair_deadline_s=35.0) -> (paired, connected)
    # Async Pair() with reply_handler/error_handler (GLib keeps
    # dispatching Agent1), skip path prints "[main] Pair() skipped
    # (--preserve-bond, bond already present)"; exact "[main] Pair()
    # async reply: OK" / "async completed with error: ..." / "async
    # timed out (35 s)" prints; post-state read prints "[main] After
    # Pair: Paired={}, Connected={}" (DBusException -> "[main] Could not
    # read device state after Pair").  Timeout is NOT fatal (pre-split
    # behavior — pairing acceptance policy unchanged).

def wait_services_resolved(dev_props_iface, dbus_mod, GLib, deadline_s=30.0) -> None
    # Polls ServicesResolved; prints "[main] ServicesResolved (link
    # encrypted)" or "[warn] ServicesResolved not set in 30 s,
    # continuing anyway" (unchanged warning, not tightened).

def disconnect_and_wait(device_iface, dev_props_iface, dbus_mod, GLib,
                        deadline_s=5.0) -> None
    # Cleanup Disconnect: prints "[cleanup] ACL link disconnected" +
    # silent Connected poll, or "[cleanup] Disconnect error: {}".
```

### `scripts/bap_central_endpoint.py` — BAP source endpoint + acquire

Stdlib import; dbus via factory.  Constants moved verbatim.

```python
ENDPOINT_PATH = "/bap_central/endpoint0"
PAC_SOURCE_UUID = "00002bcb-0000-1000-8000-00805f9b34fb"
LC3_CODEC = 0x06
LC3_CAPS / LC3_CAPS_STEREO / LC3_CONFIG_MONO / LC3_CONFIG_STEREO   # byte blobs verbatim
FRAME_BYTES = 120  # imported by session for the encoder default (single home)

def make_endpoint_class(dbus_mod, dbus_service_mod) -> cls
    # BAPSourceEndpoint (org.bluez.MediaEndpoint1) — SelectProperties /
    # SetConfiguration / Release / ClearConfiguration moved verbatim
    # (exact byte blobs, QoS dict, prints, deferred-acquire pending
    # queue, second-ASE queueing, config_done flag).  Release and
    # ClearConfiguration close fds and remove records immediately
    # (no double close); Release never recursively unregisters.

def register_endpoint(media_iface, bus, dbus_mod, endpoint_cls, path,
                      stereo) -> endpoint
    # RegisterEndpoint with UUID/Codec/Capabilities props (LC3_CAPS_STEREO
    # if stereo else LC3_CAPS); prints "[main] BAP source endpoint
    # registered at {path}".

def unregister_endpoint(media_iface, path) -> None
    # idempotent; "[cleanup] Endpoint unregistered" / "[cleanup] Endpoint
    # unregister error: {}".

def acquire_transports(bus, dbus_mod, GLib, endpoint,
                       config_timeout_s=30.0, grace_s=2.0,
                       acquire_timeout_s=35.0) -> (transports, stream_mode, sdu_size)
    # Waits for SetConfiguration (exact "[main] Waiting for
    # SetConfiguration..." + error/hint prints -> CentralError on timeout
    # or empty pending); prints "[main] Pending transports: N" and the 2 s
    # second-ASE grace ("[main] Got 1 transport, waiting 2s for a possible
    # second ASE..."); deferred async Acquire for every pending transport
    # (exact "[main]   async Acquire(...)" / "[main] Acquired: ..." /
    # "[error] Acquire(...) failed: ..." prints, 30 s reply timeout,
    # 35 s bounded wait); all-or-nothing: timeout / partial failure closes
    # every acquired fd (and Release()es them with exact "[error]
    # Release({}) failed:" prints) then CentralError.  On full success:
    # populates endpoint.transports, prints "[main] Total transports: N"
    # + per-transport "[main]   fd=... ch_alloc=... write_mtu=..." lines,
    # infers stream_mode ("stereo_b" ch_alloc==0x03 1 transport /
    # "stereo_a" 2 transports / "mono") + sdu_size, prints "[main] Stream
    # mode: {}, SDU size: {} bytes".  Returns the transports list and the
    # mode/sdu.  fd ownership transfers to the CLI/cleanup on success.

def release_transports(bus, dbus_mod, transports, endpoint) -> None
    # Per transport: MediaTransport1.Release() -> "[cleanup] Transport {}
    # released" or "[cleanup] Transport {} release error: {}"; close fd
    # (silent); clear endpoint.transports; idempotent, single-owner fds.
```

### `scripts/bap_central_session.py` — LC3 source + PacedWriter lifecycle

Stdlib import ONLY (liblc3 loads lazily at first encoder construction,
so stdlib-only module tests and the CLI never need liblc3).  Imports
`bap_central_writer` and `FRAME_BYTES` from the endpoint module.

```python
DT_US = 10000; SR_HZ = 48000; FRAME_SAMPLES = 480; LC3_PCM_FORMAT_S16 = 0

def load_liblc3() -> CDLL
    # _load_liblc3() moved verbatim (find_library -> nix-store path ->
    # glob scan); prints "[main] liblc3 loaded via ..." on first call.

class LC3Encoder:  # moved verbatim; calls load_liblc3() in __init__
def gen_sine(freq, sr_hz, nsamples, amplitude=10000)   # moved verbatim

class StreamSession:
    # Owns encoders, sine PCM, PacedWriter, and the writer teardown tail.
    def __init__(self, transports, stream_mode, freq, duration_s,
                 writer_cls=PacedWriter, encoder_cls=LC3Encoder): ...
    def start(self) -> None
        # Encoder setup per mode (stereo_b/stereo_a: enc_L+enc_R with
        # stereo_a sorting transports by channel_alloc in place; mono:
        # single encoder); sine PCM gen; writer snapshot
        # transports = list(transports); PacedWriter(encode_frame,
        # duration_s).start().
    def encode_frame(self) -> list
        # EXACT per-mode payloads: stereo_b -> enc_L(pcm_L)+enc_R(pcm_R)
        # 240-byte SDU on transports[0].fd; stereo_a -> two 120-byte SDUs
        # on transports[0]/[1] fd (sorted FL,FR); mono -> 120-byte SDU on
        # transports[0].fd.
    def run(self) -> None
        # time.sleep(duration_s) with KeyboardInterrupt ->
        # "\n[main] Interrupted during streaming" (non-fatal); writer
        # error print "[error] SDU writer failed: {}" when set; "[main]
        # Done: {} frames in {:.2f} s ({:.1f} fps)" (exact fps math);
        # captures self._writer_error for stop_writer().
    def stop_writer(self) -> None
        # EXACT bounded join/force tail (see CentralCleanup stage 3);
        # idempotent; uses self._writer_error from run().
```

The `[main] Streaming {:.0f} Hz sine for {} s...` print stays in the CLI
immediately before `stream.start()`.

### `scripts/bap_central.py` — thin CLI coordinator

- `main()` keeps: argparse (six flags verbatim), `hci_path`/`hci_dev`
  computation, `_import_dbus()` (may remain here), system-bus connect
  print, factory invocation, flow orchestration, CentralCleanup
  registration, `try/finally` with `CentralError` catch (exit 1).
- The CLI imports only the four new modules + policy + writer for wiring;
  it owns no domain logic beyond the sequence and cleanup registration.
- `wait_for_helper_ready` and the endpoint/agent class factories are
  REMOVED from bap_central.py (single owners in the new modules).
- `--help` works without dbus/liblc3 (both lazy) — unchanged.

## Behavior deltas (all deliberate, documented)

1. **liblc3 load becomes lazy** — `[main] liblc3 loaded via ...` now
   prints at first `LC3Encoder()` construction (stream start) instead of
   import time.  No consumer parses it; stdlib-only tests/CLI-golden
   require this.  Message/format unchanged.
2. **Early fatal paths now clean up** — pre-split `sys.exit(1)` paths
   leaked the raw helper/ACL/fds/discovery signal on many failures; the
   owner's `finally` cleanup closes exactly what was acquired.  Primary
   `[error]` lines and exit code 1 unchanged; `[cleanup] ...` lines may
   additionally appear after them (allowed: resource was acquired).
3. **`wait_for_helper_ready` imports `READY_PREFIX` from
   `hci_raw_connect.py`** (single source; both files previously defined
   it).  Test import updated to `bap_central_security`.
4. **Adapter Powered failure** converts an uncaught dbus traceback into
   an `[error] ...` print + CentralError (exit 1 preserved; no message
   existed to preserve).

## Tests (tests-first; new four stdlib Python children)

Python children 12 → 16; gate 51 → 55.  No CMake, no testcase.yaml, no
`__pycache__` tracked.  Each suite imports its module via the standard
`sys.path.insert(..., "scripts")` pattern.

1. `tests/unit/bap_central_device/` (new):
   - peer-addr bypass path construction + print;
   - existing-device enumeration filtering (adapter prefix, name, Paired/
     Connected flag) and connected-flag propagation;
   - discovery found (fake InterfacesAdded callback) / timeout (CentralError
     + exact `[error]` print) / KeyboardInterrupt (CentralError + `[main]
     Interrupted` print);
   - `DiscoverySession.close()` idempotence: StopDiscovery exactly once,
     signal match removed exactly once, safe from double-run;
   - `power_on_adapter` success/error.
2. `tests/unit/bap_central_security/` (new):
   - agent class method surface via fake dbus modules (all 9 methods +
     signature decorators) and registration/unregistration prints;
   - `wait_for_helper_ready` early-exit/timeout/ready-line (real pipes —
     moved from the hci_raw_connect suite which keeps its HCI tests);
   - exact raw helper argv (sudo -n python3 helper addr hold --addr-type
     public --peer-addr-type random --connect-deadline 30 --device N) and
     hold = duration+120;
   - fresh RemoveDevice boundary + recreate_proxies after removal;
     preserve_bond keeps record;
   - preserve-bond connect: success / policy fail / Connect error /
     timeout / Connected-not-true / disconnect-first;
   - pair skip / fail / success + state reads; `read_device_state`
     DBusException -> CentralError;
   - helper terminated on EVERY post-spawn failure (gate 1 fail, gate 2
     fail, read-state fail);
   - `wait_services_resolved` success + warning path;
   - `disconnect_and_wait` success/error + cleanup ordering with the
     owner (disconnect-then-helper-terminate).
3. `tests/unit/bap_central_endpoint/` (new):
   - SelectProperties exact byte configs + QoS dicts for mono (FL/FR) and
     stereo (0x03) — byte-for-byte LTV blobs;
   - SetConfiguration LTV channel-allocation parse + pending state;
   - deferred Acquire: async reply fd handling (UnixFd `.take()` / int),
     all-success; partial/error/timeout closes ALL taken fds + Release()
     errors; all-or-nothing;
   - mode inference stereo_b/stereo_a/mono + sdu sizes;
   - Release/ClearConfiguration idempotent, no fd double close, records
     removed immediately; Release never unregisters.
4. `tests/unit/bap_central_session/` (new):
   - golden sine/LC3 output sizes with REAL liblc3 when available
     (`load_liblc3()` try/except -> `skipTest` in stdlib-only runs):
     120-byte mono, 240-byte stereo_b concat, stereo_a two 120-byte
     frames; channel routing;
   - writer duration/interrupt/error via injected fake encoder/writer;
   - successful teardown exact call/order (owner stages);
   - alive-writer force stop (join False -> stop -> join 1 s);
   - tolerated release/unregister/disconnect/helper errors;
   - idempotent cleanup (run twice);
   - raw helper terminated on pre-stream failure through the owner;
   - **CLI golden tests**: all six flags/defaults/help/description,
     path constants (ENDPOINT_PATH, AGENT_PATH, RAW_CONNECT_HELPER),
     parse of `flpr_hang_gate.launch_bap_central(...)` argv shapes
     (stdlib-safe import), `_import_dbus` not required for --help.

Updated suites (assertions preserved):
- `tests/unit/hci_raw_connect/` — import `wait_for_helper_ready` from
  `bap_central_security` (HCI/state-machine tests unchanged).
- `tests/unit/bap_central_policy/`, `tests/unit/bap_central_writer/` —
  unchanged (pass untouched).
- `tests/unit/flpr_hang_gate/` launcher tests — unchanged (launcher argv
  shape proven compatible by the new CLI golden test).
- BZ2/BZ3 suites — untouched and passing.

`python -m py_compile` all five central modules + hci_raw_connect +
policy + writer.  No generated `__pycache__` tracked (gitignored).

## Inventory / gate updates

- `scripts/test-all.sh` header: Python 12 → 16, gate total 51 → 55
  (comment-only; discovery is automatic via `test_inventory.py`).
- `docs/testing/coverage-matrix.md` suite table: Python row 12 → 16 with
  the four new suite names; "Total gate children" 51 → 55.
- `STATUS.md` active R8/R9 claims: gate 51 → 55 (31 twister + 5 exec +
  16 Python + coverage + matrix + BSim), workstation active claims.
- No production C/matrix/coverage-baseline population change (33).
  Matrix hardware references to `bap_central.py` remain valid.

## Hardware plan (R9 live)

Planned non-destructive flash/reset as needed; no mass erase, no
recovery.  Capture unique `/tmp/r9-*` manifest + hashes, dynamic probe
identity (`nrf-probes`), central dongle identity, console BEFORE reset.

- **nRF54L15**: fresh Mode A 30 s and bonded reconnect Mode A 30 s;
  fresh Mode B 30 s (+ bonded Mode B if needed to prove the endpoint
  mode split).  Require ~3000 central frames @100 fps; zero
  decode_err/i2s_underrun/stream_reset; offload submit==success,
  fallback=0.
- **E83 (nRF5340)**: fresh Mode A 30 s + bonded reconnect Mode A/Mode B
  30 s (exercises discovery + preserve-bond paths); zero errors/gaps/
  underruns; APLL ACTIVE evidence where practical.
- At least one `--peer-addr` fresh raw-helper path and one no-peer
  discovery path; preserve-bond BlueZ Connect on both targets.  A short
  `flpr hang gate` accepted row may prove launcher compatibility.
- Reflash only if needed.

## Verification order

1. Commit handoff (this document).
2. Commit tests (four new suites + hci_raw_connect import update) —
   red until implementation lands.
3. Commit implementation (four new modules + thin bap_central.py +
   test-all.sh header + coverage-matrix/STATUS inventory truth).
4. Focused gate: `python3 scripts/test_inventory.py --count` == 55,
   all four new suites + hci_raw_connect + policy + writer +
   flpr_hang_gate launcher + both BlueZ suites + matrix/coverage
   runners; `python -m py_compile` all modules; `git diff --check`.
5. G1 (canonical `./scripts/test-all.sh`), builds 3/3, build contract
   79/79, warnings policy clean.
6. Live hardware rows above; then docs/results commit (this document's
   acceptance + `docs/development/refactor-r9-results.md`).
7. Update `docs/development/refactor-plan.md` R9 section to ACCEPTED.
8. No push/PR/amend/force/attribution.

## Non-scope

Changing security acceptance policy; stricter Pair/ServicesResolved
gates; restoring Pairable/Trusted; replacing the 2 s grace;
`hci_raw_connect.py` refactor; desktop BlueZ gate architecture; firmware
C changes; BSim pins; destructive hardware operations; 360-frame FLPR
offload.
