# Refactor R1 revalidation handoff

## Goal

Revalidate repaired R1 from repair tip `b0b4399` after source/test repairs
`e7b222e` and `9c67281`.  First commit this handoff alone, then run full G1 on
that clean docs-only descendant and autonomous G2 on both receivers
with continuous receiver logs and preserved raw identity/controller evidence.
Only after every criterion passes, restore R1 ACCEPTED status and commit final
evidence.

No implementation changes belong in this handoff.  Any source, test, build
script, expected-value, baseline, Kconfig, devicetree, or board change reopens
repair review and invalidates affected gates.

## Preflight

1. Confirm `git rev-parse HEAD` is `b0b4399...` and the only status entry is
   this untracked handoff.  Commit this file alone as
   `docs: define repaired R1 revalidation gate`.
2. Confirm resulting worktree is clean.  Record exact branch, new docs-only
   validation HEAD, repair tip `b0b4399`, NCS/tool versions, date, and host.
3. Verify required tools exist before long runs: repo dev shell, `west`,
   `gcovr`, `nrf-probes`, `openocd`, `btattach`, `btmgmt`, Python DBus and
   pyserial dependencies.
4. Never create/use `scripts/probe-serial.local`.  Resolve target at each flash
   with `nrf-probes`.

## G1 — clean software/build gate

Run from repository root on clean handoff commit directly atop `b0b4399` and
preserve complete raw logs:

```bash
./scripts/test-all.sh 2>&1 | tee /tmp/r1-reval-g1-testall.log
./scripts/test-coverage.sh --output /tmp/r1-reval-coverage --clean-output \
  2>&1 | tee /tmp/r1-reval-g1-coverage.log
fw-build-5340 2>&1 | tee /tmp/r1-reval-g1-build-5340.log
fw-build-54l15 2>&1 | tee /tmp/r1-reval-g1-build-54l15.log
fw-build-dongle 2>&1 | tee /tmp/r1-reval-g1-build-dongle.log
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15 \
  2>&1 | tee /tmp/r1-reval-g1-build-contract.log
git diff --check
```

Capture pipeline exit codes (`set -o pipefail` or equivalent); a successful
`tee` must not hide a failed command.

Acceptance:

- canonical gate exactly 47 PASS / 0 FAIL / 47 TOTAL;
- 28 Twister + 4 exec-only + 12 Python + coverage + matrix + BSim composition;
- coverage population exactly 26 files, gcovr 8.4 / gcov 14.3.0, no
  per-file or aggregate line/branch/function ratio regression, no baseline
  rewrite;
- builds 3/3 pass;
- build contract exactly 76/76;
- BSim Stage 1 hashes, totals, observers, and segment counts unchanged;
- zero actionable compiler, linker, Kconfig, devicetree, boot, and test
  warnings.  Document only warnings already classified in `STATUS.md` “Build
  warning diagnostics”; stop for any new/unexplained warning;
- worktree remains clean.

If G1 fails, stop.  Do not flash hardware or edit expected values.

## G2 evidence directory and raw-log rules

Create one fresh directory under `/tmp`, for example
`/tmp/r1-reval-g2-YYYYMMDD-HHMMSS/`.  Never overwrite old evidence.  Preserve:

- `git-head.txt`, `git-status.txt`, tool versions;
- complete `nrf-probes` table immediately before each flash;
- complete `nrf-probes --help` output showing tool has no verbose/AP-map mode;
- complete `fw-flash-*` output (OpenOCD cross-confirms probe serial and DPIDR);
- continuous receiver console from before flash/reset through both Mode A and
  Mode B plus final disconnect/advertising restart;
- `btattach` log;
- raw `btmgmt --index hci0 info` output;
- full central stdout/stderr for every Mode A/B command and exact exit code;
- a manifest listing commands, start/end times, file paths, and SHA-256 for
  every raw log.

`nrf-probes` output supplies probe serial, DPIDR, FICR PART, and VARIANT.  Its
CLI has no AP-IDR verbose option.  Do not fabricate an AP IDR map: record this
tool limitation explicitly and use complete `nrf-probes` plus OpenOCD DPIDR
cross-confirmation as raw identity evidence, matching accepted T8 precedent in
`docs/testing/pre-refactor-hardware-baseline.md:188-210`.

## Central setup

Use only repository nRF5340DK `hci_uart` central.  No human-operated central.
Before receiver streams, attach/configure per `AGENTS.md`:

```bash
setsid sudo btattach -B /dev/ttyACM2 -S 1000000 \
  </dev/null >"$EVIDENCE_DIR/btattach.log" 2>&1 &
sleep 5
sudo btmgmt --index hci0 power off
sudo btmgmt --index hci0 power on
sleep 2
sudo btmgmt --index hci0 io-cap 3
sudo btmgmt --index hci0 sc on
sudo btmgmt --index hci0 info \
  >"$EVIDENCE_DIR/btmgmt-info.txt" 2>&1
```

Raw info must show controller address `C0:AA:BB:CC:DD:EE` and current settings
including `powered le secure-conn cis-central`.  Run `bap_central.py` without
sudo.  Only its documented raw-HCI helper may use sudo internally.

If sudo, hci0, controller identity/settings, probe, or serial port is
unavailable, stop and report blocker.  Do not substitute another central.

## G2A — nRF54L15 receiver

1. Run and capture fresh `nrf-probes` table; resolve with
   `nrf-probes --find nrf54l`.
