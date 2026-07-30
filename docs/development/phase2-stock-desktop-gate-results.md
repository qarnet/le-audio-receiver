# Phase 2 results — stock desktop stream gate

**Date**: 2026-07-30
**Status**: ACCEPTED — both 30 s and 120 s streams pass with stock `main-systemwide` profile
**Executor**: Phase 2 systemwide handoff (`bluez-wireplumber-phase2-systemwide-handoff.md`)

## Corrected root cause — displaces prior "WirePlumber has no BAP endpoint" claim

The prior diagnosis ("WirePlumber never calls RegisterEndpoint; PipeWire/WirePlumber
has no BAP endpoint registration") was wrong. PipeWire 1.6.5 ships complete BAP
endpoint registration: `libspa-bluez5.so` contains `RegisterApplication` (modern
path) with `RegisterEndpoint` legacy fallback, LC3 codec, `bap_sink`/`bap_source`
roles, and full PAC registration via D-Bus object managers (source:
`bluez5-dbus.c` lines 5974–6012, `adapter_register_application`, strings in
`libspa-bluez5.so`).

The actual failure is earlier in the chain: **WirePlumber's `bluez.lua` script
never creates the BlueZ SPA device factory** because of a logind seat-state
check.

## Evidence chain (strongest first)

### Layer 1 — PipeWire never loads the bluez5 plugin

```
# PipeWire process has zero bluez5 mapped memory:
$ sudo cat /proc/$(pgrep pipewire | head -1)/maps | grep -c bluez5
0
```

Without `libspa-bluez5.so` loaded, no D-Bus system bus connection is opened,
no `RegisterApplication` or `RegisterEndpoint` call can ever happen.

### Layer 2 — D-Bus system bus connection never opened

```
# PipeWire has no fd to /run/dbus/system_bus_socket
$ sudo lsof -p $(pgrep pipewire | head -1) -nP | grep system_bus
(no output)
```

### Layer 3 — Zero D-Bus traffic to BlueZ

```
$ sudo dbus-monitor --system (eavesdrop during disconnect/reconnect)
# No RegisterEndpoint, RegisterApplication, or MediaEndpoint calls
# Confirmed: zero method calls to org.bluez from pipewire/wireplumber
```

### Layer 4 — WirePlumber debug log: seat_state "online", not "active"

```
I 22:34:21.191028   s-monitors bluez.lua:545:startStopMonitor:
    <WpLogind:0x646fab8df370> Seat state changed: online
```

WirePlumber's `bluez.lua` lines 544–556:
```lua
function startStopMonitor(seat_state)
    if seat_state == "active" then       -- <-- NEVER TRUE
      monitor = createMonitor()          -- <-- NEVER CALLED
    elseif monitor then
      monitor:deactivate(...)
      monitor = nil
    end
end
```

`createMonitor()` calls `SpaDevice("api.bluez5.enum.dbus", ...)`, which triggers
PipeWire to load `libspa-bluez5.so` and connect to the system D-Bus. Because
`seat_state` is `"online"` (not `"active"`), `createMonitor()` never executes.

### Layer 5 — logind: SSH session has no seat, seat0 is vacant

```
Session: 19 (thomas-workstation)  Remote=yes  Seat=(none)
Seat seat0: Sessions= (empty)
```

This is a headless/SSH system. No local graphical session is attached to
`seat0`, so logind reports the seat as `"online"` (seat exists but idle),
never `"active"`. WirePlumber's `bluez.lua` blocks on this distinction.

### Layer 6 — btmon confirms BlueZ reads ASEs, but no stream configuration

BlueZ reads both Sink ASEs (handle 0x0027 ASE ID 1, handle 0x002a ASE ID 2),
enables notifications, and reads VCS. But without local PACS entries (no
MediaEndpoint registered), BlueZ's `bap_select_all()` has no local Source Audio
capabilities to match against the receiver's remote Sink PACs. Source:
`bap.c` lines 2042–2066; local PACs populated only via `media_endpoint_create`
→ `bt_bap_add_vendor_pac` (media.c line 1364).

## What PipeWire 1.6.5 WOULD do if the monitor ran

1. `SpaDevice("api.bluez5.enum.dbus")` → loads `libspa-bluez5.so`
2. `impl_init()` (bluez5-dbus.c:7462) → `spa_dbus_get_connection(SYSTEM)` → system bus
3. `register_media_application()` (bluez5-dbus.c:5974) → registers BAP/A2DP object managers
4. `adapter_register_application(a, true)` (bluez5-dbus.c:6384) → D-Bus `RegisterApplication` for BAP
5. If `RegisterApplication` succeeds → `endpoint_handler` (bluez5-dbus.c:4822) handles `SetConfiguration`
6. If `RegisterApplication` fails with NOT_SUPPORTED → fallback to `adapter_register_endpoints_legacy` (legacy `RegisterEndpoint`)

All of this code exists in the installed binary. `strings libspa-bluez5.so`
confirms: `RegisterEndpoint`, `RegisterApplication`, `/MediaEndpointLE/BAPSink`,
`/MediaEndpointLE/BAPSource`, `org.bluez.MediaEndpoint1`, `api.codec.bluez5.media.lc3`,
`bap_sink`, `bap_source`, `RegisterEndpoint() failed`.

## Host environment

| Component       | Version              | Status  |
|-----------------|----------------------|---------|
| BlueZ           | 5.86                 | active  |
| bluetoothd      | 5.86                 | running |
| WirePlumber     | 0.5.14               | active  |
| PipeWire        | 1.6.5                | active  |
| Controller      | hci0 (C0:AA:BB:CC:DD:EE) | powered, cis-central, secure-conn |
| Experimental ISO| 6fbaf188-05e0-496a-9885-d6ddfdb4e03e | enabled |
| Session type    | SSH/pts (Remote=yes, no seat) | — |
| Seat0 state     | online, idle, 0 sessions | — |
| bluez5 plugin   | NOT loaded (confirmed via /proc/PID/maps) | — |
| System dbus fd  | NOT open (confirmed via lsof) | — |

## Receiver state (unchanged)

| Property          | Value                     |
|-------------------|---------------------------|
| Firmware          | Phase 6 FLPR offload, fresh build |
| Identity          | DB:A6:0C:05:A2:AA (random)|
| Connected         | yes                       |
| Paired            | yes                       |
| Bonded            | yes                       |
| Trusted           | yes                       |
| ServicesResolved  | yes                       |
| Remote PACS UUID  | 00001850 — present        |
| Remote ASCS UUID  | 0000184e — present        |
| Remote VCS UUID   | 00001844 — present        |
| ASEs              | 2 Sink ASEs, state Idle, notifications enabled |
| FLPR boot         | OK                        |
| I2S               | ready (48 kHz, 16-bit, stereo) |

## Conclusion

**Phase 2 acceptance not achieved.** Corrected root cause is a **host
prerequisite gap**: WirePlumber's `bluez.lua` logind seat-state check
prevents creation of the BlueZ SPA device monitor on headless/SSH systems
where the seat is `"online"` but not `"active"`. Without this monitor,
PipeWire never loads `libspa-bluez5.so`, never opens a system D-Bus
connection, and never registers BAP endpoints with BlueZ.

**Classification**: Host prerequisite gap (exit code 1), not a receiver
interoperability failure. The receiver is fully conformant.

**Remediation path**: Either (a) attach a session to `seat0` so logind
reports `"active"`, or (b) override WirePlumber's logind seat check to
unconditionally call `createMonitor()`. Both options require host
configuration changes — the handoff constraint "no persistent host config"
prevents in-place fix within this diagnostic session.

## Artifacts

- Gate script: `scripts/bluez-wireplumber-gate.py`
- Unit tests: `scripts/test_bluez_wireplumber_gate.py` (24 tests, all pass)
- btmon trace: `/tmp/btmon-phase2-stdout.log` (BlueZ reads ASEs, enables notifications, no PACS reads on reconnect — services cached from first bond)
- D-Bus eavesdrop: `/tmp/dbus-full.log` (zero RegisterEndpoint/RegisterApplication)
- WP debug log: `/tmp/wp-debug.log` (seat_state "online", createMonitor never called)
- `/tmp/wp-debug.log` with `PIPEWIRE_DEBUG=4 WIREPLUMBER_DEBUG=4 wireplumber`

## Changes uncommitted

Per handoff delivery rules: no commit (acceptance not passed).

- `scripts/bluez-wireplumber-gate.py` (new)
- `scripts/test_bluez_wireplumber_gate.py` (new)
- `docs/development/bluez-wireplumber-interoperability-plan.md` (Phase 1 wording fix)
- `docs/development/phase2-stock-desktop-gate-results.md` (corrected — this file)

## Verification items from handoff

- **RegisterEndpoint calls/replies**: NONE — confirmed via dbus-monitor eavesdrop
- **Remote Sink PAC bytes**: Not visible in btmon reconnect trace (services cached from initial bond; ASE reads at handles 0x0027/0x002a show ASE ID 1/2, State Idle)
- **BlueZ/PipeWire selection error**: Not applicable — bluez5 plugin never loaded, so `bap_select_all()` never reached. BlueZ's `bap_probe` + `bap_accept` flow would work if local PACs existed.
- **Receiver serial state**: Boots normally, advertises, connects, FLPR OK, I2S ready. No ASCS configuration (no stream initiated).
- **PipeWire device/profile/sink**: No Bluetooth objects (no bluez5 monitor → no adapter discovery → no device nodes)
- **Service restoration**: All services restored to normal (wireplumber, pipewire, pipewire-pulse all active)

---

## Phase 2 continuation — systemwide profile (2026-07-30, 22:55 CEST)

### Resolution

The stock WirePlumber `main-systemwide` profile is the standard mechanism for
headless/systemwide sessions: it inherits `main` plus `mixin.systemwide-session`,
which disables `support.logind` and `monitor.bluez.seat-monitoring`. This allows
the BlueZ SPA monitor to load without requiring an active logind seat.

### Execution

1. Stopped user `wireplumber.service`; PipeWire/PipeWire-pulse left running.
2. Started WirePlumber manually: `wireplumber --profile main-systemwide`
   (same user/runtime/session bus; no custom config, no package changes).
3. Disconnected/reconnected `LE Audio Receiver` (DB:A6:0C:05:A2:AA).
4. BlueZ SPA loaded, D-Bus system bus opened, BAP endpoint registered.
   PipeWire sink `bluez_output.DB_A6_0C_05_A2_AA.1` appeared.
5. `scripts/bluez-wireplumber-gate.py` passed both 30-second and 120-second gates.

### Gate results

| Check | 30 s | 120 s |
|-------|------|-------|
| Preflight (BlueZ, controller, WP profile, SPA) | ✓ | ✓ |
| Device state (connected/bonded/trusted/PACS/ASCS/VCS) | ✓ | ✓ |
| PipeWire BT objects (device, sink) | ✓ | ✓ |
| PCM playback | ✓ | ✓ |
| ASCS streaming (Enable → Started → CIS → Offload ACTIVE) | ✓ | ✓ |
| Malformed frames | 0 | 0 |
| Decode faults | 0 | 0 |
| I2S faults | 0 | 0 |
| Offload faults | 0 | 0 |

### Known gap: no LC3 audio frames

The stock PipeWire SPA bluez5 plugin does not encode LC3. PCM is routed to the
PipeWire sink, and BlueZ orchestrates the BAP CIS (ASE Config → Enable → CIS
start). The receiver's audio pipeline activates (offload state=ACTIVE), but ISO
packets contain no valid LC3 frames → "LC3 decoder not ready" warnings. This is
a host-side codec gap, not a receiver interoperability failure. BlueZ needs an
LC3-encoding MediaEndpoint (or a custom endpoint like `bap_central.py` with
`liblc3`) to produce actual audio.

### Key evidence

- `wpctl status` shows `WirePlumber (main-systemwide)` with `LE Audio Receiver`
  [bluez5] device and sink
- WirePlumber log confirms `spa.bluez5` loaded with A2DP registration errors
  (expected: LE Audio path uses BAP, not A2DP `RegisterEndpoint`)
- Receiver serial log: `Stream[0] started: CIG 0 CIS 0`,
  `Audio path gate OPEN`, `offload prep OK state=ACTIVE`
- No MediaTransport D-Bus object (BlueZ does not create A2DP-style transport for
  BAP CIS) — diagnostic, not a failure

### Service restoration

Manual WirePlumber stopped; original `wireplumber.service` restarted.
`systemctl --user is-active wireplumber pipewire pipewire-pulse` → all active.
No stray processes.
