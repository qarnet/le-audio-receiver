# RH3-ModeA1 dual-CIS starvation isolation handoff

Status: approved one-run hardware diagnostic. Named open product defect from
the first RH3 matrix attempt (`rh3-matrix-20260903-rh3a`, commit `ea73413`):
fresh Mode A `48_4_1` delivered `rx_valid=135/133` of `12644` expected per
stream (98.9% loss, `crc_error<=1`, controller `rx_unreceived~=14300` per CIS)
while source scored-complete and terminal-passed, and the same-row mono
(single CIS) delivered perfectly (`rx_valid=12644`, `plc=12`). The FLPR
offload active snapshot froze at `submit=116` (~1.2 s into streaming), which
coincides with the starvation onset. The RH3b raw-evidence fallback
(commit `f0ae6d8`) now guarantees truthful summary capture, so this run's
limits verdict is trustworthy in both directions.

## Goal and hypothesis structure

One bounded diagnostic row that isolates the FLPR-offload variable from the
Mode A dual-CIS path. Binary outcome:

- With FLPR offload DISABLED (cpuapp ASRC fallback), Mode A passes the frozen
  limits: the failure locus is the FLPR offload pipeline interacting with
  dual-CIS receive (its stall at submit=116 is causal or tightly correlated).
- With offload disabled, Mode A still starves: the locus is the
  transport/controller/buffer path (dual CIS at 10 ms, `BT_ISO_RX_BUF_COUNT=3`
  shared across two CIS, SW-split central CIS scheduling), and FLPR is
  cleared as the primary suspect.

This run does not fix anything, does not claim root cause beyond the binary
locus split, and does not authorize production config change.

## Exact diagnostic configuration

Create `tests/hil/receiver-offload-disabled.conf`:

```text
CONFIG_AUDIO_OFFLOAD_ASRC=n
CONFIG_WARN_EXPERIMENTAL=y
```

Rationale: `AUDIO_OFFLOAD_ASRC` default-selects on nRF54L15 with
`AUDIO_RESAMPLER_ASRC_LINEAR`; disabling it routes decoded PCM through the
cpuapp ASRC (the documented production fallback path for offload faults) with
no other behavior change. `AUDIO_RESAMPLER_ASRC_LINEAR` stays enabled; the
resampler stays identical. No trace instrumentation, no workqueue
experiments: this run changes exactly one production-path variable.

## Build proof

```bash
nix develop --command fw-build-54l15 \
  -DEXTRA_CONF_FILE="$PWD/tests/hil/receiver-offload-disabled.conf"
```

Prove resolved config (diagnostic vs normal):

```bash
diag_config=build/nrf54l15/le-audio-receiver/zephyr/.config
for expected in \
  '# CONFIG_AUDIO_OFFLOAD_ASRC is not set' \
  'CONFIG_AUDIO_RESAMPLER_ASRC_LINEAR=y' \
  'CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS=y' \
  '# CONFIG_TRACING is not set' \
  '# CONFIG_HIL_BAP_ENABLE_TRACE is not set' \
  '# CONFIG_HIL_SDC_HCI_REMOVE_ISO_PATH_TRACE is not set' \
  'CONFIG_BT_ISO_RX_BUF_COUNT=3'; do
  rg -Fx "$expected" "$diag_config"
done
sha256sum build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex \
  tests/hil/receiver-offload-disabled.conf
```

Record exact diagnostic CPUAPP/FLPR/fragment hashes. FLPR image is unchanged
(`45ab8d15...`); offload-disabled cpuapp still boots the FLPR and completes
the handshake (production fallback behavior - verify `flpr status` shows
Ready/ACKed/Healthy in the run's post-stop evidence).

## One-run execution

Run ID (validated unused):

```text
rh3-modea1-20260903-offload-disabled
row: rh3.fresh_mode_a_48_4_1
```

Preflight identical to the matrix handoff (disk gate, run-ID validation,
output-path ownership, fixture validate, source-image hash; receiver image is
the diagnostic build just proven). Then:

```bash
set +e
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-modea1-20260903-offload-disabled \
  --junit /tmp/opencode/hil-runs/rh3-modea1-20260903-offload-disabled.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
status=$?
set -e
printf 'status=%s\n' "$status"
```

Outer tool timeout 3600000 ms. Status 0/1/130 immutable; never rerun.

## Read-only review and classification

Integrity (`SHA256SUMS`), then:

- If PASSED: extract per-slot summaries; verify limits margin; confirm FLPR
  handshake healthy and cpuapp-ASRC path active (post-stop offload counters
  must show `submit=0 success=0` for the ASRC offload section since offload
  is compiled out; state STOPPED). Classification: FLPR offload pipeline is
  implicated in the Mode A starvation (locus narrowed). Next phase would be
  FLPR-offload dual-CIS investigation, not a production config change.
- If FAILED on transport limits: extract exact per-slot `rx_valid`, `plc`,
  `rx_lost`, ISO link-quality tail, and the FLPR active snapshot (should stay
  `submit=0` - offload disabled). Classification: transport/controller path
  implicated; FLPR cleared as primary suspect. Next phase moves to
  controller/buffer diagnostics (e.g. `BT_ISO_RX_BUF_COUNT` sensitivity at
  dual CIS).
- If FAILED on a different boundary: record exact boundary/detail; classify
  per triage protocol; stop for review before any further run.

## Normal restoration

```bash
nix develop --command fw-build-54l15
sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
rg -Fx 'CONFIG_AUDIO_OFFLOAD_ASRC=y' build/nrf54l15/le-audio-receiver/zephyr/.config
git diff --check
```

Local rebuild does not reflash. Disk gate re-check after.

## Documentation and commit

Write `docs/development/system-hil-rh3-modea1-result.md` (canonical result:
scope, evidence root, exact hashes/config, outcome, binary classification,
both-branch interpretation limits, restoration, stop point). Update
`system-hil-resume-state.md` (run count, last flash identity, RH3-ModeA
status line). Commit exactly:

```text
docs(hil): record Mode A offload-isolation diagnostic
```

Include the fragment, result doc, resume-state update, and handoff. No
production code changes in this phase; the fragment is diagnostic-only.

## Return report

Preflight; build proof (config lines, hashes); run status and outcome;
exact per-slot summary values and limits verdict; FLPR handshake/post-stop
offload observations; binary classification with the evidence line that
decides it; integrity results; raw identity; restoration proof; doc paths;
commit hash; blockers/deviations.