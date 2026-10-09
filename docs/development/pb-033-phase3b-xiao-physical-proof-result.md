# PB-033 phase 3B XIAO physical proof result

Status: PASS, 2026-09-22. This records only XIAO nRF54L15-to-XIAO nRF54L15
10 ms transport/runtime proof.

## Frozen source and retry repair

Physical rows used clean commit
`fc2d1efa63c1be8f798f0b065fd62d3b653ea143`
(`PB-033: repair phase 3B build preflight`). NCS was v3.3.0 with toolchain
`911f4c5c26`, Zephyr SDK 0.17.0, and GCC 12.2.0.

The first phase 3B build attempt stopped before session creation, target
mutation, reset, RF activity, row execution, host regression, or result
commit. It preserved these immutable logs:

```text
/tmp/opencode/pb-033-phase3b-fw-build-54l15.log
/tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15.log
```

Receiver configure rewrote the three tracked CMake-owned compile-database
symlinks. The subsequent source build therefore emitted `warning: Git tree ...
is dirty`. Also, sysbuild derived the receiver root image name from the
worktree basename rather than the runner's stable `le-audio-receiver` domain.
This was an execution-environment and handoff defect, not receiver or source
firmware behavior.

Retry 1 restored only the three generated symlinks, committed the repair
handoff, and used the private external `le-audio-receiver` source alias as
`APP_DIR`. The receiver build reported `Running CMake for le-audio-receiver`;
`build/nrf54l15/domains.yaml` has default `le-audio-receiver` and only the
`le-audio-receiver` and `flpr` domains. No worktree-basename image domain
remained. Receiver root/FLPR symlinks were restored immediately after that
build; the source symlink was restored immediately after the source build.
Git status was clean before session creation and before every runner call.

## Retry builds and images

Commands, both exit 0:

```bash
nix develop --command fw-build-54l15 \
  -DAPP_DIR:PATH=/tmp/opencode/pb-033-phase3b-source-alias-r1/le-audio-receiver \
  > /tmp/opencode/pb-033-phase3b-fw-build-54l15-r1.log 2>&1
nix develop --command fw-build-hil-source-54l15 \
  > /tmp/opencode/pb-033-phase3b-fw-build-hil-source-54l15-r1.log 2>&1
```

The complete receiver log contained only the documented watchdog `No SOURCES
given` and global `__ASSERT() statements are globally ENABLED` diagnostics.
The complete source log contained only the documented global `__ASSERT()`
diagnostic and no dirty-tree line. Neither log contained compiler, Kconfig
assignment, unexpected CMake, devicetree, or linker diagnostics.

`/tmp/opencode/pb-033-phase3b-image-sha256-r1.txt` records these regular,
non-symlink, nonempty images:

| Logical image | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| Receiver CPUAPP | 1,510,541 | `7938ecd5eaedd9f7e460506d9f704da02ac01064bc9c05c6571ced24ee21c9dc` |
| Receiver FLPR | 91,857 | `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2` |
| Source CPUAPP | 1,288,838 | `bd0dbcad985bac0ecf180e0e21e28f8dcff87493ed768bd43e510ecf45689192` |

The source build had no `domains.yaml`, CPUNET, `merged.hex`, or
`merged_CPUNET.hex`. Every run retained these exact three hashes in source,
receiver CPUAPP, receiver FLPR order.

## Immutable session

Session ID: `pb033-xiao-proof-20260922-r1`.

```text
manifest: /tmp/opencode/hil-sessions/pb033-xiao-proof-20260922-r1/devices.json
SHA-256: fff051aaa95e2dfbe0b19c5a3353aed584d2c591b0307541a5db977bad72a86d
parent mode: 0700
manifest mode: 0400, regular non-symlink
fixture SHA-256: ecffa4e9f21ae8fffb10fe1bdedcaa5d45456273c6d11da1c1eed0f4dde4a647
binding SHA-256: 88894eff88c82e43b5768fefdd6db6f06bf0c8e4eed6387c4d324706b7fe1326
```

