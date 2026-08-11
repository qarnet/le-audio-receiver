# FR4 prerequisite handoff: exact-artifact acceptance tooling and procedure

Date: 2026-08-09

## Goal

Prepare FR4 exact-artifact hardware acceptance without running hardware yet:

1. add a strict autonomous one-ASE mono mode to `scripts/bap_central.py`, so
   the accepted FR4 matrix can prove mono, Mode A, and Mode B on hardware;
2. add a complete internal procedure for downloading, validating, extracting,
   flashing, and testing the exact assets from draft release `367572702` on
   both receiver targets without rebuilding or staging images into build trees.

Commit tooling and procedure only. Do not download release assets, open serial
ports, reset or flash hardware, run Bluetooth traffic, create/edit/publish a
release, or create a tag. FR4 remains open until a later explicitly approved
hardware session completes every matrix row.

## Scope

### In scope

- Backward-compatible `--mono` CLI flag, mutually exclusive with existing
  `--stereo`.
- BlueZ 5.86-compatible one-transport limit for mono mode.
- Public-boundary Python tests for CLI, D-Bus selection/configuration,
  lifecycle reset, and unchanged default/Mode B behavior.
- New `docs/development/firmware-release-fr4-procedure.md` with exact release,
  extraction, no-recovery flashing, central, stream, diagnostics, evidence,
  warning, and stop rules.
- Focused Python verification, one implementation commit, then clean committed
  canonical gate.

### Out of scope

- A `--frame-duration` option. Existing `bap_central.py` remains 10 ms; the
  exact-address autonomous PipeWire gate is the accepted 7.5 ms hardware path.
- Firmware, board, Kconfig, devicetree, build, packager, release workflow,
  `VERSION`, public flashing docs, release notes, or artifact changes.
- New test-suite child or inventory count change. Existing Python suites gain
  cases; canonical gate remains 65 children.
- FR4 results/status/acceptance documents. Do not mark FR4 accepted.
- Any hardware or remote release operation.
- MCUboot, DFU, signing, recovery tooling, mass erase, or probe-rs.

## Grounding

### Current project behavior

- `scripts/bap_central.py` currently exposes default Mode A and `--stereo`
  Mode B. It has no strict one-ASE mode.
- `scripts/bap_central_endpoint.py` already contains and tests the complete
  mono path: one 120-byte LC3 frame, FL/FR channel allocation, one acquired
  transport, `stream_mode == "mono"`, and `bap_central_session.py` duplicates
  decoded mono to both output channels through receiver production behavior.
- Current default endpoint can receive two `SetConfiguration` calls, producing
  Mode A. Strict mono needs only a transport-count admission limit; no new
  codec, encoder, writer, or receiver behavior.
- `tests/unit/bap_central_endpoint/test_bap_central_endpoint.py` directly tests
  SelectProperties, SetConfiguration, deferred Acquire, mode inference, fd
  ownership, ClearConfiguration, Release, and registration with fake D-Bus.
- `tests/unit/bap_central_session/test_bap_central_session.py` owns CLI golden
  tests and stdlib-only `--help` behavior.

### BlueZ load-bearing behavior

Installed `bluetoothd` and `bluetoothctl` are version 5.86. BlueZ 5.86 source
was inspected at tag source commit `74770b1fd2be612f9c2cf807db81fcdcc35e6560`:

- `client/player.c` `endpoint_set_configuration()` lines 1222-1252 rejects
  `org.bluez.Error.Rejected` when `max_transports == 0`, then decrements the
  count only when accepting SetConfiguration.
- `endpoint_select_properties()` lines 2288-2306 also rejects when the count
  is exhausted.
- `endpoint_clear_configuration()` lines 2336-2348 restores one unit after a
  configured transport clears.
- `profiles/audio/media.c` `pac_select()` lines 991-1042 sends exact remote
  `Locations` and `ChannelAllocation` into SelectProperties.

Mirror this narrow admission behavior. Do not invent alternate BlueZ APIs,
register extra endpoint objects, ignore a second transport after accepting it,
or partially acquire a Mode A pair.

### Exact release candidate

- Repository: `qarnet/le-audio-receiver`.
- Draft release ID: `367572702`.
- Tag name reserved in draft metadata: `v0.1.0`.
- Draft true, prerelease false.
- Target commit: `3d9a9186ec288484a637dac1dc7460319daf5e84`.
- No `refs/tags/v0.1.0` exists before publication.
- Source Actions run: `31332962453`, attempt 1, workflow `Firmware build`.
- Source artifact: `9043541373`, digest
  `sha256:c35fb920066b31c541ad70f4a5f6bbb754f41d6926e9d03ff9acebecbf82d48c`.