2. Start `scripts/read_acm.py ttyACM0` before flash with enough duration to
   cover flash, boot, both 30-second streams, and final teardown (minimum 240
   seconds).  Output must be one continuous receiver log.  Confirm reader has
   opened port before flashing.
3. Flash only with `fw-flash-54l15`, preserving complete output.
4. Wait for boot log.  Extract exact receiver BLE identity from that same
   continuous log.
5. Run Mode A 30 seconds:

   ```bash
   python3 scripts/bap_central.py --peer-addr <BOOT_LOG_ADDRESS> --duration 30
   ```

6. Run Mode B 30 seconds on same boot:

   ```bash
   python3 scripts/bap_central.py --peer-addr <BOOT_LOG_ADDRESS> --stereo --duration 30
   ```

7. Keep reader running through final disconnect and advertising restart, then
   wait for clean reader exit.

Require receiver-side evidence for both runs, not central narrative alone:

- boot: `BLE ready`, `settings_load() OK`, advertising identity/name;
- correct Mode A two-ASE and Mode B single-stereo-ASE configuration/start;
- gate OPEN/CLOSED and clean reconnect between modes;
- nonzero SDUs and decoded frames; zero decode errors;
- `I2S DMA started`; zero I2S underruns and stream resets;
- coordinated FLPR ring reset and `offload prep OK`/ACTIVE for each stream;
- no unexplained cpuapp fallback, integrity fault, timeout, slab error,
  assertion, deadlock, or warning;
- final disconnect and advertising restart.

Known 360-frame fallback is unchanged, but these default 10 ms G2 runs must
remain FLPR-offloaded.

## G2B — nRF5340/E83 receiver

1. Run/capture fresh `nrf-probes`; resolve with `nrf-probes --find nrf53`.
2. Start `scripts/read_acm.py ttyUSB0` before flash for minimum 240 seconds,
   one continuous receiver log through both streams.  Confirm port open.
3. Flash only with `fw-flash-5340`, preserving complete output.
4. Extract receiver BLE identity from same boot log.
5. Run Mode A 30 seconds and Mode B 30 seconds with explicit `--peer-addr`,
   Mode B adding `--stereo`.
6. Keep capture through final disconnect/advertising restart.

Require boot/advertising, correct Mode A/B shape, gate transitions, nonzero
SDUs/decoded frames, I2S DMA start, clean stop/reconnect, APLL path stable, and
zero decode errors, underruns, resets, slab faults, assertions, deadlocks,
warnings, or unexplained behavior.

## Hardware safety and blockers

- Flash only through `fw-flash-5340` / `fw-flash-54l15` (OpenOCD).  Never use
  probe-rs.
- No mass erase, `nrf53_recover`, nRF54 recovery experiment, UICR manual write,
  direct RADIO access, persistent-setting reset, firmware source change, or
  destructive shell command.
- If stale pairing blocks smoke, first use documented central-side cache
  removal only.  If receiver-side unpair/reset or destructive recovery appears
  necessary, stop and report instead of changing persistent receiver state.
- Any warning/fault, missing receiver log section, central nonzero exit,
  wrong target identity, or competing receiver invalidates that row.  Repeat a
  row only after identifying reason; preserve failed raw evidence too.

## Final evidence update

Only after G1 and all four G2 streams pass:

1. Update `docs/development/refactor-r1-results.md` with:
   - repair chain `e7b222e`, `9c67281`, `b0b4399`;
   - clean validated/flashed handoff commit full hash and repair tip `b0b4399`;
   - exact G1 commands, exits, runtime, suite counts, coverage totals/tool
     versions, build results, contract count, BSim disposition, warning
     classification;
   - exact raw G2 evidence directory/log paths and SHA-256 manifest;
   - raw target identity evidence and AP-map tool limitation;
   - per-run receiver and central counters/outcomes;
   - explicit statement that earlier `a79ac71` evidence is superseded.
2. Change R1 heading/status in `docs/development/refactor-plan.md` and results
   from REOPENED to ACCEPTED with date and link.
3. Ensure focused counts in `docs/testing/coverage-matrix.md` match fresh G1
   exactly (audio_i2s 60, identity 58, lifecycle 31, handshake 46, ring manager
   67 unless fresh execution proves otherwise).
4. Run:

   ```bash
   python3 scripts/check-test-matrix.py --repo-root "$PWD"
   python3 -m json.tool tests/test-matrix.json >/dev/null
   git diff --check
   ```

5. Inspect status/diff/log, stage only this handoff plus final evidence docs,
   and commit one evidence commit, suggested message:

   `docs: accept repaired R1 with fresh G1 and G2 evidence`

No G1/G2 rerun is needed solely because final commit changes evidence docs.

## Executor escalation and recap

Do not weaken checks, repin outputs, normalize warnings, invent evidence, or
silently alter procedure.  After two materially different failed attempts at
one blocker, stop and return exact commands/errors/logs/status, attempts, one
question, and smallest hypothesis.  Do not commit partial or knowingly false
acceptance evidence.

Final recap must provide:

- G1 exact results and log paths;
- G2 evidence directory, identity/controller raw evidence, all four command
  exits and receiver-side counters;
- warnings/fault scan;
- files changed and evidence commit hash/message;
- final clean status;
- deviations/blockers.

No push, amend, merge, PR, force-push, attribution footer, or unrelated cleanup.
