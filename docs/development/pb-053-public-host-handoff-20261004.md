# PB-053: valid public host behavior checkpoint

## Goal and boundaries

Extend proved private guest readiness with real valid endpoint registration,
LE advertising/discovery and Just Works pairing. This is not full PB-053
acceptance: ISO payload transfer, retained/fresh daemon restart and cancellation
remain subsequent checkpoints. No PB-051 input-validation cases are included.

Files: `scripts/bluez_host_guest.py`, `scripts/bluez_guest_init.py`, new
`scripts/bluez_guest_public.py`, focused tests under `tests/unit/bluez_host_guest/`,
and PB-053 execution notes. Preserve other files and evidence. No commits/push
until reviewed clean-candidate handoff. Never use host bus/controllers/devices.

## Verified grounding

Private guest r4 passed kernel7.1.5, AF_ALG AES/CMAC, two Adapter1 objects and
ISO socket checks. Raw evidence `/tmp/opencode/pb053-guest-ready-r4/serial.log`.
No unresolved warning in r4. Initial ServiceUnknown during bounded daemon startup
is retained and followed by actual successful public readiness reply.

Installed Python imports were checked successfully by Delegator:

- dbus root `/nix/store/mvvgdnjagabhvzqqwrv22dhm6b0gymkf-python3.13-dbus-python-1.4.0`.
- gi root `/nix/store/r1ii8vh2n6r4kkz9piy394p2r405820q-python3.13-pygobject-3.54.5`.
- GLib typelib root `/nix/store/bz34hjmhv4fvqxk6wkyxj1hc8mpwgy3w-glib-2.86.3`.
- `dbus.mainloop.glib.threads_init` exists. These three exact roots need local
  store-requisite staging plus Python/typelib paths, not host environment export.
- Their additional systemd root
  `/nix/store/64qjwn4wvfnlcm5ja238m6i3mrk2q076-systemd-minimal-258.7`
  has same `lib/environment.d/99-environment.conf` literal link
  `../../../../../etc/environment`. Add only this exact origin to prior authored
  empty guest environment allowance, never copy host target.
- Pinned BlueZ `profiles/audio/a2dp.c:3789` calls `media_register(adapter)`;
  `media.c:3736` exposes Media1. BAP plugin alone does not expose Media1.
  Use explicit `-p bap,a2dp` to support LE endpoint registration.
- `doc/org.bluez.Media.rst:25-69` defines RegisterEndpoint UUID/Codec/Capabilities;
  sender disconnect unregisters endpoints. No copied GPL test implementation.
- Repo `bap_central_endpoint.py:319-351` contains verified valid LC3/QoS property
  types and values. Reference that shape without modifying production driver.
- BlueZ functional test source demonstrates valid LE advertising/discovery then
  Just Works pairing. Its malformed-input tests are not part of this work.

## Exact steps

1. Add three verified binding/typelib roots to host preparation closure. Add the
   exact third systemd link origin to guest-authored `/etc/environment` allowlist.
   Copy new public script into `/opt/pb053/public.py`; record its source hash.
   Runtime JSON includes exact binding site-packages paths and typelib directory.
   Guest constructs `PYTHONPATH` from these two explicit binding directories and
   `GI_TYPELIB_PATH` from exact typelib directory. No inherited host env paths.
   Add Python executable path to runtime for launching public child.
2. Change daemon plugin list to `bap,a2dp`, retaining all other checks. After ISO
   readiness, run Python `-u /opt/pb053/public.py` with private bus env and 90 s
   timeout. Retain stdout/stderr through existing command ledger. Require child
   exit0 and exactly one `PB053_PUBLIC_RESULT` JSON record with all three named
   cases passed. Any failure propagates to guest ok=false. Host readiness scope
   label must identify this checkpoint, not full lifecycle acceptance.
3. New public script refuses execution unless `/proc/cmdline` contains exact
   `pb053_guest=1` token and runtime file exists. Guard before imports of dbus/gi
   or bus access. Stdlib-only import has no effects.
4. Initialize `DBusGMainLoop(set_as_default=True)`, `threads_init()`, create
   `dbus.SystemBus`, start owned `GLib.MainLoop` in a daemon thread for incoming
   callbacks. Main thread performs bounded synchronous public calls with
   `timeout=10`. `GetManagedObjects` determines exactly two Adapter1 paths and
   distinct Addresses; sort guest paths only, never select a host adapter.