The manifest retained distinct task-context roles. Every snapshot matched the
nRF54L15 DPIDR `0x6ba02477`, FICR PART `0x00054b15`, variant `AAC0`, AP map,
USB parent, and stable interface-02 udev contract. It did not record a
permanent board mapping in this repository.

Each run retained byte-identical `session-devices.json`, the manifest hash,
and exactly six successful ordered revalidations: setup identity, receiver
serial open, source serial open, source flash, receiver flash, and row action.

## Retained row evidence

All rows used profile `48_4_1`, fresh state, seed `1218649181`, 12,000 scored
SDUs per stream, and a 120-second minimum scored interval. Each runner command
exited 0; each `result.json` has `outcome=passed`, null failure detail, null
first failed boundary, and `cleanup_failures=[]`. Internal and external JUnit
files each contain one passing test with zero failures, errors, and skips.

| Row and run | External JUnit | Source terminal counters | Receiver stream counters | Offload |
| --- | --- | --- | --- | --- |
| `rh3.fresh_mono_48_4_1`<br>`pb033-xiao-mono-20260922-r1` | `/tmp/opencode/hil-runs/pb033-xiao-mono-20260922-r1.junit.xml` | stream 0: `sub/sc/cb/sf/out=12644/12000/12644/0/0`; verdict `pass` | slot 0: `sdus/rx_valid/decoded/plc=12645/12645/12655/10`; `rx_lost/rx_no_ts=10/6`; `rx_error`, `rx_unknown`, `empty_sdu`, `decode_err`, `i2s_underrun`, and `stream_reset` all 0 | active `42/42`, ACTIVE; final `12655/12655`, fallback 0; faults and recovery counters 0 |
| `rh3.fresh_mode_a_48_4_1`<br>`pb033-xiao-modea-20260922-r1` | `/tmp/opencode/hil-runs/pb033-xiao-modea-20260922-r1.junit.xml` | streams 0 and 1 both `sub/sc/cb/sf/out=12644/12000/12644/0/0`; verdict `pass` | slot 0: `sdus/rx_valid/decoded/plc=12645/12645/25312/23`, `rx_lost/rx_no_ts=52/48`; slot 1: `sdus/rx_valid/decoded/plc=12644/12644/0/0`, `rx_lost/rx_no_ts=25/8`; all required error, empty, decode, I2S, and reset counters 0 | active `49/49`, ACTIVE; final `12656/12656`, fallback 0; faults and recovery counters 0 |
| `rh3.fresh_mode_b_48_4_1`<br>`pb033-xiao-modeb-20260922-r1` | `/tmp/opencode/hil-runs/pb033-xiao-modeb-20260922-r1.junit.xml` | stream 0: `sub/sc/cb/sf/out=12644/12000/12644/0/0`; verdict `pass` | slot 0: `sdus/rx_valid/decoded/plc=12644/12644/25310/22`; `rx_lost/rx_no_ts=11/7`; all required error, empty, decode, I2S, and reset counters 0 | active `47/47`, ACTIVE; final `12655/12655`, fallback 0; faults and recovery counters 0 |

The reported Mode A start-up `Mode A: half dropped` telemetry was an
informational assembler line. It was not a warning or fault, and the existing
row oracle accepted the run.

Every source terminal status had `error=ok`, zero first ASCS/errno/security
errors, zero `tx.skip`, zero `tx.lead.under`, and the required `pass` verdict.

## Ledger, warning, and integrity review

Every run directory has 32 verified `SHA256SUMS` entries. In every run,
`sha256sum -c SHA256SUMS` passed every listed entry. Complete raw source and
receiver console captures, source and receiver flash logs, command-ledger
stdout/stderr, and external command logs contained no unexpected firmware,
controller, transport, audio, OpenOCD, or host-tool warning, error, assertion,
fault, or recovery line.

