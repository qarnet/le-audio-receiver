# RH3 SN_STRICT validation handoff (ModeA9)

Status: approved one-run hardware diagnostic. Validates the phase-slip
diagnosis from the thinker analysis (2026-09-04): the fixture SW-split
central's ISOAL `SN_STRICT` default expires TX SDUs whose submission phase
slips past CIS event preparation under completion-paced host refills at
high CIG event duty. The overlay change
(`CONFIG_BT_CTLR_ISOAL_SN_STRICT=n`, already staged in
`hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf` with
recorded reason) is instrument calibration on the HIL source fixture, not
receiver or product work.

## Prediction (falsifiable before the run)

If the phase-slip diagnosis is right, Mode B 10 ms delivery recovers to
PASS under frozen transport limits (`rx_valid >= 11379`,
`plc <= 5% decoded`). If delivery still collapses with the same signature,
the diagnosis is wrong and the next discussion point is
`CONFIG_BT_CTLR_ISOAL_PSN_IGNORE=y` (user decision required; not this run).

## Fixed identity

```text
run ID: rh3-modeb-snstrict-20260904 (validated unused)
row:    rh3.fresh_mode_b_48_4_1
```

Receiver: NORMAL current-HEAD build, no fragments. Source: current-HEAD
plus the staged overlay line (the only uncommitted source-tree change).

## Sequence

1. Preflight: disk gate; `git rev-parse HEAD` + `git status` (expected:
   the overlay modification and untracked handoff docs only);
   run-ID validation; run-dir + external-JUnit ownership; fixture
   validate.
2. Build source with the staged overlay: `nix develop --command
   fw-build-hil-source`. Prove resolved cpunet config contains
   `CONFIG_BT_CTLR_ISOAL_SN_STRICT=n` (i.e. `# CONFIG_BT_CTLR_ISOAL_SN_STRICT
   is not set`) AND unchanged `CONFIG_BT_LL_SW_SPLIT=y`,
   `CONFIG_BT_CTLR_CONN_ISO=y`, `CONFIG_HIL_SOURCE_QOS_RTN=5`,
   `CONFIG_HIL_SOURCE_QOS_PHY=2`, `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=3`.
   Record source app + CPUNET hashes; CPUNET changes (controller config);
   double-build determinism proof.
3. Build receiver normal; record hashes.
4. ONE runner invocation (no special flags), outer timeout 3600000 ms:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modeb-snstrict-20260904 \
  --junit /tmp/opencode/hil-runs/rh3-modeb-snstrict-20260904.junit.xml \
  --row rh3.fresh_mode_b_48_4_1
```

5. Integrity review; per-slot summary; limits verdict; FLPR active
   (10 ms expects submit>=1 success ACTIVE); ISO tail.

## Classification

Runner-validated verdict only:

- PASS: diagnosis validated. The staged overlay line becomes the fixture
  default with the comment's "Validation run pending" clause replaced by
  the run evidence (commit hash + run ID + delivered values). Then write
  the ModeA4 correction note (its "scored-onset correlation" conclusion
  was an uptime-vs-streaming clock conflation; the data supports
  progressive phase-slip; correction appended to the result doc, original
  observations untouched). Then update resume-state. Commit exactly:

  ```text
  fix(hil): relax fixture ISOAL strict sequencing to stop payload expiry
  ```

  Include overlay, handoff, result doc, ModeA4 correction, resume-state.
  Report readiness for the full RH3 matrix attempt (separate phase, new
  run ID).

- FAIL same signature: diagnosis wrong. Revert nothing yet (overlay line
  stays uncommitted), record the result, STOP for the PSN_IGNORE user
  decision.

- Other boundary: record, classify, stop.

## Result documentation

`docs/development/system-hil-rh3-modea9-snstrict-result.md` (canonical):
scope, prediction vs outcome, hashes, per-slot values, limits verdict,
ISO tail, integrity, raw identity, restoration (no receiver change to
restore; source overlay change is intentional and stays), stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global
`__ASSERT()`). No NCS patches, no receiver firmware changes, no
rows/limits/matrix changes, no evidence mutation, no retry, status
0/1/130 immutable.