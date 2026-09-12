# RH3 ModeA11 handoff: SDC fixture TX-outstanding target six fix-validation

Status: approved one-run fix-validation diagnostic. Validates the classified
mechanism from the ModeA10 SDC run
(`docs/development/system-hil-rh3-modea10-sdc-result.md`): at per-stream TX
outstanding target 3, the completion-paced source host occasionally misses
the SDC controller's CIS event preparation point, SDC transmits the late SDU
in the next event, the current event fires empty, and the receiver conceals
two Mode B frames per empty event. ModeA10 ended `12643/12644` valid SDUs
with `plc=2378 = 2 x rx_lost=1189`, failing only the PLC ceiling (1383 of
decoded=27664). This run deepens the outstanding target to 6 (60 ms of
scheduling margin) using the committed Kconfig lever and fragment; the
receiver and the SDC netcore wiring are unchanged from ModeA10.

## Fixed identity

```text
run ID: rh3-modeb-sdc-txout6-20260907 (validate unused before invoking)
row:    rh3.fresh_mode_b_48_4_1
```

Source build: current-HEAD tree plus the uncommitted ModeA10 SDC rework
(overlay, sysbuild.cmake, comment files) plus the committed fragment
`tests/hil/source-txout6.conf` via `EXTRA_CONF_FILE`. Receiver: normal
current-HEAD build, no fragments.

## Sequence

1. Preflight: disk gate 80 GiB; `git rev-parse HEAD` + `git status
   --porcelain` (expected: the ModeA10 rework files, the ModeA10/ModeA11
   handoff and result docs, the ModeA4 correction, and the pre-existing
   untracked ModeA3 handoff); run-ID validation; `.locks` empty; fixture
   validate returns the known JSON.
2. Build source twice (determinism proof), each pristine with:

   ```bash
   nix develop --command fw-build-hil-source -DEXTRA_CONF_FILE="$PWD/tests/hil/source-txout6.conf"
   ```

   Prove resolved app-core config contains
   `CONFIG_HIL_SOURCE_TX_OUTSTANDING_TARGET=6`, `CONFIG_HIL_SOURCE_QOS_RTN=5`,
   `CONFIG_HIL_SOURCE_QOS_PHY=2`; resolved CPUNET config still proves the
   SDC block (SOFTDEVICE=y, CENTRAL_ISO=y, SDC_PERIPHERAL_COUNT=0, the two
   SDC ISO TX buffer counts 6, TX_PWR_PLUS_3=y). App-core CPUAPP hash
   changes from the ModeA10 normal-build value (the fragment is an app-core
   change); CPUNET must stay
   `19ffe5d4cfa7f7071f9b5f5211f88ff9a9505c75ce410eb67c0c3baa771f4656`
   (net core unaffected by the fragment). Record all four hashes per build;
   build 1 == build 2 byte-identical.
3. Build receiver normal once at current HEAD; record hashes (CPUAPP is
   APP_COMMIT-derived; FLPR `45ab8d15...` expected unchanged); resolved
   config proves `CONFIG_AUDIO_OFFLOAD_ASRC=y`, `CONFIG_BT_ISO_RX_BUF_COUNT=3`.
4. ONE runner invocation (no special flags), outer timeout 3600000 ms:

   ```bash
   nix develop --command ./scripts/hil-runner.py run \
     --fixture tests/hil/fixture.json \
     --binding tests/hil/fixture.local.json \
     --output-root /tmp/opencode/hil-runs \
     --run-id rh3-modeb-sdc-txout6-20260907 \
     --junit /tmp/opencode/hil-runs/rh3-modeb-sdc-txout6-20260907.junit.xml \
     --row rh3.fresh_mode_b_48_4_1
   ```

5. Integrity review; per-slot summary; limits verdict; FLPR active; ISO
   tail; pacing sanity: the active snapshot must show `out` at or near 5
   (ModeA8 proved `out=5` at target 6). If `out` never reaches near 6, that
   is a misconfiguration stop, not a verdict.

## Prediction (falsifiable)

If the empty-event mechanism is the whole residual, deepening the queue to
six removes the preparation-point misses and the row passes frozen limits:
`rx_valid >= 11379` (expected near 12644) and `plc <= 5% of decoded`
(expected single-digit to low-double-digit PLC from the ~144-SDU preamble
boundary, matching the mono healthy baseline's `plc=12` and the SDC dongle
history's `plc=2`).

## Classification

- PASS: fix validated. The SDC rework plus the target-six default becomes
  the fixture configuration question: keep `3` as committed default and run
  the matrix with the fragment, or change the committed default to 6
  (preferred: the fixture is a dedicated instrument; target 6 is its
  calibrated production value; a committed default avoids fragment
  bookkeeping in every matrix run). Write the ModeA11 result doc, update
  resume-state, commit the SDC rework plus (if chosen) the default change as:

    ```text
    fix(hil): switch fixture net core to SDC and calibrate TX depth
    ```

  Then report readiness for the full RH3 matrix attempt (separate phase, new
  run ID `rh3-matrix-<date>-2`, `run-rh3-matrix`, 14 child runs, outer
  timeout 10800000 ms, no fragment needed if the default changed).

- FAIL with plc still above ceiling and delivery still near-total
  (`rx_lost` small but nonzero, `plc = 2 x rx_lost` preserved): the queue
  depth did not remove the misses. Record the result, leave everything
  uncommitted, and STOP for redesign review; two consecutive same-shape
  failures is the plan's stop point.

- FAIL new signature (delivery collapse, connection loss, warning): record,
  classify, and either one bounded fix-validation rerun with a named change
  or STOP for user decision.

## Result documentation

`docs/development/system-hil-rh3-modea11-sdc-txout6-result.md` (canonical):
scope, prediction vs outcome, preflight, build proof (determinism table,
resolved config), per-slot summary, limits verdict, pacing sanity (`out`
value), FLPR active, QoS, ISO tail, integrity, raw identity, restoration,
stop point.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree notice, global `__ASSERT()`, receiver watchdog
empty-library). No NCS patches. No receiver firmware changes. No
rows/limits/matrix changes. No evidence mutation. No retry beyond the single
fix-validation run this handoff authorizes. Status 0/1/130 immutable.
Preserve all prior evidence roots unchanged, including
`/tmp/opencode/hil-runs/rh3-modeb-sdc-20260907/`.