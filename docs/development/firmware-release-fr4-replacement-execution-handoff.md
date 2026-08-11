# FR4 replacement execution handoff: exact draft assets

Date: 2026-08-11

User approval: explicit approval received to download the current draft assets,
flash both receiver boards, and test the released ZIP files. This handoff pins
the replacement candidate created from trusted `main`; it does not reuse the
historical failed candidate's IDs, hashes, or target.

## Goal

Execute exact-artifact hardware acceptance against draft release `368351363`
(`v0.1.0`, target
`5966d68f8155a5a96da96fc7859f1064a3591472`). Prove that both downloaded ZIPs
are intact, reproducible release products, contain the expected firmware
images, match the accepted local replacement-preflight image bytes, flash and
boot on their intended targets, and pass the autonomous receiver matrix.

Return raw evidence for Orchestrator review. Do not publish the draft, create a
tag, or claim FR4 acceptance in repository docs during execution.

## Pinned release identity

- Repository: `qarnet/le-audio-receiver`
- Release ID: `368351363`
- Tag name: `v0.1.0`
- Draft: `true`
- Prerelease: `false`
- Target: `5966d68f8155a5a96da96fc7859f1064a3591472`
- Trusted-main workflow run: `31459113243`, attempt `1`, event `push`, result
  `success`
- Workflow: `Firmware build`
- Workflow ref:
  `qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main`
- SDK: nRF Connect SDK `v3.3.0`, sdk-nrf
  `ba167d9f3db4abbdc9b67887ca3ea66c64f2d956`
- Toolchain container:
  `ghcr.io/nrfconnect/sdk-nrf-toolchain@sha256:f24d8932ff081ebcd8da9c248f4449bdabe461c0620a7a4ac9e95eb577ba2276`
- Hosted canonical gate: exact `Gate complete: 65 PASS / 0 FAIL / 65 TOTAL`
- Hosted jobs: tests `93678896322`, firmware `93687178841`, release
  `93688175449`, all successful and ordered tests -> firmware -> release

Exact release assets:

| Asset ID | Name | Bytes | GitHub digest |
|---:|---|---:|---|
| `509723232` | `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 580613 | `sha256:1bb2d2143624d9ab3c64bb1a94d6bb2ce7ea216aa167121f9d12d31629b2e83f` |
| `509723234` | `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 623434 | `sha256:cc91f07b26b7ac2a15a996793bcc65497f065b6aef2c7c24269b511d8c182ae3` |
| `509723235` | `release-provenance.json` | 1265 | `sha256:c12ccfc7049fc4949c52a1367f66654b2c939564bd64841ccd0f4ae78e200d41` |
| `509723233` | `SHA256SUMS` | 232 | `sha256:c0f23bfb6e18f3ee2a5f9258a3ee738ccb772fdf6b98bd555b1a379e2a63d608` |

## Safety boundary

Authorized:

- authenticated read-only GitHub metadata queries and asset downloads;
- fresh retained evidence under `/tmp/opencode`;
- read-only USB, serial, controller, and probe enumeration;
- temporary power-off/unbind/rebind of the two USB Bluetooth controllers so
  the repository-authorized nRF5340DK HCI UART central can own `hci0`;
- normal J-Link reset of the HCI UART central if needed;
- normal range flashing and verification of exact extracted images on both
  receiver targets;
- UART shell commands, receiver resets, `btattach`/`btmgmt`, and autonomous
  central/gate operations required below.

Never authorized:

- mass erase, `nrf53_recover`, any recovery path, settings-partition erase,
  probe-rs, or flashing any local build output;
- firmware rebuild, binary modification, copy into `build/`, criterion
  weakening, warning suppression, or release-asset substitution;
- release edit/delete/re-upload/publication, tag creation, push, merge, or PR
  operation;
- human-operated Bluetooth central or fresh human input except optional
  audibility after Orchestrator review.

Any checksum/provenance/identity mismatch, debug lock, verify failure,
unexpected warning/error, wrong stream shape, or failed mandatory row stops
execution. Preserve evidence. Do not perform materially identical retries more
than twice.

## Pre-hardware artifact gates

1. Inspect git status/log. Commit only this handoff before hardware, with
   message `docs: pin replacement FR4 execution`. Do not push.
2. Create fresh retained directories under
   `/tmp/opencode/fr4-replacement-v0.1.0-XXXXXX`: `download`, `artifact-set`,
   `metadata`, `extracted`, and `logs`.
3. Save release endpoint `repos/qarnet/le-audio-receiver/releases/368351363`
   privately and require every pinned field and exact asset tuple above.
