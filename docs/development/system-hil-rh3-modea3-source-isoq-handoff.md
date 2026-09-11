# RH3-ModeA3 central layout-policy isolation handoff

Status: REVISED after two executor stops. The original source-side
`LE Read ISO Link Quality` design is BLOCKED by an upstream Kconfig gap and
must NOT be retried: `BT_CTLR_READ_ISO_LINK_QUALITY_SUPPORT` is a no-prompt
bool with no selector anywhere in NCS v3.3.0 (verified: no
`select BT_CTLR_READ_ISO_LINK_QUALITY_SUPPORT` exists; hidden-symbol
assignment in a fragment is a hard Kconfig error; patching NCS is out of
scope). Keep the `isoq` firmware/host design below as a deferred phase that
becomes viable only if a future NCS adds a selector. Do not patch NCS.

REVISED PHASE GOAL: isolate the SW-split central's CIS layout policy as the
Mode A/Mode B starvation variable, using a fully supported one-line
diagnostic change.

## Grounding

All runner-validated failing shapes share one controller-side layout fact:
the SW-split central default is
`CONFIG_BT_CTLR_CONN_ISO_RELIABILITY_POLICY` (visible choice, default; see
`zephyr/subsys/bluetooth/controller/Kconfig.ll_sw_split` lines ~293-311).
The reliability policy spreads payloads across the full
Max_Transmission_Latency: receiver tails show `cig_sync_us=8184` (Mode B
10 ms) and `5304` (Mode A) with `nse=6`, versus the passing mono row's
identical `nse=6` but 120-byte PDU. The failing shapes are exactly the ones
whose per-subevent airtime is larger (240-byte Mode B PDU, dual CIS), which
pushes subevent timings deep into the reliability-policy spread window. The
SW-split `LOW_LATENCY_POLICY` choice compacts payload transmission for
lowest latency instead (`cis_sync` collapses toward subevent time).

Hypothesis (no root-cause claim): if the starvation is a
layout/spread-timing interop between the SW-split central reliability
layout and the SDC peripheral at larger PDU airtime, compacting the layout
recovers delivery. If delivery still starves under low-latency packing, the
layout policy is exonerated and the locus moves to radio/window timing
between the two controllers.

## Exact change

One file: `hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf`.
Remove the blocked `CONFIG_BT_CTLR_READ_ISO_LINK_QUALITY=y` line entirely
(the executor left it uncommitted; ensure it is gone). Add:

```text
# RH3-ModeA3 diagnostic-only: compact CIS layout (upstream visible choice).
# Reliability-policy spread is the current starvation suspect; low latency
# compacts subevents. Diagnostic only - not a production choice.
CONFIG_BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY=y
```

Note: AGENTS.md already documents that the SW Split
`CONN_ISO_LOW_LATENCY_POLICY` choice has no NONE fallback (upstream gap);
selecting LOW_LATENCY here is legal because it is the other choice in the
same visible `choice BT_CTLR_CONN_ISO_POLICY_CHOICE`. Reverting to the
default reliability policy is simply deleting the line.

## Build and config proof

```bash
nix develop --command fw-build-hil-source
```

Prove in the resolved cpunet `.config`
(`build/hil-source/hci_ipc/zephyr/.config`):

```text
CONFIG_BT_CTLR_CONN_ISO_LOW_LATENCY_POLICY=y
# CONFIG_BT_CTLR_CONN_ISO_RELIABILITY_POLICY is not set
CONFIG_BT_LL_SW_SPLIT=y
CONFIG_BT_CTLR_CONN_ISO=y
```

No `READ_ISO_LINK_QUALITY` lines anywhere. Source app + CPUNET hashes change
(HEAD-dependent CPUAPP note applies to app core; CPUNET hash must be
recorded as the new diagnostic source identity).

## Hardware phase (one run)

Same shape as the Mode B control for direct comparison:
row `rh3.fresh_mode_b_48_4_1`, new run ID
`rh3-modeb-lowlat-20260904` (validate unused first), normal receiver image
(current HEAD, no fragment), diagnostic SOURCE image. Preflight identical to
the Mode B control handoff including source-image hash record and a
double-build determinism proof for the source images. ONE runner invocation
(outer timeout 3600000 ms, status immutable, never rerun).

Classification (runner-validated only):

- PASS under frozen limits: central layout policy implicated; next phase is
  a production-decision handoff (keep low-latency for the fixture vs
  investigate reliability-policy layout with Nordic), plus reinstating the
  full matrix attempt.
- FAIL on limits: layout policy exonerated at this shape; locus moves to
  inter-controller radio/window timing; next phase designs a controlled
  RF/proximity or PDU-size ladder diagnostic.
- Other boundary: record, classify, stop for review.

