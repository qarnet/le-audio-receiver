# Linux LE Audio host setup

This page describes how a Linux machine is set up and verified as an LE Audio
**source** for this receiver: a BAP unicast transmitter streaming LC3 audio
through BlueZ + PipeWire. It is the practical companion to the [Bluetooth
adapter support and evaluation](bluetooth-adapter-evaluation.md) page (which
adapter to use, and how new adapters get accepted) and the [Supported LE Audio
sources on Linux](supported-sources.md) overview/source matrix.

The path described here is the **native Linux HCI path**: Linux owns the
Bluetooth stack, BlueZ implements BAP and the ISO transport, and PipeWire
(with the WirePlumber session manager) encodes LC3 and drives the stream. The
receiver itself accepts **LC3 at 48 kHz** only (see [Known
limitations](known-limitations.md)).

## Required setup and verification

### Required and recommended versions

| Component | Requirement | Project-tested baseline |
|---|---|---|
| Linux kernel | **6.4 minimum**; newer stable preferred (ongoing ISO fixes) | 7.1.5 |
| BlueZ | **5.85 minimum** — first stable release containing the PAC callback fix ([commit `6b0a087`](https://github.com/bluez/bluez/commit/6b0a08776ae44a9102d7c6875a77e83dc6a11a37)) | 5.86 |
| PipeWire | current release, built with the BlueZ SPA and LC3 support | 1.6.6 |
| WirePlumber | current release | 0.5.14 |

Version notes:

- **PipeWire:** the historical BAP floor (0.3.59) is context, not a
  recommendation — use a current release. A version number alone does not
  prove LC3 support (see [Checking PipeWire for LC3](#checking-pipewire-for-lc3)).
- **BlueZ:** building or patching older BlueZ releases yourself is not
  recommended; use 5.85 or later.
- These are the *protocol floor*. A system that satisfies every number can
  still fail in practice — see [Protocol floor vs. project
  baseline](#protocol-floor-vs-project-baseline).

### BlueZ configuration

Enable the BlueZ experimental features. Prefer the surgical ISO-only form in
`/etc/bluetooth/main.conf`:

```ini
[General]
Experimental = true
KernelExperimental = 6fbaf188-05e0-496a-9885-d6ddfdb4e03e
```

`KernelExperimental` lists the kernel experimental features to enable; the
value above is the UUID of the kernel ISO socket feature. This is narrower than
turning on every kernel experimental feature.

The same configuration can be applied at daemon start with the equivalent
flags:

```console
bluetoothd -E -K 6fbaf188-05e0-496a-9885-d6ddfdb4e03e
```

Either is sufficient; using both is redundant — they configure the same
experimental features. After changing
the configuration, restart the daemon (`systemctl restart bluetooth`).

### PipeWire and WirePlumber

**PipeWire** must provide:

- the **BlueZ SPA plugin** (part of PipeWire, not BlueZ), which implements the
  BAP endpoints and the BlueZ device monitor;
- **LC3 support**, enabled at build time with
  `-Dbluez5-codec-lc3=enabled`, with `liblc3` available during the build.

**WirePlumber** must:

- load the BlueZ monitor (`monitor.bluez`), which is enabled by default; and
- include the **`bap_source` role** in `bluez5.roles`. The current default
  includes it; a custom role list may remove it.

Optional explicit role configuration, to guarantee `bap_source` is present
(a fragment under `~/.config/wireplumber/wireplumber.conf.d/`, then restart the
WirePlumber user session):

```
monitor.bluez.properties = {
  bluez5.roles = [ a2dp_sink a2dp_source bap_sink bap_source hfp_hf hfp_ag ]
}
```

**Session ownership:** PipeWire and WirePlumber must be active in the current
logind user session. WirePlumber's BlueZ monitor only creates device and node
objects for the active logind session (seat monitoring), so an
inactive-session or headless setup needs attention. No separate PulseAudio
daemon may own the Bluetooth audio — `pipewire-pulse` is fine. rtkit is
**recommended** (real-time scheduling), not protocol-mandatory.

### Verification procedure

Run these checks in order. Static checks are necessary but **not sufficient**:
the final proof is an end-to-end stream (see below).

| Check | Command | Pass interpretation |
|---|---|---|
| Kernel version | `uname -r` | 6.4 or newer; newer stable preferred |
| BlueZ version | `bluetoothd --version` | 5.85 or newer |
| PipeWire version | `pipewire --version` | current release (project baseline 1.6.6); version alone does not prove LC3 |
| WirePlumber version | `wireplumber --version` | current release (project baseline 0.5.14) |
| BlueZ service | `systemctl is-active bluetooth` | `active` |
| Audio session | `systemctl --user is-active pipewire wireplumber` | both `active` in the current logind user session |
| BlueZ experimental | `bluetoothctl show` | exposes `ExperimentalFeatures: BlueZ Experimental ISO ...` (plus the kernel ISO feature from `KernelExperimental`) |
| Controller capabilities | `btmgmt --index hci0 info` | `le` and `cis-central` present in supported settings — **necessary, not sufficient** |

Fail interpretations:

- `bluetoothctl show` missing the experimental features line: the BlueZ
  experimental configuration is not effective — the daemon was not restarted,
  the config file was not read, or the equivalent flags were not used.
- `btmgmt` missing `cis-central`: the controller cannot act as an ISO central —
  stop here; the adapter cannot be accepted (see [Bluetooth adapter support and
  evaluation](bluetooth-adapter-evaluation.md)).

#### Checking WirePlumber for `bap_source`

Inspect the effective WirePlumber configuration — system directories such as
`/etc/wireplumber/` plus user overrides in `~/.config/wireplumber/`, including
fragments under `wireplumber.conf.d/` — for
`monitor.bluez.properties` → `bluez5.roles`. The default includes
`bap_source`; only a custom role list can remove it. WirePlumber's
documentation lists the supported roles and defaults.

#### Checking PipeWire for LC3

There is no one universal distro command. Consult your distro's package and
build metadata: PipeWire must be built with the BlueZ SPA and with
`-Dbluez5-codec-lc3=enabled`, with `liblc3` available at build time. The
authoritative runtime evidence is the negotiated BAP endpoint/profile once a
session is active — PipeWire's `pw-cli`, `pw-dump`, or WirePlumber's `wpctl`
can list nodes and endpoints. Package/build metadata and the runtime BAP
endpoint are authoritative, not a static version string.

#### Final proof

The only acceptance is an end-to-end stream: a BAP unicast session carrying
LC3 audio to the receiver, sustained, with clean logs. Static capability
output never proves this. The dynamic acceptance procedure lives on the
[Bluetooth adapter support and evaluation](bluetooth-adapter-evaluation.md)
page.

#### Headless production-path verification (no GUI required)

The same production data path can be verified headless. KDE and the command
line drive the same components:

- Bluetooth UI pair/connect = BlueZ D-Bus (`bluetoothctl` is a CLI client for
  the same API).
- KDE audio output selection = WirePlumber/PipeWire default-target selection
  (`wpctl`).
- Audio playback = an ordinary PipeWire client (`pw-play`).

So a GUI is not required to test the production data path. The safe workflow
below uses placeholders — substitute the peer's addresses and node names:

1. Ensure one intended HCI adapter owns the peer; disconnect/power down
   competing adapters for test isolation.
2. Ensure the receiver is paired/trusted/connected with `bluetoothctl` and
   verify PACS/ASCS UUIDs.
3. On a true headless host, disable WirePlumber BlueZ seat monitoring through
   supported configuration, or use an isolated temporary `main-embedded`
   profile. Running two WirePlumber instances concurrently is invalid: stop
   the normal user service before starting the temporary instance and restore
   it afterward.
4. Verify `wpctl status --name` contains `bluez_card.<peer>` and
   `bluez_output.<peer>.*`.
5. Select the sink with `wpctl set-default <sink-id>` or target it directly
   with `pw-play --target <node-name> <48-kHz-test-file>`.
6. Capture `btmon`, WirePlumber journal/output, PipeWire graph, and receiver
   serial concurrently.
7. Pass only with the expected BAP/LC3/QoS, sustained ISO, reconnect/cold
   repeat, and no unexplained warnings, malformed SDUs, PLC, decode errors,
   underruns, or teardown failures.

`main-embedded` is a diagnostic profile, not a permanent desktop
recommendation — prefer supported seat-monitoring configuration for
day-to-day desktop use. Concrete worked evidence for this workflow (including
the failure items still under investigation) is recorded in the [BT540
headless production-stack results](development/bt540-headless-pipewire-results.md).

### Project-tested baseline

This project validated the native HCI path end-to-end with the **Intel Wi-Fi
6E AX210** (see [Bluetooth adapter support and
evaluation](bluetooth-adapter-evaluation.md)) on a host running exactly:

- Linux kernel **7.1.5**
- BlueZ **5.86**
- PipeWire **1.6.6**
- WirePlumber **0.5.14**
- BlueZ configured with `Experimental = true` and
  `KernelExperimental = 6fbaf188-05e0-496a-9885-d6ddfdb4e03e` in
  `/etc/bluetooth/main.conf` (the daemon runs without explicit `-E`/`-K`
  flags)
- PipeWire and WirePlumber enabled in the logind user session, PulseAudio
  disabled, rtkit enabled

**Tested-baseline disclosure:** that BlueZ 5.86 build carried a small
downstream patch to `profiles/audio/bap.c` fixing a QoS-property spelling
(`MimimumDelay` → `MinimumDelay`, `PreferredMimimumDelay` →
`PreferredMinimumDelay`). This is an exact disclosure about the host we
tested, not a requirement for every distribution: the typo was fixed upstream
on 2026-05-07 by commit
[`d45fd43a1cc3ba791858f11c144112c518a9ad84`](https://github.com/bluez/bluez/commit/d45fd43a1cc3ba791858f11c144112c518a9ad84),
and current BlueZ master has the correct keys.

## Detailed host requirements

### Kernel

- Bluetooth LE support and the Management API.
- `AF_BLUETOOTH` sockets with `SOCK_SEQPACKET` and `BTPROTO_ISO` (the ISO
  socket used for isochronous data).
- A matching transport driver for the adapter (USB, UART, PCIe, SDIO, or
  integrated).
- ISO packet flow control (credits, Number of Completed Packets).
- There is **no separate mainline `CONFIG_BT_ISO` symbol**; ISO support comes
  with the normal Bluetooth configuration on current kernels.

### BlueZ

- BAP, PACS, and ASCS support (the GATT services and roles for LE Audio
  unicast).
- `MediaEndpoint1` / `SelectProperties` (the D-Bus media endpoint API PipeWire
  uses to negotiate codec and QoS).
- The experimental D-Bus and kernel ISO socket features (see [BlueZ
  configuration](#bluez-configuration)).

### PipeWire

- Software LC3 encoding for the BAP source role.
- The BlueZ SPA plugin (BAP endpoints + device monitor).
- The audio graph that routes application audio to the BAP source node.

### WirePlumber

- Loads the BlueZ monitor and manages the resulting devices and nodes.
- Owns the Bluetooth device only for the **active logind session**.

### Peer contract with this receiver

For a source to stream to this receiver over BAP unicast, the pair must
negotiate:

- **PACS + ASCS** GATT services (the receiver registers both).
- **Sink ASEs** (the receiver exposes two sink ASEs; stereo Mode A uses two
  mono CIS, Mode B one stereo ASE).
- **LC3 at 48 kHz** — the receiver accepts LC3 48 kHz only (see [Known
  limitations](known-limitations.md)).
- Compatible codec configuration, QoS, PHY, and presentation delay.
- Pairing/encryption when the receiver's pairing policy requires it.

### Protocol floor vs. project baseline

The minimum versions in [Required and recommended
versions](#required-and-recommended-versions) are a *floor*: BlueZ 5.85 is the
first stable release with the PAC callback fix, and kernel 6.4 is the
practical floor for the ISO path. Satisfying the floor is **not a promise**
that a system works — firmware, driver, and stack interactions differ per
host. The project-tested baseline is the configuration this project validated
end-to-end. The dynamic acceptance procedure on the [Bluetooth adapter support
and evaluation](bluetooth-adapter-evaluation.md) page is the final arbiter.

## Sources

- BlueZ LE Audio support: <https://www.bluez.org/le-audio-support/>
- BlueZ LE Audio support in PipeWire:
  <https://www.bluez.org/le-audio-support-in-pipewire/>
- WirePlumber Bluetooth configuration (roles, monitor, logind behavior):
  <https://pipewire.pages.freedesktop.org/wireplumber/daemon/configuration/bluetooth.html>
- PipeWire Bluetooth properties:
  <https://docs.pipewire.org/page_man_pipewire-props_7.html>
- Linux practical floor and hardware caveats (Collabora):
  <https://www.collabora.com/news-and-blog/blog/2025/11/24/implementing-bluetooth-le-audio-and-auracast-on-linux-systems/>
- Linux ISO implementation:
  <https://github.com/torvalds/linux/blob/master/net/bluetooth/iso.c> and
  <https://github.com/torvalds/linux/blob/master/net/bluetooth/hci_conn.c>
- Bluetooth Core HCI functional specification:
  <https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html>
- BlueZ PAC callback fix:
  <https://github.com/bluez/bluez/commit/6b0a08776ae44a9102d7c6875a77e83dc6a11a37>
- BlueZ QoS spelling fix (upstream):
  <https://github.com/bluez/bluez/commit/d45fd43a1cc3ba791858f11c144112c518a9ad84>
