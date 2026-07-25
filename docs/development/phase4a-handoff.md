# Phase 4a Handoff — nRF54L15 First End-to-End Audio Stream

Status: ready for implementation/execution (2026-07-25)

## Goal

Prove the full Linux BlueZ → LE Audio CIS → nRF54L15 BAP sink → LC3 decode →
I2S20 data path for the first time. Produce serial and logic-analyzer evidence,
fix only blockers encountered in this path, and commit a concise results record.

This phase proves data flow, not final drift quality. GRTC+DPPI remains mandatory
Phase 4b work and is explicitly out of scope here.

## Current verified baseline

- Clean `main` at/after commit `5ef9546`.
- Toolchain shell is `nix-nrf-dev` via `mkNrfShell`; do not replace it or eval
  Nordic sdk-manager environment globally.
- `nix develop` provides:
  - `west`, `openocd`, `nrf-probes`, `nrfutil`, sigrok-cli, all `fw-*` helpers.
  - shell Python with `dbus-python` + `pygobject3`.
  - `scripts/bap_central.py --help` works and finds liblc3.
- nRF54L15 build + flash work (`fw-build-54l15`, `fw-flash-54l15`).
- Xiao console: `/dev/ttyACM0`, 115200 8N1, use serial-mcp.
- Xiao I2S20 wiring:
  - D0/P1.4 → BCLK
  - D1/P1.5 → WSEL/LRCK
  - D2/P1.6 → DIN/SDOUT
  - 3V3 → logic analyzer CH3
- Logic analyzer: `0925:3881`, sigrok `fx2lafw`, 24 MHz works.
- Logic channels this session: CH0=D0/BCK, CH1=D1/LRCK, CH2=D2/DIN,
  CH3=3V3, CH4–CH7 unconnected.
- Primary BlueZ controller: hci0 = ASUS BT540 / Realtek RTL8761BU,
  address `A0:AD:9F:7B:C7:95`.
- Receiver was already visible under BlueZ as
  `/org/bluez/hci0/dev_DB_A6_0C_05_A2_AA`, exposing PACS `0x1844` and ASCS
  `0x1850`.
- Fallback controller: hci1 = nRF5340 DK USB HCI (`2fe3:000b`). Do not use it
  until hci0 has failed for a clearly central-side ISO/CIS reason.
- `SESSION_USB_TABLE.md` is a session snapshot; re-verify identities rather
  than trusting static bus/device numbers.

## In scope

1. Rebuild and flash latest nRF54L15 firmware.
2. Capture a clean boot through serial-mcp before/while reset occurs.
3. Fix all build/boot warnings or errors encountered. No warning is accepted as
   “expected.”
4. Run `scripts/bap_central.py` from `nix develop` on hci0.
5. Make minimal test-driver/firmware fixes required for the first stream.
6. Capture serial evidence, BlueZ/test-driver output, and a short sigrok I2S
   capture that overlaps active streaming.
7. Query `audio status` before and after streaming.
8. Record exact results in `docs/development/phase4a-results.md`.
9. Run verification gates and commit the completed scoped work.

## Known blockers to investigate first

### A. Stock-DK SPI NOR is enabled on Xiao

Previous boot printed:

```text
<err> spi_nor: Device id 00 00 00 does not match config c2 28 17
```

Resolved DTS shows stock-DK node `&mx25r64` (`jedec,spi-nor`) status `okay`
under `spi00`; Xiao hardware has no such flash. Fix in
`boards/nrf54l15dk_nrf54l15_cpuapp.overlay` by disabling the unused node and,
if safe after checking resolved DTS users, the unused SPI peripheral. Rebuild,
flash, and prove the boot error is gone. Do not suppress the log.

### B. `bap_central.py` assumes a newly discovered device

The receiver is already cached in BlueZ. Current script only listens for future
`InterfacesAdded` signals, then starts discovery; it does not enumerate existing
`ObjectManager.GetManagedObjects()` entries. It may time out despite the receiver
being present. Make the script:

- inspect existing hci0 `Device1` objects before discovery;
- prefer the existing object whose Name/Alias contains `LE Audio Receiver`;
- only discover if no matching existing object exists;
- handle already-paired and already-connected states without treating them as
  fatal errors;
- keep hci0 as the explicit primary adapter for this phase (do not add broad
  adapter-selection refactoring unless needed for hci1 fallback).

## Out of scope

- GRTC + DPPI implementation (Phase 4b; mandatory next phase).
- Controller algorithm tuning based on ISO timestamp noise.
- Long-duration ≥10 minute stability/listening test (Phase 4c).
- ASRC/FLPR work (Phases 5/6).
- nRF5340 receiver changes unless hci0 and hci1 both fail and the known-good
  receiver fallback is required to isolate the central/test setup.
- General refactors unrelated to the observed stream blocker.

## Execution order

1. **Inventory + baseline**
   - `git status`, `git log --oneline -5`.
   - `nrf-probes`; `btmgmt -i hci0 info`; `btmgmt -i hci1 info`.
   - Confirm `/dev/ttyACM0` maps to Xiao CMSIS-DAP CDC.
   - Confirm sigrok detects `fx2lafw`.

2. **Fix boot noise first**
   - Disable stock-DK SPI NOR on Xiao overlay.
   - `fw-build-54l15` and scan complete output for warnings/errors.
   - Start serial-mcp on `/dev/ttyACM0`, clear its log, then `fw-flash-54l15`
     so reset/boot is captured.
   - Boot must show BLE/settings/I2S/advertising and no `spi_nor`, ISO buffer,
     Kconfig, or other warning/error lines.

