# FR4 procedure: exact-artifact hardware acceptance

Status: **prepared, not executed**. FR4 remains unaccepted.

Date: 2026-08-09

This document is the complete internal procedure for FR4 exact-artifact
hardware acceptance of draft release `367572702` (`v0.1.0`, target
`3d9a9186ec288484a637dac1dc7460319daf5e84`). It covers download,
validation, extraction, no-recovery flashing, autonomous streaming, and
evidence retention for both receiver targets.

Hardware execution requires explicit user approval after the FR4 tooling
commit is reviewed. Until that approval, nothing below is run.

## 1. Safety and phase boundary

- Hardware execution requires explicit user approval after the FR4
  tooling commit is reviewed.
- Flashing replaces firmware on both receiver targets. No mass erase,
  nRF53 recovery, settings erase, probe-rs, release publication, or tag
  action is ever performed by this procedure.
- Any unexpected debug lock, identity mismatch, checksum mismatch,
  warning, asset drift, missing device, or command failure stops before
  the next step.
- A failed FR4 run leaves the draft untouched and unpublished.

## 2. Run directory and immutable download

Use a fresh retained path under `/tmp/opencode`:

```bash
RUN_DIR="$(mktemp -d /tmp/opencode/fr4-v0.1.0-XXXXXX)"
mkdir "$RUN_DIR/download" "$RUN_DIR/artifact-set" \
  "$RUN_DIR/metadata" "$RUN_DIR/extracted" "$RUN_DIR/logs"
```

Before downloading, query release `367572702` to a private JSON file and
parse it with stdlib Python:

```bash
gh api repos/qarnet/le-audio-receiver/releases/367572702 \
  > "$RUN_DIR/metadata/release.json"
```

Repository identity is bound by querying the exact endpoint
`repos/qarnet/le-audio-receiver/releases/367572702`; the GitHub release
JSON has no standalone repository-name field, so no such property is
demanded. Require, by exact comparison of the parsed JSON, exact `url`
(or exact owner/repo URL prefix) for `qarnet/le-audio-receiver`, release
ID `367572702`, `tag_name == v0.1.0`, `draft == true`,
`prerelease == false`, target commit
`3d9a9186ec288484a637dac1dc7460319daf5e84`, and the exact asset
ID/name/size/digest set below. Never print the release body or any secret.

Download the four exact assets by ID, each with
`Accept: application/octet-stream`, to exact final names:

```bash
gh api -H "Accept: application/octet-stream" \
  repos/qarnet/le-audio-receiver/releases/assets/507857930 \
  > "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip"

gh api -H "Accept: application/octet-stream" \
  repos/qarnet/le-audio-receiver/releases/assets/507857934 \
  > "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"

gh api -H "Accept: application/octet-stream" \
  repos/qarnet/le-audio-receiver/releases/assets/507857932 \
  > "$RUN_DIR/download/release-provenance.json"

gh api -H "Accept: application/octet-stream" \
  repos/qarnet/le-audio-receiver/releases/assets/507857931 \
  > "$RUN_DIR/download/SHA256SUMS"
```

Exact asset table (from draft `367572702`):

| Asset ID | Name | Bytes | GitHub digest |
|---:|---|---:|---|
| `507857930` | `le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip` | 579408 | `sha256:e91e404c9f6016b357a0c6692e43f644ebe05ece5fe89c69df96776e73a54e7b` |
| `507857934` | `le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` | 621968 | `sha256:56086ae43d75ab779228fab2340190edefc26d88c429540c19973ae884beff1d` |
| `507857932` | `release-provenance.json` | 1265 | `sha256:1ea84062d12e3a5550151455132cb5426d51a4a914cceed1f536f60f13a793c9` |
| `507857931` | `SHA256SUMS` | 232 | `sha256:0da8d6b3aa1d64bbea773afb4864f067abe29662357c5546c593cd28c0bd1803` |

## 3. Download verification and provenance

After download, in order:

1. Verify each file size and SHA-256 against the table above.
2. Copy only the two ZIPs plus the top-level `SHA256SUMS` into
   `$RUN_DIR/artifact-set`.
3. Run the strict checksum gate there:

   ```bash
   cd "$RUN_DIR/artifact-set" && sha256sum --strict -c SHA256SUMS
   ```