Exact release assets:

| Asset ID | Name | Bytes | GitHub digest |
|---:|---|---:|---|
| `507857930` | `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 579408 | `sha256:e91e404c9f6016b357a0c6692e43f644ebe05ece5fe89c69df96776e73a54e7b` |
| `507857934` | `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 621968 | `sha256:56086ae43d75ab779228fab2340190edefc26d88c429540c19973ae884beff1d` |
| `507857932` | `release-provenance.json` | 1265 | `sha256:1ea84062d12e3a5550151455132cb5426d51a4a914cceed1f536f60f13a793c9` |
| `507857931` | `SHA256SUMS` | 232 | `sha256:0da8d6b3aa1d64bbea773afb4864f067abe29662357c5546c593cd28c0bd1803` |

## Exact mono implementation

### `scripts/bap_central.py`

1. Update module usage/docs to describe three explicit 10 ms modes:
   - `--mono`: one ASE, 120-byte SDU;
   - no mode flag: Mode A, two mono ASEs;
   - `--stereo`: Mode B, one two-channel ASE, 240-byte SDU.
2. Put `--mono` and `--stereo` in one argparse mutually exclusive group.
   Keep existing `args.stereo` compatibility. New `args.mono` defaults false.
3. Pass `mono=args.mono` and `stereo=args.stereo` to
   `bap_central_endpoint.register_endpoint()`.
4. Do not change pairing, connection, acquisition, writer, cleanup, duration,
   frequency, or exit semantics.

### `scripts/bap_central_endpoint.py`

1. Inside `make_endpoint_class()`, define a private exception subclass of
   `dbus_mod.exceptions.DBusException` whose `_dbus_error_name` is exactly
   `org.bluez.Error.Rejected`. This must also work with existing stdlib fake
   D-Bus module.
2. Extend endpoint constructor to `__init__(..., stereo=False, mono=False)`.
   Reject impossible `stereo and mono` defensively with `ValueError` before
   state mutation. Store `self.mono`.
3. Mono admission invariant: at most one accepted transport across
   `self._pending_transports + self.transports`.
4. In `SelectProperties()`:
   - if mono mode already has an accepted pending/acquired transport, log one
     precise rejection line and raise the private Rejected exception;
   - if mono mode receives a combined/unknown allocation rather than exact FL
     (`0x01`) or FR (`0x02`), fail closed with Rejected; never return a stereo
     configuration under `--mono`;
   - otherwise retain exact current mono LC3 bytes and QoS.
5. In `SetConfiguration()`:
   - reject before queue mutation when mono mode already owns one pending or
     acquired transport;
   - parse channel allocation as now, then reject mono mode unless allocation
     is exactly FL or FR;
   - accepted first mono configuration follows current deferred-Acquire path
     unchanged.
6. `ClearConfiguration()` and `Release()` already remove records. Capacity is
   therefore restored from actual owned-list state, with no separate counter
   that can drift. After clearing the sole mono transport, a new first mono
   configuration must be accepted.
7. Extend `register_endpoint(..., stereo=False, mono=False)` and pass both flags
   into endpoint construction. Capability blob remains `LC3_CAPS_STEREO` only
   for `stereo`; mono and default Mode A both advertise existing one-channel
   `LC3_CAPS`.
8. Keep fd all-or-nothing ownership and cleanup unchanged.

Use stable diagnostics such as:

```text
[endpoint] Mono transport limit reached: rejecting
[endpoint] Mono mode requires FL or FR allocation: rejecting 0x03
```

Exact capitalization may differ only if tests pin one final form. Never log a
successful mono mode before one transport is acquired and mode inference
returns `mono`.

## Exact mono tests

### `tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`

Extend helpers to construct/register mono endpoints. Add public-boundary tests:

1. mono first FL SelectProperties returns exact existing mono config/QoS;
2. mono combined `0x03` and unknown `0x00` SelectProperties raise exact
   Rejected error with no state mutation;
3. mono first FL or FR SetConfiguration queues exactly one;
4. second mono SetConfiguration raises Rejected and leaves first record,
   `config_done`, and all ownership state unchanged;
