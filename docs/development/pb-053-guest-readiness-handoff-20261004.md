# PB-053: private guest readiness checkpoint

## Goal

Build a custom initramfs entirely from existing local artifacts, boot one private
QEMU guest, and prove matching kernel, guest-only VHCI controllers, ISO socket,
private D-Bus and BlueZ readiness. This is a preparation checkpoint, not completed
discovery/pairing/BAP/ISO lifecycle acceptance. Keep PB-053 In Progress.

## Scope and exclusions

Touch only new `scripts/bluez_host_guest.py`, new
`scripts/bluez_guest_init.py`, new
`tests/unit/bluez_host_guest/test_bluez_host_guest.py`, and PB-053 notes through
Backlog.md. Prior preparation code stays unchanged. No production/SDK/vendor
changes, PB-051 work, host adapters/modules/bus, network, device passthrough,
host filesystem sharing, package downloads/installers, commits or pushes in this
checkpoint. Preserve all other dirty files and immutable previous evidence.

## Grounding already checked by Delegator

- Running `uname -r` is 7.1.5. `/run/booted-system/kernel` resolves to
  `/nix/store/z94chi3wa8zcz0l170bfz6cq578cfw0s-linux-7.1.5/bzImage`.
  `/run/current-system/kernel` is a different generation and must not be used.
- Matching module root:
  `/nix/store/g0wfy5q8m3f7daz4vrbadvgrna0zh1v0-linux-7.1.5-modules`.
  `lib/modules/7.1.5` includes indexes, VHCI, Bluetooth and crypto dependencies.
  VHCI/Bluetooth vermagic is `7.1.5 SMP preempt mod_unload`.
- `/proc/config.gz` is matching config. Required proc/sysfs/devtmpfs/8250 console,
  initrd/gzip and ELF support are built in. Bluetooth, VHCI and crypto are modules.
- QEMU 11.0.2 is executable and `/dev/kvm` is current-user readable/writable.
  QEMU path:
  `/nix/store/brhybv2j85y5f384c1nqrq1qbdgynrha-qemu-for-vm-tests-11.0.2/bin/qemu-system-x86_64`.
- Static shell works:
  `/nix/store/zrynrzpsy2993w555ns9a734lbzfff2b-busybox-1.37.0/bin/ash`.
  It is a shell-only BusyBox, do not assume mount/modprobe applets.
- Python:
  `/nix/store/sdfysgb89zdysrknjavcr0crs4qxpk8r-python3-3.13.12/bin/python3.13`.
- Mount:
  `/nix/store/vpv0bx37wxwmkhfxqs28il1l24qldj6b-util-linux-2.42.3-mount/bin/mount`.
- Modprobe:
  `/nix/store/bc9a5ng1vn0v74kidqzkbx4y06yxqd4i-kmod-31/bin/modprobe`.
  Preserve symlink argv[0]; calling `kmod modprobe` is not supported.
- BlueZ daemon:
  `/nix/store/8l7syi03wm19x41yvarswrqzz7jq404r-bluez-5.87/libexec/bluetooth/bluetoothd`.
  Verified options `-n -E -K -p bap -f FILE`.
- D-Bus:
  `/nix/store/w9gn9sy71j4v3jia681vvx5j4d7f5ly7-dbus-1.16.2/bin/dbus-daemon`
  and sibling `dbus-send`. Daemon supports `--config-file`, `--nofork`,
  `--nopidfile`, `--nosyslog`.
- Emulator `/tmp/opencode/pb053-emulator-build-r2/btvirt`, SHA-256
  `06611569862825010327200c354377428e996dade36d96c1a4872eb20ed0083c`.
  `emulator/main.c:167-170,230-244` supports `-l2`; default core type 6.2.
  `vhci.c:129-192` creates guest controllers through guest `/dev/vhci`.
  `crypto.c:85-125` requires AF_ALG `ecb(aes)` and `cmac(aes)`.
  Emulator libc root:
  `/nix/store/yhawd8dka2563b5mg3vjm5h14sw5lv95-glibc-multi-2.40-224`.
- `nix-store --query --requisites` was executed on these exact runtime roots.
  It is a local store database query and succeeded without fetching. Do not query
  derivations/source metadata or invoke Nix evaluation/build/realisation.
