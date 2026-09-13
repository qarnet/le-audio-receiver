# RH3 matrix execution handoff (post-RH3a)

Status: approved hardware phase. This is the first RH3 matrix attempt under
the revised plan of record (`docs/development/system-hil-milestones.md`,
2026-09-03) with frozen transport limits enforced.

## Goal and boundary

Run the fixed two-pass RH3 matrix (`run-rh3-matrix`) once on current images:
4 healthy 10 ms rows + reconnect + FLPR hang + FLPR stall = 7 rows per pass,
14 child runs total. Record the aggregate outcome as evidence. A pass records
`TRANSPORT_RUNTIME_ACCEPTED` evidence for the first time; a failure is
classified per the plan's failure-triage protocol and becomes the next work
item. Either outcome is progress; do not retry the same run ID.

This run uses the normal production receiver image (no diagnostic fragments).
The runner enforces the frozen transport limits (RH3a) at every child's
session end.

## Preconditions (verified at handoff writing)

- Repository HEAD `3755d92`, clean worktree.
- Disk `123.9 GiB` free (gate: 80 GiB).
- Normal local images byte-identical to the committed identities:
  - Receiver CPUAPP `e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f`
  - Receiver FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`
  - Source app `f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333`
  - Source CPUNET `4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48`

## Fixed identity

```text
matrix run ID: rh3-matrix-20260903-rh3a
output root:   /tmp/opencode/hil-runs
aggregate JUnit: /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.junit.xml
schedule:      rows.rh3_schedule() = 2 passes x 7 rows
```

Validate the run ID and verify both aggregate output paths are absent and
not symlinks immediately before invocation. If either exists, stop.

## Scope

### In scope

1. Read-only preflight (disk, run-ID validation, path ownership, fixture
   validation, image hashes).
2. One `run-rh3-matrix` invocation (runner owns flashing, identity, consoles,
   cleanup, evidence).
3. Read-only integrity review of aggregate and child evidence.
4. Result documentation (aggregate outcome, per-child outcomes, any limit
   violations with exact values).
5. Commit the result documentation.

### Out of scope

- Any production source/Kconfig change, any diagnostic fragment build.
- Manual hardware actions (serial, debugger, OpenOCD, nrf-probes, flash,
  reset) outside the runner; `nrf-probes` read-only invocation for identity
  evidence is allowed only if the runner did not already retain it.
- Retrying this matrix run ID, retrying failed child rows ad hoc, or starting
  RH3-7p5 work. Failed child rows are classified, not rerun in this phase.
- `STATUS.md`/public docs (documentation phase follows).

## Ordered procedure

From repository root, sequentially:

```bash
python3 -c 'import shutil; free = shutil.disk_usage(".").free; print(f"free_gib={free / 1024**3:.1f}"); assert free >= 80 * 1024**3'
git status --short   # must be empty or documentation-only
git diff --check

python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-matrix-20260903-rh3a"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'
test ! -e /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a
test ! -L /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a
test ! -e /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.junit.xml
# child-root sibling is derived; the runner validates it internally.

nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json

sha256sum --check <<'EOF'
e67265c14faa7a9e860178f65f6b50d6d96c56d6956a490300c620a112b2267f  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
f0e1c5ab74c1ce53c3c5bda1f1082026971e9c81d6e36f9967f6abb789a21333  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
EOF
```

Then the single matrix invocation with outer tool timeout `10800000` ms
(3 hours; 14 child runs x up to ~10 min). Do not use shell `timeout`:

```bash
set +e
nix develop --command ./scripts/hil-runner.py run-rh3-matrix \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-matrix-20260903-rh3a \
  --junit /tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a.junit.xml
matrix_status=$?
set -e
printf 'matrix_status=%s\n' "$matrix_status"
```

Status 0 (passed), 1 (failed), 130 (cancelled) is immutable evidence. Let the
runner finish cleanup and evidence finalization. Never rerun.

## Read-only review

Verify aggregate integrity, then per-child integrity; record outcomes:

```bash
AGG=/tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a
(cd "$AGG" && sha256sum --check SHA256SUMS)
for d in $(python3 - <<'PY'
import json
with open("/tmp/opencode/hil-runs/rh3-matrix-20260903-rh3a/children.jsonl") as fh:
    for line in fh:
        rec = json.loads(line)
        print(rec["child_run_id"])
PY
); do
  echo "== $d"
  (cd "/tmp/opencode/hil-runs/$d" 2>/dev/null && sha256sum --check SHA256SUMS 2>&1 | tail -1)
done
```

Read aggregate `result.json`, `schedule.json`, `children.jsonl`, JUnit. For
each failed child, read its `result.json` failure boundary/detail and
extract receiver summary values where present. Classify each failure per the
plan (product / fixture / environment) with the evidence line that justifies
the classification. Do not write into any evidence root.

## Documentation and commit

Write `docs/development/system-hil-rh3-matrix-20260903-result.md`:

- aggregate outcome, matrix status, attempted/completed child counts;
- per-row table (pass 1 and pass 2): outcome, boundary, failure detail
  (exact), or PASS counters;
- every transport-limit violation with exact observed value and frozen
  threshold;
- integrity results (aggregate + children);
- raw identity evidence (receiver/source probe serials, DPIDR, PART);
- classification of each failure per the triage protocol;
- explicit statement of what the outcome does and does not prove
  (a pass is transport/runtime acceptance evidence for these exact images;
  it is not audio acceptance; a failure is classified evidence, not a
  diagnosis of cause).

Then update `docs/development/system-hil-resume-state.md` (run count, last
matrix outcome) and `STATUS.md` (System HIL section) with a concise factual
summary, and commit everything as one scoped commit:

```text
docs(hil): record first RH3 matrix attempt under frozen transport limits
```

## Return report

Return: preflight results; matrix status and aggregate outcome; per-row
outcomes; every limit violation with exact numbers; integrity results; raw
identity evidence; classifications; documentation paths; commit hash;
blockers/deviations. Report a blocker instead of improvising if the runner
fails before any child completes (fixture/lock/discovery class), if a
precondition hash mismatch appears, or if evidence integrity fails.