# FR4 cadence fix local hardware-validation handoff

Date: 2026-08-10

User approval: explicit approval received to rebuild and normally flash local
HEAD to both receiver targets, then run strict mono and Mode B for 120 seconds
plus a Mode A regression on each target.

## Goal

Validate timestamp-cadence omission concealment from commits `60e2ac1` and
`606fbed` on real nRF5340 E83 and nRF54L15 Xiao hardware before creating any
replacement release candidate.

The exact draft `v0.1.0` firmware failed nRF5340 strict mono with 8876 receiver
callbacks for 12000 transmitted SDUs, zero PLC, 225 I2S DMA restarts, and 225
`Next buffers not supplied on time` warnings. Local HEAD adds bounded timestamp-
cadence PLC for mono and Mode B while leaving Mode A timestamp synthesis out of
scope. Prove the local fix under the same central path, prove Mode B, and prove
Mode A did not regress.

This is pre-release diagnostic validation of local build outputs. It is not
FR4 exact-artifact acceptance and cannot accept or publish `v0.1.0`. Do not
touch GitHub release/tag state, VERSION, remote branches, or CI.

## Current resume software evidence

- Expected start HEAD: `ec8c846` `fix: increase nRF5340 system workqueue stack`.
- Expected worktree: clean.
- Focused suites: `audio.iso_seq` 39/39; `audio_stream_session` 47/47;
  build-contract checker tests 52/52.
- Canonical gate: 65 PASS / 0 FAIL / 65 TOTAL.
- Coverage population: 36; committed baseline enforcement passes.
- Build contract: 96/96 (includes the FR4 nRF5340
  `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048` assertion `5340-032`).
- BSim hashes unchanged.

## Handoff commit state

Read full project `AGENTS.md`, both cadence-fix handoffs, FR4 procedure, and
this file. Inspect status/log. The original hardware handoff is already
committed at:

```text
3da4862 docs: record cadence hardware validation handoff
```

Do not recommit that original handoff, amend, push, merge, or open a PR. Require
clean worktree before building.

## Authorization and prohibitions

Authorized:

- pristine local production builds from committed HEAD for both receiver
  targets;
- read-only probe enumeration and nRF5340 attach/examine;
- normal image-range flash writes and verification for nRF5340 app+net and
  nRF54L15 cpuapp+FLPR;
- receiver resets caused by normal flashing;
- UART boot/stream capture and non-destructive shell diagnostics;
- production `bt unpair` commands;
- approved nRF5340DK HCI central setup, pairing, mono/Mode A/Mode B streams,
  and normal central cleanup;
- fresh retained evidence under `/tmp/opencode`.

Never authorized:

- mass erase, `nrf53_recover`, any recovery path, settings-partition erase,
  probe-rs, APPROTECT changes beyond the existing normal UICR unprotected
  writes in the known flash sequence, or unsupported nRF54 recovery;
- direct RADIO access or experimental controller changes;
- source/test/config changes during hardware execution;
- central QoS changes, lowered duration, weakened warning/counter criteria,
  or hidden retries;
- flashing old/stale build outputs without rebuilding from committed HEAD;
- release asset creation/replacement/deletion/upload, draft edit/publication,
  VERSION change, tag creation, push, merge, PR, or CI action;
- human-operated Bluetooth central;
- fresh human input except optional audibility after every autonomous row
  passes.

## Run directory and evidence

Create one fresh retained run directory:

```bash
RUN_DIR="$(mktemp -d /tmp/opencode/fr4-cadence-local-XXXXXX)"
mkdir "$RUN_DIR/build" "$RUN_DIR/logs" "$RUN_DIR/metadata"
```

Record its absolute path immediately. Preserve full command lines, exits, build
logs, image hashes, probe output, flash output, boot logs, central logs,
receiver row logs, and warning scans. Do not commit raw logs or binaries.

At completion or failure create `$RUN_DIR/MANIFEST.md` and
`$RUN_DIR/SHA256SUMS` covering all retained files. Manifest status must say
local pre-release validation, never exact-artifact FR4 acceptance.

## Build exact local HEAD

From clean committed HEAD inside project dev shell:

```bash
fw-build-5340 > "$RUN_DIR/build/nrf5340-build.log" 2>&1
fw-build-54l15 > "$RUN_DIR/build/nrf54l15-build.log" 2>&1
```

