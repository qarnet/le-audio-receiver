# Bluetooth testing repositories: comparative research, 2026-10-03

## Scope, storage and method

The owner requested deep inspection of seven repositories for testing resources
and inspiration. This is read-only source research, not a tested integration,
tool adoption, license approval or firmware acceptance result. No build,
installer, vendor executable, VM, kernel module, test suite, RF or hardware
operation ran. No existing backlog item or production source was changed.

Before cloning, filesystem capacity was approximately 203 GiB available on a
457 GiB filesystem (54% used). `/tmp/opencode` occupied 44 MiB; receiver build
outputs occupied 417 MiB. Cleanup was unnecessary: **no temporary folders or
evidence were deleted**. Clones consume approximately 755 MiB plus a small CI
dependency checkout; approximately 202 GiB remained available afterward.

Source clones are external at
`/tmp/opencode/bluetooth-test-resources-20261003/`, shallow at the default branch.
Kernel checkout additionally uses a Bluetooth-focused sparse selection. No
submodules, Git LFS tools or build/download scripts were executed. Shallow
history does not establish full ancestry or fork-only commits. These temporary
clones may disappear; the pinned remote commits below are the durable source
identities. They are not added as ephemeral OpenCode references.

Repository APIs provided identity/size metadata, not permission conclusions.
README, implementation, test registration, workflow and per-file notices were
inspected. Kernel missing paths were checked against the Git tree, not inferred
from sparse filesystem absence. Important source claims were cross-checked
against live files. Source registration counts are not passing-test counts.

## Exact inspected snapshots

