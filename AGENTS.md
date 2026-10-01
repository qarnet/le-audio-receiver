# nRF Connect SDK — Knowledge Lookup Rules

Active development uses NCS v3.4.1 at `~/ncs/v3.4.1` (nrf
`b20f8619ba9a5530f8c34b0a130d829947cfe55d`, Zephyr
`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`). NCS v3.3.0 remains
installed for historical evidence; do not select an SDK via directory order.
Use a fresh shell (`env -u ZEPHYR_BASE nix develop` if an old v3.3.0 shell is
active) so old `ZEPHYR_BASE` cannot mix with the new toolchain. Active
toolchain pin: `8285d8ad56` (GNU 14.3 ARM/RISC-V, Zephyr SDK 1.0.1).

Treat the installed source tree as the authoritative reference. Do NOT guess
at Kconfig symbols, devicetree compatibles, or API signatures — grep the
source. The web docs are a JavaScript SPA and cannot be fetched.

## Kconfig discovery

Workflow when you need a Kconfig symbol:
  1. Grep the definition (NOT just usages):
       `grep -rn "^config FOO\b" ~/ncs/v*/nrf ~/ncs/v*/zephyr ~/ncs/v*/modules`
  2. View the surrounding Kconfig block to read deps and help text.
  3. If searching by topic, grep Kconfig* files for keywords:
       `grep -rn -i "lte modem" ~/ncs/v*/nrf --include="Kconfig*"`

Prefer the resolved config for a built project:
  `build/zephyr/.config` — final merged config (post-Kconfig)
  `build/zephyr/include/generated/zephyr/autoconf.h`
These show what is ACTUALLY enabled, vs. what is merely declared.

## Devicetree

Bindings live at `~/ncs/v*/zephyr/dts/bindings/` (upstream) and
`~/ncs/v*/nrf/dts/bindings/` (Nordic-specific).

For a built project, the resolved DT is at:
  `build/zephyr/zephyr.dts`
  `build/zephyr/include/generated/zephyr/devicetree_generated.h`

## Headers / APIs

Public Zephyr headers:  `~/ncs/v*/zephyr/include/zephyr/`
Public Nordic headers:  `~/ncs/v*/nrf/include/`

When asked "how do I use X", grep the header for the function declaration
and read the surrounding `/** ... */` Doxygen block.

## Samples

Nordic samples are the best learning resource:
  `~/ncs/v*/nrf/samples/`
  `~/ncs/v*/zephyr/samples/`

## What NOT to do

- Don't fabricate Kconfig symbol names. If you cannot grep it, say so.
- Don't web-search for nRF Connect SDK docs — fetches fail. Use the local tree.
- Don't suggest API calls without verifying the function exists in a header.

---

# AGENTS.md — LE Audio Receiver (nRF54L15 migration)

## Autonomous execution and hard blockers

### Audio validation and fixture-identification continuation

For the analyzer/audio-testing track, start with:

1. `docs/testing/logic-analyzer-setup.md`: sole current analyzer wiring declaration,
   fresh-session identity, capture templates, electrical safety and DAC-presence limits.
2. `docs/development/audio-validation-handoff-20260925.md`: proposed independent
   codec/ASRC/transport/presentation tests, timestamp ledger and BLE clock mapping.
3. PB-041 in `docs/product/backlog/tasks/`: separate fixture-identification work.
   Backlog owns status/readiness; documentation is not implementation or acceptance.

Project `opencode.json` advertises `le-audio-resources`, `nix-nrf-dev` and active
`ncs` references. The external resource library's `README.md` indexes authored
research and searchable vendor extracts. Keep PDFs, archives, vendor tools and
generated extracts there; do not copy them into this repository without a specific
need and rights review. Historical research may target v3.3.0; current SDK pin wins.
Do not bulk-load the library as startup instructions. Read relevant files on demand.
These documents do not authorize hardware actions beyond existing safety rules.
The earlier separate PB-019 pause is historical: explicit final-migration
direction resumed qualification, and clean v3.4.1 HCI evidence now exists. Do not
treat the audio research handoff itself as approval for unrelated new features.

### Current migration continuation

For the all-nRF54L15 migration, read
`docs/development/nrf54l15-source-batch-guard-results-20261001.md`,
`docs/development/nrf54l15-flpr-fresh-reload-results-20261001.md` and
`docs/development/nrf54l15-final-reference-audit-20261001.md` for the latest
repairs and platform audit. Clean `104e67a` software (80/0/80), builds,
73-check build contract and six HCI cases passed. Its exact local-artifact
matrix passed 20/20, with 120 ordered identity checks and independently
rehashed child/aggregate evidence. See
`docs/development/nrf54l15-migration-verification-results-20261001.md` for
local completion and separate analog, release and human-acceptance boundaries.
Earlier `b21c7a7` matrix failed FLPR
hang recovery after eight passing rows; that failure is retained, not acceptance.
For dated chronology, read
`docs/development/nrf54l15-observability-resume-20260925.md` first, then
`docs/development/nrf54l15-only-resume-20260924.md` as historical context.
The primary repository on `feature/nrf54l15-only-continuation` owns the work;
the former `/tmp` worktree is not the execution location. Preserve the current
uncommitted changes. Its 2026-09-24 pause tables are a historical checkpoint,
not today's implementation status. Read
`docs/development/pb-019-hci-resume-results.md`,
`pb-034-primary-repair-results.md`, `pb-035-source-matrix-results.md`,
`pb-036-source-artifact-results.md`, and `pb-037-retirement-results.md` for
later dirty-tree results. None constitutes final clean-commit acceptance.
Historical evidence and safety rules remain binding.

Latest owner direction (2026-10-01 local date): finish the approved migration,
use attached lab boards/analyzer and local commits as necessary, and report only
verified completion or a genuine hard blocker. This does not waive fresh identity,
immutable evidence, physical safety, frozen acceptance, or the human-only merge
rule. Preserve user-owned PB-013 edits and private untracked graph/checkpoint data.

Product-owner direction (2026-09-24): carry approved goals through to verified
completion. Research, diagnosis, grounded software changes, fixture repairs,
regression tests, and expansion of the technical investigation are part of
the job. Do not ask for fresh permission for each of these steps.

- A hard blocker is an obstacle the agent cannot resolve with available
  authority, tools, access, or evidence, or a necessary decision that would
  substantially change the intended product or what acceptance proves.
- A failing test, unfamiliar controller behavior, additional research, or a
  need to fix production code is not by itself a hard blocker. Investigate,
  identify the faulty boundary, fix it, and rerun the relevant gates.
- Earlier plans or handoffs that underestimated the necessary work are not
  permission barriers. Record evidence and necessary technical scope updates
  transparently, then continue toward the approved product outcome. Do not
  silently rewrite product requirements or historical evidence.
- Preserve intended capabilities and meaningful acceptance. Removing audio
  transmission because it fails, skipping failing scenarios, weakening limits,
  or substituting a different fault merely to obtain a pass is not a fix.
  Escalate if completing the goal requires such a material product or
  acceptance change, rather than a grounded repair.
- Exhaust reasonable investigation and repair paths before escalating. A
  blocker report must identify the specific missing capability, access, or
  consequential decision, with evidence and a recommendation. Do not turn
  routine engineering work into a request for permission to do more work.
- When the user requests silence until a hard blocker, continue without
  routine progress reports or approval requests. Report verified completion
  or a genuine hard blocker, not ordinary intermediate failures.

This authority does not waive explicit safety boundaries, hardware identity
checks, immutable evidence, required validation, or commit/PR/merge rules.

## Policy — never ignore warnings

Compiler warnings and Kconfig "assigned value but got" warnings are hard errors:
fix the source or suppress with a recorded reason. Boot-time `LOG_WRN` and
openocd/flashing warnings are treated the same — don't normalize noise.

