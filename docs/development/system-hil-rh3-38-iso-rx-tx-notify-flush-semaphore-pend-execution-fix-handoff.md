# RH3-38 execution run-ID correction handoff

Status: approved replacement physical execution. This correction replaces only
the invalid 67-character run ID in
`system-hil-rh3-38-iso-rx-tx-notify-flush-semaphore-pend-execution-handoff.md`.
Read that base handoff in full. Its scope, evidence boundaries, immutable image
hashes, source/config/link checks, preflight commands, one-run rule, evidence
review, and normal-local-build restoration apply unchanged except where this
document explicitly overrides them.

## Why replacement is valid

The original H38 runner invocation used:

```text
rh3-20260825-38-sdc-hci-iso-rx-tx-notify-flush-semaphore-pend-trace
```

It has length `67`; `scripts/hil/lifecycle.py` accepts only IDs matching
`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`. Runner failed at `_step_validate()` before
fixture-lock acquisition or run-directory creation. It returned failed status
`1`, not CLI status `2`, because `Runner.run()` caught `HilLifecycleError`.

No hardware, identity resolution, flash, reset, serial access, source control,
or evidence directory was created. No HIL execution occurred. The old ID must
never be invoked again.

## Replacement identity

```text
run ID: rh3-20260825-38-sdc-iso-rx-pend-trace
length: 37
run-ID validation: PASS
row: rh3.fresh_mode_b_48_3_1
output directory: /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace
external JUnit: /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace.junit.xml
```

At correction creation, all four replacement destinations are absent and are
not symlinks. Recheck them before trace build and immediately before runner
execution. If any exists or is a symlink, preserve it and stop. Do not choose a
third ID without a new handoff.

Before any build, run this read-only validation in addition to base handoff
step 1:

```bash
python3 -c 'import sys; sys.path.insert(0, "scripts"); from hil.lifecycle import validate_run_id; run_id = "rh3-20260825-38-sdc-iso-rx-pend-trace"; validate_run_id(run_id); print(f"run_id={run_id}\nlength={len(run_id)}\nvalid=yes")'

test ! -e /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace
test ! -L /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace
test ! -e /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace.junit.xml
test ! -L /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace.junit.xml
```

All base-handoff H38 host-only preflight and exact trace build/link proof must
run again. Current local normal image and source/fragment hashes remain exactly
the base-handoff values. Do not build source images or change any source,
config, runner, test, or evidence code.

## One runner-owned execution

Use outer terminal-tool timeout `7200000` ms. Do not use shell `timeout`. After
all base preflight succeeds and replacement absence checks pass again, invoke
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260825-38-sdc-iso-rx-pend-trace \
  --junit /tmp/opencode/hil-runs/rh3-20260825-38-sdc-iso-rx-pend-trace.junit.xml \
  --row rh3.fresh_mode_b_48_3_1 \
  --sdc-hci-remove-iso-path-trace
```

Only `scripts/hil-runner.py` may change targets. No manual flash, reset,
recovery, serial, RF, pairing, OpenOCD, `btattach`, `bap_central.py`, or serial
MCP action. Nonzero status is evidence and never permits retry of replacement
ID. Let runner cleanup/evidence finalization finish.

After runner, follow base-handoff read-only evidence review and normal local
nRF54L15 restoration, substituting replacement run path/JUnit path everywhere.

## Return report

Return exact commands/totals; validated ID length; replacement output ownership
checks; one-run confirmation; raw target identity evidence; image hashes;
checksum/artifact count; all H38 trace/schema records; bounded interpretation;
normal restoration proof; final `git status --short`; no-manual-hardware/no-
commit confirmation; blockers/deviations; and smallest evidence-backed next
step.
