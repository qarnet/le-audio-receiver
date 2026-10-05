# PB-053 isolated Linux/BlueZ host lane

## Purpose and boundary

This is a separately selected, local Linux/BlueZ host-stack regression lane. It tests real encoded public D-Bus and ISO transport behavior inside a private QEMU guest. It does not establish physical nRF RF delivery, firmware behavior, independent LC3 codec/conformance, PCM, I2S, analog sound, presentation timing, or release acceptance. The fixture LC3 bytes are **valid transport stimulus only**. No vendor LC3 terms, reference tool execution, or redistribution right is implied. PB-051 input-validation work remains outside this lane; the owner lifted its pause on 2026-10-05, and the separate item resumes after PB-053.

The current machine-specific profile is pinned in `scripts/bluez_host_guest.py`: booted Linux 7.1.5 kernel image, matching modules and config; installed BlueZ 5.87 daemon; QEMU 11.0.2; Python 3.13.12, dbus-python 1.4.0 and GI/GLib runtime; and independently built `btvirt` from clean BlueZ source revision `4dc15be8ee3f7422d447087f1893d215575cb2c8`. The emulator's pinned binary SHA-256 is `06611569862825010327200c354377428e996dade36d96c1a4872eb20ed0083c`. GPL/LGPL component notices remain in the private local runtime; no packaged vendor assets or redistribution claim follows. Source and build pins, exact absolute runtime paths, emulator mode and staged source hashes are verified, not discovered by searching installed versions. Other machines must deliberately provision and review equivalent pinned capabilities; missing prerequisites fail, without automatic downloads, installers or host module/kernel changes. Native source-snapshot unit tests use an authored temporary byte fixture, **not** the real emulator.

QEMU has one vCPU (`host,ssbd=off`), 4096 MiB memory, `-nodefaults`, `-no-user-config`, `-nic none`, no display or monitor and no host shares, passthrough or host adapter/device/bus export. The custom initramfs uses PID1 and guest-only kernel crypto/ISO modules, two guest-only emulated VHCI controllers, a private D-Bus, BlueZ daemon and `/var/lib/bluetooth` state. No workstation Bluetooth adapter, `/dev/vhci`, host root/home/run or privileged RPC is used. Host process groups, per-process subreaper/pidfd ownership and restoration constrain children and detached descendants. A previously exited actor, even with code 0, cannot masquerade as an owned stop. Host-run SIGINT/SIGTERM is first-signal latched across input snapshots, VM ownership, post-processing and record sealing.

## Prepare, anchor and execute separately

Run from repository root with the reviewed local runtime available. These are templates, **not** instructions to reuse existing output directories. Confirm `/tmp/opencode` exists and choose a new, absent, absolute preparation root, a separate new suite root and absent sibling roots `-normal`, `-sigint`, `-sigterm`, `-timeout`, `-repeat`. Keep every previous root intact.

```sh
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-NEW_UNIQUE_ID
# Paste prepare's returned manifest_sha256 as a literal, independent caller anchor.
python3 scripts/bluez_host_guest.py check \
  --prepared /tmp/opencode/pb053-guest-prep-NEW_UNIQUE_ID \
  --manifest-sha256 LITERAL_64_HEX_FROM_PREPARE_OUTPUT
python3 scripts/bluez_host_lane.py \
  --prepared /tmp/opencode/pb053-guest-prep-NEW_UNIQUE_ID \
  --manifest-sha256 LITERAL_64_HEX_FROM_PREPARE_OUTPUT \
  --output /tmp/opencode/pb053-accounted-lane-ANOTHER_NEW_UNIQUE_ID
```

Replace placeholders before invocation. Do not compute an expected digest from a supplied manifest at check/run time. `prepare` creates exclusive external evidence, snapshots bounded regular source/fixture bytes, checks frozen source and staged stimulus again before archive and manifest, and pins exact output identities. `check` independently verifies the caller-supplied literal SHA-256, schema and fixed pins. The lane checks prepared runtime, QEMU/KVM and exact pytest version before actual pytest collection; any missing capability fails. No unavailable prerequisite is a successful skip. Historical, occupied R32 preparation `/tmp/opencode/pb053-guest-prep-r32` returned digest `086b7eadcec769d5595af0408422b305dd3278827476d60e29a2693dd0e32356`; it is **evidence, not a reusable output template**.

Production-time limits: closure regular payload 4 GiB; 100000 entries for closure and separately final stage; 4096 closure roots; 4096 encoded bytes per path/link target; 32 MiB cumulative encoded path/link metadata and 32 MiB `paths.list`; 64 MiB manifest; 5 GiB uncompressed cpio; 2 GiB compressed initramfs; 64 KiB cpio stderr; 16 MiB each Nix-query stdout and 64 KiB stderr; query deadline 120 s and cpio deadline 300 s. Any overflow, unexpected stderr or nonzero exit fails with partial **exclusive** evidence retained, never an accepted manifest. Guest child file cap is 8 MiB, event ledger 4096 events/524288 bytes with sticky first failure, guest state 1024 files/2 MiB each/8 MiB total, monitor capture 4 MiB and host serial log 32 MiB. Do not increase caps to hide errors.

## Public checkpoint and accounting

Guest marker stays schema 1; successful nested public reports require schema 2. Structured events bind initial Device1 identities/state, two role UUID endpoint registrations, actual discovery `true` to `false`, fresh `Pair` versus retained `Connect`, two typed LC3 MediaTransport1 configurations, two Acquire MTUs/leases, sixteen exact 120-byte source-to-sink ISO messages per phase, source Release/close and observed sink idle/POLLHUP/close. Both Device1 peers must remain disconnected through actual samples spanning at least 0.5 s, within the original 10 s disconnect deadline. Three daemon lifetimes preserve then reset guest-only bonds/state hashes; six actor starts/stops and per-phase prepower/readiness are checked. Public schema-1 archival evidence cannot pass the current parser. `repr` callback properties remain diagnostics, not authoritative parsed state.

`tests/host_bluez/inventory.json` has **nine** reviewed mandatory cases, zero exclusions, SHA-256 `1de02a5083330284600ac740ab21a1f35ff76e5411377beb029ea251d324f276`. Pytest **8.4.2** is pinned and invoked with `--runxfail`, actual collection emits selected IDs, native xunit2 JUnit is retained, and PB-052's independently anchored accountant rejects missing, skipped, failed or unselected cases. Four intentional negative controls test coverage, outcome, child execution and inventory drift. See `docs/testing/external-test-result-accounting.md`. Portable helpers run in the regular unit gate; the actual VM host lane does **not** run automatically in canonical CI and needs this exact local runtime. Raw monitor `reported_drops: null` means drop count unavailable, **not zero loss**.

Raw console, daemon, emulator, monitor, process records, preparation logs, JUnit, collection and accountant controls remain under exclusive external roots. Do not copy raw guest logs or private images into Git. Successful software simulation does not override separate physical or analog gates. Dated observed evidence and narrowly source-backed log diagnostic dispositions are in `docs/development/pb-053-host-lane-results-20261005.md`.
