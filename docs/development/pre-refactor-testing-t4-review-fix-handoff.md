# Phase T4 review-fix handoff

## Goal

Close protocol, oracle, teardown, timestamp, and harness-race defects found in
orchestrator review. Rebaseline hashes only because the FNV seed is corrected.
T4 remains open until strict matrix and final gate pass.

## Scope

- `src/bt_bap.c`
- T4 BSim receiver/client/oracle/observer
- T4 runner/parser and parser tests
- T4 docs, behavior contract, coverage matrix, STATUS
- this handoff

No T5, physical hardware, push, merge, PR, amend, or history rewrite.

## 1 — application ASCS response must be valid

NCS v3.3.0 authoritative evidence:

- `zephyr/include/zephyr/bluetooth/audio/bap.h`, `struct bt_bap_ascs_rsp`,
  accepts application codes SUCCESS, CAP_UNSUPPORTED, CONF_UNSUPPORTED,
  CONF_REJECTED, metadata codes, NO_MEM, UNSPECIFIED. It excludes CONF_INVALID.
- `zephyr/subsys/bluetooth/audio/ascs.c:130-145` warns for excluded app codes.

Change missing/invalid codec-shape response from
`CONF_INVALID / CODEC_DATA` to `CONF_REJECTED / CODEC_DATA`. Keep source
`CONF_UNSUPPORTED / NONE` and pool exhaustion `NO_MEM / NONE`.

Update all client expectations, observer assertions, parser tests, response
tables, behavior contract, and STATUS. Remove ASCS warning allowlist entirely.
Final invalid-codec scenario must contain no `Invalid application error code`
warning.

Convert expected negative remote-request logs (unsupported source, rejected
codec shape, pool full) from LOG_WRN/LOG_ERR to LOG_INF. Malformed-SDU rejection
is also an intentionally handled packet and should log INFO plus observer/error
counter. Final runner must reject every bt_bap/ASCS warning; no scenario warning
allowlist for expected control paths.

## 2 — validate Mode A timestamp metadata

Mode A may use `info->ts` only when `BT_ISO_FLAGS_TS` is set. Before decoding a
Mode A half:

- if TS flag missing, skip decoder/pair mutation/push;
- increment one observable receive/decode fault counter;
- emit test observer event;
- log a real warning (not allowlisted; therefore normal matrix proves zero).

Store each half's original ISO-valid flag separately from “decoder returned
success/PLC.” Pair only equal validated timestamps. Preserve wrap-safe ordering.

Add parser/observer assertions that all normal/lifecycle scenarios have zero
missing-TS events.

## 3 — release must stop sink immediately

When Release closes an open audio path, call `audio_sink_stop()` immediately,
before returning to ASCS. Reset stats only after sink oracle/summary can snapshot
them. Later disabled/disconnect paths remain idempotent and must not create a
second segment or hide pushes.

Scenario `release_without_disable_10ms` must prove sink-stop/segment finalize
occurred on Release before ACL disconnect. Add passive ordering counters/events;
do not infer immediate stop from later disconnect cleanup.

## 4 — correct and define ordered hashes

Initialize full, left, and right hashes to FNV offset basis at every segment
start. Convert each signed sample to `uint16_t` before extracting low/high bytes;
never right-shift negative signed values. Document exact byte order and whether
frame index is prepended/appended consistently. Prefer prepend frame index for
full/L/R, then channel sample bytes, using one helper.

Update oracle unit/parser tests. This deliberately changes all audio hashes:

1. run baseline matrix twice;
2. require pairwise identical full/L/R/count fields;
3. pin new values;
4. run pinned matrix twice.

No other reason may change PCM/hash.

## 5 — strict PLC startup boundary

Do not accept arbitrary post-first-nonzero PLC or claim concealment “inaudible.”
Add passive per-push source-valid metadata from real `bt_bap.c` to BSim oracle:

- mono/Mode B: source-valid equals packet `BT_ISO_FLAGS_VALID`;
- Mode A: source-valid only when both paired halves had VALID set;
- call observer immediately before each sink push;
- observer affects only test oracle, never production state.

Oracle startup remains open until first nonzero push sourced entirely from valid
ISO input. While startup is open, zero or nonzero concealment may be counted as
startup transient; update `startup_plc` after each push. After boundary closes:

- any source-invalid/PLC push is immediate FAIL for normal scenarios;
- final `plc_frames == startup_plc` exactly;
- zero-energy push is immediate FAIL (remove arbitrary 20-push grace);
- no pinned post-start PLC delta table.

Add explicit startup-transient counters to PASS record if useful. Prove Mode A
3/18 PLC deltas move into startup evidence and steady-state delta becomes zero.
Remove unsupported “inaudible” statement everywhere.

## 6 — synchronize custom TX state

Protect cross-thread `tx_streams` registration, pointer, pause, injection,
sequence, limits, and counters with a Zephyr mutex/spinlock or atomics with a
clear ownership protocol. No C data races between scenario thread and TX thread.

Requirements:

- never hold spinlock across blocking allocation/send;
- if using mutex, define lock order and avoid callback recursion;
- unregister cannot clear encoder/stream state while TX uses it;
- getters return synchronized snapshots;
- controls remain deterministic;
- fix the registration LOG format (`%zu` index, `%p` stream pointer with matching
  argument types).

Add focused native/unit tests if practical; otherwise matrix lifecycle scenarios
must exercise pause/unregister/reconnect under assertions.

## 7 — warning-as-error policy

Remove `CONFIG_COMPILER_WARNINGS_AS_ERRORS=n` from T4 runner and compile both
BSim binaries with warnings as errors. If the documented glibc
`_FORTIFY_SOURCE` diagnostic genuinely recurs, capture exact compiler message,
file, compiler version, and upstream ownership; use the narrowest source/flag
suppression with adjacent recorded reason. Never globally disable warning errors
for repo code.

Final BSim compile logs must show zero repo compiler warnings.

## 8 — evidence accuracy and cleanup

- T4 evidence must name exact final review-fix commit, not `f50dcea` or prior
  docs commit.
- Reconcile scenario-15 accepted-config count: nine rejected attempts plus valid
  mono and missing-frame-block fallback are two successes overall. Explain
  per-round observer resets if final receiver PASS exposes only one.
- Remove duplicated runner header comments.
- Temporary debug commits may remain in history under no-rewrite policy, but
  final tree must contain no debug prints or dead debug controls.
- Remove stale PLC-delta parser options if no longer used; parser must fail on
  any post-start PLC.

## Verification

On workstation detached exact-commit worktree:

```bash
BSIM_BASELINE=1 bash scripts/bsim-stage1-run.sh
BSIM_BASELINE=1 bash scripts/bsim-stage1-run.sh
# pin pairwise-identical new FNV hashes
bash scripts/bsim-stage1-run.sh
bash scripts/bsim-stage1-run.sh
python3 tests/unit/bsim_runner/test_bsim_stage1_parse.py
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
git diff --check
```

One full gate after two pinned matrix runs is sufficient because matrix is full
gate's long BSim child; run second full gate only if any non-BSim code changed
after first. Capture durations and exact warnings/errors. Clean all temporary
refs/worktrees/bundles/logs/build dirs.

## Commit

Use logical new commits, no amend. Suggested:

```text
fix: use valid ASCS rejection responses
tests: tighten BAP startup and hash oracles
fix: synchronize BSim audio transmission
docs: correct T4 BAP acceptance evidence
```

Return new hashes, zero post-start PLC proof, response matrix, warning-as-error
result, release ordering proof, tests/gates/builds, commits, cleanup, deviations,
and blockers.