3. **Capture baseline stats**
   - Through serial-mcp write `audio reset-stats\n`, then `audio status\n`.
   - Preserve output.
   - Note: shell status exposes Frames decoded/PLC/decode errors/I2S underruns/
     stream resets/drift state/ppm. It does **not** expose `recv_cnt`; use
     `Frames decoded` as the observable receive/decode progress for Phase 4a.

4. **Run first stream on hci0**
   - Run inside the project shell, e.g.:

     ```bash
     nix develop --command python3 scripts/bap_central.py --duration 30 --freq 1000
     ```

   - Start with mono/default. If mono succeeds, run Mode B stereo:

     ```bash
     nix develop --command python3 scripts/bap_central.py --stereo --duration 30 --freq 1000
     ```

   - Do not invoke `sudo python3` outside `nix develop`; that loses the Nix
     Python runtime. If system D-Bus policy or socket acquisition genuinely
     requires privilege, diagnose first and preserve the dev-shell environment
     explicitly rather than installing packages globally.

5. **Parallel evidence capture**
   - Keep serial-mcp open throughout.
   - Save BAP driver stdout/stderr verbatim to `/tmp/phase4a-bap.log`.
   - Once `Streaming ...` or receiver `Stream started` appears, capture a short
     overlapping window (roughly 0.5–1.0 s) at 24 MHz. Avoid a 30-second raw
     capture (hundreds of MB):

     ```bash
     sigrok-cli --driver fx2lafw --config samplerate=24mhz \
       --time 500ms -O binary -o /tmp/phase4a-i2s.bin
     ```

     If this sigrok build does not accept `--time 500ms`, use a bounded sample
     count (12,000,000 samples = 0.5 s at 24 MHz).
   - Decode/measure:
     - CH0 BCK ≈ 3.072 MHz (48 kHz × 64).
     - CH1 LRCK ≈ 48 kHz, BCK/LRCK ratio exactly 64.
     - CH2 toggles during PCM data.
     - CH3 remains high throughout.

6. **Post-stream stats + serial review**
   - Send `audio status\n` after mono and stereo runs.
   - Frames decoded must increase substantially (about 100 frames/s per active
     stream; exact total depends on mode).
   - Decode errors = 0; I2S underruns and stream resets do not climb in steady
     state; no `LC3 decoder not ready`, slab-full, or DMA-underrun warnings.
   - Preserve all timestamps/error codes; do not summarize away evidence.

7. **Fallback decision (only if needed)**
   - If hci0 fails before receiver ASCS callbacks and failure is clearly
     central/BlueZ ISO/CIS, document exact D-Bus/HCI error and try hci1.
   - For hci1 all-zero public BD, use a documented temporary static/privacy
     address approach (`btmgmt -i hci1 privacy on` if sufficient); record every
     management command. Do not overwrite persistent controller state blindly.
   - If both centrals fail, stop and report before flashing the nRF5340DK as a
     receiver unless the user explicitly approves that fallback step.

## Allowed fixes during execution

- Disable incorrect stock-DK peripherals in Xiao overlay.
- Make `bap_central.py` robust to cached/paired/connected devices.
- Fix concrete BAP endpoint negotiation, D-Bus, LC3 packing, pacing, or cleanup
  bugs proven by logs.
- Add minimal diagnostic counters/logs if existing `audio status` cannot prove a
  required criterion. Keep logs bounded and useful; no per-SDU info spam.
- Fix every new warning found; do not suppress without a documented reason.

## Validation

Required commands/results:

```bash
nix flake check --no-build
nix develop --command python3 scripts/bap_central.py --help
fw-build-54l15
fw-build-5340
```

Also run focused Python syntax validation:

```bash
nix develop --command python3 -m py_compile scripts/bap_central.py
```

If actuator/decoder/I2S code changes, run relevant unit tests plus both firmware
builds. Build/boot logs must be scanned for warnings, not merely return zero.

## Phase 4a acceptance criteria

Accepted only when all are true:

1. Latest nRF54L15 image flashes and boots cleanly, with no warnings/errors.
2. BlueZ source establishes BAP transport and writes LC3 SDUs for the requested
   duration (mono required; stereo Mode B required if the source supports it).
3. Receiver logs ASE Configure/QoS/Enable/Start and `Stream started`.
4. Frames decoded climb; decode errors = 0.
5. Logic capture proves BCK/LRCK/DIN activity at correct rates during stream.
6. No steady-state I2S underrun/slab-full/reset storm.
7. Results document contains exact commands, controller used, receiver address,
   serial evidence, LA measurements, failures/fixes, and remaining blockers.

This does **not** complete Phase 4. Phase 4b GRTC+DPPI is next and mandatory.

## Commit + return requirements

- Inspect `git status`, `git diff`, and recent log before committing.
- Stage only Phase 4a files/results; do not include unrelated changes.
- Commit completed scoped work with a concise human commit message.
- Do not push, amend, merge, open a PR, or add AI/tool attribution.
- Return one concise recap containing:
  - files changed;
  - behavior changed;
  - exact tests/commands and results;
  - serial + logic analyzer findings;
  - controller used (hci0 or fallback hci1);
  - commit hash/message;
  - blockers/deviations;
  - recommended Phase 4b follow-up.