4. Regenerate provenance with the exact values:

   ```bash
   python3 scripts/prepare-draft-release.py \
     --tag v0.1.0 \
     --version 0.1.0 \
     --git-commit 3d9a9186ec288484a637dac1dc7460319daf5e84 \
     --ncs-version v3.3.0 \
     --repository qarnet/le-audio-receiver \
     --workflow "Firmware build" \
     --workflow-ref qarnet/le-audio-receiver/.github/workflows/firmware-build.yml@refs/heads/main \
     --run-id 31332962453 \
     --run-attempt 1 \
     --artifact-dir "$RUN_DIR/artifact-set" \
     --output-dir "$RUN_DIR/metadata/regenerated"
   ```

5. Require the downloaded `release-provenance.json` to be byte-equal to
   the regenerated provenance:

   ```bash
   cmp "$RUN_DIR/download/release-provenance.json" \
       "$RUN_DIR/metadata/regenerated/release-provenance.json"
   ```

6. Save the draft body privately and require it byte-equal to the
   regenerated release notes, without printing the body:

   ```bash
   gh api repos/qarnet/le-audio-receiver/releases/367572702 --jq .body \
     > "$RUN_DIR/metadata/draft-body.txt"
   cmp "$RUN_DIR/metadata/draft-body.txt" \
       "$RUN_DIR/metadata/regenerated/release-notes.md"
   ```

7. Require the git-ref lookup for `refs/tags/v0.1.0` to return exact
   HTTP 404 (the draft is untagged). Keep the private captured response
   but parse only the first HTTP status line with stdlib Python using the
   anchored expression `^HTTP/\S+\s+(\d{3})\b`; require exactly one parsed
   status and exact code 404. Any missing/malformed status, success,
   auth/network/rate-limit error, or other code fails. Never print the
   response body; do not use grep/jq over JSON or hardcode an HTTP
   protocol version:

   ```bash
   gh api --include repos/qarnet/le-audio-receiver/git/ref/tags/v0.1.0 \
     > "$RUN_DIR/logs/tag-ref-lookup.txt" 2>&1 || true
   python3 - "$RUN_DIR/logs/tag-ref-lookup.txt" <<'PY'
   import re
   import sys

   with open(sys.argv[1], "rb") as fh:
       response = fh.read()
   statuses = re.findall(rb"^HTTP/\S+\s+(\d{3})\b", response, re.MULTILINE)
   if len(statuses) != 1 or statuses[0] != b"404":
       raise SystemExit("tag ref lookup: expected exactly one HTTP 404 status")
   PY
   ```

## 4. Extraction

Only after `prepare-draft-release.py` passes and both byte-equality checks
hold, test and extract the ZIPs into separate fresh target directories:

```bash
python3 -m zipfile --test "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip"
python3 -m zipfile --test "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip"
mkdir "$RUN_DIR/extracted/nrf5340" "$RUN_DIR/extracted/nrf54l15"
python3 -m zipfile -e \
  "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf5340-e83-factory.zip" \
  "$RUN_DIR/extracted/nrf5340"
python3 -m zipfile -e \
  "$RUN_DIR/download/le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip" \
  "$RUN_DIR/extracted/nrf54l15"
```

Extract and verify exact members:

- nRF5340: `merged.hex`, `merged_CPUNET.hex`;
- nRF54L15: `cpuapp.hex`, `flpr.hex`.

Record SHA-256 for all four extracted images in the run manifest. Never
copy any extracted image into `build/`.

## 5. Firmware/build identity preflight

Before hardware, prove the current tooling commit changed no
firmware/build input relative to the draft target:

```bash
git diff --exit-code 3d9a9186ec288484a637dac1dc7460319daf5e84 -- \
  src/ boards/ prj.conf CMakeLists.txt Kconfig Kconfig.sysbuild \
  sysbuild.cmake sysbuild/ dongle/
```

Any diff blocks FR4 until classified. The expected FR4 tooling diff is
scripts/tests/internal docs only.

Record accepted FR3 software evidence (65/65 canonical gate, contract
95/95, coverage and BSim pins) and the clean tooling-commit canonical
gate. Do not rebuild release firmware: the exact draft bytes are the
tested candidate.

## 6. Probe identity