- `cpio` exists on PATH. `cc`, Python, gzip stdlib are available.

## Implementation decisions, follow in order

### 1. Host CLI and local-only preparation

`bluez_host_guest.py` exposes `prepare --output EXCLUSIVE_EXTERNAL_ROOT` and
`run --prepared PREPARED_ROOT --output EXCLUSIVE_EXTERNAL_ROOT --timeout 240`.
Use the checked absolute paths above as explicit local profile constants. No
arbitrary QEMU extra-argument or guest command option. No downloads. Output
parent must exist; reject repository/vendor source locations, home/root itself,
existing output or symlink output. Check paths before any output modification.

Preparation validates matching running version 7.1.5, booted-kernel resolution,
existing module directory, emulator hash and required files. Fail nonzero on
missing prerequisites. Query runtime closure with `nix-store --query
--requisites` for exact eight roots: shell, Python, mount, kmod, BlueZ, D-Bus,
matching module root and emulator libc. Validate every returned root exists
directly beneath `/nix/store`; reject `.drv` and special files. These are copied
files, not host shares. Preserve symlinks. Never dereference symlinks into host
root/home/run/dev. Reject links resolving outside selected closure; permit
relative links that remain within closure. Copy roots into `stage/nix/store`.
Record selected roots, commands, per-file hashes/link targets and versions.

Create stage directories `/proc /sys /dev /run /tmp /etc /var/lib/bluetooth
/lib /bin /opt/pb053`. Link `/lib/modules` to matching root's `lib/modules`.
Copy emulator as `/opt/pb053/btvirt`; copy repository guest init as
`/opt/pb053/guest.py`. Write `/opt/pb053/runtime.json` containing checked guest
executable paths and kernel version. No host secrets or stock host bus configs.
Generate `/etc/passwd` and `/etc/group` with guest root only, `/etc/machine-id`
with fresh 32 lowercase hex digits, `/etc/hostname` = `pb053-guest`.

Generate `/init` with absolute static-ash shebang. First require shell PID `$$`
equals 1, otherwise print refusal and exit before mounts. Then use absolute
mount executable for `proc /proc`, `sysfs /sys`, `devtmpfs /dev`. Exec absolute
Python with `-u /opt/pb053/guest.py`. Direct host invocation cannot mount anything.
Create archive console device only via cpio serialization if needed, never host
mknod or sudo. If shell mounts require initial `/dev/console`, pack a character
device entry major5/minor1 using archive metadata rather than creating host node.

Pack staged relative filenames with installed `cpio --null -o --format=newc
--quiet --reproducible`, cwd=stage, retained stderr, then gzip via Python stdlib.
Sort filenames, retain symlinks, include parents. If console entry is needed,
implement a tiny newc metadata entry only for `dev/console`; do not redesign
general packing. Save `initramfs.cpio.gz`, copied kernel, config, manifest and
hashes. Bound staged data to 4 GiB; fail rather than consuming unlimited output.

### 2. Fixed guest entrypoint

`bluez_guest_init.py` must refuse unless PID1 and `/proc/cmdline` contains exact
token `pb053_guest=1`. Its normal host import is side-effect-free.

Use subprocess timeouts (15 s per short operation), guest-created directories,
and finally cleanup for owned children only. Print raw command stdout/stderr and
JSON stage events on serial. No interactive root console, RPC, pickle or host
network. Create private config files with guest-only root policy:

- bus XML: `<busconfig><type>system</type><listen>unix:path=/run/dbus/system_bus_socket</listen><auth>EXTERNAL</auth><policy user="root"><allow own="*"/><allow send_destination="*"/><allow receive_sender="*"/></policy></busconfig>`;
  no service activation/includes/host paths.
- BlueZ config: `[General]`, `Name=PB053 guest`, `Experimental=true`.
- Environment `DBUS_SYSTEM_BUS_ADDRESS=unix:path=/run/dbus/system_bus_socket`.

