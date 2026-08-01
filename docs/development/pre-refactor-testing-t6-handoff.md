# Phase T6 handoff — boot, diagnostics, and resolved configuration

## Goal

Close Phase T6 from `docs/development/pre-refactor-testing-plan.md` by adding a
narrow testable boot coordinator, direct production shell-command tests, and a
stdlib-only resolved build-contract checker for both production targets.

Start from clean `test/pre-refactor-behavior` at `c1629cc`. Preserve all
accepted T0–T5 behavior and pinned T4 BabbleSim output.

## Scope

In scope:

- `src/app_lifecycle.{c,h}`, minimal `main.c` delegation, production CMake.
- `tests/unit/app_lifecycle/`.
- Narrow test seams in `src/audio_shell.c` and `tests/unit/audio_shell/` for
  common and nRF54 status paths.
- Defined/zero-safe shell formatting defects found by tests.
- `scripts/check-build-contract.py` and stdlib Python tests/fixtures.
- Canonical gate bookkeeping and T6 testing/status docs.

Out of scope:

- BAP/ASCS behavior or BSim scenario/hash changes.
- Broad boot-framework architecture or dependency injection outside this app.
- Rewriting FLPR shell acceptance commands.
- Coverage enforcement/tooling (T7).
- Physical hardware acceptance (T8).
- Push, merge, PR, amend, force-push, or history rewrite.

## A. Narrow boot coordinator

### Current invariant

Current `src/main.c` fatal init order is:

1. watchdog
2. `bt_enable(NULL)`
3. `settings_load()`
4. `audio_volume_init()`
5. `bt_bap_init()`
6. `audio_sink_init()`
7. nRF54-only nonfatal FLPR handshake/offload/runtime initialization
8. initial `bt_bap_restart_advertising()`

Every nonzero result from steps 1–6 or 8 triggers
`sys_reboot(SYS_REBOOT_COLD)`. After startup, each disconnect wakes the loop;
advertising restart failure also triggers cold reboot.

`settings_load()` must remain after Bluetooth enable and before BAP/PACS
registration. Do not alter this order.

### Required API and ownership

Create `src/app_lifecycle.h` with a narrow operations structure. Exact naming
may follow repo style, but semantics must be:

```c
struct app_lifecycle_ops {
    int (*watchdog_init)(void);
    int (*bluetooth_init)(void);
    int (*settings_init)(void);
    int (*volume_init)(void);
    int (*bap_init)(void);
    int (*sink_init)(void);
    void (*platform_init)(void);       /* optional, nonfatal nRF54 wiring */
    int (*advertising_start)(void);
    void (*cold_reboot)(void);
};

int app_lifecycle_boot(const struct app_lifecycle_ops *ops);
int app_lifecycle_restart_advertising(const struct app_lifecycle_ops *ops);
```

`app_lifecycle_boot()` runs the exact order above. On any fatal callback error,
it logs the step/error, calls `cold_reboot()` exactly once, stops immediately,
and returns the original error only if the reboot callback returns (unit-test
behavior). `platform_init`, when non-NULL, runs only after all six subsystem
steps succeed and is nonfatal. Initial advertising is the final fatal step.

`app_lifecycle_restart_advertising()` calls only `advertising_start`; failure
logs, cold-reboots exactly once, and returns original error if reboot returns.
Success returns zero. Validate null operations/required callbacks with
`-EINVAL`; do not dereference missing required callbacks or reboot for malformed
test wiring.

`main.c` retains:

- watchdog device/thread/configuration;
- wrappers adapting `bt_enable(NULL)`, `sys_reboot(SYS_REBOOT_COLD)`, and real
  subsystem functions to operation signatures;
- nRF54-only platform initializer calling `flpr_handshake_init()`,
  `audio_offload_init()`, then `flpr_runtime_init()` in current order;
- device-name startup logs;
- infinite `bt_bap_wait_disconnect()` loop.

Main calls `app_lifecycle_boot()` once, then uses
`app_lifecycle_restart_advertising()` after each disconnect. If a test-style
reboot callback unexpectedly returns in production, main must not continue into
normal operation after nonzero result.

Add `src/app_lifecycle.c` to production target sources.

### Required lifecycle tests

Create `tests/unit/app_lifecycle/` compiling production
`src/app_lifecycle.c`. Record ordered callback IDs and configurable return
codes. Cover:

- exact all-success order including platform init and initial advertising;
- platform callback absent;
- each of seven fatal steps failing independently: no later callback, one cold
  reboot, original errno returned;
- platform callback is nonfatal/void and remains between sink and advertising;
- initial advertising failure reboot;
- restart success invokes only advertising;
- restart failure invokes advertising then one cold reboot and returns error;
- malformed/null ops and each missing required callback return `-EINVAL`
  without calls/reboot.

