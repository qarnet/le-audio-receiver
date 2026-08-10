# FR4 nRF54L15 pairing-workqueue stack fix handoff

Date: 2026-08-10

## Goal

Fix unsafe nRF54L15 pairing workqueue high-water discovered during otherwise
successful targeted local hardware validation. Increase dedicated pairing
workqueue stack from 1024 to 1536 bytes, update resolved contract/tests/docs,
and require runtime margin in hardware rerun.

This handoff is config/tests/docs only. Do not run hardware, flash, or touch
release/tag/VERSION/remote state.

## Grounding

Retained successful run: `/tmp/opencode/fr4-cadence-local-stSZAJ`.

`kernel thread stacks` reported unnamed thread object `0x2000ebb8`, real stack
1024, unused 12, usage 1012/1024 (98%). ELF symbol lookup at the same address
identifies `g_pairing_wq` from `src/pairing_mode.c`. This queue owns RESET,
BONDING, feedback, blink, security, and advertising transition work. Twelve
bytes remaining is not acceptable stack margin, especially after an actual
nRF5340 system-workqueue overflow was found in the preceding run.

Current nRF54L15 cpuapp build uses 161084 B of 160 KB RAM (98.32%), leaving
2756 B. Increasing pairing stack by 512 B to 1536 leaves about 2244 B before
alignment/build variation, while changing to 2048 would consume another full
kilobyte unnecessarily. Observed 1012-byte high-water becomes about 66% of
1536, leaving 524 B.

`CONFIG_USER_PAIRING_WORKQ_STACK_SIZE` is already nRF54L15 board-specific and
build-contract pinned. No production source or queue ownership change needed.

## Exact changes

### Board config

In `boards/nrf54l15dk_nrf54l15_cpuapp.conf`:

- change `CONFIG_USER_PAIRING_WORKQ_STACK_SIZE=1024` to `1536`;
- replace stale comments claiming 1024 runtime sufficiency / 2048 cannot fit;
- record pre-fix hardware high-water 1012/1024 (98%, 12 B unused), selected
  1536 budget, expected 524 B margin, and current RAM headroom calculation;
- do not change system workqueue, offload queue, MPSL, ICMsg, BT RX, shell, or
  any other stack.

### Build contract

In `scripts/check-build-contract.py`, keep ID `54l15-041` and assertion count
96, but require exact pairing stack 1536 and update message with high-water
rationale.

In `tests/unit/build_contract/test_build_contract.py`:

- valid fixture uses 1536;
- wrong-stack mutation changes 1536 to 1024 and still requires `54l15-041`;
- test count stays 52.

### Current docs/hardware gate

Update:

- `docs/testing/behavior-contract.md` BUILD-007 current `54l15-041` value and
  pre-fix hardware evidence;
- `docs/testing/coverage-matrix.md` exact stack value if present;
- `docs/development/firmware-release-fr4-cadence-hardware-handoff.md` current
  resume evidence and stack gate:
  - identify pairing queue by matching runtime thread object address to ELF
    `g_pairing_wq` symbol when shell line remains unnamed;
  - require real size 1536;
  - require at least 256 bytes unused and no more than 80% high-water usage;
  - retain full stack table and symbol-resolution evidence;
  - failure blocks nRF54L15 local acceptance.

Do not rewrite retained manifests or claim new hardware success. Historical P8
1024 evidence remains history; current contract must state why it is
superseded.

## Validation

Run:

```bash
python3 tests/unit/build_contract/test_build_contract.py
python3 -m py_compile scripts/check-build-contract.py
git diff --check
fw-build-5340
fw-build-54l15
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
```

Require:

- nRF5340 system workqueue remains 2048;
- nRF54L15 system workqueue remains 2048;
- nRF54L15 pairing workqueue resolves 1536;
- nRF54L15 cpuapp links with at least 2 KB reported RAM headroom;
- no new/actionable warning;
- 96 assertions, 0 failed.

Inspect status/diff/log. Commit:

```text
fix: increase nRF54L15 pairing workqueue stack
```

Do not amend. Require clean worktree, then run `./scripts/test-all.sh`.
Expected 65/65, coverage population 36, contract 96/96, BSim pins unchanged.

## Scope

Expected files:

- this handoff;
- `boards/nrf54l15dk_nrf54l15_cpuapp.conf`;
- `scripts/check-build-contract.py`;
- `tests/unit/build_contract/test_build_contract.py`;
- `docs/testing/behavior-contract.md`;
- `docs/testing/coverage-matrix.md` if exact value exists;
- `docs/development/firmware-release-fr4-cadence-hardware-handoff.md`.

Do not touch production C source, queue naming/ownership, cadence/audio/I2S,
central scripts/QoS, other stacks, coverage baseline, VERSION/workflow,
hardware, release/tag state, or remote branches.

Return files, 52 tests, both builds/warnings, resolved values, RAM delta,
96/96 contract, full gate/coverage/BSim, commit/status, and exact hardware
rerun entry point.