Run `nrf-probes` immediately before each target flash and preserve the raw
output. Resolve the serial only at execution time:

```bash
nrf-probes                      # raw table: probe serial -> chip
nrf-probes --find nrf53         # nRF5340 probe serial
nrf-probes --find nrf54l        # nRF54L15 probe serial
```

Never place a static serial-to-board table in the procedure or results.
Record raw DPIDR, AP/FICR PART, and VARIANT evidence.

## 7. nRF5340 direct extracted-image flash, no recovery

Read-only attach/examine preflight first:

```bash
PROBE="$(nrf-probes --find nrf53)"
openocd -f interface/cmsis-dap.cfg -c "adapter serial $PROBE" \
  -c "transport select swd" -c "adapter speed 1000" \
  -f target/nordic/nrf53.cfg \
  -c init -c "nrf53.cpuapp arp_examine" -c shutdown
```

If `nrf53.cpuapp arp_examine` fails, stop. Do not call `nrf53_recover`.

Flash with the explicit normal sequence (not `flash_both` or
`check_approtect`, both of which can auto-recover). The repo-owned
`boards/ebyte/e83_nrf5340/support/flash_nrf5340.tcl` is sourced only for
the normal flash and UICR helper procedures:

```bash
APP_HEX="$RUN_DIR/extracted/nrf5340/merged.hex"
NET_HEX="$RUN_DIR/extracted/nrf5340/merged_CPUNET.hex"
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
  -c "halt" \
  -c "wait_halt 2000" \
  -c "flash probe 2" \
  -c "flash write_image erase $NET_HEX" \
  -c "verify_image $NET_HEX" \
  -c "uicr_unprotect_net" \
  -c "reset run" \
  -c shutdown
```

Preserve the full log. Both exact extracted images must report
verification success (`verify_image` for `merged.hex` on cpuapp and for
`merged_CPUNET.hex` on cpunet, each immediately after its write and
before UICR handling). Any UICR warning, recovery line, verify/error, or
unexpected warning blocks acceptance. Normal range erase preserves
settings and bonds.

Do not use `fw-flash-5340`, west rebuild, `--hex-file` against a build
tree, or copy staging for FR4.

## 8. nRF54L15 direct extracted-image flash

Use the runtime probe and the stock xiao board OpenOCD config:

```bash
PROBE="$(nrf-probes --find nrf54l)"
CPUAPP_HEX="$RUN_DIR/extracted/nrf54l15/cpuapp.hex"
FLPR_HEX="$RUN_DIR/extracted/nrf54l15/flpr.hex"
openocd -c "adapter serial $PROBE" \
  -f "$ZEPHYR_BASE/boards/seeed/xiao_nrf54l15/support/openocd.cfg" \
  -c init \
  -c "reset halt" \
  -c "nrf54l-load $CPUAPP_HEX" \
  -c "verify_image $CPUAPP_HEX" \
  -c "nrf54l-load $FLPR_HEX" \
  -c "verify_image $FLPR_HEX" \
  -c "reset run" \
  -c shutdown
```

Explicit order: init, reset halt, `nrf54l-load` exact `cpuapp.hex` then
`verify_image` same path, `nrf54l-load` exact `flpr.hex` then
`verify_image` same path, reset run, shutdown. Preserve the full log. Do
not use a build-tree helper or copy staging. RRAM writes only encoded
image ranges and preserve settings and bonds. Any identity mismatch,
verify error, warning, or unexpected reset blocks acceptance.

## 9. Boot capture

Start `scripts/read_acm.py` before each reset/flash and retain logs:

- nRF5340 console: `/dev/ttyUSB0`, 115200 8N1.
- nRF54L15 console: live Xiao CDC port resolved at execution time
  (normally `/dev/ttyACM0`), 115200 8N1.

```bash
python3 scripts/read_acm.py ttyUSB0 "$RUN_DIR/logs/nrf5340-boot.log" 60 &
python3 scripts/read_acm.py ttyACM0 "$RUN_DIR/logs/nrf54l15-boot.log" 60 &
```

Require both targets: `BLE ready`, `settings_load() OK`, random identity,
I2S ready, and `Advertising as "LE Audio Receiver"`; zero unexplained
warnings or errors. nRF54L15 additionally requires FLPR handshake init,
READY/ACK, rings/offload/runtime init, and ACTIVE health.