Do not compile `main.c` into this unit suite; hardware wiring remains proven by
production builds.

## B. Shell behavior tests

### Test real production command bodies

Create `tests/unit/audio_shell/` that compiles `src/audio_shell.c` directly.
Use Zephyr shell test/dummy backend or a project-owned capture backend and mock
the public subsystem APIs. Do not copy formatting logic into a test model.

Add only narrow `AUDIO_SHELL_TEST`-guarded wrappers/declarations needed to call
otherwise-static command handlers. Test seams must not enter production builds.
It is acceptable to build common and nRF54 variants as separate testcase
directories/configurations when that keeps mocks bounded and proves conditional
fields. Both must remain auto-discoverable by `scripts/test-all.sh`.

Keep command names and parseable field labels stable. Cover at minimum:

### Common commands

- `audio status`: exact field order/labels for frames, PLC, decode errors, I2S
  underruns, stream resets, drift state/ppm, resampler, volume/muted suffix.
- zero frames produces `PLC frames : 0 (0%)` without division by zero.
- large `plc_frames`/`total_frames` values do not overflow percentage
  numerator. Change calculation to `uint64_t` before multiply/divide while
  preserving integer truncation.
- identity and ASRC resampler variant strings are each compiled/proved; do not
  claim runtime selection.
- `audio perf`: exact path labels, zero-count averages, integer one-decimal
  deadline percentage, queue fields consumed by diagnostics, and no divide by
  zero when measurement is disabled/deadline unavailable.
- `audio reset-stats`, `audio perf-reset`, and `audio stop` each call the proper
  dependency exactly once and emit stable confirmation.
- `bt unpair`: success text/zero result; failure text and exact negative errno
  propagation.

For disabled perf measurement, display a truthful unavailable/zero deadline
percentage rather than computing against a fake 1 us deadline. Keep parseable
table shape stable.

### nRF54 FLPR fields consumed by gates

With controlled status structures, prove exact parseable labels and values for:

- `flpr status`: ready, ACKed, healthy, epoch, errors, TX/RX/loss/order fields.
- `flpr ring status`: initialized, epoch, both ring counters/used/space,
  CPUAPP diagnostics, FLPR diagnostics when present, test and latency sections.
- `flpr offload`: state/epoch/generation, counters, fault counters, recovery,
  probation, runtime restart line, heartbeat dedup, RTT/last error, and ASRC
  counters/faults/RTT/cycles used by `scripts/flpr_hang_gate.py`.
- `flpr runtime`: state, failed stage, request/success/failure/busy counts,
  epochs, reload bytes, CRC, errno, duration, DMCONTROL, INITPC, CPURUN.

Provide out-of-range enum values to status mocks and ensure status commands
print `UNKNOWN` rather than indexing past state/stage string arrays. Fix with
bounded conversion helpers if current code fails.

No need to unit-test long-running FLPR acceptance/stress/stall mechanics again;
their production modules already have direct suites. T6 locks diagnostics and
side-effect commands named by plan.

Compiler/Kconfig warnings are failures. Do not disable warnings-as-errors.

## C. Resolved build-contract checker

Create executable `scripts/check-build-contract.py`, Python stdlib only.
Invocation stays:

```bash
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
```

### Input resolution

Resolve exact files beneath each sysbuild root:

- app: `le-audio-receiver/zephyr/.config` and `zephyr.dts`;
- nRF5340 controller: `hci_ipc/zephyr/.config` and `zephyr.dts`;
- nRF54 FLPR DTS/config when needed for cross-image memory proof.

Missing, duplicate, unreadable, or malformed required inputs are hard failures.
Never silently select top-level sysbuild `.config` instead of image config.

Parse `.config` into explicit set/unset/string/integer values. Parse resolved
DTS after removing comments into node/property structure with labels and nested
paths; do not satisfy checks from comments or substring coincidence. Numeric
cells must accept hex and decimal. Produce deterministic human-readable PASS or
FAIL lines, list all failed assertions in one run, and exit 0 only when every
contract passes.

### nRF5340 assertions

- app: `CONFIG_AUDIO_RESAMPLER_IDENTITY=y`, APLL=y, ASRC/NONE not enabled,
  output sample rate 48000, `CONFIG_LIBLC3=y`, two sink ASEs,
  `CONFIG_I2S_NRFX_ALLOW_MCK_BYPASS=y`.
- host counts: ACL TX=7, ISO TX=6, ISO RX=6.
- app I2S0 is okay, 12.288 MHz HFCLKAUDIO, and resolved default pin cells are
  exact BCK P1.15, LRCK P1.12, SDOUT P1.13; QSPI disabled; WDT0 okay.
- netcore Kconfig: `CONFIG_BT_LL_SW_SPLIT=y`, peripheral ISO and connection ISO
  enabled, ACL TX=7 and ISO TX=6 matching app host values.