5. Export one Just Works Agent1 at `/pb053/agent`: Release, Cancel,
   RequestAuthorization(device), RequestConfirmation(device,uint32),
   AuthorizeService(device,uuid) return normally. RegisterAgent with capability
   `NoInputNoOutput`, RequestDefaultAgent. Do not invent PIN/passkey fallback.
6. Export local source MediaEndpoint1 `/pb053/source` on first adapter and sink
   `/pb053/sink` on second. RegisterEndpoint with corresponding PAC UUIDs
   `00002bcb-0000-1000-8000-00805f9b34fb` and
   `00002bc9-0000-1000-8000-00805f9b34fb`; Codec dbus.Byte(6),
   Capabilities bytes `03 01 80 00 02 02 02 02 03 01 05 04 78 00 78 00`
   (48kHz, 10ms, one channel, 120-byte frame), Locations UInt32(1),
   Context UInt16(4), SupportedContext UInt16(4). Include metadata
   `03 01 04 00` if registration requires context advertisement.
   Endpoint callbacks Release(), ClearConfiguration(o), SetConfiguration(o,a{sv})
   retain public path/properties ledger and return normally.
   SelectProperties(a{sv})->a{sv} returns valid 48kHz/10ms/120-byte mono config
   `02 01 08 02 02 01 03 04 78 00 05 03 01 00 00 00` plus QoS typed as in
   current driver: Framing byte0, PHY byte2, Interval uint32 10000, SDU uint16
   120, Retransmissions byte5, Latency uint16 20, PresentationDelay uint32 40000,
   TargetLatency byte2. No transport Acquire or PCM claim in this checkpoint.
   Verify registration returns successfully and public SupportedUUIDs contain
   respective roles; log actual public values. Record endpoint-registration case.
7. Set Powered and Pairable true on both adapters via Properties.Set. Export
   LEAdvertisement1 `/pb053/advertisement` on second adapter with Properties.GetAll
   returning Type='peripheral', ServiceUUIDs=['00001850-0000-1000-8000-00805f9b34fb'],
   LocalName='PB053 sink'; Release returns normally. RegisterAdvertisement.
   First adapter SetDiscoveryFilter Transport='le', then StartDiscovery.
   Poll GetManagedObjects <=20 s for Device1 under first adapter whose Address
   equals freshly read second adapter address. Require Discovering true before
   StopDiscovery, then false after it. Record discovered device/address and case.
8. Call Device1.Pair on discovered peer; poll <=20 s for Paired true. Require
   reciprocal peer Device1 under second adapter with first adapter Address and
   Paired true. Record actual property snapshots and pairing case. No private
   filesystem content substitutes for public pairing.
9. Finally unregister advertisement/endpoints/agent owned by this child, disconnect
   paired peer if connected, close bus, quit loop and join <=5 s. Retain cleanup
   outcomes. Unexpected cleanup error fails result, not suppressed. Expected
   registration flags track only successfully registered resources. Print one
   terminal JSON marker with case IDs/results and diagnostic error; exit1 if any
   case or cleanup failed. Existing guest owner terminates daemon/emulator/bus.

## Tests and real evidence

Add public-process direct-host invocation refusal test for new script. Test new
terminal case-result parser rejecting missing, duplicate, failure and unknown
case IDs, plus single complete case ledger. Run existing focused tests and real
new preparation/guest roots r5. Retain all prior evidence. On first unexplained
warning, protocol failure, or missing load-bearing decision, stop and return raw
log and exact question. Do not try unplanned alternatives or suppress warnings.

```sh
python3 -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r5
python3 scripts/bluez_host_guest.py run --prepared /tmp/opencode/pb053-guest-prep-r5 --output /tmp/opencode/pb053-guest-public-r5 --timeout 240
git diff --check
```

Use fresh suffix if occupied. No PB-053 Done/criteria check from this partial
checkpoint. Record real outcome with Backlog.md. No host adapter/bus access,
passthrough, downloads, SDK/vendor changes, PB-051 inputs, commits or push yet.