Also record the receiver ISO tail `cig_sync_us`/`cis_sync_us` for the
low-latency run and compare with the reliability-policy Mode B control
(`cig_sync_us=8184`): the layout change must be visible in the tail as
reduced sync delays (sanity proof the policy actually changed on air).

## Result documentation and commit

`docs/development/system-hil-rh3-modea3-result.md` (canonical result),
resume-state update (run count, source image identity change, verdict).
Commit exactly:

```text
docs(hil): record central layout-policy isolation verdict
```

Include the overlay change, handoff, result, resume-state. No attribution
footers, no push.

## Deferred: source-side isoq (blocked, keep design)

The full `isoq` firmware/host design from the previous revision stays in
this document's git history and is viable only when an NCS release adds a
selector for `BT_CTLR_READ_ISO_LINK_QUALITY_SUPPORT`. Do not implement it
now.

## Constraints

Runner owns all hardware. Actionable build warnings are errors (allowed:
documented dirty-tree, nRF54L15 watchdog empty-library, global `__ASSERT()`;
a Kconfig "assigned but got" warning for the removed READ_ISO_LINK_QUALITY
line must NOT reappear - the line must be gone). No NCS patches, no receiver
firmware changes, no rows/limits changes, no evidence mutation, no retry.

AMENDMENT (after first executor stop): no controller patch is needed. The
NCS v3.3.0 SW-split controller implements `LE Read ISO Link Quality`
(`ull_iso.c` lines 1020-1070) behind
`CONFIG_BT_CTLR_READ_ISO_LINK_QUALITY`, whose dependency
`BT_CTLR_READ_ISO_LINK_QUALITY_SUPPORT` is already satisfied because
`BT_LL_SW_SPLIT` selects `BT_CTLR_CENTRAL_ISO_SUPPORT`
(`Kconfig.ll_sw_split` line 46). The blocker was only that the source
cpunet overlay never sets the visible symbol. Resolution: add
`CONFIG_BT_CTLR_READ_ISO_LINK_QUALITY=y` to
`hil/source/app/overlay-nrf5340_cpunet_iso-bt_ll_sw_split.conf` (it
complements the existing ISO controller settings there). After building,
prove the resolved cpunet `.config` contains
`CONFIG_BT_CTLR_READ_ISO_LINK_QUALITY=y`. If Kconfig rejects it (dependency
not actually satisfied in the resolved tree), stop and report the exact
resolved dependency state - do not patch NCS.

## Grounding: why source-side evidence

Runner-validated verdicts so far: mono 10 ms delivers perfectly; Mode A
(dual CIS) and Mode B (240-byte SDU) starve with the same signature -
receiver controller reports near-total `rx_unreceived`, zero CRC errors,
host alive, FLPR healthy, no I2S faults. In Mode B the receiver's
`rx_unreceived=13288` EXCEEDS the central's `tx_last_subevent=11376`, and the
receiver's expected `nse=6`: the peripheral counts subevents the central
never transmitted or that never reached it, while the link stays
CRC-clean. The missing evidence is the CENTRAL side: the HIL source never
reports its own ISO link-quality counters, so central-side
transmitted/unacked/flushed/subevent counts are invisible. The receiver
already has this diagnostic (`src/bt_bap.c` `iso_link_quality_read` via
`BT_HCI_OP_LE_READ_ISO_LINK_QUALITY`).

Hypothesis space this evidence splits (no root-cause claim yet):

- central actually transmitting fewer subevents than the peripheral expects
  (layout/NSE interop between SW-split central and SDC peripheral);
- central transmitting but peripheral radio missing them (RF/window timing
  at larger PDU airtime);
- source host starving its own controller TX queue under 240-byte/dual-CIS
  load (would show as central TX buffer exhaustion / unacked growth).

## Goal

Add a synchronous `isoq` command to the HIL source shell that reports
central-side ISO link quality for every active stream, and host runner
collection of it during the scored phase. Diagnostic-only; does not change
run lifecycle, row behavior, or acceptance rules.

## Exact changes

### 1. Source firmware

Files: `hil/source/core/hil_source_types.h`,
`hil/source/core/hil_source_protocol.c` (+ its header),
`hil/source/app/src/hil_source_bap.c/.h`, `hil/source/app/src/hil_source_app.c`.

- Add `HIL_SOURCE_CMD_ISOQ` to the command enum, parser mapping for
  `"isoq"`, and `hil_source_command_name` case emitting `"isoq"`.