- netcore DTS: chosen `zephyr,bt-hci` resolves to `bt_hci_controller`, that node
  is okay and compatible `zephyr,bt-hci-ll-sw-split`; `bt_hci_sdc` is disabled.
  This proves both required SW Split Kconfig and devicetree overlays.

### nRF54L15 assertions

- app: ASRC_LINEAR=y, NONE=y, APLL/IDENTITY not enabled,
  `CONFIG_AUDIO_OFFLOAD_ASRC=y`, output sample rate 47619,
  `CONFIG_LIBLC3=y`, two sink ASEs.
- host/controller: ACL TX=3, host ISO TX=1,
  `CONFIG_BT_CTLR_SDC_ISO_TX_HCI_BUFFER_COUNT=1`, host ISO RX=3; explicitly
  assert host ISO TX equals controller ISO TX.
- I2S20 is okay with `clock-source = "PCLK32M"`; resolved default pin cells are
  exact SCK P1.4, LRCK P1.5, SDOUT P1.6, MCK P1.7.
- PDM20, SPI00, and MX25R64 disabled; TIMER20 reserved.
- RF switch fixed regulators: ctl GPIO2.5 active-low (`flags=1`), power GPIO2.3
  active-high (`flags=0`), both `regulator-boot-on`.
- LFXO and HFXO use internal load capacitors and exactly 16000 fF.
- exact app-side ranges:
  - cpuapp SRAM `0x20000000 + 0x28000`;
  - RX `0x20028000 + 0x2000`;
  - TX `0x2002A000 + 0x2000`;
  - PCM ring `0x2002C000 + 0x4000`;
  - FLPR execution SRAM `0x20030000 + 0x10000`;
  - FLPR code partition `0x165000 + 0x18000`.
- SRAM intervals are exact, contiguous where designed, non-overlapping, and all
  lie within physical `0x20000000..0x20040000`.
- Cross-check FLPR image resolved memory/chosen/code-partition values against
  app-side launcher ranges where represented in FLPR DTS.

### 48 kHz capability statement

Resolved build data can prove output rate, liblc3 inclusion, and two sink ASEs;
PACS LC3 frequency LTV is a C object, not a Kconfig/DTS property. Do not invent
a resolved property. Checker reports this build proof as the resolved half of
the 48 kHz contract and references existing direct production-source T2/T4
tests for exact advertised 48 kHz/7.5+10 ms/1+2 channel capability. If needed,
add a small source-contract check against `src/bt_bap.c`, clearly labeled as
source rather than resolved data, and unit-test it. Never claim `.config` alone
contains PACS frequency LTV.

### Checker tests

Create stdlib `unittest` tests under `tests/unit/build_contract/` with minimal
temporary sysbuild fixtures. Cover:

- complete valid dual-target fixture;
- missing image/file;
- malformed/duplicate config key;
- explicit unset vs set symbols;
- comments cannot satisfy DTS assertions;
- wrong node status/compatible/chosen;
- wrong encoded pin;
- host/controller count mismatch;
- wrong RF polarity/capacitance;
- missing/overlapping/out-of-range memory interval;
- SW Split Kconfig-only and DTS-only half failures;
- deterministic multi-error report and nonzero exit.

Expose parser/check functions for direct imports; also test CLI success/failure.
Add this Python suite to `scripts/test-all.sh`. Do not make `test-all.sh` depend
on pre-existing firmware build directories; actual resolved-contract acceptance
runs after pristine production builds.

## D. Documentation and validation

Update:

- `docs/testing/behavior-contract.md`
- `docs/testing/coverage-matrix.md`
- `STATUS.md`
- `scripts/test-all.sh` documented suite counts

Record exact focused counts, checker assertion/test counts, exact validated code
commit, production defects fixed, full gate/build/checker output, and cleanup.
Mark T6 accepted only after all checks pass. Say T7 next.

Run focused suites/checker tests during development. Then create a completed
code commit and validate that exact commit:

```bash
./scripts/test-all.sh
fw-build-5340
fw-build-54l15
fw-build-dongle
python3 scripts/check-build-contract.py \
  --nrf5340 build/nrf5340 \
  --nrf54l15 build/nrf54l15
git diff --check
git status --short
```

Workstation detached-worktree validation is allowed. Keep workstation `main`
untouched and remove temporary refs/worktrees/bundles/logs. Commit code/tests,
then evidence docs separately. No attribution footer.

## Escalation

After two materially different failed attempts, stop and ask Orchestrator.
Especially stop for unresolved shell-linking, DTS grammar, cross-image range,
or generated-path contradictions. Do not copy production behavior into test
models, weaken parser assertions, match DTS comments, suppress warnings, accept
stale builds, or keep guessing. Return blocker, attempts, commands/logs/diff,
git status, one precise question, and smallest hypothesis. Do not commit
knowingly failing/incomplete work.