Each `commands.jsonl` has 931 records. The authoritative corrected contract
is satisfied by all three:

- exactly one preflight `lsof -- <resolved source console>` record has status
  1, null error, and empty stdout/stderr;
- every other command record has status 0 and null error;
- each ledger has exactly one source flash helper with only the source-role
  override and one receiver flash helper with only the receiver-role override.

The initial mono review incorrectly treated that expected `lsof` status 1 as a
hard failure and stopped before Mode A. Runner behavior is authoritative:
status 1 means no process owns the source console and passes preflight, status
0 means occupied and fails preflight, and any status other than 0 or 1 fails.
`tests/hil/rh2_test.py` and `tests/hil/capture_runner_test.py` explicitly use
status 1 for passing runs. The mono evidence was retained unchanged and passed
the corrected review before Mode A and Mode B ran.

## Focused host regressions

Run after physical evidence with only this result document and the PB-033 task
notes uncommitted:

```bash
nix develop -c python3 scripts/test_hil_runner.py
nix develop -c python3 tests/hil/rh2_test.py
nix develop -c python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
nix develop -c backlog doctor
git diff --check
```

All commands passed. The runner test suite passed 106 tests. The RH2 suite
passed 266 tests in 151.998 seconds. Python compilation, Backlog.md validation,
and whitespace validation passed. Each `nix develop` invocation printed the
expected Nix dirty-worktree provenance notice because the two allowed
documentation changes were uncommitted. It was not a compiler, Kconfig,
firmware-build, or host-test diagnostic; no warning suppression was used.

## Scope boundary

This proves only session-bound, 10 ms XIAO source-to-XIAO receiver
transport/runtime behavior for the three listed rows. It does not prove analog
output, audibility, physical channel wiring, 7.5 ms behavior, reconnect or
fault-injection behavior, RH4/FR4, release acceptance, publication, or broader
product acceptance. No production, test, runner, fixture, build-helper,
configuration, protocol, or hardware-control source changed in phase 3B.

## Canonical software gate

The first `nix develop -c ./scripts/test-all.sh` run from the linked worktree
at commit `fa0bbca411c804a2a8a080b1d978014f1fdf4992` ended
`72 PASS / 2 FAIL / 74 TOTAL`. All unit children and BSim passed. Coverage
failed only with `FATAL: not a git checkout: /tmp/opencode/le-audio-receiver-pb-fixture`; matrix then failed because the
coverage JSON was absent. Log:
`/tmp/opencode/pb-033-canonical-gate.log`. This was an environment-only
coverage-runner contract issue: the linked worktree has a `.git` file, not a
standalone `.git` directory.

No software changed between attempts. A clean standalone local clone at the
same exact commit reran the full one-command gate and ended exactly
`74 PASS / 0 FAIL / 74 TOTAL`. Coverage, matrix, and BSim passed, and the
clone remained clean. Log:
`/tmp/opencode/pb-033-canonical-gate-clone.log`. This satisfies acceptance
criterion #5.

## 2026-10-08 preservation appendix: retired handoff design facts

> The phase 1/2/3A handoffs, the phase 3B execution handoff, the retry-1
> repair handoff, and the PB-032 policy handoff were retired in the
> documentation reconciliation of 2026-10-08. Unique design facts not already
> stated above are preserved here verbatim in meaning. They are historical
> design/method context (NCS v3.3.0 where noted), not current instructions:
> v3.4.1 and the later migration reports take precedence where they differ.

### Phase 1 session-binding design rationale (identity, safety, review repair)

Grounding facts behind the session identity contract:

- Probe family and volatile tty paths cannot select roles; raw AP/FICR
  evidence supplements the probe table. Live read-only scans on both boards
  returned identical family signatures (AP0 `0x84770001`, AP1 `0x84770001`,
  AP2 `0x32880000`, AP3 `0x00000000`, PART `0x00054b15`, raw VARIANT
  `0x41414330`/`AAC0`); DPIDR `0x6ba02477`. Live serials remain session
  evidence only and must never become a committed role mapping. XIAO tty
  interfaces expose VID `2886`, PID `0066`, interface `02`, with
  `ID_SERIAL_SHORT` equal to the CMSIS-DAP probe serial; `/dev/ttyACM*`
  numbering is observation only. `nix-nrf probes` scans AP0-AP3 internally
  but its public table prints only DPIDR/PART/printable VARIANT, so the
  repository owns an explicit-probe OpenOCD call to retain raw AP values.
- Exact FICR addresses used after `chip.dap apcsw 0x01000000 0x01000000`:
  INFO.PART `0x00FFC31C`, INFO.VARIANT `0x00FFC320`.
- Read-only phase allowed no reset, halt, program, or write-memory command.
- Manifest exact bytes, hashes, strict schema, and the exclusive 0400 file
  lifecycle (O_CREAT|O_EXCL|O_NOFOLLOW, fsync file and directory, never
  overwrite) were designed before any target action.
- Phase-1 review repair gate (recorded defect classes, all fixed before the
  physical phase): snapshot/parse race on fixture/binding bytes, float
  values comparing equal to integer schema fields, trailing-newline session
  IDs, and custom-root `/`/repository-overlap path comparison that
  special-cased `/` incorrectly. Repair required byte-oriented parsers,
  exact JSON integer types, full-match session-ID grammar, path-component
  root rejection, and regressions for input drift before session-directory
  creation.

### Phase 2 source-port design decisions (SDK v3.3.0 context)

- Controller time split: the source reads Zephyr-owned direct GRTC; it adds
  no private GRTC channel, compare channel, GPPI link, IPC transport, or
  SYS_INIT hook, and does not transplant the nRF5340 HFCLK divider. The
  `hil_source_controller_time_get` contract deliberately preserves the
  modulo-`2^32` microsecond API used by signed-delta scheduling (32-bit wrap
  is handled by the callers), with readiness/errors (`-ENODEV` when GRTC not
  ready, `-EINVAL` on NULL) preserved; no nRF53-era `-EIO` epoch-fault path.
- No custom SDC assertion handler: the default integrated-host fatal path
  retains file/line evidence.
- Explicit `--no-sysbuild` is required for the single-image source build:
  omission accidentally selected `sysbuild_default` (the tree contains
  `Kconfig.sysbuild`) and produced `app/zephyr/`, `domains.yaml`, and a
  merged image instead of the intended single artifact shape.
- Overlay/resource safety: stock buttons and SPI flash are disabled to avoid
  UART/RF-switch pin collisions; the source overlay excludes every receiver
  peripheral block (I2S/FLPR/TIMER20/pdm/LEDs/ADC/IMU/battery/pairing nodes).

### Phase 3A runner-checkpoint design facts

- Six ordered identity checks guard setup, both serial opens, source flash,
  receiver flash, and the row action; prior snapshots cannot be overwritten.
- Tty renumbering before an open may succeed (identity is udev/USB, not the
  `/dev/ttyACM*` node); post-open drift must fail before the guarded action
  and close owned descriptors.
- Boot markers and the exact source hello remain required before the first
  clean-state command: a manifest schema proves the session contract, not
  that a matching image is actually flashed and booted.
- Original artifact mode was nRF5340-only and the XIAO local inventory
  initially excluded CPUNET (historical; superseded by PB-036's XIAO
  single-CPUAPP artifact contract).

### Retry-1 rejected build-path options (historical SDK v3.3.0 diagnosis)

Rejected alternatives to the `APP_DIR` source alias, retained so nobody
re-derives them against a different SDK expecting the same behavior: no
`APP_DIR_NAME` and no command-line `DEFAULT_IMAGE` override existed in that
SDK; the sysbuild default-domain name derives from the CMake `APP_DIR`
basename before any environment fallback. The alias workaround is a
version-specific artifact of that SDK behavior, not a general build rule.