Require both exits zero. Classify every warning against `STATUS.md`; any new or
actionable warning stops before flashing.

Record HEAD and SHA-256/size of:

- `build/nrf5340/merged.hex`
- `build/nrf5340/merged_CPUNET.hex`
- `build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex`
- `build/nrf54l15/flpr/zephyr/zephyr.hex`

Require each file exists and was produced by this pristine build. Do not copy
images into another build tree.

## Probe and flash safety

Run and retain raw `nrf-probes` output immediately before each target. Never
assume or document a static probe mapping. If `scripts/probe-serial.local`
exists or is nonempty, stop and report instead of using the override.

### nRF5340

Resolve current nRF53 probe with `nrf-probes --find nrf53`. Run read-only
attach/examine first:

```bash
openocd -f interface/cmsis-dap.cfg -c "adapter serial $PROBE" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg \
  -c init -c "nrf53.cpuapp arp_examine" -c shutdown
```

Any examine/debug-lock failure stops. Never recover.

Start `/dev/ttyUSB0` boot capture before flash. Use the explicit FR4
no-recovery sequence against freshly built local images, not `fw-flash-5340`,
because the standard runner can enter a recovery helper when attach fails:

```bash
APP_HEX="$PWD/build/nrf5340/merged.hex"
NET_HEX="$PWD/build/nrf5340/merged_CPUNET.hex"
openocd -f interface/cmsis-dap.cfg -c "adapter serial $PROBE" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg \
  -f boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl \
  -c init \
  -c "nrf53.cpuapp arp_examine" \
  -c "targets nrf53.cpuapp" \
  -c "reset halt" \
  -c "wait_halt 2000" \
  -c "flash write_image erase $APP_HEX" \
  -c "verify_image $APP_HEX" \
  -c "uicr_unprotect_app" \
  -c "nrf53_cpunet_release nrf53" \
  -c "nrf53.cpunet arp_examine" \
  -c "targets nrf53.cpunet" \
  -c halt \
  -c "wait_halt 2000" \
  -c "flash probe 2" \
  -c "flash write_image erase $NET_HEX" \
  -c "verify_image $NET_HEX" \
  -c "uicr_unprotect_net" \
  -c "reset run" \
  -c shutdown
```

Require both verification operations and zero warning/recovery/error lines.

Boot must include `BLE ready`, `settings_load() OK`, random identity, I2S ready,
and `Advertising as "LE Audio Receiver"`, with no unexplained warning/error.
Capture live receiver address from this boot only.

### nRF54L15

After all nRF5340 rows pass, resolve current nRF54L15 probe and current Xiao CDC
port from live tools. Start UART capture before flash. `fw-flash-54l15` is
allowed because it has no recovery path and explicitly loads/verifies both
fresh local cpuapp and FLPR images. Preserve full output:

```bash
fw-flash-54l15
```

Require cpuapp and FLPR verification, zero unexpected warnings/errors, and
clean boot: BLE/settings/random identity/I2S/advertising plus FLPR handshake,
READY/ACK, rings/offload/runtime initialization, and ACTIVE health. Capture
live receiver address from this boot only.

## Autonomous central setup

Before each target's streaming session, run the project-required central
ritual:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 \
  </dev/null >/tmp/btattach.log 2>&1 &