Current NCS v3.4.1 `native_sim` host-only builds emit one CMake product-support
notice from `nrf/cmake/device_support.cmake:34`: `SoC native is not supported by
this release.` This does not apply to physical SoC builds and is not a compiler
or Kconfig warning. Retain the raw notice; never make this a general warning
waiver. Native fake-entropy banners are test-only, not a reason to change
production entropy configuration. Physical receiver, standalone source, and
HCI builds have no compiler or Kconfig warnings after disabling the verified
unused deprecated `NRF_PLATFORM_LUMOS` alias in each target. Historical NCS
v3.3.0 informational `__ASSERT()` and documented watchdog no-sources diagnostic
(wdt30/wdt31 disabled with SDC) remain historical, not current warning
exceptions; check `STATUS.md` "Build warning diagnostics" for older logs.
Historical nRF5340 ISO experimental symbols, SW Split low-latency-policy gap,
and dual-target watchdog tradeoff are not current receiver exceptions.
Diagnose new warnings; do not suppress them by citing an obsolete target.

NCS v3.4.1 Zephyr `CMakeLists.txt:2356-2359` intentionally emits
`__ASSERT() statements are globally ENABLED` when `!CONFIG_TEST &&
CONFIG_ASSERT && !CONFIG_FORCE_NO_ASSERT`. All three physical development/
acceptance images intentionally enable assertions for fault guards; their build
logs retain this exact CMake informational configuration diagnostic. This
current source-checked condition also occurred in old builds, but is not a
blanket reuse of a historical waiver. It is neither a compiler/Kconfig warning
nor the host-only unsupported-SoC notice. Do not disable assertions to quiet it,
filter the log, or grant a general CMake warning waiver. Inspect and resolve
any other CMake warning separately.

Receiver-only TX-notify workqueue disposition (2026-09-28): the earlier
"no Kconfig warnings" physical receiver statement above records the
2026-09-25 SDK checkpoint, not the latest receiver configuration. The
nRF54L15 board conf deliberately selects NCS v3.4.1's experimental
`CONFIG_BT_CONN_TX_NOTIFY_WQ=y` with stack 1536 and priority 8 to isolate
the observed BT RX/sysworkq teardown dependency. Keep
`CONFIG_WARN_EXPERIMENTAL=y` so the exact
`warning: Experimental symbol BT_CONN_TX_NOTIFY_WQ is enabled.` remains
visible in receiver build logs. This is a target-specific, evidence-backed
experimental configuration choice, not a generic warning waiver; compiler,
assigned-value, runtime, and unrelated build warnings remain errors. See
`docs/development/nrf54l15-tx-notify-workqueue-results-20260928.md`.

## Style rule: no em dashes in user-facing documentation

User-facing documentation must not contain the Unicode em dash (U+2014).
Rewrite em dashes with commas, parentheses, colons, semicolons, or separate
sentences, preserving meaning and formatting (links, tables, code spans,
numeric ranges, and warning strength).

User-facing scope: `README.md`, `docs/product/README.md`, every
`docs/product/backlog/**/*.md` task file, and the public docs listed in the
README Documentation table (`docs/user-guide.md`,
`docs/supported-sources.md`, `docs/linux-le-audio-host-setup.md`,
`docs/bluetooth-adapter-evaluation.md`, `docs/hardware-wiring.md`,
`docs/known-limitations.md`, `docs/technology/nrf5340.md`,
`docs/technology/nrf54l15.md`, `docs/flashing.md`), plus
`release/flashing/*.md`.

Internal and historical contributor docs (for example `docs/development/`,
`docs/testing/`, `STATUS.md`) are outside this style rule unless explicitly
requested.

## Documentation and product backlog lifecycle

- Current product planning lives under `docs/product/`, managed with
  Backlog.md. `docs/product/README.md` owns configuration, field vocabulary,
  and lifecycle contract. `docs/product/backlog/` is sole current source for
  product item status, priority, and dependencies; one Markdown file represents
  each item.
- `tasks/` holds active Backlog, Ready, In Progress, Blocked, and Review items;
  `completed/` holds accepted Done history; `archive/` holds dropped history.
  Completed and dropped items stay retained. Change status through `backlog`
  CLI, not hand edits, so IDs, filenames, and metadata stay consistent. Record
  drop rationale in Final Summary before `backlog task archive`.
- Implementation agents may only begin Ready work. Do not silently edit
  product-owned title, priority, status, type, Description product sections, or
  acceptance-criteria text. An agent may take item to Done only through PR
  gate: criteria checked from evidence, repository gates green, Final Summary
  filled, and Done transition committed with work in one PR titled with item ID
  prefix, for example `PB-006: ...`. Human product-owner merge is official
  acceptance. If PR is rejected or changes are requested, move item back to
  `tasks/` with In Progress or Review, fix, and re-PR. An agent never merges
  own PR.
- Existing `docs/development/` plans, results, and handoffs remain technical
  context and immutable evidence where applicable. `STATUS.md` remains
  implementation and evidence snapshot, not a second task list. Active
  standalone plans and design docs cite PB IDs; completed or historical plans
  do not need conversion to Done tasks. Do not move existing development or
  testing documentation as part of backlog work.
- Use product-backlog skills when available. Keep execution subtasks inside PB
  item rather than creating independent product items.

## Plan of record

`docs/development/system-hil-milestones.md` is the accepted plan of record
for the System HIL track (revised 2026-09-09): nRF54L15 is the only production
receiver target, the 10 ms RH3 transport/runtime matrix is accepted at clean
commit `8123b94`, 7.5 ms is diagnostic-only until RH3-7p5 closes it, receiver
transport limits are frozen and runner-enforced, and reruns are the
fix-validation mechanism. RH4 waits for exact candidate archives.
`docs/development/refactor-plan.md` remains the accepted plan of record for the
refactoring track R0–R10. Read the applicable one before structural changes.
`docs/design.md` remains the historical architecture and evidence document, not
an active structural plan.