Load guest modules via exact modprobe executable: `hci_vhci`, `algif_hash`,
`algif_skcipher`, `cmac`, `ecb`, `aes_generic`. Kernel autoload dependencies
use `/lib/modules/7.1.5`; no host command is permitted. Missing modules return
exact evidence, not skip. Verify `os.uname().release == 7.1.5`. Prove AF_ALG
binding to `('skcipher','ecb(aes)')` and `('hash','cmac(aes)')` with Python socket.
Open/close ISO socket `(AF_BLUETOOTH, SOCK_SEQPACKET, 8)`; no packet claim yet.

Start bus with `--config-file=/etc/pb053-bus.conf --nofork --nopidfile --nosyslog`.
Start emulator `-l2`, then daemon `-n -E -K -p bap -f /etc/pb053-bluez.conf`.
Use bounded readiness polling (30 s total) with private `dbus-send --system
--print-reply --reply-timeout=2000 --dest=org.bluez / org.freedesktop.DBus.ObjectManager.GetManagedObjects`.
Require two distinct adapter objects and Adapter1 interfaces in actual reply.
Keep full raw reply. Never infer readiness solely from children being alive.
Children logs go to guest files and are printed fully during finally; parent
serial captures them. Check children remain alive through readiness. Stop only
owned children with TERM, bounded wait, then KILL; retain exit details. At end
print exactly one `PB053_GUEST_RESULT ` JSON line with kernel/controller count,
stages and `ok` true only after all readiness checks. On exceptions print failed
stage and `ok` false. Sync and guest poweroff using libc reboot constant
`0x4321fedc` only after guest guards; if poweroff fails, host deadline cleans up.

### 3. Host VM ownership

Run fixed QEMU argv: `-nodefaults -no-user-config -display none -monitor none
-serial stdio -nic none -machine q35,accel=kvm -cpu host -m 4096 -smp 2
-no-reboot -kernel COPY -initrd COPY -append 'console=ttyS0 rdinit=/init
panic=-1 pb053_guest=1'`. No disk, network, passthrough, virtfs, writable share
or extra channels. stdin=DEVNULL, stdout/stderr to exclusive raw serial log,
start_new_session=True. Deadline bounded 30..600 s; finally terminate owned
process group, wait, kill group if needed. KeyboardInterrupt also cleans up and
returns failure. Validate prepared artifacts' hashes before launch. Save argv,
start/end/returncode/deadline/cancellation/hash information in run record even on
failure. Accept readiness only from normal exit0 and one well-formed guest
result with ok=true; zero/duplicate/malformed/false results fail. Distinguish
readiness result from full PB-053 acceptance in JSON and documentation.

### 4. Focused tests and real readiness attempt

Tests exercise public CLI refusing existing output without changing its sentinel,
missing/mismatched prepared artifacts, invalid timeout, and direct guest script
execution refusal before any mount/module/daemon operation. Test pure serial
result parser with zero/duplicate/malformed/false/single-valid messages. No
fake VM output is called real guest acceptance.

Run focused unittest, then actual prepare/run into new exclusive roots
`/tmp/opencode/pb053-guest-prep-r1` and `/tmp/opencode/pb053-guest-ready-r1`
(choose fresh suffix if occupied). Retain all evidence; do not clobber previous
directories. Stop after first unexplained runtime/build warning or failure and
return exact stage/log evidence for Delegator analysis. Do not invent a patch
or suppress logs. Record truthful outcome through PB-053 notes.

## Verification commands

```sh
python3 -m unittest discover -s tests/unit/bluez_host_guest -p 'test_*.py' -v
python3 scripts/bluez_host_guest.py prepare --output /tmp/opencode/pb053-guest-prep-r1
python3 scripts/bluez_host_guest.py run --prepared /tmp/opencode/pb053-guest-prep-r1 --output /tmp/opencode/pb053-guest-ready-r1 --timeout 240
git diff --check
```

## Executor assistance and escalation

Work top to bottom, one numbered section at a time. Do not fill unresolved gaps
with guessed tools/flags/libraries. Ask Delegator exact question when blocked.
Return first exact failed command/stage, raw external log path and current diff.
Two failed materially different attempts, unexplained warnings, changed scope or
missing architectural decisions mandate stop. No commit or push until review
and clean-candidate gate handoff. No PB-053 Done/criteria checks from readiness.