sleep 5
sudo btmgmt --index hci0 power off
sudo btmgmt --index hci0 power on
sleep 2
sudo btmgmt --index hci0 io-cap 3
sudo btmgmt --index hci0 sc on
```

Require `hci0` address `C0:AA:BB:CC:DD:EE` and settings including
`powered le secure-conn cis-central`. Preserve `btmgmt info` and btattach log.
Run `bap_central.py` without sudo.

Before every fresh row:

1. send production `bt unpair` over current receiver console and require exact
   target text:
   - nRF5340: `Pairing mode reset: bonds cleared; open pairing enabled.`
   - nRF54L15: `Pairing reset complete: bonds cleared; BONDING advertising active.`
2. let the default fresh central strategy own central Device1 removal and
   re-pairing;
3. start a fresh target/row-specific UART capture before launching central;
4. retain central stdout/stderr and exit status;
5. wait for receiver capture cleanup even when central fails.

Do not run two serial readers on one port. Close command sessions before
starting `scripts/read_acm.py`. Never reuse one row's receiver log filename.

## Targeted matrix

Run rows in this order on nRF5340, then repeat same order on nRF54L15. Stop on
first failed row; do not flash second target after nRF5340 failure.

After each target's rows complete successfully, run the receiver shell
`kernel thread stacks` over the current console and retain the exact output.
For nRF5340 require the `sysworkq` line to report stack size 2048 and retain
the used/unused high-water evidence; a stack command failure or an unreadable
`sysworkq` line blocks acceptance of that target. On nRF54L15 retain the same
command output for evidence.

### Row 1: strict fresh mono, 120 s

```bash
python3 scripts/bap_central.py --mono --peer-addr <live> --duration 120
```

Require central exit 0, exactly one transport/CIS, `Stream mode: mono`,
120-byte SDU, 12000 frames in 120 s at about 100 fps, and clean teardown.

Receiver requires one configured/started CIS, output cadence sustained through
the run, and summary:

- `decode_err=0`
- `i2s_underrun=0`
- `stream_reset=0`
- zero `Next buffers not supplied on time`
- zero `Cannot write in state`
- zero cadence RESYNC warnings, sequence discontinuities, or other warnings.

Record SDUs, total rendered `decoded` field, PLC, empty SDUs, and central frame
count. If delivered SDUs are materially below central frames, require PLC to
account for omitted output cadence and rendered total to remain duration-
consistent. If RF happens to deliver every event and cadence-gap activation is
not observed, record `activation not observed`; do not fabricate loss or weaken
other gates. Unit/BSim evidence remains activation proof, while hardware proves
the timestamp path does not regress real traffic.

### Row 2: strict fresh Mode B, 120 s

```bash
python3 scripts/bap_central.py --stereo --peer-addr <live> --duration 120
```

Require exactly one transport/CIS, `Stream mode: stereo_b`, 240-byte SDU,
12000 frames / 120 s / about 100 fps, clean teardown, and same receiver
zero-fault/warning criteria. Record SDU/rendered/PLC counts.

For nRF54L15 additionally require FLPR offload healthy for the row: submit
equals success, fallback zero, state ACTIVE, and all ring/runtime/fault/recovery
counters zero. Use non-destructive production shell diagnostics if needed;
preserve exact output.

### Row 3: fresh Mode A regression, 30 s

```bash
python3 scripts/bap_central.py --peer-addr <live> --duration 30
```

Require exactly two transports/CISes, `Stream mode: stereo_a`, 120-byte SDU per
transport, 3000 frames / 30 s / about 100 fps, clean teardown, and same
receiver zero-fault/warning criteria. Mode A is unchanged by cadence synthesis;
this row proves no regression.

For nRF54L15 additionally require healthy offload: submit equals success,
fallback zero, ACTIVE, all fault/ring/runtime counters zero.

## Warning and result policy

Every row requires central success and receiver
`decode_err=0 i2s_underrun=0 stream_reset=0`. PLC caused by RF loss is expected
evidence, not failure. Fail on any unexplained `LOG_WRN`/`LOG_ERR`, FATAL,
assert, stack overflow, cadence RESYNC, sequence discontinuity,
`i2s_nrfx: Next buffers not supplied on time`, `Cannot write in state`, FLPR
fault/fallback/recovery, probe/flash warning, or command failure.

Do not repeat a deterministic firmware failure. At most one retry is allowed
for a clearly evidenced environmental central setup/bond issue after a
materially different, already documented non-destructive remediation. Preserve
both attempts. No code edit during execution.

## Required recap

Return no FR4 acceptance claim. Report:

- handoff commit and clean git status;
- run directory;
- build commands/exits, warning classification, HEAD, four image hashes/sizes;
- raw probe identity evidence and exact flash commands/exits/verifications;
- clean boot evidence and live address per target;
- central setup evidence;
- every row command, exit, transport/CIS count, mode, SDU size, frame count,
  duration/fps, receiver SDU/rendered/PLC/empty/error/underrun/reset counters,
  warning scan, and PASS/FAIL;
- cadence activation observed/not observed;
- nRF54L15 FLPR counters/health;
- all remediation/deviations;
- manifest and evidence hashes;
- exact blocker if stopped;
- whether targeted local hardware validation passed on both targets.

Do not ask audibility yourself. Orchestrator reviews evidence first and asks
only after all autonomous rows pass.