4. Download all four assets by pinned asset ID using
   `Accept: application/octet-stream`. Require exact size and SHA-256.
5. Copy only both ZIPs and top-level `SHA256SUMS` into `artifact-set`; run
   `sha256sum --strict -c SHA256SUMS` there.
6. Regenerate provenance and release notes with:

   ```bash
   python3 scripts/prepare-draft-release.py \
     --tag v0.1.0 \
     --version 0.1.0 \
     --git-commit 5966d68f8155a5a96da96fc7859f1064a3591472 \
     --ncs-version v3.3.0 \
     --repository qarnet/le-audio-receiver \
     --workflow "Firmware build" \
     --workflow-ref qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main \
     --run-id 31459113243 \
     --run-attempt 1 \
     --artifact-dir "$RUN_DIR/artifact-set" \
     --output-dir "$RUN_DIR/metadata/regenerated"
   ```

7. Require downloaded provenance byte-equal to regenerated provenance. Extract
   the release body from saved release JSON with stdlib Python, without an
   added newline, and require it byte-equal to regenerated release notes.
8. Require authenticated `refs/tags/v0.1.0` lookup to produce exactly one HTTP
   404 status. The draft must remain untagged.
9. Test both ZIPs with `python3 -m zipfile --test`, extract separately, verify
   internal `SHA256SUMS`, and require exact members:
   - nRF5340: `merged.hex`, `merged_CPUNET.hex`;
   - nRF54L15: `cpuapp.hex`, `flpr.hex`.
10. Require extracted image hashes to equal the accepted local replacement
    preflight recorded in `firmware-release-fr4-results.md`:

    | Image | SHA-256 |
    |---|---|
    | nRF5340 `merged.hex` | `ab8abda54987d2cd0cb664ca58ee95a42907d0713e562f95f1e4fb7463a990fd` |
    | nRF5340 `merged_CPUNET.hex` | `2ce0ca1aa9fdc27d9fc1a4b25148af82da2fb52f1834f61113685d0851119619` |
    | nRF54L15 `cpuapp.hex` | `85253a4b69a89c6bc8d7073a0d0c8ccbc50ba559ea6c6eefe5cbf5f3839cbbc0` |
    | nRF54L15 `flpr.hex` | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |

11. Require zero executable/build-input diff between current checkout and
    release target across `src/`, `boards/`, `prj.conf`, `CMakeLists.txt`,
    `Kconfig`, `Kconfig.sysbuild`, `sysbuild.cmake`, `sysbuild/`, and `dongle/`.
    Current planning evidence showed exit zero; execution must repeat it.

## Live hardware identities

Never assume probe mapping. Immediately before each flash, retain raw
`nrf-probes` output and exact target lookup. Planning evidence, to be
reconfirmed:

- nRF54L15 receiver: probe `8EE9B3FF`, DPIDR `0x6ba02477`, PART
  `0x00054b15`, VARIANT `AAC0`;
- nRF5340 receiver: probe `E6635C08CB1F502B`, DPIDR `0x6ba02477`, PART
  `0x00005340`, VARIANT `QKAA`.

Receiver consoles, resolved by stable USB identity:

- nRF5340 E83: `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`, 115200;
- nRF54L15 Xiao:
  `/dev/serial/by-id/usb-Seeed_Studio_Seeed_Studio_XIAO_nrf54_CMSIS-DAP_8EE9B3FF-if02`,
  115200.

## Authorized central isolation

Receiver acceptance must not use the unverified BT600. Current planning
identity:

- BT600: USB `0b05:1d70`, sysfs interface `1-8:1.0`, controller address
  `B0:82:E2:1F:A2:80`;
- BT540: USB `0b05:1bef`, sysfs interface `1-4:1.0`, controller address
  `A0:AD:9F:7B:C7:95`.

Before receiver streams:

1. Record controller/USB state and any connected devices.
2. Stop stale `btattach` instances only after recording their PID/argv.
3. Power off then unbind BT600 and BT540 from `btusb`, recording exact sysfs
   paths. This is temporary and reversible.
4. Attach the nRF5340DK HCI UART central through stable J-Link interface-02:
   `/dev/serial/by-id/usb-SEGGER_J-Link_001050023938-if02`, H4 at 1,000,000
   baud. A normal J-Link reset is allowed if attachment needs it; no flash or
   recovery.
5. Require attached controller index `hci0`, address `C0:AA:BB:CC:DD:EE`, and
   current settings `powered le secure-conn cis-central` after the documented
   power-cycle, IO capability 3, and Secure Connections setup.