- Backend (`hil_source_bap.c`): add `iso_link_quality_read()` mirroring the
  receiver's implementation in `src/bt_bap.c` lines ~290-345
  (`BT_HCI_OP_LE_READ_ISO_LINK_QUALITY` with per-stream
  `bt_hci_get_conn_handle`), plus a backend ops hook
  `isoq(&run_state, &out_isoq)` returning one record per active stream:
  `handle, tx_unacked, tx_flushed, tx_last_subevent, retransmitted,
  crc_error, rx_unreceived, duplicate` (central-side fields; the
  iso_interval/sync layout fields are receiver-side and omitted here).
  Streams with no conn handle are skipped; zero active streams returns an
  empty list with ok=true.
- Handler `hil_app_handle_isoq`: synchronous status record (same envelope
  as `status`) with command `"isoq"`, `ok`, `error`, and a new
  `"isoq_streams"` array. The status envelope gains this ONE new key for the
  isoq command only. Runs under the app mutex like other synchronous
  handlers; callable while a run is active (that is the point: mid-scored
  capture) - it must only READ state, never mutate run_state.
- Host protocol note: `scripts/hil/protocol.py` validates HIL1 `status`
  records loosely (data is a dict); unknown data keys are allowed by the
  parser today - VERIFY this in `source_client.validate_active_status`:
  if it rejects unknown keys, extend its accepted-key set for command
  `isoq` only.
- Keep `CONFIG_HIL_SOURCE_APP_TEST` fake backend updated with the new ops
  entry (test-only no-op returning empty list).

### 2. Host runner

Files: `scripts/hil/source_client.py`, `scripts/hil/runner.py`.

- `SourceClient` gains a bounded synchronous helper
  `collect_isoq(timeout)` reusing `_sync_command("isoq", "isoq", ...)`;
  record retained in the run's summary JSON.
- Runner: in `_collect_receiver_tail` (or immediately after it, while the
  source is still in scored phase), call `source.collect_isoq()` once per
  segment and store the records in the segment summary
  (`summary["receiver_streams"][i]["source_isoq"]`). Keep it bounded and
  cancellation-aware. On `ok=false` or missing streams when the source is
  still streaming: fail the row (fixture evidence gap), EXCEPT when the
  source has already torn down (segment complete), where absence is
  expected and the record is simply retained as absent.
- Evidence: include the isoq status line in the existing
  `source-records.jsonl` retention automatically (it already retains every
  record line).

### 3. Tests

`tests/unit/hil_source_app/` (native): isoq command parse round-trip;
active-stream record shape; empty-stream ok=true; read-only (no state
mutation - assert run_state unchanged); fake backend op invoked once.
`tests/unit/hil_source_control/`: parser accepts `isoq` command string.
`tests/hil/rh2_test.py`: fake wire serves an isoq status record during the
scored phase; row passes and `summary.json` retains `source_isoq`; source
still streaming but isoq absent/failing -> row fails at the boundary;
post-teardown absence tolerated.
`scripts/test_hil_runner.py`: any CLI-level coverage follows existing
patterns; no new flags.

### 4. Firmware build + verification

- `nix develop --command fw-build-hil-source` must pass with only allowed
  diagnostics (dirty-tree notice class; the source build is a normal
  sysbuild).
- Native unit suites for source app/control/signal must pass.
- Full host verification block:

```bash
python3 -m py_compile scripts/hil/source_client.py scripts/hil/runner.py
python3 -m pytest -q scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/
python3 -m pytest -q tests/unit/hil_source_app tests/unit/hil_source_control tests/unit/hil_source_signal 2>/dev/null || \
  (cd tests/unit/hil_source_app && python3 -m pytest -q .)   # follow existing native/Twister invocation actually used by the repo; if these are Twister suites, use the repo's established runner instead of pytest
python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

Use the repo's existing invocation for native source suites (see
`docs/development/system-hil-rh1a-handoff.md` history or
`scripts/test-all.sh` for the established command). Do not invent a new
runner.

## Out of scope

- No receiver firmware changes, no row/limit/matrix changes, no new
  hardware run (next phase: one Mode B rerun with the new evidence capture
  under a new run ID, once this lands).

## Commit

Exactly:

```text
feat(hil): capture central-side ISO link quality during scored phase
```

## Return report

Changed paths; command/protocol semantics; runner collection boundaries;
test names and proofs; native/host totals; commit hash; deviations.
## Supersession note (2026-09-11)

This handoff is DORMANT and SUPERSEDED, not pending. It predates two settled
developments: the fixture net core switched from SW-split to SDC (ModeA10,
2026-09-07) and the source moved to 128 MHz + mirrored controller-clock
scheduling (2026-09-08/09), after which the full reinstated RH3 matrix
including all Mode A rows passed 20/20 (TRANSPORT_RUNTIME_ACCEPTED for both
10 ms and 7.5 ms, 2026-09-11). The SW-split layout-policy question this
handoff investigated no longer exists in the fixture; the upstream Kconfig
gap noted above also still stands. Do not implement any part of this
handoff without a new reviewed plan.