| Repository | Branch | Commit | Source/maturity observation |
| --- | --- | --- | --- |
| [bluez/mahesh_bluez](https://github.com/bluez/mahesh_bluez/tree/df658c6c4ab5bd5ec4a8a3f8faa36e0d0a5f906a) | master | `df658c6c4ab5bd5ec4a8a3f8faa36e0d0a5f906a` | BlueZ 5.69-era contribution tree; latest inspected commit 2023-09-07. |
| [bluez/pybluez](https://github.com/bluez/pybluez/tree/4d46ce14d9e888e3b0c65d5d3ba2a703f8e5e861) | master | `4d46ce14d9e888e3b0c65d5d3ba2a703f8e5e861` | Latest inspected commit 2022-08-13; README explicitly says not under active development. |
| [bluez/bluez](https://github.com/bluez/bluez/tree/4dc15be8ee3f7422d447087f1893d215575cb2c8) | master | `4dc15be8ee3f7422d447087f1893d215575cb2c8` | Current main BlueZ tree, declared 5.87; strong protocol/kernel test resources. |
| [bluez/bluetooth-next](https://github.com/bluez/bluetooth-next/tree/1bdf32d5949a3b927f08be4a5d7dd08fd4ca04b4) | workflow | `1bdf32d5949a3b927f08be4a5d7dd08fd4ca04b4` | Default automation-bearing kernel mirror branch; source Makefile declares 7.3.0-rc2, not installed-host kernel. |
| [bluez/pytest-bluezenv](https://github.com/bluez/pytest-bluezenv/tree/a1266d7edfc2acf5aa1418098fecbf1b5ec7240c) | main | `a1266d7edfc2acf5aa1418098fecbf1b5ec7240c` | Developing pytest/VM harness; pyproject classifier Alpha and version 0.1.10.dev0. |
| [ReinBentdal/le_audio_synthesizer](https://github.com/ReinBentdal/le_audio_synthesizer/tree/3b2fabfc33ae882582acd3e63a82893ddf78eb60) | main | `3b2fabfc33ae882582acd3e63a82893ddf78eb60` | Historical nRF5340/NCS v2.0.2 synthesizer demo; latest inspected commit 2023-01-31. |
| [telink-semi/tl_bluetooth_audio_sdk](https://github.com/telink-semi/tl_bluetooth_audio_sdk/tree/1a8520e4cc0192599071aaddb319b989c74f6be1) | master | `1a8520e4cc0192599071aaddb319b989c74f6be1` | Telink product SDK, source release note V6.1.0.0(PR), multiple vendor MCU platforms. |

Supplemental dependency inspected because both BlueZ/kernel workflows delegate
their actual test execution to it:
[bluez/action-ci](https://github.com/bluez/action-ci/tree/60348956fc7bad52a64e802f3e90f90c03ba8860),
main at `60348956fc7bad52a64e802f3e90f90c03ba8860`. This was source inspection
only, not execution of its Docker/KVM/udev/email/Patchwork machinery.

## Executive decision

**Prioritize modern BlueZ's protocol dialogues, ISO tests and lifecycle matrix.
Use pytest-bluezenv as a host-harness design reference. None of the seven
repositories closes the independent validated LC3/PCM oracle requirement.**

| Resource | Recommended value | Not established |
| --- | --- | --- |
| BlueZ main | Highest: encoded BAP/ASCS dialogues, ISO fragmentation/metadata, multi-ASE races, host emulation | Independent LC3 decoder/PCM oracle, physical Nordic RF or ASRC correctness |
| pytest-bluezenv | High infrastructure value: isolated daemon/bus/state, multi-host lifecycle and diagnostics | Bundled BAP/ISO/codec acceptance; safe drop-in physical XIAO ownership |
| bluetooth-next | High host-backend contract reference: ISO/H4/credits/timestamps, VHCI and crypto selftests | LC3 corpus, Bluetooth-specific KUnit/kselftest suite in inspected tree, RF delivery |
| mahesh_bluez | Dated comparison/reproduction context | Demonstrated new independent oracle or a reason to prefer it over modern main |
| le_audio_synthesizer | Authored stimulus and sample-domain timeline ideas | Quantitative codec/latency validation, current nRF54L15 implementation |
| Telink SDK | Boundary-adapter design and interoperability scenarios | Provenanced valid codec vectors or independent expected PCM |
| PyBluez | Low priority historical Classic/HCI examples | Modern LE Audio ISO/BAP testing dependency |

The organization is well established, but individual resources have different
ages/maturity. Do not infer validation quality from the repository name.

## 1. Modern BlueZ: concrete high-value protocol resources

### Declarative BAP/ASCS wire dialogues

[`unit/test-bap.c`](https://github.com/bluez/bluez/blob/4dc15be8ee3f7422d447087f1893d215575cb2c8/unit/test-bap.c)
contains scripted ATT discovery/configuration/notification exchanges and broad
codec/QoS/lifecycle families. `src/shared/tester.c:942-1087` supplies actual
local SOCK_SEQPACKET traffic; `test_io_recv()` checks length and byte equality.
This is useful capture-independent protocol truth, not an old trace replay.

`test_usr_spe()` at `test-bap.c:4391-4446` defines concrete negative cases:
truncated control-point command, zero ASE count, truncated metadata update,
unsupported metadata type and invalid context, with expected response code/reason.
Release/disable/metadata families exercise multiple ASE states. Adapt behaviors
to real firmware GATT transactions; do not copy private handle constants such
as CP_HND into hardware tests. Rediscover actual handles and ASE identities.

Current main has 697 literal BAP test registrations versus 40 in the inspected
fork, and 141 literal ISO registrations versus 97. These are source counts,
not tests executed here. Different source trees demonstrate richer coverage;
shallow history cannot prove exact fork ancestry or authorship of each feature.

### Multi-ASE asynchronous race and routing controls

`test/functional/test_bap.py` and `doc/functional-bap.rst:126-130` describe an
ATT-MTU-64 race: successful multi-ASE response precedes every fresh Configured
notification. High-value firmware/central regression is observable successful
reconfiguration without premature QoS or stale state, not a private helper-call
assertion. Functional unicast tests check codec/configuration, transport objects,
acquisition and active state, **not transmitted audio or decoded waveform**.

`streaming_ucl_do_stream()` at `test-bap.c:9871-9935` uses per-stream indices and
a sentinel to catch reading twice from one fd. Its comment explicitly says:

```c
/* NB: dummy data, LC3 packet encoding/decoding out of scope */
```

Borrow the anti-cross-routing concept, then use independently validated,
distinct audio channels for our content lane. An acquired fd is not sound.

### ISO/HCI synthetic tests and emulator boundaries

[`tools/iso-tester.c`](https://github.com/bluez/bluez/blob/4dc15be8ee3f7422d447087f1893d215575cb2c8/tools/iso-tester.c)
registers invalid QoS/CIG/CIS, rejection, send/receive, fragmented receive,
sequence/status ancillary data, timestamps/error queue, deferred setup,
suspend, partial/multiple connections, disconnect and repeated reconnect cases.
Its example payloads include opaque all-0xff arrays, including 512-byte data;
they are transport fixtures, not independently validated LC3 audio.

`bthost_send_iso()` can inject sequence, timestamp and packet status; hciemu
hooks allow controlled events/faults. `btvirt` plus `/dev/vhci` tests the Linux
host/controller boundary. Virtual direct forwarding and completed-packet events
do not model Nordic RF retransmission, SDC scheduling or I2S presentation.
Some HCI ISO test commands remain unsupported. Check every oracle: ISO packet
status handling can return before the later payload comparison, so one passing
case must not be relabeled complete content validation.

Keep ATT PDUs, LTV codec metadata, H4 ISO packets, raw SDUs and btsnoop records
distinct. `src/shared/lc3.h` describes configuration/presets, not codec algorithms.
No inspected BlueZ/fork LC3 encoder/decoder implementation or independent PCM
corpus was found.

### Source/prose disagreements and capture limits

`bluetoothctl-transport.rst` describes sending broad audio file formats, but
`client/player.c:5787-5818` reads and sends raw bytes. It does not encode WAV,
MP3 or PCM into LC3. `isotest` likewise reads SDU-sized raw chunks. Never send
container headers or raw PCM expecting implicit codec conversion.

Receive-file output appends bytes without per-SDU boundary/timing records;
serious reusable fixtures need sidecar lengths, sequence, status, timestamp,
channel identity and negotiated shape. `btmon --read` decodes a trace, not
replays it into firmware. `tools/magic.btsnoop` is a file-format recognition
rule, not a captured fixture. New LLM-based trace-analysis workflows are not
deterministic acceptance oracles and add external-service/privacy concerns.

## 2. pytest-bluezenv and PyBluez

[`pytest-bluezenv`](https://github.com/bluez/pytest-bluezenv/tree/a1266d7edfc2acf5aa1418098fecbf1b5ec7240c)
offers real pytest fixtures, multi-host QEMU environments, btvirt controllers,
private D-Bus/BlueZ state/configuration, reverse plugin teardown, asynchronous
event collection, daemon/monitor logs, btsnoop and core/backtrace handling.
`host_plugins.py:259-490` and `env.py:297-357,574-624` are useful ownership designs.

Limits requiring deliberate adoption design:

- Default VM scope is package, while guest tester/userspace state restarts per
  test. A userspace restart is not fresh kernel/controller state. `hosts_once`
  and intentional reuse cases have different lifecycle contracts.
- Default fresh BlueZ state is not a persistence test; physical receiver bonds
  are another owner. Power-off does not mean erased firmware or bonds.
- Pickle RPC, root console, permissive private bus and writable guest sharing
  are trusted-test-process mechanisms, not hostile-code sandbox guarantees.
- Evidence copies into the working directory can overwrite and originals are
  removed. Retain our exclusive immutable run directories and SHA manifests.
- Hardware selection handles native USB/PCIe HCI ancestry, not automatically
  serial H4. XIAO SAMD11 USB serial is not a btusb HCI controller.
- Its own tests mostly verify harness mechanisms. It supplies no dedicated
  BAP/ISO/LC3 acceptance stack or independent vectors; those live in consumers.
- Version 0.1.10.dev0 is Alpha. BlueZ functional requirements pin 0.1.9, and
  bundled helper BlueZ revision is distinct from our inspected main checkout.
  Pin a compatible complete tool/kernel/controller set, not just latest names.

PyBluez's README explicitly reports inactive development. Linux socket handling
covers HCI/RFCOMM/L2CAP/SCO; experimental BLE delegates to `gattlib`. SCO is
Classic audio, not LE ISO. No ISO/QoS/BAP workflow or audio oracle was found.
**Do not replace the existing D-Bus/MediaTransport central with PyBluez.**

## 3. bluetooth-next and actual CI dependency

Kernel `net/bluetooth/iso.c` provides concrete send-size/state errors, receive
fragment/header validation, timestamp/sequence/status propagation and deferred
setup behavior. `hci_core.c`/`hci_event.c` expose controller-credit scheduling and
completed-packet handling. `hci_h4.c` offers byte-stream parser/recovery ideas.
`hci_vhci.c` is an injection backend, not a controller implementation by itself.

Candidate host/firmware-facing cases: every H4 chunk boundary, concatenated
packets, invalid type/length, truncated headers, start/continuation/end errors,
oversized SDUs, withheld/released credits, queued teardown and explicit timestamp
meaning. Kernel software TX/completion and controller RX timestamps are not
on-air delivery or receiver presentation timestamps. QoS may be validated at
connection rather than the initial setsockopt call.

`CONFIG_BT_SELFTEST` runs ECDH/SMP cryptographic vectors, not LC3/ISO audio tests.
No Bluetooth-specific KUnit registrations were found in inspected subsystem.
Git-tree inspection confirms `tools/testing/selftests/bluetooth` and
`Documentation/bluetooth` do not exist at this snapshot. KCOV receive hooks
support fuzzing, but no actual fuzz campaign/corpus/result was established.
BlueZ's ISO tester and btvirt are separate userspace resources.

The inspected workflow branch delegates to mutable `bluez/action-ci@main`.
Supplemental source inspection resolved that dependency at the pinned commit
above: config lists ISO/L2CAP/management/SMP and other kernel testers with
file-based selection; TestRunner uses QEMU kernel image and ASAN settings;
functional tests request ASAN/UBSAN and exclude tester-marked cases. This is
source-supported CI design, **not evidence those campaigns passed here**.

Adoption cautions from `action-ci/ci/testrunner.py:84-117` and
`ci/testfunctional.py:93-152`: Not Run/skipped counts are not complete-case
acceptance; missing requirements can produce SKIP; XML error/failure absence
can permit a result without enforcing all required cases. Our gate must require
declared cases and reject unexpected skips/zero-execution. The action itself
uses a latest-tagged external image and changes KVM permissions; it was not run
and should not be imported as a pinned, hermetic or least-privilege solution.

## 4. Synthesizer and Telink SDK

### le_audio_synthesizer

Useful authored waveform/phase-accumulator and sample-domain musical timeline
ideas: `scripts/waveform_sine.py`, `waveforms.h`, `oscillator.c`, `tick_provider.c`.
Sine LUT contains repeated endpoint for interpolation; actual demo mixes
triangle oscillators, envelopes and echo. `scripts/drift.py` is an unseeded LFO
plot experiment, not clock-recovery validation.

Only declared sample test is build-only; main validation is manual playback.
No quantitative decoder/latency oracle or independently validated LC3 corpus
was found. Encoding delegates to external nrfxlib. README mono/7.5-ms prose
does not settle configuration: source selects stereo for CIS and Kconfig
defaults 10 ms, marking 7.5 ms untested.

Platform/controller/clock recipes are historical nRF5340/NCS v2.0.2/Packetcraft,
not active nRF54L15/v3.4.1 guidance. Mixed source licenses include Nordic notices
and `audio_sync_timer.c` terms restricting use to nRF53. Do not port that timing
file or flash bundled historical HEX images as part of this research.

### Telink audio SDK

Telink implementation paths below are relative to
`telink_b91m_bluetooth_sdk/tlk_bluetooth_sdk/` within its checkout; repository
release notes and `.gitlab-ci.yml` remain at repository root.

Strong architecture inspiration: input/output adapters, generated/codec/USB
source choices, codec/DSP bypass lanes and timestamp-plus-presentation-delay
queues. See `tlkmw/audio/le_audio/le_audio_common.c`, its README and audio
diagnostic files. These are Telink-specific mechanisms, not Zephyr/NCS APIs.
Vendor phone examples are manual interoperability demonstrations, not autonomous
receiver acceptance or independent measured audio quality.

LC3 paths expose APIs and prebuilt archives, not a complete inspected reference
algorithm. LC3/LC3plus/24-bit/new adapters are distinct. API naming similarity to
another codec does not prove independent implementation lineage or enabled modes.
**LC3plus code/binary/vector reuse remains excluded from our validation track.**

Sine tables are useful signals, but equal L/R cannot detect swaps. One runtime
generator has a 16-sample period: at 48 kHz it is 3 kHz, not the 1-kHz implied by
other table names. Its copy-then-reset indexing deserves block-size review before
reuse. Do not treat labels as measured frequency/geometry.

`tlkmw/audio/ll_audio/ll_audio_main.c:175-296` contains small encoded arrays,
including LC3/LC3plus-labeled data, inside `#if 0`. No reviewed generation
manifest, frame-history/configuration record, expected decoded PCM or independent
validation accompanies them. They are leads, **not trusted golden vectors**.

`tlkmdi_audio_codec_test.c` is compile-gated hardware diagnosis using LC3plus
in its microphone path; decoded mono buffer is not the final stereo buffer sent
to speaker in inspected code. A working listen-through path cannot prove decoder
output there. Repository CI inspected is source/license/spelling checks, not
numerical codec/audio acceptance. Vendor Win32/Android tools were not extracted
or executed.

## Rights, security and platform boundaries

Receiver is Apache-2.0. BlueZ test-bap/functional code is GPL-2.0-or-later;
shared/emulator/ISO tooling includes LGPL-2.1-or-later. Kernel implementations
are GPL; the syscall exception does not blanket internal Bluetooth headers.
pytest-bluezenv is principally GPL with LGPL agent files. PyBluez contains
mixed GPL-2+/GPL-3+/MIT notices. Synthesizer default Apache grant has per-file
Nordic/Packetcraft/PJRC exceptions. Telink inspected wrappers declare Apache,
while release notes separately discuss codec royalties and LC3plus licensing.
Vendor claims are not our legal opinion or permission to distribute binaries.

Prefer separately managed host test tools and independently authored scenarios
derived from public behavior/specification. Copying arrays/code, linking shared
code into distributed firmware and distributing binaries/dependencies require
per-component review. Subprocess separation is not blanket legal immunity.
No reference-codec patent/qualification or restricted-vector rights are granted
by any repository's top-level license.
The supplemental action-ci checkout has no clear repository-wide license
established here; its availability is not permission to copy its implementation.

Keep current fresh nRF DP/AP/FICR and USB/session matching, scoped H4 attachment,
ordinary-user BAP child, hard containment and immutable evidence ownership.
Auto HCI enumeration, generic --usb selection, permissive system-bus agents or
btproxy fallback to another adapter must not replace lab identity safety.

## Recommendations and existing backlog mapping

1. **First reuse protocol design from BlueZ main.** Independently author a
   compact ASCS negative/lifecycle matrix: malformed metadata, release/disable
   at each state, delayed second ASE, MTU-64 notification ordering, partial
   acquisition and valid recovery. Prove public responses and subsequent
   stream behavior, not internal cleanup counts. This is complementary scope;
   create/refine a separate product item only on owner direction.
2. **Use synthetic encoded transport recipes and exact metadata ledgers** for
   PB-047. Borrow fragmentation/status/sequence/credit case design, but preserve
   valid LC3 SDU format and distinguish virtual HCI injection from physical RF.
   Do not send opaque 0xff transport data as a valid LC3 audio oracle.
3. **Explore a separately pinned Linux/BlueZ VM lane**, using pytest-bluezenv
   isolation ideas or selected upstream tools. Keep it separate from nRF
   firmware/HIL acceptance; exact kernel/config, BlueZ, emulator and harness
   identities and required-case execution are part of readiness.
4. **Retain fresh btmon evidence under our owner**, with sidecar boundaries,
   timestamps and hashes. It supports PB-047/PB-049 diagnosis, not a replacement
   for PCM/clock/presentation truth or raw payload boundaries.
5. **Borrow original signal/adapter/isolation concepts**, not vendor binaries
   or unsupported platform recipes. Distinct L/R, sample-domain markers,
   bypass/codec/full-path comparisons and timestamp-ledger controls support
   PB-043/PB-045/PB-046/PB-048/PB-049 after refinement.
6. **Continue LC3-only independent-reference work under PB-042/PB-043/PB-044
   and PB-050.** These repositories provide protocol and infrastructure ideas,
   not the missing independent LC3 oracle. Telink bytes or another same-lineage
   codec API do not close the rights/provenance/validation gap.

PB-041 remains fixture identity owner; analog PB-023/PB-025 and matrices
PB-024/PB-026 remain separate. No acceptance threshold, unsupported rate/155-byte
frame, broadcast role, 360-frame FLPR capability or LC3plus feature is added.

## Verification and nonclaims

All requested clones plus supplemental CI checkout stayed clean; source
identities and disk measurements were retained in tool output. No external
test passed merely because its registration was found. Linux/kernel/VM
compatibility, nRF54 controller behavior, codec conformance, ASRC math, RF
reliability, wire presentation, DAC presence and analog quality require their
own public-boundary execution and fresh evidence. No implementation, backlog
rewrite, commit, push or hardware action was performed for this research.