All `bap_central.py`, FLPR hang, and stock-gate rows therefore use the approved
central and their existing `hci0` defaults. In cleanup, stop `btattach`, rebind
BT600 first and BT540 second, then require their original USB identities and
controller addresses to return. Do not continue to BT600 evaluation until this
restoration is proven.

## Exact-image flash and boot

Use the no-recovery direct OpenOCD sequences from sections 7 and 8 of the
historical `firmware-release-fr4-procedure.md`, substituting only this run's
exact extracted paths and live probe serials. Start console capture before
flash/reset. Do not use `fw-flash-*` or build trees.

- nRF5340: read-only `arp_examine`; flash and verify exact `merged.hex`, normal
  UICR unprotect helper, release cpunet, flash and verify exact
  `merged_CPUNET.hex`, normal UICR helper, reset run. Any recovery line or UICR
  warning fails.
- nRF54L15: reset halt; `nrf54l-load` and verify exact `cpuapp.hex`; load and
  verify exact `flpr.hex`; reset run.

Both boots require `BLE ready`, `settings_load() OK`, live random identity,
I2S ready, and `Advertising as "LE Audio Receiver"`, with no unexplained
warning/error. nRF54L15 additionally requires FLPR READY/ACK, rings, offload,
runtime initialization, and ACTIVE health.

## Autonomous matrix

Use live receiver addresses from current boot captures. For each target, run
in order and retain central plus concurrent receiver logs:

1. `bt unpair`, then fresh mono 120 s: `bap_central.py --mono --peer-addr
   <live> --duration 120`; one transport, `mono`, 120-byte SDU, 12000 frames.
2. `bt unpair`, then fresh Mode A 120 s: default mode, two transports,
   `stereo_a`, 120-byte SDU per transport, 12000 frames.
3. `bt unpair`, then fresh Mode B 120 s: `--stereo`, one transport,
   `stereo_b`, 240-byte SDU, 12000 frames.
4. Immediate preserved-bond Mode B 120 s: same peer with `--stereo
   --preserve-bond`; Pair skipped, encrypted reconnect, 12000 frames.
5. Exact-address stock BlueZ/PipeWire/WirePlumber gate for at least 30 s
   against retained bond, using target-specific output/log paths. Require
   7.5 ms, about 133.3 fps, duration-consistent SDUs, clean gate and reader
   exits, and zero fault fields.

Every stream requires central success and receiver
`decode_err=0 i2s_underrun=0 stream_reset=0`. Record PLC and cadence
concealment; zero unexplained cadence RESYNC, `LOG_WRN`, `LOG_ERR`, FATAL,
assert, stack overflow, I2S deadline/write-state warning, or recovery.

nRF5340 additionally requires mid-stream `audio status`: drift ACTIVE, APLL
path, zero decode errors, underruns, and resets.

nRF54L15 additionally requires:

- healthy Mode A/Mode B offload, submit equals success, fallback zero, all
  offload/ring/runtime fault counters zero;
- `flpr_hang_gate.py --port <stable-xiao-console> --duration 180 --peer-addr
  <live>` Mode A: 16/16 PASS, ACK, fallback during fault, epoch change, one
  recovery/restart, probation cleared, final ACTIVE, final faults zero;
- timed 60 ms `flpr_stall_gate.py` during live Mode B, then healthy `flpr
  status`, `flpr ring status`, `flpr stress 5`, and `flpr ring test 50`;
- machine-observable pairing reset: exact feature-on success text, old bond
  rejected, fresh pair succeeds, preserved reconnect succeeds, reboot returns
  NORMAL with saved bond.

## Evidence and recap

Create `$RUN_DIR/MANIFEST.md` and `$RUN_DIR/SHA256SUMS` covering release
metadata, downloads, extracted images, provenance, tag probe, probe/USB state,
central isolation/restoration, flash logs, boot logs, every stream/gate log,
commands, exits, counters, and warning scans. Do not commit raw logs or
binaries.

Return:

1. handoff commit and clean/dirty git status;
2. retained run directory;
3. exact release/download/internal/extracted identity results;
4. live probe evidence and exact flash/verify outcomes;
5. boot evidence for both targets;
6. every matrix row command, exit, mode, duration/fps/frame count, receiver and
   offload counters, and PASS/FAIL;
7. every warning/error classification and remediation;
8. central isolation and USB-adapter restoration evidence;
9. manifest/checksum paths;
10. blockers and whether every mandatory technical criterion passed.

Do not ask the audibility question. Orchestrator asks only after reviewing
autonomous evidence. Do not begin BT600 evaluation in this phase.
