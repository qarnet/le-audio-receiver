# PB-034 continuation evidence, 2026-09-24

## Scope and provenance

The product owner approved full nRF54L15 migration. Cross-worktree history
inspection located the actual continuation in
`/tmp/opencode/le-audio-receiver-pb-fixture`, branch
`feature/nrf54l15-hil-fixture-session`, committed HEAD
`6941c82`. PB-033 already records physical XIAO-to-XIAO proof. PB-034 and
PB-035 through PB-038 were present as uncommitted migration work. Those
changes were preserved, not reconstructed from the older primary worktree.

PB-039 records the renewed approval, without replacing the existing component
items or PB-038 integration ownership. The primary worktree remains at
`12b1ba6`; it is not the implementation location.

These are dirty-worktree diagnostic runs, not clean-commit acceptance. No
production firmware behavior, fixture bytes, TX hash pins, PCM limits,
scenario JSON or parser acceptance was changed during this continuation.
No device was flashed, erased or reset. No commit or PR was created.

## Reproduction

From the continuation worktree:

```sh
nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/pb034-continuation-20260924-01 bash scripts/bsim-stage1-run.sh
nix develop -c python3 -m unittest discover -s tests/unit/bsim_runner -p 'test_*.py'
nix develop -c env BSIM_LOG_ROOT=/tmp/opencode/pb034-continuation-20260924-02 bash scripts/bsim-stage1-run.sh
```

Both runs built the nRF54L15BSim receiver and client. Both failed overall.
Each attempted the first run of all 17 scenarios; failed first runs prevented
the configured second run for the nine repeated scenarios. Neither execution
completed 26 successful runs. The seven focused runner/config tests passed,
which does not establish radio or audio correctness.

Current nRF54L15 build `cmake.out` files reported experimental-symbol notices
for `BT_LL_SW_SPLIT`, `BT_CTLR_SET_HOST_FEATURE`, and the appropriate
`BT_CTLR_PERIPHERAL_ISO` or `BT_CTLR_CENTRAL_ISO` role. No compiler/linker or
Kconfig assignment diagnostic appeared in the two current build output files.
Old nRF5340 output directories are not evidence for these builds.

## Verified fixture fix

Run 01 failed `modea_first_stop_10ms` with:

```text
client: stream 1 send timeout (44 < 45)
```

`scenario_modea_first_stop()` held the TX admission requirement at two streams
even after deliberately disabling the first stream. The surviving stream
could never finish its send cap once the active count fell to one.

The scenario now calls `bsim_tx_set_required_streams(1)` immediately before
disabling the first ASE, after both streams have completed startup. No TX
threshold or lifecycle assertion was weakened.

Run 02 then recorded both device processes exiting zero:

- Client: `sends0=44 sends1=45`, `txh0=0x2F29FED4`,
  `txh1=0x642A8B99`.
- Receiver: 34 paired pushes, `total1=68`, `obs_blk=11`, zero PLC,
  decoder errors and stale halves, PCM maxima 257 and RMS 181/182,
  correlations 32767.
- Strict parser still rejected the old `total1=86` and legacy recipe IDs.
  This is not a canonical pass; approved target-native parser/manifest work
  remains pending.

The before/after public-boundary regression witness is the complete
`modea_first_stop_10ms-run1` directory in each run root.

## Unresolved transport failures

### Single CIS

Both runs failed mono and Mode B. Mono 10 ms reported:

```text
left recipe start0_10ms_l action 5 requires 120-byte corpus payload
```

The 7.5 ms failures occurred at action 7 for mono and action 8 for Mode B.
These observations prove unexpected concealment or missing payload metadata,
not a specific controller defect. The source currently uses a three-SDU
prefill followed by relative interval sleeps. Raw TX/RX sequence, timestamp,
flags and payload traces are needed before assigning root cause or changing
production behavior. Do not rebaseline this loss as ordinary startup.

### One-CIS loss contract: blocking semantic difference

Both runs failed `modea_one_cis_loss_10ms` after right-side omission:

```text
left recipe start0_10ms_l action 48 payload mismatch source=bsim_48k_10ms_120b_l frame=66 expected=bsim_48k_10ms_120b_l/48
```

The receiver logged 14 stale-half/overflow observations before failure.
Normal two-CIS scenarios completed their target-native recipes, so this is
not merely the legacy-startup manifest mismatch.

Installed NCS v3.3.0 explains why omission is not an invalid-SDU injection:

- `zephyr/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_central_iso.c`
  sends a NULL PDU (`npi=1`) when no payload is available (lines 249-259).
- `lll_peripheral_iso.c` only enqueues received ISO data when `!pdu_rx->npi`
  (lines 610-614); RX flush advances counters without manufacturing an SDU.
- `zephyr/subsys/bluetooth/controller/ll_sw/isoal.c` advances the RX SDU
  sequence on SDU emission (lines 621-627). TX packet sequence is not a
  guaranteed end-to-end RX callback sequence.
- Repository `src/audio_stream_session.c` limits timestamp-gap synthesis to
  non-Mode-A paths (lines 531-563). Without right-side callbacks the bounded
  Mode A assembler cannot receive the expected per-event concealment markers.

An explicit zero-length transmitted SDU takes a different path: ISOAL creates
a real complete/end data PDU, and existing receiver code normalizes that empty
SDU into concealment. It could preserve the 18-PLC numerical recipe, but it
would test delivered empty SDUs, not the currently frozen absent-transport
fault. Silently substituting it would change what acceptance proves.

PB-034 currently freezes explicit loss injection and prohibits production
audio changes. Resolution requires an explicit contract decision: separately
qualify absent-callback behavior and use an honestly named empty-SDU PLC
fixture, or expand scope to investigate and repair actual absent-callback
Mode A behavior, with physical reproduction before any production fix.
Neither path has been implemented or claimed accepted.

## Next boundary

Keep historical pins and failed logs intact. Do not remove the old fixtures
or claim migration complete while the target replacement gate is red.
Once the loss-contract decision is resolved, diagnose single-CIS delivery,
finish recipe manifest/parser work, and rerun the entire strict matrix.
