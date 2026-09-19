# PB-031 P0 handoff: ARM calibration image

Status: Approved implementation handoff. Reviewed execution result appended
below; original implementation instructions remain historical context.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

## Goal

Add standalone Zephyr calibration image that runs same checked-in 128-frame
LC3 corpus and same integer comparator on production-class ARM Cortex-M
liblc3. Produce bounded machine-readable metrics over UART. Do not change gate
acceptance or choose thresholds.

## Scope

Add only:

- `tests/calibration/lc3_pcm_oracle/CMakeLists.txt`
- `tests/calibration/lc3_pcm_oracle/prj.conf`
- `tests/calibration/lc3_pcm_oracle/src/main.c`
- optional small private headers under same calibration app
- documentation for build/run and output format in
  `tests/fixtures/lc3/README.md`
- focused host parser tests only if a parser is added
- PB-031 Implementation Notes after evidence exists

The app may compile existing `tests/support/pcm_oracle.c` and embed existing
corpus files. Do not modify corpus bytes or manifest.

## Non-scope

- No changes to production receiver, BSim, scenario parser, workflow, liblc3,
  compiler flags, product behavior, or old hash oracle.
- No new canonical unit-suite discovery. Keep app under `tests/calibration/`.
- No thresholds and no broad warning suppressions.
- No flash in implementation session. Orchestrator performs identity check and
  target action after code/build review.
- No commit, push, or PR.

## Target and build

Target exact installed NCS v3.3.0 board:

```text
xiao_nrf54l15/nrf54l15/cpuapp
```

Installed board DTS selects UART20 as console. Kconfig must enable FPU and
`CONFIG_LIBLC3=y`; installed `zephyr/modules/liblc3/Kconfig` proves LIBLC3
depends on FPU and selects full libc. Keep logging minimal and synchronous so
metric records do not interleave. Treat every build warning as failure.

Build command:

```bash
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
```

## Embedded corpus

Use `generate_inc_file_for_target()` for all eight files listed by
`portable-oracle-manifest.json`: four `.lc3` streams and four mono
little-endian `.pcm` references. Embed as `static const uint8_t` data in flash.
Compile `tests/support/pcm_oracle.c` unchanged.

At compile time assert each array size against geometry:

- 10 ms LC3: `128 * 120`; PCM: `128 * 480 * 2`
- 7.5 ms LC3: `128 * 90`; PCM: `128 * 360 * 2`

Runtime validates every geometry before decode. App uses one frame-sized
`int16_t[480]` work buffer and one decoder state at a time. Do not retain full
decoded corpus in RAM.

## Metric engine

Run these records in fixed order, matching host calibrator names and fields:

1. `valid` for 10 ms left, 10 ms right, 7.5 ms left, 7.5 ms right.
2. `channel-swap` for 10 ms left against right reference, then 7.5 ms left
   against right reference.
3. For each stream in manifest order: `prior-frame-shift`,
   `next-frame-shift`, `dead-channel`, `low-correlation-synthetic`.

Total: 22 metric records.

Each decode comparison starts with fresh `lc3_setup_decoder()` state and then
decodes sequentially. Valid processes frames 0 through 127. Prior shift decodes
frames 0 through 127, skips accumulation for actual frame 0, then compares
actual frame 1..127 to reference frame 0..126. Next shift decodes actual frames
0..126 and compares to reference frames 1..127. Channel swap decodes entire
left stream and compares to matching-sequence right PCM. Dead and synthetic
records need no LC3 decode but use same comparator and reference corpus.

Use existing synthetic waveform rule from host calibrator: alternating
`INT16_MIN`, `INT16_MAX` by corpus-global sample index.

Do not duplicate comparator arithmetic. All metrics come from
`pcm_oracle_accumulate()` and `pcm_oracle_finalize()`.

## UART protocol

Emit ASCII only with exact boundaries:

```text
PB031_ARM_BEGIN schema=1 manifest_sha256=<64 lowercase hex> ncs=v3.3.0 liblc3=48bbd3eacd36e99a57317a0a4867002e0b09e183
PB031_METRIC {single-line JSON object matching host calibrator metric fields}
... exactly 22 metric lines ...
PB031_ARM_PASS metrics=22
```

On first error emit one `PB031_ARM_FAIL stage=<token> stream=<stem-or-none>
code=<integer>` line and return without PASS. Tokens remain alphanumeric plus
hyphen/underscore. Never print raw PCM.

Manifest SHA-256 is
`portable-oracle-manifest.json` file hash at implementation time, passed by
CMake as generated config/header or a compile definition. Do not hand-copy an
unverified value. Build configuration must also include source SHA-256 values
for calibration `main.c`, `pcm_oracle.c`, and `pcm_oracle.h`; emit them in one
additional bounded `PB031_ARM_SOURCE` line before metrics. CMake computes these
at configure time.

Use `printk()` only from main thread. Require each formatted metric line to fit
a fixed bounded buffer; fail on truncation. Numeric fields match host records:
`record`, `comparison`, `stem`, `reference_stem`, `squared_error`,
`actual_energy_scaled`, `reference_energy_scaled`, `dot_product_scaled`,
`samples`, `frames`, `max_abs_error`, `rms_error`, `correlation_q15`.

