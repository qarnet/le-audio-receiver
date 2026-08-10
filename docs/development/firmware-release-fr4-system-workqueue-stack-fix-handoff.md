# FR4 nRF5340 system-workqueue stack fix handoff

Date: 2026-08-10

## Goal

Fix real nRF5340 `sysworkq` stack overflow observed during strict fresh-mono
stream establishment on local HEAD `7d4fc71`. Increase only nRF5340 cpuapp
system-workqueue stack from 1024 to 2048 bytes, pin resolved value in build
contract, and preserve all cadence/audio behavior.

This handoff is code/config/tests/docs only. Do not run hardware or touch
release/tag/VERSION/remote state. Hardware validation resumes after review
under existing user approval.

## Grounding

Retained run: `/tmp/opencode/fr4-cadence-local-bcrVW1`.

Exact receiver sequence:

- fresh pairing completed;
- ASE Config and QoS succeeded;
- before Enable/ISO callback, receiver emitted `USAGE FAULT`, `Stack overflow
  (context area not valid)`, `ZEPHYR FATAL ERROR 2`, current thread
  `sysworkq`, then rebooted;
- central Acquire failed downstream;
- resolved nRF5340 cpuapp config proves
  `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=1024`, `CONFIG_THREAD_STACK_INFO=y`,
  `CONFIG_INIT_STACKS=y`, `CONFIG_STACK_SENTINEL=y`, and
  `CONFIG_HW_STACK_PROTECTION=y`, so fault classification is authoritative;
- same target had passed earlier runs, making overflow timing-sensitive, but
  one valid production flow exhausting stack is sufficient proof that 1024 is
  unsafe. Do not normalize or retry unchanged firmware.

Budget evidence:

- fresh nRF5340 cpuapp build reports 145296 B used of 448 KB RAM (31.67%);
  adding 1024 bytes has ample headroom;
- current nRF54L15 cpuapp resolved system-workqueue stack is already 2048;
- installed NCS v3.3.0 applications commonly select 1536-4096 bytes for
  Bluetooth/application system workqueues; 2048 is a conservative smallest
  doubling and matches this project's other receiver target;
- project-owned offload and pairing work run on dedicated queues. This change
  protects Zephyr/Bluetooth system work that remains on `sysworkq`; do not move
  unknown stack work or invent a new queue.

## Exact implementation

### Board config

In `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf`, add:

```conf
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048
```

Add nearby normal comments recording:

- hardware fault evidence (`sysworkq`, 1024-byte resolved stack, fresh mono
  Config/QoS transition);
- 2048-byte selected budget;
- 1 KB RAM cost and observed 448 KB region headroom;
- nRF54L15 already resolves 2048 and needs no config change.

Keep setting board-specific. Do not add it to shared `prj.conf`, netcore config,
or nRF54L15 board config. Do not change shell, pairing, offload, RX, main, ISR,
or other stack sizes.

### Resolved build contract

In `scripts/check-build-contract.py`, add one nRF5340 app-config assertion:

- ID `5340-032`;
- exact `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE == 2048`;
- message states hardware-validated system-workqueue stack budget.

Build contract total becomes **96 assertions**.

In `tests/unit/build_contract/test_build_contract.py`:

- add `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048` to valid nRF5340 app fixture;
- add one mutation test replacing 2048 with 1024 and requiring failure ID
  `5340-032`;
- preserve all current tests. Test count becomes 52.

### Current contract/docs

Update:

- `docs/testing/behavior-contract.md` BUILD-007: nRF5340 2048-byte resolved
  stack assertion, 96 assertions, 52 tests, and hardware rationale;
- `docs/development/firmware-release-fr4-cadence-hardware-handoff.md`: current
  build-contract expectation 96/96 and, after each successful target row,
  capture `kernel thread stacks`; for nRF5340 require `sysworkq` stack size
  2048 and retain used/unused high-water evidence. Stack command failure or
  unreadable sysworkq line blocks acceptance;
- `docs/testing/coverage-matrix.md` build-contract test count/description if it
  carries exact count.

Do not rewrite historical 95/95 results, retained run manifests, FR3/FR4 exact
draft facts, or accepted past phase evidence. Do not claim hardware pass.

## Validation

Focused:

```bash
python3 tests/unit/build_contract/test_build_contract.py
python3 -m py_compile scripts/check-build-contract.py
git diff --check
```

Build both receiver targets pristine:

```bash
fw-build-5340
fw-build-54l15
```

Require:

- nRF5340 resolved `.config` exact 2048;
- nRF54L15 resolved value remains 2048;
- no new/actionable warning;
- nRF5340 RAM increase explained and ample;
- direct resolved build contract:

```bash
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
```

Expected **96 assertions, 0 failed**.

Inspect status/full diff/log. Commit complete green work only:

```text
fix: increase nRF5340 system workqueue stack
```

Do not amend. Require clean worktree, then run:

```bash
./scripts/test-all.sh
```

Expected 65 PASS / 0 FAIL / 65 TOTAL, coverage population 36, build contract
96/96, BSim pins unchanged.

## Scope

Expected files:

- this handoff;
- `boards/ebyte_e83_nrf5340_nrf5340_cpuapp.conf`;
- `scripts/check-build-contract.py`;
- `tests/unit/build_contract/test_build_contract.py`;
- `docs/testing/behavior-contract.md`;
- `docs/testing/coverage-matrix.md` if exact count exists;
- `docs/development/firmware-release-fr4-cadence-hardware-handoff.md`.

Do not touch firmware source, cadence formula/API/tests, central scripts/QoS,
I2S/drift/ASRC, other stacks, coverage baseline, VERSION/workflow, hardware,
release/tag state, or remote branches.

Return files, focused test count, both builds/warning classification, resolved
stack values, RAM delta, 96/96 contract, full gate/coverage/BSim, commit/status,
deviations/blockers, and exact hardware rerun entry point.
