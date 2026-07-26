# Phase 4a.1 — New-DAC Main Receiver Retest

Status: ready for execution

## Goal

Run current receiver firmware unchanged with new DAC, proven nRF5340DK
`hci_uart` central, serial logging, and logic analyzer. Establish whether old
DAC was sufficient explanation for observed receiver I2S failure before making
any integration code change.

## Preconditions

- New DAC connected:
  - D0/P1.04 → BCK
  - D1/P1.05 → LRCK/WSEL
  - D2/P1.06 → DIN
  - common GND/AGND
- MUTE low if DAC requires MUTE pin; record observed DAC model/strap state if
  available. Do not modify wiring or persistent DAC state.
- nRF5340DK `hci_uart` central attached as hci0 through `/dev/ttyACM2`.
- Receiver console `/dev/ttyACM0`; analyzer CH0=D0, CH1=D1, CH2=D2, CH3=3V3.

## Scope

1. Build/flash current nRF54L15 receiver. Existing unstaged diagnostic edits in
   `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` and `src/bt_bap.c` are intended
   test state; preserve them, do not reset/revert/stage them.
2. Attach/verify central if necessary using existing `STATUS.md` procedure.
   Resetting dongle via existing `fw-reset-dongle` is allowed only if it has
   stale connection slots; record it. Do not change dongle source/config.
3. Run 30-second Mode A stream from `scripts/bap_central.py`.
4. Capture receiver serial + sigrok while stream active. Measure external I2S.
5. Update only results documentation, unless concrete evidence shows a narrow
   application queue/producer defect.

## Out of scope

- GRTC/DPPI, drift tuning, ASRC, FLPR.
- New audio architecture/refactor.
- HFXO UsageFault investigation.
- Altering DAC hardware or using old DAC.
- Mass erase/recover or changing bonds.

## Execution

1. Inventory before action:
   ```bash
   git status --short
   nrf-probes
   btmgmt -i hci0 info
   ```
2. Build receiver. Scan complete output for warnings/errors:
   ```bash
   fw-build-54l15
   ```
3. Start serial-mcp on `/dev/ttyACM0` before flashing. If serial MCP fails,
   record exact error and use `scripts/read_acm.py` only for this test.
4. Flash `fw-flash-54l15`; record clean boot. Required boot evidence: BLE,
   settings, I2S ready, advertising, no warning/error.
5. Reset stats then capture baseline `audio status` through console.
6. Ensure hci0 has `powered`, `le`, `secure-conn`, `static-addr`, and
   `cis-central`; reattach existing hci_uart central only if required.
7. Start `btmon` capture and 30-second source stream:
   ```bash
   nix develop --command python3 scripts/bap_central.py --duration 30 --freq 1000
   ```
8. During `Streaming`, capture 0.5–1 s at 24 MHz with fx2lafw. Save raw file
   under `/tmp`. Extract and record actual edge-rate/ratio/data evidence:
   - BCK close to 1.536 MHz;
   - LRCK close to 48 kHz;
   - ratio exactly 32;
   - SDOUT has transitions;
   - 3V3 stable high.
   Do not call capture PASS merely because a raw file exists.
9. Query post-stream `audio status`. Preserve exact counters/log warnings.

## Decision rules

### PASS pending user listening

All must hold:

- BAP source runs requested duration and receiver stream starts;
- frames decoded increase; decode errors zero or explained PLC only;
- no steady-state slab-full/EIO/`Next buffers not supplied on time`;
- analyzer measurements meet expected waveform;
- clean receiver boot.

Do not claim audible audio. Mark it **PENDING USER LISTENING** after technical
criteria pass.

### Technical failure

If main receiver still slab-fills/EIO while standalone did not:

1. Collect exact first-error time, slab/status counters, and analyzer state.
2. Compare application prefill/queue producer timing to standalone test.
3. Instrument only if existing diagnostics cannot distinguish queue starvation,
   driver state, or missing peripheral events. Keep logs bounded.
4. Fix only direct measured defect, rerun once. Do not invoke GRTC/ASRC.

## Results and commit

Create `docs/development/phase4a1-new-dac-main-pipeline-results.md` with
commands, central identity, boot/stream evidence, external analyzer measurement,
audio stats, raw artifact paths, exact user listening pending status, outcome,
and any fix. Update `STATUS.md` only if technical result changes status; do not
write user audible result without user input.

Commit scoped docs and any narrow measured fix. Do not stage pre-existing
unstaged diagnostics. Do not push, amend, merge, or open PR.

## Verification

```bash
nix flake check --no-build
fw-build-54l15
fw-build-5340
```

## Executor recap

Return files/behavior, exact serial/analyzer/stat evidence, builds, commit
hash/message, technical blocker if any, and explicit user listening question.