Historical pre-migration status (not current clean-commit acceptance):
**canonical gate 74 PASS / 0 FAIL / 74 TOTAL** on clean
PB-031 P4 commit `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22` (41 Twister +
5 exec-only + 25 Python + coverage + matrix + BSim; the FR2 clean-tree run at
`75a8093`, the FR1 clean run at
`1671a9f`, and earlier clean runs recorded in
`docs/development/documentation-hygiene-behavior-fix-results.md` at
`b8bd633` and the production-fix canonical run at `f2f9336`, after the
empty-SDU concealment (`9dc0859`) and 11-block startup reservoir
(`f2f9336`) fixes — the committed coverage baseline is unchanged),
coverage population **37** (4969/5427 lines, 2177/2984
branches, 380/380 functions, gcovr 8.4 / gcov (GCC) 14.3.0, committed
baseline unchanged), builds 3/3, build contract **96/96**, BSim Stage 1
pins exact fixture/sequence TX FNV hashes, mono 10 ms `0xC5C840B0`, mono
7.5 ms `0x2CE69E65`, Mode A 10 ms L/R `0x8980C79D`/`0xDD25CC21`, Mode A
7.5 ms L/R `0x7D1EAC0F`/`0x001D6366`, Mode B 10 ms `0xE5D37A85`, Mode B
7.5 ms `0x4D9A9ED7`, and zero-stream `0x811C9DC5`; payload/recipe-aware
portable PCM metrics are maximum/RMS/minimum-correlation 257/182/32767
within immutable 2048/512/32750 limits, not byte-identical decoded PCM pins.
P1–P8 user pairing control ACCEPTED (nRF54L15
enabled, nRF5340 feature-off), FR1 deterministic firmware packager
ACCEPTED, FR2 firmware-build CI ACCEPTED (hosted run 31326612845
PASS; workflow artifacts only, no tag/release/hardware acceptance), and
FR3 automatic draft-release creation ACCEPTED (final merged hosted run
`b70b978` PASS with release SKIPPED on unchanged `VERSION`). PR 12 logical
parallelization is ACCEPTED on hosted run `34725825883` (source HEAD
`463fa6b57042e026985ffd0a25d1ba0687f651b7`, workflow/artifact merge SHA
`11c4c6fda43e93d7214f4d463b25b50874342d02`): `test-unit` unblocks
`firmware`; `test-unit`, `test-heavy (coverage)`, and `test-heavy (bsim)` feed
aggregate `tests`; `tests` and `firmware` join at trusted-main-only `release`.
The workers combined to `72 PASS / 0 FAIL / 72 TOTAL`; aggregate `tests` and
`firmware` passed, `release` was SKIPPED on pull requests, and total run time
was 26m31s versus 58m02s monolithic. FR4 exact-artifact hardware acceptance is
**BLOCKED**: historical exact draft `v0.1.0` FAILED mandatory nRF5340 mono
acceptance. It served as stable harness baseline, then its GitHub draft and
assets were deleted 2026-09-19; historical failure evidence remains immutable.
Failed assets were not restored or clobbered. Nothing was published. The local
replacement preflight passed both targets at `5e7f502` but remains historical
preflight, not exact-artifact acceptance. The root `VERSION` remains `0.1.0`.
PR #13 human-merged PB-031 into `main` at
`b59e1d8f99b8f4e7435c7086bfe81700007b221d`. Trusted-main workflow run
`35429538264` attempt `1` passed `test-unit`, `test-heavy (coverage)`,
`test-heavy (bsim)`, aggregate `tests`, `firmware`, and `release`, then created
active private replacement draft release ID `391991202`: tag label `v0.1.0`,
title `LE Audio Receiver v0.1.0`, target
`b59e1d8f99b8f4e7435c7086bfe81700007b221d`, draft `true`, prerelease `false`,
and `published_at: null`. No `refs/tags/v0.1.0` exists. Exact assets are
`le-audio-receiver-v0.1.0-nrf54l15-xiao-factory.zip` (627096 bytes, SHA-256
`bd5fe73636b831e9b685cd20f53704fbe342be60b124f5ae5379dd7969ac9f10`),
`SHA256SUMS` (117 bytes, SHA-256
`ea653102fc318d22e0b4b5d3c7b08a05874aa435b73098ea5f65790a913398ff`), and
`release-provenance.json` (1077 bytes, SHA-256
`49ca660cc83e99a12e07982f466d5d44f64730e708f162a03582dc60fe157238`).
Provenance binds version `0.1.0`, NCS `v3.3.0`, run `35429538264` attempt `1`,
the exact SHA, and the nRF54L15-only factory ZIP. No RH4 or FR4 acceptance has
run. FR4/FR5 remain blocked until exact active nRF54L15 assets pass acceptance
through PB-007; nothing published.
Historical PR 11 (`feature/firmware-release-acceptance`) established
the monolithic 65-child hosted canonical software gate: in that topology,
`tests` passed before `firmware`, and `release` followed `firmware`. The job ran
on the plain host runner inside the locked Nix dev shell with the exact
NCS v3.3.0 SDK + `911f4c5c26` toolchain installed by pinned
`nrfutil sdk-manager` 1.16.1.  Hosted attempts `31422292550` (pre-gate
on the container's incompatible gcov first-line assertion) and
`31424437357` (BabbleSim build on the dangling `tools/bsim/Makefile`
symlink) failed; `31426937629` passed the exact Nix/NCS environment,
coverage baseline, matrix, and the 17-scenario/26-run BabbleSim Stage 1
but ended `64 PASS / 1 FAIL / 65 TOTAL` solely because the mocked unit
test `test_enable_pairing_agent` launched a real `bt-agent` through an
unmocked `subprocess.Popen`.  The process-boundary mocking correction
landed at `647361c`, and the hosted canonical software gate is ACCEPTED
on run `31432411543` (PR head `32bdc98`): `tests` job `93598711857`
SUCCESS with exact console summary `Gate complete: 65 PASS / 0 FAIL /
65 TOTAL`, `firmware` job `93611002998` SUCCESS started after tests,
`release` job `93612477731` SKIPPED on pull_request; the active ruleset
`20658259` requires exact status contexts `tests` and `firmware`. PR 12
preserves those contexts; `release` remains joined on both and is SKIPPED on
pull requests. An early
disk-cleanup step frees only
well-known preinstalled toolchain caches, the NCS install branches on
the exact `cache-hit` output of the NCS cache step, never on directory
presence, and a west population step runs `west update --narrow
-o=--depth=1 --group-filter +babblesim` (the bundle ships bsim_west but
the root group-filter excludes the `babblesim` components, leaving the
Makefile symlink dangling).  Plan of record:
`docs/development/firmware-ci-test-gate-plan.md`.  Protected-main runs,
draft-release creation, and FR4 hardware acceptance remain separate and
are not claimed.  Historical baselines: T0–T8 locked
behavior on production code `971e6a4` (T8 canonical gate **47 PASS /
0 FAIL / 47 TOTAL**, coverage baseline `1a5842d` (26 files), build
contract 76/76 — `docs/testing/pre-refactor-hardware-baseline.md`); the
R0–R10 refactor track closed 2026-08-06 with gate **55 PASS / 0 FAIL /
55 TOTAL** (31 twister + 5 exec-only + 16 Python + coverage + matrix +
BSim), coverage population **33** (4024/4402 lines, 1695/2356 branches,
289/289 functions, committed baseline `54a6b8e`), build contract
**79/79**, and the full R10 hardware matrix passed on
both targets.  R8/R9 acceptance details and the final evidence:
`docs/development/refactor-r10-results.md`, `refactor-r9-results.md`,
`refactor-r8-results.md`, and `STATUS.md`.
**BabbleSim Stage 1 is an accepted regular local gate** — the
**17-scenario** T4+R7 BAP matrix via `scripts/bsim-stage1-run.sh`
(scenarios 1–9 run twice, 10–17 once = 26 runs), strict PCM oracle,
deterministic across runs (mono 10 ms `0x22AB5C0D`, Mode A/B 10 ms
`0xBAE24F7E`, reconnect = fresh mono oracle, `duplicate_release_10ms`).
Official upstream smoke remains PARTIAL (documented upstream teardown
disable-race) and is **not** production acceptance.

Known behavior question (see `STATUS.md`): nRF54L15 360-frame (7.5 ms) calls
fall back to cpuapp ASRC because the FLPR payload contract is 480 frames
(`FLPR_RING_PAYLOAD_MAX_INPUT == 480U` in `src/flpr_ring.h`).  Witnesses:
`tests/unit/audio_i2s` `test_offload_reject_360_input_falls_back` pins the
exact 360-frame caller fallback; `tests/unit/audio_offload`
`test_asrc_invalid_frames` pins general non-480 rejection (its current
concrete input is 240); `tests/unit/flpr_ring` MAX_INPUT assertions pin the
480 contract.  Not a new failure and not permission to implement 360-frame
offload.

Consequences for work in this repo today (2026-09-25):

- PB-032's retained-E83 policy above was superseded by the approved all-nRF54L15
  migration. XIAO receiver is the only production target. Its DK-target plus
  XIAO overlay build produces CPUAPP and FLPR; production APLL, E83 board and
  receiver helpers are removed on the dirty continuation tree. Historical APLL
  remains test-local; native unit tests remain hardware-independent.
- Second XIAO alternates standalone source (DK-target, direct GRTC, one
  CPUAPP image) and Linux HCI controller (XIAO-target, SDC UART H4 1 Mbaud,
  no flow control). Never treat these roles as simultaneous on one board.
  Canonical BSim uses two nRF54L15BSim peers with integrated SW Split and
  client reliability policy. No nRF5340 hardware dependency is an active gate.
- Fixed-image diagnostics do not complete PB-035/036/037/038, canonical gate,
  clean coverage baseline, exact-artifact RH4/FR4, analog qualification, or
  public release. Follow the current backlog and evidence, not the historical
  gate totals quoted above.

## Standing lab nRF hardware authority

The user grants standing permission for agents working in this repository to use
any attached Nordic nRF development board. Permitted actions include read/debug
access, serial interaction, reset, flash, full erase/recovery, DTR/RTS control,
RF/Bluetooth testing, and firmware replacement. No fresh per-action or per-run
confirmation is needed for these attached nRF boards. Full erase/recovery
remains subject to target and tooling support, and existing recovery limitations
still apply.

Before any target-changing action, run `nrf-probes` or the appropriate project
identity resolver and retain raw identity evidence. Never rely on a static
probe-to-board mapping. Do not operate on unknown or non-Nordic hardware. This
broad permission applies to attached nRF lab boards, not unrelated host
peripherals or arbitrary USB devices.

Use `scripts/hil-runner.py` when its owned end-to-end evidence lifecycle is
useful, but it is not the only permitted hardware owner. Direct debugger,
serial, and board testing are allowed when they provide clearer diagnosis or
validation.

Preserve immutable run directories. Board erasure or reflashing does not
authorize alteration of prior evidence.

A simulator or host-test failure is evidence, not automatic proof of a
production firmware defect. Before making a potentially behavior-changing
source fix, evaluate the suspected failure on a physical nRF board when
practical, then retain both simulator and board evidence.

Keep all existing central-only pairing/streaming requirements unless a later
plan deliberately changes those requirements.

## Central-only test rule

All agents run the LE Audio stream autonomously via the second XIAO nRF54L15
as Linux HCI central. This board alternates HCI and standalone HIL source
firmware; the receiver is the other XIAO. The stock SAMD11 bridge carries
UART20 P1.9/P1.8 at 1,000,000 baud H4 8N1 without flow control. Select
the adapter by fresh identity, not a fixed HCI index or tty. Use
`scripts/bap_central.py` to stream LC3. No human-operated central is allowed.
For standalone HIL source qualification, use the same second XIAO after
reflashing its source role, not the Linux HCI role concurrently.

The only allowed user input is a true physical observation that an agent
cannot make: whether sound is audible from connected speakers/headphones
after the agent has completed its test run.

### Central setup (required before every test session)

Build the single-image `xiao_nrf54l15/nrf54l15/cpuapp` SDC controller with
`fw-build-dongle` (`build/dongle/zephyr/zephyr.hex`). Create an external
session after resolving both live probes with `nix-nrf probes`:

```bash
python3 scripts/hil-runner.py create-session \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --session-id UNIQUE_SESSION_ID \
  --receiver-probe RECEIVER_PROBE_FROM_LIVE_DISCOVERY \
  --source-probe SOURCE_PROBE_FROM_LIVE_DISCOVERY \
  --session-root /tmp/opencode/hil-sessions
```

Verify binding against live hardware. Flash or reset HCI role only with
`fw-flash-dongle` / `fw-reset-dongle`, passing `--session-manifest` (absolute
external `devices.json`), `--fixture`, `--binding`, `--output-root` (existing
external root), and unique `--run-id` each time. These actions recheck raw
DP/AP/FICR and USB identity. Never guess a probe/tty/HCI mapping.

Attach the session-bound lab adapter and run the child before each streaming
session (use new run ID; no persistent service):

```bash
fw-attach-dongle --session-manifest /absolute/external/session/devices.json \
  --fixture tests/hil/fixture-xiao-source.json \
  --binding tests/hil/fixture-xiao-source.local.example.json \
  --output-root /existing/external/output --run-id UNIQUE_ATTACH_RUN \
  --timeout 180 -- python3 scripts/bap_central.py --adapter @HCI@ --duration 120
```

`fw-attach-dongle` checks lab BD_ADDR `C0:AA:BB:CC:DD:EE` and HCI settings
`powered le secure-conn cis-central`, substitutes live `@HCI@`, and cleans
up only its owned adapter after the child. Run BAP child **without sudo**.
Fresh pairing uses normal BlueZ discovery. Bonded reconnect adds
`--preserve-bond --peer-addr RECEIVER_ADDRESS_FROM_BOOT_LOG`; raw-HCI
exact-peer connection is not the default. Optional `--mono` selects mono,
no mode flag selects Mode A, and `--stereo` selects Mode B. Standard QoS is
RTN 5 / latency 20 ms at 10 ms interval. Observed loss and PLC are nonzero;
consult `docs/development/pb-019-hci-resume-results.md` for counts and limits.
Use a system-manager `systemd-run` transient service with `User=` set to the
ordinary user, `RuntimeMaxSec=`, `TimeoutStopSec=` and `KillMode=control-group`
for hard containment of detached root descendants; user-manager services and
shell timeout are insufficient. No static adapter mapping or persistent
attachment service.

## Build

Build **from the repo root** using NCS v3.4.1. Enter a fresh dev shell first
(`env -u ZEPHYR_BASE nix develop` from an old v3.3.0 shell), then run the build
helper:

```bash
direnv allow         # or: nix develop
fw-build-54l15
fw-build-hil-source-54l15
fw-build-dongle
```

Receiver DK target with XIAO overlay builds CPUAPP and FLPR into
`build/nrf54l15/`; standalone XIAO source uses DK target and direct GRTC,
building a single CPUAPP image at `build/hil-source-nrf54l15/zephyr/zephyr.hex`.
The Linux HCI role uses XIAO target, SDC and one CPUAPP image at
`build/dongle/zephyr/zephyr.hex`. These are separate sequential roles for
the second XIAO. Rebuild pristine after configuration or overlay changes.
Never use deleted E83 or nRF5340 receiver helpers as active build gates.

nRF54L15
RRAM needs no flash driver — with RRAMC write-enable (`mww 0x5004b500
0x101`) it is plain writable memory, so `load_image` + `verify_image`
suffice. **FLPR firmware flashes the same way**: the FLPR code partition
is a RRAM slice at `0x165000` in the app core address space (verified by
write/read-back with both OpenOCD and probe-rs) — relevant for Phase 6
FLPR offload. The build targets the stock `nrf54l15dk` board + a small
overlay (`boards/nrf54l15dk_nrf54l15_cpuapp.overlay`) that remaps UART20
to the Xiao SAMD11 USB CDC bridge (P1.9 TX / P1.8 RX) and I2S20 to Xiao
D0/D1/D2 (P1.4/P1.5/P1.6). Resolve console port through the session binding.

## LSP (clangd) setup

clangd parses each tree through per-image `compile_commands.json` symlinks
(clangd closest-ancestor discovery) plus `--query-driver` in the launch
command. Full background: `~/.config/opencode/rules/clangd-zephyr.md`.

- Links are created two ways: (a) `file(CREATE_LINK)` in each app
  CMakeLists at every configure (root receiver link guarded on
  `CONFIG_SOC_NRF54L15` so only nRF54L15 owns the root; `src/flpr`,
  `hil/source/app` emits `hil/source/`, `dongle/hci_uart`,
  `tests/bsim`, `tests/bsim/client`), and (b)
  `scripts/gen-lsp-links.sh` for the links CMake cannot own
  (`tests/unit` and the BSim out-of-tree builds; also repairs dangling
  links). Run `bash scripts/gen-lsp-links.sh --check` to inspect.
- clangd must be launched with `--query-driver` covering the Zephyr SDK
  compilers and the Nix host gcc wrapper; the repo `opencode.json` ships
  this for opencode sessions. Without it, cross-target parses pick up
  host glibc (`'gnu/stubs-32.h' file not found`) and native_sim/BSim
  files get no builtin headers.
- The root `.clangd` deliberately has NO `CompilationDatabase` pin and
  NO `Add: --target`: a root pin would override per-tree discovery, and
  a repo-wide ARM target breaks BSim/native_sim host parses. Do not
  re-add either.
- After `--pristine` rebuilds the link files are re-created at configure
  time (seconds); BSim links refresh via `scripts/gen-lsp-links.sh` or
  the next `bsim-stage1-run.sh` configure.
- Verify a file parses clean (0 real diagnostics expected; `tweak:`
  lines in `--check` output are artifacts, not diagnostics):

```bash
clangd --query-driver='/nix/store/**,/home/thomas-workstation/ncs/toolchains/*/opt/zephyr-sdk/**' \
  --check=src/main.c 2>&1 | grep -E '^[WE]\[' | grep -cvE 'SwapBinary|tweak:'
```

## Flash

Receiver flash uses `fw-flash-54l15` with
`FW_NRF54L15_PROBE_SERIAL` set to the **freshly role-resolved receiver probe**.
It requires the explicit serial, checks identity, and verifies both CPUAPP and
FLPR; it has no family-wide fallback. Source flash uses
`fw-flash-hil-source-54l15` with freshly role-resolved
`FW_HIL_SOURCE_NRF54L15_PROBE_SERIAL`. HCI firmware uses session-bound
`fw-flash-dongle` as described above. Use the runner's session manifest and
fresh identity checks for every target-changing action; never choose a probe
by target family alone when both XIAOs are attached.

## Serial

nRF54L15 XIAO receiver console uses SAMD11 UART20 bridge at 115200 8N1.
Resolve receiver USB/tty through current bound session and live identity,
not a fixed `/dev/ttyACM*` index. Capture source console separately before
reset; see `scripts/hil-runner.py` session and capture workflows.

Expected after receiver boot: `BLE ready`, `settings_load() OK`,
`Advertising as "LE Audio Receiver"`. During streaming,
`i2s_nrfx: Next buffers not supplied on time` should no longer occur
in steady-state once the PI clock recovery controller converges
(Phase 3). Recovery is automatic (`TRIGGER_PREPARE` + re-arm) if
transient underruns happen.

### Probe identification — NEVER assume the probe↔board mapping

Probes get replugged; documentation rots. `nrf-probes` (provided on PATH by
the [nix-nrf-dev](https://github.com/qarnet/nix-nrf-dev) flake, along with
openocd-master and the NCS toolchain shell) is the source of truth:

```bash
nrf-probes            # enumerate probes and target fingerprints (read-only)
```

It fingerprints each CMSIS-DAP probe's target over SWD (DPIDR → AP IDR map →
FICR INFO.PART/VARIANT). Two XIAOs share the same family: compare raw
fingerprints and USB identity with the bound receiver/source session before
selecting either probe or tty. The runner revalidates these at six lifecycle
checkpoints; do not replace role matching with `--find nrf54`.

**Doc hygiene rule:** never write a static probe-serial↔board table into
docs or handoffs — reference `nrf-probes` instead. Any hardware-identity
claim in a handoff MUST include the raw evidence it rests on (DPIDR, AP IDR
map, FICR PART value), not just the conclusion. A 2026-07-05 session lost a
day chasing a phantom APPROTECT problem because a handoff asserted an
inverted probe mapping without evidence.

### Capturing boot logs during testing

The console must be captured **before** reset. Use a bound external session,
role-resolved serial, and `scripts/hil-runner.py` for the six-checkpoint
identity/evidence lifecycle. For direct diagnostics, open the live matched
receiver or source console before resetting that board. Never reuse an old
tty or probe index from a log.

## Gotchas

### Historical nRF5340-only SW Split and recovery recipes

`add_overlay_config()` alone sets Kconfig, but the nRF5340 cpunet DTS
defaults to `bt_hci_sdc` (SoftDevice). Without `add_overlay_dts(...,
bt-ll-sw-split.overlay)` the net core quietly stays on SoftDevice and
`bt_enable()` fails with `Bluetooth init failed: -5`
(`HOST_BUFFER_SIZE` returns `UNSUPPORTED_FEATURE`).

This is a dated nRF5340 CPUNET finding, not an instruction for current
nRF54L15 receiver, XIAO source or integrated nRF54L15BSim controller.

### `settings_load()` must run after `bt_enable()` and before `bt_pacs_register()`

`CONFIG_BT_GATT_DYNAMIC_DB=y` registers PACS/ASCS dynamically. Without
`settings_load()` these characteristics are invisible to remote peers.
The call must be after `bt_enable(NULL)` and before `bt_pacs_register()`.
**Do NOT skip `settings_load()`** to "clear bonds" — it will break PACS registration.

### Settings persistence; nRF53 recovery is historical only

`west flash` only erases the firmware address ranges. The ZMS settings
partition (bonds, PACS registered handles) persists across flashes.
The old `nrf53_recover` + E83 reflash recipe applied to nRF5340 only.
Never run it on either XIAO. For nRF54L15 bonding problems, diagnose both
peers' stored bonds and use supported, identity-checked target-specific
procedures; do not infer that firmware flashing cleared settings.

### nRF5340 APPROTECT is a SOFT branch — an erased UICR bricks debug access

On the nRF5340, debug access after any reset is only open if
`UICR.APPROTECT == 0x50FA50FA` (Unprotected): SystemInit copies that UICR
word into `CTRLAP.APPROTECT.DISABLE` at boot. After a mass erase, UICR reads
`0xFFFFFFFF` → the AP hard-locks at every reset **even though the firmware
boots and runs fine**. Symptoms: `Examination failed` /
`Failed to read memory at 0xe000ed00` on connect while the board happily
advertises. The only way back in is a CTRL-AP recovery (= another mass erase).

Historical `flash_nrf5340.tcl` therefore programmed `UICR.APPROTECT`,
`UICR.SECUREAPPROTECT` (app, `0x00FF8000`/`0x00FF801C`) and net
`UICR.APPROTECT` (`0x01FF8000`) to `0x50FA50FA` after every flash
(`uicr_unprotect_app` / `uicr_unprotect_net`). This is not a current XIAO
flash step; never apply the nRF53 UICR recipe to nRF54L15.

### Recovery coverage: nRF5340 only — the nRF54L15 has NO recovery path

Known gap (documented 2026-07-05, deliberately not fixed yet):

- **nRF5340**: recovery works but is nRF53-specific — `nrf53_recover` /
  `check_approtect` chain to `_nrf_ctrl_ap_recover` in openocd's
  `common.cfg`, which hardcodes the nRF53 CTRL-AP IDR (`0x12880000`).
- **nRF54L15**: **no valid recovery exists in our tooling.** Upstream
  OpenOCD (master) has no `nrf54l_recover`, no flash bank, nothing; the
  generic CTRL-AP proc rejects the 54L's CTRL-AP (different IDR, AP#2).
  If a 54L15 ever ends up APPROTECT-locked, current options are Nordic's
  official path (`nrfutil device recover` — requires a J-Link) or writing
  and testing an adapted CTRL-AP TCL proc against a sacrificial board.
  Nothing we do in normal operation locks the 54L15 (its APPROTECT is not
  the 5340's soft-branch design), but treat this as unprotected territory.

### Do NOT use probe-rs — openocd-master is the only flash backend

Project policy: flashing goes through openocd-master (receiver and source
helpers, and session-bound HCI flash). probe-rs was evaluated 2026-07-05
(0.31.0) and rejected; the following nRF5340 incident is historical:
on the nRF5340 its attach sequence reset-catches the core *before*
SystemInit runs the APPROTECT soft-unlock, concludes the chip is locked,
and its only remedy is `--allow-erase-all` — a full mass erase that also
wipes UICR, re-creating the lock for the next invocation (it bricked debug
access three times during the eval; openocd recovered it each time). It
also has no notion of the dual-core flash ordering (net FORCEOFF release).
It did work on the nRF54L15, but a second backend for one chip is not
worth the complexity.

### Stale bonds cause pairing failures that block PACS/ASCS reads

If a central was previously bonded and the bond info is reloaded from the
settings partition on boot (`settings_load()`), but the central still tries
to pair fresh or the firmware version changed security params, pairing
will fail.  The central then disconnects before it can read the encrypted
PACS/ASCS services.

**Fix:** Clear mismatched peer bonds with the supported identity-checked
procedure (for example remove the bond on the central). The historical
`nrf53_recover` option is not valid for the XIAOs.

### printk and LOG output race on the same UART

When both `printk()` and `LOG_*()` macros write to the same UART console
simultaneously, lines can interleave and become unreadable. Add the
following to `prj.conf` to route `printk()` through the same backend as
`LOG_*()`:

```
CONFIG_LOG_PRINTK=y
```

This serializes output and eliminates garbled lines.

### ZMS settings backend requires explicit flash deps

ZMS needs `CONFIG_FLASH=y`, `CONFIG_FLASH_PAGE_LAYOUT=y`, and
`CONFIG_FLASH_MAP=y`. Without all three, `SETTINGS_ZMS` silently falls
to `SETTINGS_NONE` (no storage, no bond persistence across reboots).

### Board-specific Kconfig belongs in board conf, not prj.conf

`prj.conf` applies to all targets. Board-specific symbols belong in the
corresponding app-level `boards/<board_target>.conf`, not `prj.conf`; an
inapplicable assignment creates a Kconfig "assigned value but got" warning.
The former E83 bypass example is historical, not a current board recipe.

### ACL/ISO TX buffer counts must match the controller

Historically the nRF5340 SW Split controller reported 7 ACL and 6 ISO TX
buffers; that dual-core configuration is not the receiver's current contract.
Host/controller buffer mismatches must still be fixed, not normalized.

On nRF54L15 the SDC controller defaults `BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=3`.
For sink-only, the board conf sets both `CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1`
and `CONFIG_BT_ISO_TX_BUF_COUNT=1` so they match — the host's
`Num of Controller's ISO packets != ISO bt_conn_tx contexts` warning is
silenced at the source, not tolerated.

### Centrals require Just Works pairing

Default `CONFIG_BT_SMP_ENFORCE_MITM=y` forces authenticated pairing.
Without a passkey UI the central shows "incorrect PIN". Disable MITM
(`CONFIG_BT_SMP_ENFORCE_MITM=n`) and add `pairing_accept` /
`pairing_complete` / `pairing_failed` callbacks returning
`BT_SECURITY_ERR_SUCCESS`. See `src/bt_bap.c` pairing callbacks.

### The `sdk-nrf` west project must be named `nrf`

`nrf/modules/modules.cmake` hardcodes `SYSBUILD_NRF_KCONFIG`. Naming it
`sdk-nrf` breaks cmake with "Could not open '/workspace/zephyr/' (EISDIR)".

### WSL2 J-Link

WSL2 requires `usbipd` on the Windows host to bind the J-Link to WSL2.
Without it, `west flash` fails with "Cannot connect to the probe".

### `bt_audio_codec_cfg_get_chan_allocation` returns 0 on success

The API fills `*chan_allocation` via pointer and returns **0 on success**,
negative errno on failure. Checking `if (ret > 0)` silently falls through
to the mono default for every source that sends a valid channel allocation
LTV — making all stereo ASEs appear mono. Use `if (ret == 0)`. See
`lc3_config` in `src/bt_bap.c`.

### Stereo single-ASE (Mode B) needs two LC3 decoders

A BAP source may send one ASE with `chan_count=2` (stereo) rather than two
mono ASEs. In that case, the SDU is `[L_frame][R_frame]` concatenated.
One `lc3_decode` call with stride=2 only fills even (L) positions;
odd (R) positions stay zero → right channel silent. Two independent
`lc3_decoder_t` instances are required: decode L into `stereo_out[0]`
stride 2, R into `stereo_out[1]` stride 2.

Per-channel octets = `(sdu_len / frames_per_sdu) / chan_count`.

This logic lives in `audio_decode_sdu` (`src/audio_decode.c`).

### I2S double-write of same slab block causes DMA corruption

Passing the same `void *block` pointer to `i2s_write` twice queues the
same DMA buffer twice. When the first DMA transfer completes the driver
frees the slab block; the second DMA transfer then operates on freed
memory → underrun or heap corruption. Always allocate a separate slab
block for each `i2s_write` call.

### I2S DMA underrun recovery requires `TRIGGER_PREPARE`

After `i2s_nrfx: Next buffers not supplied on time`, subsequent
`i2s_write` calls return `-EIO` (state 4 = ERROR). Call
`i2s_trigger(dev, TX, I2S_TRIGGER_PREPARE)` to reset to READY, then
re-arm: set `started = false` so the next `audio_i2s_push` pre-fills
and re-triggers.

### `audio_sink_stop` must not clear `configured`

After disconnect, `audio_sink_stop` drops the DMA (`TRIGGER_DROP`) and
resets `started`. Clearing `configured` causes every subsequent
`audio_sink_push` on reconnect to return `-EIO`. Keep `configured = true`
so reconnect works without re-calling `audio_sink_init`.

### Clock recovery actuator must match platform

The production actuator is `NONE` (`src/audio_clock_actuator_none.c`):
nRF54L15 ASRC consumes controller ppm directly, without a physical actuator.
Production APLL was retired. Historical APLL conversion and no-HFCLKAUDIO
tests use `tests/unit/actuator_apll/src/audio_clock_actuator_apll_historical.c`
and do not restore an E83 production path.

The production actuator API is init/apply_ppm/reset only (clock steering,
no data-path adjustment).  The historical SAMPLE_ADJUST actuator — including
its retired `audio_clock_actuator_consume_sample_adjustment()` symbol — is
retained for regression testing only as a test-local copy under
`tests/unit/actuator_sample_adjust_historical/src/`; no longer selectable in
production Kconfig.

### Drift controller: PCLK feedforward + per-block phase PI (Phase 4b.2)

Controller has two explicit inputs:
- `audio_drift_frequency_error_update(local_clock_error_ppm)` — from platform
  timing (nRF54L15: PCLK TIMER20 vs GRTC; BSim uses no-op timing).
  Positive = local PCLK/I2S runs faster than controller. Feedforward correction
  = `-measured` (local fast → negative correction → eventual insert).
- `audio_drift_controller_update(slab_free)` — called ONCE per rendered stereo
  block in `audio_sink_push()`, before slab allocation. Phase error =
  `PHASE_SETPOINT - slab_free` (corrected sign vs earlier code). Combines
  filtered frequency feedforward + phase PI. No floating point; pure 32-bit
  integer with 64-bit intermediate multiplication.

Output sign: positive ppm = consume source faster / drop frame eventually;
negative ppm = consume source slower / insert frame eventually.
Output clamp: `CONFIG_AUDIO_DRIFT_OUTPUT_CLAMP` (default 500; nRF54L15: 2000).
Phase integral clamp: `CONFIG_AUDIO_DRIFT_PHASE_INTEGRAL_CLAMP` (default 500;
nRF54L15: 150).

`audio_sink_sdu_ref_update()` is REMOVED. ISO timestamps go ONLY to
`audio_timing_sdu_ref_update()` for GRTC scheduling. Never call drift
controller from ISR — work/thread context only.

### SDC/MPSL owns RADIO — never access RADIO directly

On nRF54L15 (SDC on cpuapp), MPSL owns the RADIO peripheral. Never configure
or read RADIO registers, RADIO events, RADIO IRQ, or RADIO DPPI publication
subscriber. Any direct RADIO access will conflict with the SoftDevice
Controller runtime. For drift measurement on nRF54L15, use ISO `info->ts` with
`BT_ISO_FLAGS_TS` (controller-clock ISO SDU reference), GRTC future
compare/action (Nordic ISO-time-sync pattern; sample at
`nrf/samples/bluetooth/iso_time_sync/`), and TIMER20 in TIMER mode (PCLK-
derived free-running ticks) with GRTC compare → GPPI → TIMER20 CAPTURE
(hardware-snapshotted counter). Phase 4b.1 logs diagnostics; Phase 4b.2
feeds measured ppm into the PI controller. **Historical: I2S20 FRAMESTART
→ GPPI → TIMER20 COUNT was invalidated — HW validation on 2026-07-26 showed
FRAMESTART fires at DMA buffer boundaries (~100 Hz), not LRCK edges.**
`sdc_hci_cmd_vs_set_event_start_task()` is an ACL-event diagnostic,
not a CIS RX timestamp.

### Zephyr does NOT detect devicetree pinctrl overlaps

Two peripherals claiming the same pin in their `pinctrl-N` default groups produce
**no compile error and no runtime warning**. The last peripheral to init a
contested pin wins the PSEL; the loser silently corrupts. Verify pin assignments
against all enabled peripherals by decoding the resolved `zephyr.dts` (psel
encoding: `NRF_PSEL(fun, port, pin)` = `(fun << 24) | ((port*32+pin) & 0x1ff)`;
see `nrf-pinctrl.h`). This is how the nRF54L15 I2S20 pin conflict (P1.10/P1.11/P1.12
vs pwm20/pdm20) went unnoticed — fixed by moving I2S20 to D0/D1/D2 (P1.4/P1.5/P1.6)
and disabling `&pdm20`.

### CJMCU-1334 (UDA1334A) wiring

| Board | BCK | DIN | LRCK | VIN | GND |
|-------|-----|-----|------|-----|-----|
| nRF54L15 (Seeed Xiao) | D0 (P1.4) | D2 (P1.6) | D1 (P1.5) | 3V3 | GND + AGND |

Config pins (SF0/SF1/MUTE): on the Adafruit UDA1334A breakout these are
**pre-pulled to GND by on-PCB resistors** (R10/R2/R9 — verified against the
Adafruit PCB schematic), so leaving them floating = I2S format + unmuted. On
a bare clone without the pulldowns, wire all three to GND explicitly. MUTE
is LOW = unmuted (inverted vs most mute pins). SCLK/PLL leave unconnected
(internal PLL locks to BCLK). Audio out: Lout / Rout to headphone L/R,
AGND to sleeve.

Either UDA1334A (CJMCU-1334) or PCM5102A works — same 3-wire no-MCK topology.
PCM5102A's spec lead (112 dB / 32-bit / 384 kHz vs 100 dB / 16-bit) is
inaudible at the 48 kHz/16-bit LC3 floor. PCM5102A cheap breakouts need the
SCK pad solder-bridged to GND for 3-wire mode or you get silence/hiss.

## HIL source fixture timing — the pinned lessons (2026-09-09/11)

Historical nRF5340DK source incident below, **not an active XIAO build or
clock recipe**. Current single-CPUAPP XIAO source uses direct GRTC; the
CPUNET RTC mirror and nRF53 128 MHz divider are retired. Its Bluetooth TX
processor stack is 2048 after a measured 900-byte stack overflow during a
preserved Mode B row (see `docs/development/pb-035-source-matrix-results.md`).
Keep the incident's telemetry limits and validation discipline, but use the
current source/receiver image pair for isolation, not an old three-image tuple.

The RH3 "Mode B delivery collapse" investigation (ModeA9–ModeA18, 2026-09,
evidence immutable under `/tmp/opencode/hil-runs/`, canonical record
`docs/development/system-hil-rh3-controller-clock-result.md`) burned ~10
hardware runs and a week on a fixture defect that was ours, while
repeatedly concluding — with confidence — that the SoftDevice Controller
was broken. Pin these before touching the fixture again:

1. **The nRF5340 source app core must run at 128 MHz for this workload.**
   `hil/source/app/src/main.c` calls
   `nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1)`
   before Bluetooth init. Without it the core runs at 64 MHz, two LC3
   encodes do not fit one 10 ms SDU interval, and the fixture starves the
   controller-clock scheduler (the 2026-09-08 baseline: `sub=10907`,
   `skip=10185`, roughly alternate events missed with zero send errors —
   looks exactly like a controller-side flush). Nordic's own real-time
   audio code sets DIV_1 for the same reason:
   `nrf/applications/nrf5340_audio/src/modules/audio_clock.c`,
   `nrf/tests/bluetooth/iso/src/main.c`,
   `zephyr/subsys/bluetooth/audio/shell/bap_usb.c`. Any new HIL source
    feature that added per-SDU CPU work had to re-check throughput at
    128 MHz before inventing controller theories. Do not apply this divider
    setting to the XIAO source.

2. **Schedule against the controller clock, never a host-derived offset.**
   The working scheduler mirrors the CPUNET MPSL RTC into app-core RTC0
   via IPC channel 4 + PPI before Bluetooth starts
   (`hil/source/app/src/hil_source_controller_time.c`, the pattern from
   `nrf/samples/bluetooth/iso_time_sync/src/controller_time_nrf53_app.c`),
   encodes BEFORE the send window, and submits at 3000 us lead / 2000 us
   minimum against that clock. The failed designs derived a
   host-vs-controller offset from `HCI VS ISO Read TX Timestamp` +
   callback time — self-referential bookkeeping that "proved" whatever
   the fixture was already doing.

3. **Know what each telemetry source actually measures.**
   - ISO `sent` callback = controller ACCEPTED the SDU (completion may
     follow enqueue, transmit, or flush — `iso.h`,
     `struct bt_iso_chan_ops.sent`). Instant completions do NOT mean
     on-air success.
   - `HCI VS ISO Read TX Timestamp` = the SCHEDULED event of the last
     provided SDU (re-documented in v2.9.0, DRGN-23708). NOT current
     controller time, NOT air proof.
   - `HCI LE Read ISO TX Sync` = per-SDU sync reference of the last
     SCHEDULED SDU (DRGN-21293); the command SUCCEEDING requires a
     transmitted SDU but the poll is not an aired-SDU counter.
   - Receiver ISO counters = the only peer-delivery truth. They show the
     collapse was real, but not which side caused it.

4. **Investigation discipline (the part that failed hardest).**
    - For this historical nRF53 fixture, validate against the FULL Nordic reference pattern
     (including the RTC mirror and clock setup) before assigning
     controller causation. The ModeA17 draft claimed "exactly the
     iso_time_sync pattern" while omitting its central mechanism.
    - A variation table is only an isolation if every run used the same
      image tuple (historically source/CPUNET/receiver; now XIAO source CPUAPP
      plus receiver CPUAPP/FLPR); dirty-worktree runs with
     different hashes are diagnostics, not isolation evidence.
   - Cite changelog entries by the header that actually governs the
     line, not by adjacency — the ModeA17 draft confidently placed the
     DRGN-23776 fix in the "v3.3.0 block" when it sits in v2.9.0
     (corrected 2026-09-11, `3dc12af`).
   - Do not reopen the withdrawn SDC-defect escalation
     (`docs/development/devzone-sdc-central-iso-tx-question-draft.md`)
     without a clean-commit regression that satisfies the conditions in
     its "Guidance for the next agent" section.

## Stack

- App: BAP Unicast Server sink-only, 2 sink ASEs, LC3 decode → I2S
- Receive/session: `audio_stream_session.c` (R6 — exclusive owner of app
  audio receive state: validated codec shape, decoder contexts, per-CIS
  ISO sequence trackers, Mode A assembler, receive counters, mode
  inference, decode/conceal/volume/push with admission/lease discipline);
  `bt_bap.c` keeps only Bluetooth service/lifecycle orchestration plus
  the R7 private teardown transition owner (first close wins, per-slot
  release once, universal close→drain→sink-stop→offload-stop→reset)
- Audio: `audio_sink.h` interface → `audio_i2s.c` (slab/DMA backend)
- Clock recovery: `audio_drift.c` (PI controller, ppm output) → `audio_clock_actuator_none.c` (nRF54L15, ASRC consumes ppm)
- ASRC: `audio_asrc.c` (fixed-point linear stereo, cpuapp) + FLPR offload (`src/flpr/`, handshake/runtime/rings)
- Decode: `audio_decode.c` (LC3 decode + channel routing, unit-testable)
- Controller: physical nRF54L15 receiver and source use SDC; two nRF54L15BSim peers use integrated SW Split with client reliability policy
- DAC: CJMCU-1334 (UDA1334A) or PCM5102A, 3-wire connection with DAC MCK unconnected

## Key Files

| File | Purpose |
|------|---------|
| `src/main.c` | Hardware wiring, watchdog, and advertising-loop adapter (fatal boot order lives in `app_lifecycle.c`) |
| `src/app_lifecycle.c` | Pure fatal boot coordinator: ordered init, cold reboot, advertising restart |
| `src/bt_bap.c` | BAP unicast server, ASCS callbacks, PACS, pairing, advertising, the thin recv adapter (R6: app audio receive state lives in `audio_stream_session.c`), and the R7 private teardown transition owner (`teardown_transition`/`teardown_close_path`: first close wins, per-slot release once, universal close→drain→sink-stop→offload-stop→reset order) |
| `src/bt_pairing_policy.c` | Pure OPEN/BONDED_ONLY policy snapshot; Bluetooth controller work stays in `bt_bap.c` |
| `src/audio_stream_session.c` | Exclusive owner of app audio receive/session state (R6): validated codec shape, decoder ctx, per-CIS ISO seq trackers, Mode A assembler, recv counters, decode/conceal/volume/push, admission/lease (rx_open/rx_close) |
| `src/audio_modea.c` | Bounded two-CIS event assembler and per-channel PLC |
| `src/audio_iso_seq.c` | Pure per-CIS omitted-callback sequence tracker |
| `src/audio_decode.c` | LC3 decode + channel routing (Mode A / Mode B / mono) |
| `src/audio_sink.h` | Platform-neutral audio-sink interface (init, push, stop; R1 stream_open/stream_close admission + drain) |
| `src/audio_i2s.c` | I2S TX driver (slab + DMA, 48 kHz stereo) — implements audio_sink.h |
| `src/audio_shell.c` | Audio diagnostics shell commands (`audio status`, `audio perf`, reset-stats/perf-reset/stop) |
| `src/bt_shell.c` | `bt unpair` pairing-mode reset command (R4) |
| `src/flpr_shell.c` | FLPR production diagnostics (`flpr status/offload/runtime/restart`, R4) |
| `src/flpr_acceptance_shell.c` | FLPR acceptance-harness commands (`flpr ring *`, `flpr stress`, `flpr hang`) — `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS`-gated (R4) |
| `src/audio_drift.c` | PI clock recovery controller (dual-term, ppm output) |
| `src/audio_drift.h` | Controller API |
| `src/audio_rate_convert.c` | Fixed-rate frame-count/remainder converter (I2S drain-rate matching; init/next_frames only, no resampling/copy API) |
| `src/audio_rate_convert.h` | Rate converter public API (unit-testable) |
| `src/audio_timing.h` | Platform timing interface (frequency error, GRTC scheduling) |
| `src/audio_timing_math.c` | Timing math shared across platforms |
| `src/audio_timing_nrf54.c` | nRF54L15 TIMER20-vs-GRTC PCLK frequency measurement |
| `src/audio_timing_none.c` | Simulator timing backend for nRF54L15BSim receiver |
| `src/stream_lifecycle.c` | Stream start/stop lifecycle (unit-testable) |
| `src/audio_clock_actuator.h` | Actuator interface (init, apply_ppm, reset) |
| `tests/unit/actuator_apll/src/audio_clock_actuator_apll_historical.c` | Historical APLL test-local conversion and no-HFCLKAUDIO regression |
| `tests/unit/actuator_sample_adjust_historical/src/audio_clock_actuator_sample_adjust_historical.c` | Historical sample insert/drop actuator, test-local copy (regression testing only) |
| `src/audio_clock_actuator_none.c` | nRF54L15 no-op actuator (ASRC consumes ppm directly) |
| `src/audio_asrc.c` | Fixed-point linear stereo ASRC (cpuapp + FLPR fallback) |
| `src/audio_offload.c` | FLPR offload manager (handshake, IPC, fallback path) |
| `src/flpr/` | FLPR firmware (RISC-V VPR): ASRC, ICMsg/VEVIF IPC |
| `src/flpr_handshake.c` | cpuapp↔FLPR boot handshake + VEVIF (R8: production slot reset/consumer + diagnostic slot registration; stress/fault-hang state moved to flpr_acceptance) |
| `src/flpr_protocol.h` | Shared protocol constants (ring layout, commands) |
| `src/flpr_ring.c` | SPSC ring buffer (shared SRAM, cache-safe) |
| `src/flpr_ring_mgr.c` | Ring manager production core: paired rings, reset, typed ASRC produce/consume, notify, wait, remote restart (R8) |
| `src/flpr_acceptance.c` | Cpuapp FLPR acceptance module (R8): ring test, stalls + ACK correlation, stale produce, report aggregation, stress, fault hang, gates 1–6 — `CONFIG_AUDIO_ACCEPTANCE_DIAGNOSTICS` |
| `src/flpr_control_ack.c` | Shared control-ACK correlation engine (R8): ONE owner for reset + stall ACK correlation |
| `src/flpr/acceptance.c` | FLPR-image acceptance handlers (R8): RING_TEST/STALL/STRESS/FAULT_HANG + diagnostic hooks — `CONFIG_FLPR_ACCEPTANCE_DIAGNOSTICS` |
| `src/flpr_runtime.c` | FLPR runtime: IPC submit, watchdog, fault detection |
| `src/flpr_audio_process.c` | FLPR audio block wrapper (metadata + PCM) |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | Xiao nRF54L15 remap: UART20 to SAMD11, I2S20 to D0/D1/D2 (MCK on D3/P1.7 — peripheral-needed routing, DAC does not consume it; 3-wire no-MCK at the DAC), pdm20 disabled, TIMER20 reserved, FLPR IPC SRAM regions |
| `prj.conf` | App Kconfig (ACL/ISO buffers, SMP, 2 ASEs, liblc3, FPU, ZMS) |
| `sysbuild.cmake` | Receiver CPUAPP/FLPR sysbuild integration |
| `Kconfig.sysbuild` | nRF54L15 sysbuild configuration |
| `scripts/bap_central.py` | BAP central test driver — thin CLI coordinator (argparse + wiring + flow) plus the `CentralCleanup` idempotent resource owner (fixed teardown order, safe from `finally`; every fatal path raises a module `CentralError` with the message already printed and exit 1 preserved) |
| `dongle/hci_uart/` | XIAO nRF54L15 single-image SDC Linux HCI central, async UART20 H4 bridge and directly tested H4 parser |
| `scripts/hci_dongle.py`, `scripts/bin/fw-{build,flash,reset,attach}-dongle` | Session-bound HCI build, identity-checked flash/reset/attach and scoped cleanup |
| `scripts/bap_central_device.py` | Central device resolution (R9): adapter power, `--peer-addr` exact-peer path, existing Device1 enumeration, bounded `InterfacesAdded` discovery — `DiscoverySession` owns its signal match and StopDiscovery exactly once |
| `scripts/bap_central_security.py` | Central agent/pairing/connect (R9): JustWorks agent factory, raw-HCI fresh-connect strategy (exact `sudo -n` argv, ready + Connected gates), BlueZ preserve-bond Connect strategy, `wait_for_helper_ready` (READY_PREFIX from `hci_raw_connect.py`), RemoveDevice fresh-only, Pairable/Trusted/async Pair, services-resolved, cleanup Disconnect |
| `scripts/bap_central_endpoint.py` | Central BAP source endpoint (R9): constants/LC3 blobs, `MediaEndpoint1` class factory, registration, deferred async Acquire, pending/acquired fd ownership, second-ASE grace, all-or-nothing, mode inference |
| `scripts/bap_central_session.py` | Central LC3 source/writer (R9): lazy liblc3 loader + encoder (stdlib-safe import), sine, per-mode payloads, `StreamSession` writer lifecycle + exact teardown tail |
| `README.md` | Public LE Audio explainer: XIAO receiver, supported sources, docs links |
| `docs/user-guide.md` | Public user guide: boot, pairing modes (NORMAL/BONDING/RESET), flashing notes, troubleshooting |
| `docs/hardware-wiring.md` | Public wiring: DAC choice, XIAO I2S pins, config pins, line-level warning |
| `docs/known-limitations.md` | Public known-limitations list (48 kHz only, 360-frame FLPR fallback, linear volume, pop, duplicate adv, etc.) |
| `docs/supported-sources.md` | Public researched Linux LE Audio source hardware + software requirements (status labels Project-validated / Vendor-supported / Unverified; Intel AX210 project-validated, dongles unverified) |
| `docs/technology/nrf54l15.md` | Current platform technology notes; `docs/technology/nrf5340.md` is historical |