5. malformed/combined mono SetConfiguration rejects atomically;
6. SelectProperties after one pending or acquired mono transport rejects;
7. ClearConfiguration of sole pending/acquired mono record restores capacity,
   and a fresh first configuration can queue;
8. Release clears mono ownership and permits fresh first configuration;
9. default endpoint still accepts two FL/FR transports for Mode A;
10. stereo endpoint still accepts one combined allocation for Mode B;
11. constructor rejects `mono=True, stereo=True` atomically;
12. registration forwards mono flag while retaining exact one-channel caps.

Assert exception `_dbus_error_name`, exact record lists, config_done behavior,
and fd ownership where relevant. Do not merely assert private boolean wiring.

### `tests/unit/bap_central_session/test_bap_central_session.py`

Update CLI golden tests:

- defaults: mono false, stereo false;
- all-flags coverage includes `--mono` in a separate invocation;
- `--mono --stereo` exits argparse status 2;
- help includes `--mono` and still imports without D-Bus/liblc3;
- existing FLPR hang-gate argv remains parse-compatible.

Do not weaken established CLI or cleanup assertions.

## FR4 procedure document

Add `docs/development/firmware-release-fr4-procedure.md`. Status must be
`prepared, not executed`; FR4 remains unaccepted. Document all sections below.

### Safety and phase boundary

- Hardware execution requires explicit user approval after this tooling commit
  is reviewed.
- Flashing replaces firmware on both receiver targets. No mass erase,
  nRF53 recovery, settings erase, probe-rs, release publication, or tag action.
- Any unexpected debug lock, identity mismatch, checksum mismatch, warning,
  asset drift, missing device, or command failure stops before next step.
- Failed FR4 leaves draft untouched and unpublished.

### Run directory and immutable download

Use a fresh retained path under `/tmp/opencode`, for example:

```bash
RUN_DIR="$(mktemp -d /tmp/opencode/fr4-v0.1.0-XXXXXX)"
mkdir "$RUN_DIR/download" "$RUN_DIR/artifact-set" \
  "$RUN_DIR/metadata" "$RUN_DIR/extracted" "$RUN_DIR/logs"
```

Document authenticated `gh api` downloads by the four exact asset IDs above,
each with `Accept: application/octet-stream`, writing to exact final names.
Before download, query release ID `367572702` to a private JSON file and parse
with stdlib Python. Require exact repository/release/tag/draft/prerelease/target
and exact asset ID/name/size/digest set. Do not print release body or secrets.

After download:

1. verify each file size and SHA-256 against the table above;
2. copy only the two ZIPs plus top-level `SHA256SUMS` into `artifact-set`;
3. run `sha256sum --strict -c SHA256SUMS` there;
4. run `scripts/prepare-draft-release.py` with exact values:
   - tag `v0.1.0`;
   - version `0.1.0`;
   - git commit `3d9a9186ec288484a637dac1dc7460319daf5e84`;
   - NCS `v3.3.0`;
   - repository `qarnet/le-audio-receiver`;
   - workflow `Firmware build`;
   - workflow ref
     `qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main`;
   - run ID `31332962453`, attempt `1`;
   - downloaded `artifact-set` and fresh `metadata` directories;
5. require downloaded `release-provenance.json` byte-equal to regenerated
   provenance;
6. save draft body privately and require byte-equal to regenerated release
   notes without printing body;
7. require git-ref lookup for `refs/tags/v0.1.0` returns exact HTTP 404.

Use `python3 -m zipfile --test` and extract into separate fresh target
directories only after `prepare-draft-release.py` passes. Extract exact members:

- nRF5340: `merged.hex`, `merged_CPUNET.hex`;
- nRF54L15: `cpuapp.hex`, `flpr.hex`.

Record SHA-256 for all four extracted images. Never copy them into `build/`.

### Firmware/build identity

Before hardware, prove current tooling commit changed no firmware/build input
relative to draft target. Use a documented `git diff --exit-code` over exact
paths: `src/`, `boards/`, `prj.conf`, `CMakeLists.txt`, `Kconfig`,
`Kconfig.sysbuild`, `sysbuild.cmake`, `sysbuild/`, and `dongle/`. Any diff
blocks FR4 until classified. The expected FR4 tooling diff is scripts/tests/
internal docs only.

Record accepted FR3 software evidence (65/65, contract 95/95, coverage and
BSim pins) and the clean tooling-commit canonical gate. Do not rebuild release
firmware; exact draft bytes are the tested candidate.