## Tests and checks

No hardware run in Executor session. Verify:

1. Manifest and all corpus hashes with existing calibration CLI loader or
   default generator verification.
2. Pristine ARM build command above.
3. Inspect resolved `.config`: `CONFIG_LIBLC3=y`, `CONFIG_FPU=y`, no unexpected
   Bluetooth or application subsystem dependency.
4. Inspect `zephyr.elf` or map sizes: image fits code partition and static RAM
   fits target SRAM with explicit headroom. Report FLASH and RAM usage.
5. Existing comparator native suite remains 11/11.
6. Calibration Python tests remain green.
7. `git diff --check` and `backlog doctor`.

## Hardware execution after review

Orchestrator will:

1. Retain raw read-only identity evidence: expected DPIDR `0x6ba02477`, AP IDR
   map, FICR PART at `0x00FFC31C` equal `0x00054b15`, and VARIANT at
   `0x00FFC320`.
2. Start UART capture before flash/reset.
3. Flash only reviewed calibration `zephyr.hex` through stock Xiao OpenOCD
   `nrf54l-load` and verify image.
4. Capture BEGIN, SOURCE, exactly 22 METRIC records, and PASS to immutable
   `/tmp/opencode/pb031-calibration/` evidence.
5. Restore production firmware afterward with project `fw-flash-54l15` only
   after fresh target identity evidence.

## Escalation

Stop without commit if corpus does not fit, ARM liblc3 cannot compile with
project warning policy, metric arithmetic differs from host comparator, UART
records truncate/interleave, or implementation requires production changes.

Return exact files, build command/output, resolved Kconfig, FLASH/RAM usage,
tests, warnings, deviations, and git status.

## Reviewed orchestrator result

This appendix supersedes pending ARM-rerun language in the historical handoff.
Fresh repaired-flash identity evidence is
`/tmp/opencode/pb031-calibration/arm-identity-stack-fix.log`: CMSIS-DAP serial
`8EE9B3FF`, DPIDR `0x6ba02477`, AP IDRs `0x84770001`, `0x84770001`,
`0x32880000`, `0x00000000`, PART `0x00054b15`, and VARIANT `0x41414330`
(`AAC0`). Repaired flash and verify evidence is
`/tmp/opencode/pb031-calibration/arm-stack-fix-flash.log`, recording 592680
bytes downloaded and verified.

Run 1 UART evidence is `/tmp/opencode/pb031-calibration/arm-stack-fix-console.log`,
SHA-256 `4d71bbfa7e10ebfad62edfa6cee862c4b10789a1af61717cd29ef1df5711bdb8`.
It has exact BEGIN and SOURCE records, 22 metrics, PASS, no fault, FAIL, or
error, and main-stack usage `2816 / 8192` with `5376` bytes unused (`34%`).
Repeat identity and reset evidence is
`/tmp/opencode/pb031-calibration/arm-identity-repeat.log` and
`/tmp/opencode/pb031-calibration/arm-repeat-reset.log`. Run 2 UART evidence is
`/tmp/opencode/pb031-calibration/arm-repeat-console.log`, SHA-256
`fe6f2d7ff4889a929773b8e2a3e58784877bb913ac1a01b9603a72df67cde34c`.
It has the same 22 records, PASS, and main-stack usage. Machine comparison:
`run1_metrics=22 run2_metrics=22 equal=True`. CPU-cycle telemetry differs and
is not a calibration metric.

Valid records have `max_abs_error=1`, `rms_error=1`, and
`correlation_q15=32767`. Diagnostic classes strongly separate but do not set
thresholds: channel swap max `65535`, RMS at least `21686`, correlation `-202`
or `108`; prior and next shifts max `65535`, RMS at least `21604`, correlation
`79..300`; dead channel max `32768`, RMS at least `15283`, correlation `0`;
low-correlation synthetic max `65535`, RMS at least `36158`, correlation
`-9..-2`.

Production cpuapp and FLPR were rebuilt and restored. Evidence:
`/tmp/opencode/pb031-calibration/production-restore-identity.log`,
`/tmp/opencode/pb031-calibration/production-restore-build.log`,
`/tmp/opencode/pb031-calibration/production-restore-flash.log`, and
`/tmp/opencode/pb031-calibration/production-restore-console.log`, SHA-256
`e0e59ade36e08cfe24d38c4db050d5a7928a69d0274200f14c4a31609f750bf3`.
Boot reached BLE ready, `settings_load() OK`, audio timing and I2S ready, FLPR
READY with rings and runtime ready, then advertising. Restore-build output has
only repository-known documented diagnostics: dirty-worktree notice, nRF54L15
watchdog no-sources CMake diagnostic, and Zephyr `__ASSERT()` globally enabled
CMake diagnostic. Do not call it warning-free.

The original 1024-byte stack fault remains superseded diagnostic evidence.
Thresholds remain unset. P0 remains blocked only on identified Intel x86_64
calibration and later full mandatory threshold-bound controls. P0a diagnostic
classes do not complete parent-plan adversarial coverage.
