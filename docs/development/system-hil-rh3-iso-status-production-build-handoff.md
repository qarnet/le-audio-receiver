# RH3 ISO-status diagnostic production-build handoff

Status: software-only image-validation phase. It validates the reviewed
receiver ISO-status diagnostic change on both production targets and records
new local image identities. It does not authorize flashing, HIL execution, or
any hardware action.

## Goal

Build current dirty worktree for nRF5340 and nRF54L15 after adding receiver ISO
callback-status summaries. Prove both production configurations compile with no
new actionable diagnostic. Record exact hashes needed for later runner-owned
physical diagnostic planning.

## Scope

Run only:

1. `fw-build-5340`
2. `fw-build-54l15`
3. resolved build-contract checker
4. test-matrix checker
5. SHA-256 inventory and whitespace/status inspection

Build output under `build/` may change. Do not edit repository source,
configuration, tests, docs, HIL inputs, or evidence during this phase.

## Grounding

The reviewed change touches `src/bt_bap.c` and
`src/audio_stream_session.[ch]`, compiled by both production application
targets. Existing HIL source images are unchanged and currently hash:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
```

Current pre-build receiver images hash:

```text
bdb898df7cac638def55a86073937d1e3d8cf6ae13a26a02fd79233add9d7de9  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

New CPUAPP hash is expected after source change. FLPR hash may remain unchanged.
No physical claim follows from either result.

## Strict diagnostic policy

Treat compiler warnings, Kconfig assigned-value warnings, linker/memory errors,
and unlisted CMake diagnostics as failures. Existing repository-level NCS
diagnostics documented in `AGENTS.md` and `STATUS.md` may appear:

- deprecation notices for partition manager/sysbuild;
- informational `__ASSERT()` and ISO experimental-symbol notices;
- SW Split choice gap without a NONE fallback;
- `CONFIG_BT_CTLR_ADVANCED_FEATURES=y` CMake notice on nRF5340 CPUNET. The
  applied upstream NCS `hci_ipc`
  `nrf5340_cpunet_iso_peripheral-bt_ll_sw_split.conf` intentionally enables the
  visibility gate so explicit non-default scheduler/reservation settings remain
  effective. Removing it changes resolved controller defaults and is forbidden;
- nRF54L15 watchdog `No SOURCES given` CMake diagnostic caused by disabled
  watchdog DT nodes under SDC.

Report all output. Stop on any other warning or error; do not change code to
silence it without returning to orchestrator.

## Exact commands

Run sequentially from repository root in NCS v3.3.0 dev shell:

```bash
nix develop --command fw-build-5340
nix develop --command fw-build-54l15
nix develop --command python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 --nrf54l15 build/nrf54l15
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
sha256sum \
  build/hil-source/app/zephyr/zephyr.hex \
  build/hil-source/hci_ipc/zephyr/zephyr.hex \
  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex \
  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Do not run `fw-build-hil-source`, `tests/hil`, full canonical gate, HIL runner,
flash helper, serial tool, OpenOCD, reset, pairing, RF action, or a build in
parallel with these commands.

## Constraints

- Worktree is intentionally dirty. Preserve every unrelated change.
- No commit, push, merge, PR, reset, stash, clean, format, or `STATUS.md` edit.
- No hardware action. Build helpers must not be followed by flash helpers.
- Do not claim HIL, transport, release, or audio acceptance.

## Completion report

Return exact build/check outcomes, actionable versus documented diagnostics,
all four hashes, `git diff --check`, final `git status --short`, and explicit
confirmation of no hardware action or commit. If either build fails or emits an
unexplained warning, stop before later commands and return exact evidence.