### Probe identity

Run `nrf-probes` immediately before each target flash and preserve raw output.
Resolve serial only at execution time with `nrf-probes --find nrf53` or
`nrf-probes --find nrf54l`. Never place a static serial-to-board table in the
procedure/results. Record raw DPIDR, AP/FICR PART, and VARIANT evidence.

### nRF5340 direct extracted-image flash, no recovery

Document one shell block using:

- `interface/cmsis-dap.cfg`;
- `target/nordic/nrf53.cfg`;
- repo-owned
  `boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl` only for normal flash
  and UICR helper procedures;
- extracted absolute app/net HEX paths;
- runtime probe from `nrf-probes --find nrf53`.

Run a separate read-only OpenOCD attach/examine preflight first. If
`nrf53.cpuapp arp_examine` fails, stop. Do not call `nrf53_recover`.

Flash with explicit normal sequence, not `flash_both` or `check_approtect`
(both can auto-recover): init, examine app, target app, reset halt, wait,
`flash write_image erase` exact `merged.hex`, `uicr_unprotect_app`, release and
examine cpunet, target/halt/wait/probe, `flash write_image erase` exact
`merged_CPUNET.hex`, `uicr_unprotect_net`, reset run, shutdown. Preserve full
log. Any UICR warning, recovery line, verify/error, or unexpected warning
blocks acceptance. Normal range erase preserves settings and bonds.

Do not use `fw-flash-5340`, west rebuild, `--hex-file` against a build tree, or
copy staging for FR4.

### nRF54L15 direct extracted-image flash

Use runtime `nrf-probes --find nrf54l` and
`$ZEPHYR_BASE/boards/seeed/xiao_nrf54l15/support/openocd.cfg`. Explicit order:

1. init, reset halt;
2. `nrf54l-load` exact extracted `cpuapp.hex`, then `verify_image` same path;
3. `nrf54l-load` exact extracted `flpr.hex`, then `verify_image` same path;
4. reset run, shutdown.

Preserve full log. Do not use build-tree helper or copy staging. RRAM writes
only encoded image ranges and preserve settings/bonds. Any identity mismatch,
verify error, warning, or unexpected reset blocks acceptance.

### Boot capture

Start `scripts/read_acm.py` before each reset/flash and retain logs:

- nRF5340 console: `/dev/ttyUSB0`, 115200 8N1;
- nRF54L15 console: live Xiao CDC port resolved at execution time (normally
  `/dev/ttyACM0`), 115200 8N1.

Require both targets: `BLE ready`, `settings_load() OK`, random identity,
I2S ready, and `Advertising as "LE Audio Receiver"`; zero unexplained warnings
or errors. nRF54L15 additionally requires FLPR handshake init, READY/ACK,
rings/offload/runtime init, and ACTIVE health.

### Autonomous central setup

Use only repo-authorized nRF5340DK `hci_uart` central on `/dev/ttyACM2`, H4
1,000,000 baud, attached as `hci0`. Run AGENTS.md btattach/btmgmt ritual before
each target session. Require address `C0:AA:BB:CC:DD:EE` and settings
`powered le secure-conn cis-central`. Run Python central/gates without sudo.

Capture receiver identity from that exact boot log; pass it via `--peer-addr`
or `--receiver-address`. Never reuse historical receiver addresses as assumed
truth.

### Hardware matrix

All durations below are minima. Each row needs exact command, exit status,
central mode/fps/frame count, receiver summaries, and warning scan.

For **each target**:

1. fresh mono 120 s: `bap_central.py --mono --peer-addr <live> --duration 120`;
   require exactly one transport, `Stream mode: mono`, 120-byte SDU;
2. fresh Mode A 120 s: no mode flag; require exactly two transports,
   `Stream mode: stereo_a`, 120-byte SDU per transport;
3. fresh Mode B 120 s: `--stereo`; require exactly one transport,
   `Stream mode: stereo_b`, 240-byte SDU;
4. preserved-bond reconnect 120 s: rerun one stereo mode with
   `--preserve-bond`; require Pair skipped, encrypted bonded reconnect, clean
   disconnect, advertising restart, and stream success;
5. 7.5 ms 30 s minimum: use
   `scripts/bluez-wireplumber-gate.py --receiver-address <live>`, exact
   receiver log/output paths, and current system PipeWire/WirePlumber; require
   parsed `Frame Duration: 7500 us`, about 133.3 fps, duration-consistent SDUs,
   and zero fault fields.