## 10. Autonomous central setup

Use only the repo-authorized nRF5340DK `hci_uart` central on
`/dev/ttyACM2`, H4 1,000,000 baud, attached as `hci0`. Run the AGENTS.md
btattach/btmgmt ritual before each target session:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 </dev/null >/tmp/btattach.log 2>&1 &
sleep 5
sudo btmgmt --index hci0 power off
sudo btmgmt --index hci0 power on
sleep 2
sudo btmgmt --index hci0 io-cap 3
sudo btmgmt --index hci0 sc on
```

Require address `C0:AA:BB:CC:DD:EE` and settings
`powered le secure-conn cis-central`. Run the Python central and gates
without sudo.

Capture the receiver identity from the exact boot log of the current
session; pass it via `--peer-addr` or `--receiver-address`. Never reuse
historical receiver addresses as assumed truth.

## 11. Hardware matrix

All durations below are minima. Each row needs exact command, exit
status, central mode/fps/frame count, receiver summaries, and warning
scan. For **each target**, run rows in this exact stateful order.

Before each of fresh mono, fresh Mode A, and fresh Mode B, invoke the
production `bt unpair` command on the receiver and require the
target-correct exact success text:

- nRF5340 (feature-off legacy path):
  `Pairing mode reset: bonds cleared; open pairing enabled.`
- nRF54L15 (feature-on path):
  `Pairing reset complete: bonds cleared; BONDING advertising active.`

The default `bap_central.py` fresh strategy owns central Device1 removal
and re-pairing. Any reset failure blocks the row.

1. Fresh mono 120 s:

   ```bash
   python3 scripts/bap_central.py --mono --peer-addr <live> --duration 120
   ```

   Require exactly one transport, `Stream mode: mono`, 120-byte SDU.

2. Fresh Mode A 120 s (no mode flag):

   ```bash
   python3 scripts/bap_central.py --peer-addr <live> --duration 120
   ```

   Require exactly two transports, `Stream mode: stereo_a`, 120-byte SDU
   per transport.

3. Fresh Mode B 120 s:

   ```bash
   python3 scripts/bap_central.py --stereo --peer-addr <live> --duration 120
   ```

   Require exactly one transport, `Stream mode: stereo_b`, 240-byte SDU.

4. Preserved-bond Mode B 120 s: run immediately after successful fresh
   Mode B with no intervening receiver `bt unpair` or central
   `RemoveDevice`, so the exact bond created by that fresh row is
   retained:

   ```bash
   python3 scripts/bap_central.py --stereo --peer-addr <live> \
     --preserve-bond --duration 120
   ```

   Require Pair skipped, encrypted bonded reconnect, clean disconnect,
   advertising restart, and stream success.

5. Exact-address 7.5 ms PipeWire gate 30 s minimum, against that retained
   bond. Use target-specific receiver log and output directory with
   concurrent console capture:

   ```bash
   # nRF5340 example; nRF54L15 uses the same pattern with its own names
   # (live Xiao CDC port resolved at execution time, normally ttyACM0).
   mkdir -p "$RUN_DIR/metadata/nrf5340-7p5"
   python3 scripts/read_acm.py ttyUSB0 \
     "$RUN_DIR/logs/nrf5340-7p5-receiver.log" 60 \
     > "$RUN_DIR/logs/nrf5340-7p5-reader.log" 2>&1 &
   READER_PID=$!
   # Wait until the reader has opened the console port.
   for _ in $(seq 1 50); do
     grep -q "Opened" "$RUN_DIR/logs/nrf5340-7p5-reader.log" 2>/dev/null && break
     sleep 0.2
   done
   python3 scripts/bluez-wireplumber-gate.py --receiver-address <live> \
     --duration 30 --log "$RUN_DIR/logs/nrf5340-7p5-receiver.log" \
     --output-dir "$RUN_DIR/metadata/nrf5340-7p5/"
   wait "$READER_PID"
   ```

   Use the current system PipeWire/WirePlumber; require parsed
   `Frame Duration: 7500 us`, about 133.3 fps, duration-consistent SDUs,
   and zero fault fields. Never overwrite one target's evidence with the
   other: nRF5340 evidence lives under `nrf5340-7p5*` names and nRF54L15
   under `nrf54l15-7p5*` names
   (`$RUN_DIR/logs/nrf54l15-7p5-receiver.log`,
   `$RUN_DIR/metadata/nrf54l15-7p5/`).

**Why this order**: `bt unpair` before every fresh row guarantees the
receiver is in open pairing mode with zero bonds, so the fresh central
strategy (central `RemoveDevice` + re-pairing) is never rejected by a
stale receiver bond. In NORMAL/BONDED_ONLY mode a surviving bond can make
the receiver reject or demote a supposedly fresh central, which would
masquerade as a fresh-pair success. Running preserved-bond Mode B
immediately after fresh Mode B, with no intervening bond deletion on
either side, then proves persisted-bond reconnect against the exact bond
the fresh row created: Pair skipped, encrypted bonded reconnect, clean
disconnect, advertising restart. The exact-address 7.5 ms gate runs
against that same retained bond, proving the PipeWire path on the bonded
receiver without ever weakening the fresh-pair rows.

For nRF5340 also capture mid-stream `audio status` during a bonded Mode B
row: drift ACTIVE, APLL path/resampler identity, zero decode errors, I2S
underruns, and stream resets.

## 12. nRF54L15 FLPR gates

For nRF54L15 additionally require:

1. Healthy Mode A and Mode B offload: submit equals success, fallback 0,
   all offload/ring/runtime fault counters zero.
2. FLPR hang recovery Mode A, 180 s, through `flpr_hang_gate.py`:
   16/16 PASS, ACK, fallback observed during fault, epoch change, one
   recovery/restart, probation cleared, final ACTIVE, all fault counters
   zero.
3. Timed stall gate during a live Mode B stream through
   `flpr_stall_gate.py`: exact 60 ms ACK, recovery/probation success, then
   healthy `flpr status`, `flpr ring status`, `flpr stress 5`, and
   `flpr ring test 50`.
4. Machine-observable pairing reset through the exact artifact: exact
   feature-on success text, old bond rejected, fresh pair succeeds,
   preserved-bond reconnect succeeds, reboot returns NORMAL with saved
   bond.

Physical button/LED observations are not repeated in FR4 because current
central-only policy allows no user input except audibility. Record
inherited P8 evidence explicitly, not as a fresh observation: `c966dc2`
accepted physical 3 s/8 s button and LED patterns; between `c966dc2` and
draft target `3d9a918...`, pairing source/config/DT changes are comments
only (`src/pairing_mode.c`, `src/user_pairing_io.c`, `src/main.c`, board
conf/overlay, Kconfig/CMake), so compiled pairing behavior/config is
unchanged. FR4 exact-artifact shell reset, pair/reconnect/reboot rows
exercise the same transition owner. If this identity claim does not hold
at execution-time diff review, stop rather than weakening the criterion.

## 13. Acceptance and warnings

Every stream requires central success plus receiver
`decode_err=0 i2s_underrun=0 stream_reset=0`. PLC due RF loss is
recorded, not automatically failed. For all logs require zero unexplained
`LOG_WRN`/`LOG_ERR`, FATAL, fault, assert, stack overflow,
`i2s_nrfx: Next buffers not supplied on time`, `Cannot write in state`,
sequence discontinuity, or unexpected recovery. Expected fault-injection
lines appear only inside named FLPR hang/stall windows and must reconcile
to final healthy counters.

Audibility is optional and is the only permitted fresh user observation.
Ask after autonomous runs complete; record the exact user response or
`UNAVAILABLE`, never infer sound from counters.

## 14. Evidence retention

Retain all logs under `$RUN_DIR`, create `MANIFEST.md` and `SHA256SUMS`
for the retained run directory, and record exact paths/hashes in later
FR4 results. Do not commit raw logs or downloaded firmware.

FR4 accepts only when every mandatory row passes. Any failed or partial
row leaves FR4 open and the draft unpublished. FR5 alone owns manual
publication and post-publication lightweight-tag verification.

## 15. Stop rules

Stop and escalate on any of: unexpected debug lock, probe/board identity
mismatch, checksum/digest mismatch, asset drift (ID/name/size/digest),
missing device, unexpected warning or error in any log, `arp_examine`
failure, recovery line, verify failure, or any command failure. After two
materially different failed attempts, preserve the worktree and escalate
with exact evidence.
