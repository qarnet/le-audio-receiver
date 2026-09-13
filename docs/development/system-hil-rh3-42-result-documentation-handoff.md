# RH3-42 result documentation handoff

Status: document completed H42 passed diagnostic. Do not run builds or touch
hardware.

## Goal

Record immutable H42 callback-return evidence, correct restart state, and link
the completed physical result from H42 software preparation. This is
documentation only. Do not make an acceptance, production, trace-effect, or
causal-repair claim.

## Ground truth

The completed execution is:

```text
run ID: rh3-20260903-42-bap-enable-trace
row:    rh3.fresh_mode_b_48_3_1
root:   /tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace
```

Read these immutable files before editing:

```text
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/result.json
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/environment.json
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/images.json
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/identity.json
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/source-jlink.txt
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/source-flash.log
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/source-records.jsonl
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/receiver-console.bin
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/receiver-active-status.txt
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/receiver-status.txt
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/receiver-post-stop-status.txt
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/junit.xml
/tmp/opencode/hil-runs/rh3-20260903-42-bap-enable-trace/SHA256SUMS
docs/development/system-hil-rh3-41-tx-notify-workqueue-untraced-result.md
docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md
docs/development/system-hil-resume-state.md
```

Already reviewed facts:

- `environment.json` records runner status `0` and NCS `3.3.0` on dirty HEAD
  `c13fe204e4d7f2b0cdd1dcc4222bf2773b2b51e1`.
- `result.json` records `outcome=passed`, `first_failed_boundary=null`,
  `failure_detail=null`, and `cleanup_failures=[]`. JUnit has one test and zero
  failures, skipped tests, or errors. `SHA256SUMS` verification passed `26/26`.
- H42 exact diagnostic CPUAPP SHA-256 is
  `533f2f82f48e2e5d073eb419ddbb682acd24de838fa10d9f0e2627067dcd0e8a`;
  FLPR, source app, and source CPUNET hashes are unchanged from H41 software
  result. Fragment SHA-256 is
  `065162a7d152530ea511294b71a58f9d2488febd483718258efef0aa7b98c9f7`.
- The diagnostic retained WQ enabled, 1536-byte stack, default priority `8`,
  default init priority `50`, one `HIL_BAP_ENABLE_TRACE` marker, no HCI/SDC
  trace, and no `CONFIG_TRACING`.
- Exact raw marker extraction is:

  ```json
  {"h42_enable_markers": [["0", "0"]]}
  ```

  Receiver console and active status retain
  `HIL BAP enable: stream[0] start=0`.
- Source lifecycle reached `idle -> configured -> connecting -> secured ->
  discovered -> qos -> streaming -> scored_complete -> teardown`, then terminal
  pass. Final stream counters were `seq=16859 sub=16859 sc=16000 sf=0 cb=16859
  out=0`; `first_errno=0`.
- Receiver summary retained `rx_valid=24`, `rx_lost=19462`, `decoded=38972`,
  `plc=38924`, `rx_no_ts=12`, zero decode errors, I2S underruns, stream resets,
  and empty SDUs. ISO tail retained `rx_unreceived=18891`, zero CRC errors,
  retransmissions, and duplicates. No audio capture was available. These are
  observations only, not audio-health or causal proof.
- Post-stop receiver diagnostics retained zero push failures and FLPR handshake
  Ready, ACKed, Healthy, with no recorded handshake errors. The 7.5 ms 360-frame
  row uses documented nRF54L15 CPUAPP ASRC fallback, so zero FLPR submits is
  expected here.
- Receiver raw identity: nRF54L15, nrf-probes serial `8EE9B3FF`, DPIDR
  `0x6ba02477`, part `0x00054b15`, variant `AAC0`.
- Source raw identity: nRF5340, J-Link `001050023938`, DPIDR `0x6ba02477`, AP
  map `0x84770001`, `0x84770001`, `0x12880000`, `0x12880000`, part
  `0x00005340`, variant `0x514b4141`.
- `source-flash.log` retains existing documented page-tail OpenOCD messages
  `Warn : Adding extra erase range` at `0x01023afc .. 0x01023fff` and
  `0x0005741c .. 0x00057fff`. Both programming and verification steps reported
  success. Do not hide them or call them a new repair/diagnostic result.
- Normal local output was restored without reflash. Normal CPUAPP hash is
  `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`.
  Marker was absent from normal ELF. Disk was `120.0 GiB` before and after.
- H41 and H42 are not identical images. H42 adds the one gated marker. Both use
  the private workqueue and no broad tracing. H41 failed before streaming; H42
  passed. This contrast proves neither trace effect nor workqueue causation.

## Scope

Touch only:

```text
docs/development/system-hil-rh3-42-bap-enable-trace-result.md
docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md
docs/development/system-hil-resume-state.md
```

### In scope

1. Create canonical detailed H42 result.
2. Link H42 software result to completed physical result.
3. Correct current restart state facts made stale by H42.

### Out of scope

- Code, Kconfig, fragments, tests, runner, build, source image, HIL execution,
  hardware, evidence mutation, cleanup, deletion, Nix GC, staging, commit,
  `STATUS.md`, public docs, or another diagnostic plan.

## Exact documentation shape

### 1. Canonical result

Create `docs/development/system-hil-rh3-42-bap-enable-trace-result.md`.
Use sections for status/scope, immutable evidence, exact images/configuration,
marker observation, row/lifecycle result, bounded H41 comparison, restoration,
and stop point.

State all exact marker limits. Specifically, `start=0` proves only receiver
callback return success. It does not prove H41's callback ran, source reception
of enabled state, ASCS delivery, radio ordering, a cause, a repair, production
safety, or workqueue adoption. State H42's single row passed but does not turn
the high-loss/no-capture observations into audio acceptance.

Record expected existing source page-tail OpenOCD `Warn` records exactly and
their programming/verification outcome. Do not copy raw binary logs into repo.

H42 is a completed bounded diagnostic and must not be retried. The stop point
must not prescribe a repair. It may say a later causal or production phase
needs a new reviewed plan.

### 2. Software-result stop text

Replace only final future-tense text in
`system-hil-rh3-42-bap-enable-trace-software-result.md` with a link to the new
canonical result. Preserve software proof.

### 3. Resume state

In `system-hil-resume-state.md`:

1. Correct retained physical-run count to twenty-three: sixteen failed, one
   cancelled, and six passed direct diagnostic/control executions. State H40
   and H42 are bounded diagnostics, not acceptance evidence.
2. Replace H41 as last recorded runner-owned flash with H42's exact CPUAPP and
   FLPR hashes. Preserve statement that local normal restoration does not prove
   current hardware image.
3. Append concise `## RH3-42 current stop point` section linking canonical
   result, preserving immutable root, prohibiting retry/adoption, and requiring
   a new reviewed plan before later hardware or production work.

Keep all detailed H42 facts in canonical result. Do not revise historical
H40/H41 text or older historical count snapshots.

## Verification

Docs only. Do not run builds, test gates, Nix, hardware, or tools that mutate
artifacts.

```bash
git diff --check
! rg -n '—' \
  docs/development/system-hil-rh3-42-bap-enable-trace-result.md \
  docs/development/system-hil-rh3-42-bap-enable-trace-software-result.md \
  docs/development/system-hil-resume-state.md
git status --short
df -h . /tmp /nix/store
```

Return changed paths, facts recorded, verification output, final status,
free-space result, no-build/no-hardware/no-commit confirmation, and blockers or
deviations.