For nRF5340 also capture mid-stream `audio status` during a bonded Mode B row:
drift ACTIVE, APLL path/resampler identity, zero decode errors, I2S underruns,
and stream resets.

For nRF54L15 additionally require:

1. healthy Mode A and Mode B offload: submit equals success, fallback 0, all
   offload/ring/runtime fault counters zero;
2. FLPR hang recovery Mode A, 180 s, through `flpr_hang_gate.py`: 16/16 PASS,
   ACK, fallback observed during fault, epoch change, one recovery/restart,
   probation cleared, final ACTIVE, all fault counters zero;
3. timed stall gate during a live Mode B stream through
   `flpr_stall_gate.py`: exact 60 ms ACK, recovery/probation success, then
   healthy `flpr status`, `flpr ring status`, `flpr stress 5`, and
   `flpr ring test 50`;
4. machine-observable pairing reset through exact artifact: exact feature-on
   success text, old bond rejected, fresh pair succeeds, preserved-bond
   reconnect succeeds, reboot returns NORMAL with saved bond.

Physical button/LED observations are not repeated in FR4 because current
central-only policy allows no user input except audibility. Record inherited P8
evidence explicitly, not as a fresh observation: `c966dc2` accepted physical
3 s/8 s button and LED patterns; between `c966dc2` and draft target `3d9a918...`,
pairing source/config/DT changes are comments only (`src/pairing_mode.c`,
`src/user_pairing_io.c`, `src/main.c`, board conf/overlay, Kconfig/CMake), so
compiled pairing behavior/config is unchanged. FR4 exact-artifact shell reset,
pair/reconnect/reboot rows exercise the same transition owner. If this identity
claim does not hold at execution-time diff review, stop rather than weakening
the criterion.

### Acceptance and warnings

Every stream requires central success plus receiver
`decode_err=0 i2s_underrun=0 stream_reset=0`. PLC due RF loss is recorded, not
automatically failed. For all logs require zero unexplained `LOG_WRN`/`LOG_ERR`,
FATAL, fault, assert, stack overflow, `i2s_nrfx: Next buffers not supplied on
time`, `Cannot write in state`, sequence discontinuity, or unexpected recovery.
Expected fault-injection lines appear only inside named FLPR hang/stall windows
and must reconcile to final healthy counters.

Audibility is optional and is the only permitted fresh user observation. Ask
after autonomous runs complete; record exact user response or `UNAVAILABLE`,
never infer sound from counters.

Retain all logs under `$RUN_DIR`, create `MANIFEST.md` and `SHA256SUMS`, and
record exact paths/hashes in later FR4 results. Do not commit raw logs or
downloaded firmware.

FR4 accepts only when every mandatory row passes. Any failed/partial row leaves
FR4 open and draft unpublished. FR5 alone owns manual publication and
post-publication lightweight-tag verification.

## Verification and commit

Run focused checks before commit:

```bash
python3 tests/unit/bap_central_endpoint/test_bap_central_endpoint.py
python3 tests/unit/bap_central_session/test_bap_central_session.py
python3 scripts/test_bluez_wireplumber_gate.py
python3 -m py_compile scripts/bap_central.py scripts/bap_central_endpoint.py
git diff --check
```

Inspect status, full diff, and recent log. Stage exactly:

- `docs/development/firmware-release-fr4-handoff.md`
- `docs/development/firmware-release-fr4-procedure.md`
- `scripts/bap_central.py`
- `scripts/bap_central_endpoint.py`
- `tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`
- `tests/unit/bap_central_session/test_bap_central_session.py`

Commit once:

```text
feat: add FR4 mono acceptance tooling
```

After commit, require clean worktree and run:

```bash
./scripts/test-all.sh
```

Expected composition remains 65 children: 35 Twister + 5 exec-only + 22
Python + coverage + matrix + BSim. Do not alter inventory or coverage baseline
for Python-only tooling.

Do not push, merge, open a PR, amend, run hardware, download assets, or touch
remote release state. Return files changed, behavior, exact focused/canonical
results, commit hash/message, blockers, deviations, and hardware-session next
step.

Stop and escalate before commit when repository/BlueZ evidence contradicts
this shape, exception behavior cannot be proven, default Mode A/Mode B behavior
regresses, or a fix requires architecture invention. After two materially
different failed attempts, preserve worktree and escalate with exact evidence.
