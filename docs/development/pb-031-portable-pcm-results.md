# PB-031 portable LC3/PCM oracle consolidated results

Consolidation of the completed PB-031 execution record: the parent plan
(`a94f010:docs/development/portable-lc3-pcm-oracle-plan.md`, P0 through P4)
and the
sixteen dated phase handoffs
(`a94f010:docs/development/portable-lc3-pcm-oracle-*handoff.md`). Those
retired
records were preserved into this document and then removed from the tree;
their content is citable only at the exact Git revision
`a94f010de00e25d4a2433f7b4c56b31a5377446e` (cited below as
`a94f010:<path>`); the source SHA-256 table below identifies each retired
record. This document
grouped the completed plan's necessary historical proof in one place so the
results survive later cleanup of session scaffolding. It is evidence
consolidation, not a new acceptance, not a regeneration, and not a gate run.
Current maintained contract: `docs/testing/portable-lc3-pcm-oracle.md`;
current authority: the completed PB-031 task
(`docs/product/backlog/completed/pb-031 - Make-LC3-PCM-test-oracle-platform-independent.md`).

Sourcing rule used below: every value is a factual, dated citation from a
specific handoff and Git revision, or from current checked-in files verified
by this session. Nothing was re-executed. Raw `/tmp/opencode/pb031-*` roots
were verified absent on 2026-10-08 (93 referenced literal paths plus 12
unresolved `$`-template references); the quoted values below are the
retained evidence, and no fresh rerun or availability is promised.

All section citations below use the form
`source at Git rev a94f010:<path>` because the content quoted was verified
against the repository at commit `a94f010de00e25d4a2433f7b4c56b31a5377446e`
(the last revision containing the plan and all sixteen handoffs). Retired
record SHA-256 values from
that snapshot, for identity of these sources:

| Source | SHA-256 at a94f010 |
| --- | --- |
| `portable-lc3-pcm-oracle-plan.md` | `392934f88f1d42748b50bbf1f144301f1732458f27d22bd03fff0afc5b11f1f4` |
| `portable-lc3-pcm-oracle-p0-handoff.md` | `71db546908edcc8df9a697c7f776eef89d1439e51da7e8fe3bb1ba826ec56f98` |
| `portable-lc3-pcm-oracle-p0-arm-handoff.md` | `45a4106806c0c1d9a88f4579db4e6102dc77acf107c0beafb02960b26d1315b7` |
| `portable-lc3-pcm-oracle-p0-intel-handoff.md` | `ef9084b749383b79317958627561e713226a6b55e109f4b3f5398d187f10c36e` |
| `portable-lc3-pcm-oracle-p0b-threshold-handoff.md` | `1bf7cbe825f053eea51a2de4fcccbb990b033155668f9d63edf1821ff1db546d` |
| `portable-lc3-pcm-oracle-p0c-handoff.md` | `67e221418ab9f31ef425301f4d65a8a77c5c7cc9886a5beff0dea61dc1bc22a0` |
| `portable-lc3-pcm-oracle-p0d-handoff.md` | `ac58499a94e795d4a64ad5e4f9fa160e86b38856c137f5acb6b9845d68ec6903` |
| `portable-lc3-pcm-oracle-p0e-handoff.md` | `e6480aa7ff059c9c615186852ded60b600a156478252e0816a1739724de8cebd` |
| `portable-lc3-pcm-oracle-p1-handoff.md` | `ecc12a15b4ef2e8b05096fc585c81038775dce7f153bb9e97593a137a9b8be3a` |
| `portable-lc3-pcm-oracle-p1-loss-comparison-handoff.md` | `d464f2fec17f8c54525a67f6253a7747d6fe4c5a596ff498d4297f93ffabee25` |
| `portable-lc3-pcm-oracle-p1-loss-diagnostic-handoff.md` | `009f534f79102d4b1a2cf3c562cb9359dfaff00ad36d54fe78131d846c33bbd9` |
| `portable-lc3-pcm-oracle-p1-loss-placement-repair-handoff.md` | `bacb9cc0cc67ce487442f9997fc8ded085688bb9c6ac9531000457971739b989` |
| `portable-lc3-pcm-oracle-p1-loss-repair-handoff.md` | `43f5199f7ab602eb3a06386bb248e33aba9bd98e26bfb2a2e292321babf62e66` |
| `portable-lc3-pcm-oracle-p1-send-cap-wait-review-fix-handoff.md` | `278da591bca6aa6632b26c01235c80d9cb81a462a12ad651a931675a1b56ff8` |
| `portable-lc3-pcm-oracle-p2-handoff.md` | `8ac024587637fbb30cc23a6b9c8e2b5544acf72e13c2a31af07a36f12c30b28b` |
| `portable-lc3-pcm-oracle-p3-handoff.md` | `2fa5e55f1d8e885139c55d718ac7a97704f543ba27d5e2ba333748089f5fe20d` |
| `portable-lc3-pcm-oracle-p4-handoff.md` | `82736718e9977b26ef6c24d56389e898f822d1c17ccc3ffa2709160b0577f003` |

## Timeline and commits (measured, all Git objects verified present)

| Phase/step | Commit | Message | Date |
| --- | --- | --- | --- |
| P0a scaffold | `15282df443e7e582f3c38667df96baded0c08c76` | `test: checkpoint portable LC3 oracle work` | 2026-09-15 |
| P1 loss candidate (semaphore, superseded) | `63e818194f3cccb0d6467b8cb54155ebde99c135` | `test: stabilize one-CIS loss placement` | 2026-09-15 |
| P1 send-cap wait lifecycle (condvar) | `d4c321c390a915e89b4b28711dc8e535367f9c13` | `test: harden send-cap wait lifecycle` | 2026-09-16 |
| P0b limit freeze | `94684b6f3d55b79d1cc85f8aaab3cd2879cf581a` | `test: freeze portable PCM oracle limits` | 2026-09-16 |
| P0b review implementation | `c7f64aa76b0525e81ddf31fbdccf238e5d978a25` (review) | `test: fix P0b review findings` | 2026-09-16 |
| P0c acceptance docs | `88a073afa5d9ccbd56d28500830f3bc79572293c` | `docs: record P0c cross-platform evidence` | 2026-09-17 |
| P0c implementation | `262805eb51731ff7b2511e7e522762e4061bb270` | `test: add PLC-aware PCM calibration traces` | 2026-09-17 |
| P0d implementation | `4fbe9bc135d9077aff90a56f0f6f70fe92637ebb` | `test: correct Mode A 7.5 ms oracle recipe` | 2026-09-17 |
| P0d acceptance docs | `d136aaeec91719be41ed47263c4258a59a980132` | `docs: record P0d oracle acceptance` | 2026-09-17 |
| P0d chronology clarification | `3f6dee06fe357fb39b3b58d37ebd5539c2ebec0b` | `docs: clarify P0d acceptance chronology` | 2026-09-17 |
| P0e implementation | `b38cfecebe6842172f2885e9439799a538946b8d` | `test: add reconnect PCM oracle recipe` | 2026-09-18 |
| P0e acceptance docs | `558fbe07b5d7a48c73d8b38cb4d03766cb7769f3` | `docs: record P0e oracle acceptance` | 2026-09-18 |
| P2 implementation | `2c0c3ba4209d1966388bc532d4eb9a58b416d351` | `test: use payload-aware portable PCM oracle` | 2026-09-18 |
| P3 implementation | `4957a1f37e306c44ff777b6801c73154981b4d69` | `test: migrate decoder fixtures to portable PCM oracle` | 2026-09-18 |
| P3 doc repair | `d14a1b9a7430918d37891bc973ff93d8f3306aeb` | `docs: fix portable decoder contract ID` | 2026-09-18 |
| P4 preparation checkpoint | `56472e2ca5a0d0434ee031bc437ee7bdd482407e` | `docs: prepare PB-031 full acceptance` | 2026-09-18 |
| P4 tested commit | `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22` | `test: repair decoder matrix witness` | 2026-09-18 |

Post-PB-031 continuity (not part of PB-031 but it changed the recipe table and
scenario mapping, which later docs bind): PB-034 at
`21ff2f0fd1dc4e16071dfbc0cad9359d33885018` (PB-034: checkpoint nRF54L15
migration and qualification evidence, 2026-09-24) and PB-040 at
`daf7cd9404e32bacbff4b6431dafccbd28e4a8eb` (PB-040: upgrade to NCS v3.4.1 and
pin toolchain, 2026-09-25).

## P0 phase results

### P0a scaffold (commit 15282df, handoff p0-handoff.md)

Delivered: checked-in 128-frame portable corpus (four streams), shared
integer comparator `tests/support/pcm_oracle.h/.c`, focused comparator suite
`tests/unit/pcm_oracle` (11 tests, later 12), fail-closed portable generator
(`generate.sh`), schema-1 manifest without tolerance values, and the
diagnostic calibration CLI (`tests/fixtures/lc3/calibrate.c` plus
`scripts/lc3_pcm_calibrate.py` with atomic no-output-failure semantics:
rejects relative/existing/repository-contained output, verifies every
manifest size and SHA-256 before compiling, uses temporary build output and
removes it, writes one bounded JSON atomically mode 0644).

First P0a/AMD calibration data (22 diagnostic records, diagnostic only): both
repeat reports at `/tmp/opencode/pb031-calibration/amd-provenance-run-1.json`
and `...-run-2.json` contained 22 equal records, raw identity
`AuthenticAMD` and `AMD Ryzen 9 5950X 16-Core Processor`; AMD-only data
never established thresholds. P0 handoff also corrected the parent plan's
ARM64 wording to an identified ARM environment: production decoder runs on
ARM Cortex-M, and calibration later ran on the attached identified nRF54L15.
ARM-only data and AMD-only data are both insufficient to set thresholds:
this P0 handoff records the reason (cross-environment envelope is the
mandatory basis), which is why P0b froze limits only after Intel and ARM
report pairs existed.

Comparator arithmetic (still current): incremental accumulation with
explicitly bounded accumulators; `squared_error`, scaled (by /256) energies,
signed dot product, running maximum absolute error; unsigned overflow
returns `-EOVERFLOW` without mutating output metrics; integer RMS via
ceiling mean then integer sqrt; signed scale-safe correlation via
`dot / (sqrt(actual_energy) * sqrt(reference_energy))` in Q15 with
`INT16`-clamped bounds. Conservative by design: no floating-point acceptance
math. (The review repair that moved the denominator overflow check into the
transaction is the 2026-09-15 P0a record; the checked current source already
contains that shape.)

P0 focused comparator negative controls (synthetic; comparator-level, not
liblc3-level): channel swap, prior-frame shift, next-frame shift, dead
channel, low-correlation synthetic; all isolated to named reasons. Scope
limitation recorded then: synthetic comparator controls do not prove
decoder behavior; they isolate comparator reason ranking.

### P0 ARM calibration image and stack fault (handoff p0-arm-handoff.md)

Target: exact installed NCS v3.3.0 board
`xiao_nrf54l15/nrf54l15/cpuapp` (historical phase; current build commands use
active v3.4.1). Kconfig required `CONFIG_FPU=y` and `CONFIG_LIBLC3=y`
(installed `zephyr/modules/liblc3/Kconfig` proves LIBLC3 depends on FPU).
Embedded-corpus design: `generate_inc_file_for_target()` for all eight
portable files, compile-time array-size assertions (10 ms LC3 `128 * 120`,
PCM `128 * 480 * 2`; 7.5 ms LC3 `128 * 90`, PCM `128 * 360 * 2`), runtime
geometry validation, one frame-sized `int16_t[480]` work buffer plus one
decoder state, no full RAM corpus, exact 22-record order (four valid, two
channel-swap, then prior/next/dead/synthetic per stream), metric engine via
`pcm_oracle_accumulate()`/`pcm_oracle_finalize()` unchanged.

First ARM execution (original 1024-byte resolved main stack) faulted in
liblc3 SNS before its first metric. Serial output reported:

```text
***** USAGE FAULT *****
Stack overflow (context area not valid)
```

PC `0x00006460` resolved to `spectral_shaping` at
`modules/lib/liblc3/src/sns.c:723`; LR `0x00007209` resolved to
`lc3_sns_synthesize` at line 822. No ARM metric was accepted from that run,
and per PB-031 Implementation Notes lines 88 through 90 no persisted serial
fault-capture file exists: those PC/LR values are quoted serial-capture
observations, not a replayable raw UART artifact. This early ARM 1024 stack
fault is superseded diagnostic evidence retained for stack-repair provenance.

Repair: `CONFIG_MAIN_STACK_SIZE=8192` plus thread analyzer
(`CONFIG_THREAD_ANALYZER=y`, `CONFIG_THREAD_ANALYZER_USE_PRINTK=y`,
`CONFIG_THREAD_NAME=y`, `thread_analyzer_print(0U)` after metrics and
immediately before PASS, `CONFIG_THREAD_ANALYZER_AUTO` unset). Pristine
repair build for `xiao_nrf54l15/nrf54l15/cpuapp` with `CONFIG_FPU=y`,
`CONFIG_LIBLC3=y`, no Bluetooth subsystem, `CONFIG_HW_STACK_PROTECTION=y`,
`CONFIG_ARM_STACK_PROTECTION=y`, `CONFIG_BUILTIN_STACK_GUARD=y`; linker
summary FLASH `592688 B` of `1428 KiB`, RAM `25768 B` of `188 KiB`. The
board slot0 code-partition capacity is `664 KiB` (leaving `87248 B` for that
image; RAM headroom then `166744 B`); `CONFIG_USE_DT_CODE_PARTITION=n` means
the direct calibration build links against full RRAM, so slot0 was a checked
board-capacity budget, not the active linker limit.

Reviewed repaired ARM runs (identity at
`/tmp/opencode/pb031-calibration/arm-identity-stack-fix.log` and repeat/reset
logs; flash log `arm-stack-fix-flash.log`, 592680 bytes downloaded and
verified): run 1 console SHA-256
`4d71bbfa7e10ebfad62edfa6cee862c4b10789a1af61717cd29ef1df5711bdb8`; run 2
console SHA-256
`fe6f2d7ff4889a929773b8e2a3e58784877bb913ac1a01b9603a72df67cde34c`. Both had
exact BEGIN and SOURCE records, 22 metrics, `PB031_ARM_PASS metrics=22`, no
fault/FAIL/error, and main stack `2816 / 8192` (34 percent used). Raw CPU
and board identity: CMSIS-DAP serial `8EE9B3FF`, DPIDR `0x6ba02477`, AP IDRs
`0x84770001`, `0x84770001`, `0x32880000`, `0x00000000`, FICR PART
`0x00054b15`, VARIANT `0x41414330` (`AAC0`). Machine comparison
`run1_metrics=22 run2_metrics=22 equal=True`; CPU-cycle telemetry differs
and is not a metric. ARM valid records: `max_abs_error=1`,
`rms_error=1`, `correlation_q15=32767`. Diagnostics in the 22-record data
were strongly separated (channel swap max 65535, RMS >= 21686, correlation
-202 or 108; prior/next shifts max 65535, RMS >= 21604, correlation
79..300; dead channel max 32768, RMS >= 15283, correlation 0; synthetic max
65535, RMS >= 36158, correlation -9..-2), and remained diagnostic only.

Stack sizing rule learned and retained: 8192 is a conservative repair
configuration, not an accepted stack high-water mark; observed high-water
values vary by phase (2816, 2888, 2920 bytes). The ARM protocol is
bounded and fail-closed: on first error, one
`PB031_ARM_FAIL stage=<token> stream=<stem-or-none> code=<integer>` line and
no PASS line. Byte identity was restored after the calibration image and
restore work by rebuilding and flashing production CPUAPP plus FLPR
(restore evidence
`/tmp/opencode/pb031-calibration/production-restore-*.log`, console SHA-256
`e0e59ade36e08cfe24d38c4db050d5a7928a69d0274200f14c4a31609f750bf3`; boot
reached BLE ready, `settings_load() OK`, audio timing and I2S ready, FLPR
READY with rings and runtime, then advertising; build output had only the
repository-documented dirty-worktree notice, watchdog no-sources CMake
diagnostic, and `__ASSERT()` CMake diagnostic, and is not called
warning-free).

### P0 Intel measurement (handoff p0-intel-handoff.md)

Pinned calibration identity at this phase (schema-1, 22 records): five
calibration-input hashes as quoted in the handoff
(`scripts/lc3_pcm_calibrate.py`
`4c2c3df5fbd879ee526d56ad9a4cc729e144e42b94a1dc03501c97b44255b5ba`;
`tests/fixtures/lc3/calibrate.c`
`76030161fd1ea30c0eb5f82b63010fa51801b75f48d831a7440c316107ac83e1`;
`tests/support/pcm_oracle.c`
`90a79b96d3e793d37f1579d5d0bfa43302bc83bfef48527c58b083396c7112eb`;
`tests/support/pcm_oracle.h`
`49d898f1f93379b697e72c2e7c48cb76d462a25c8cc5eb94ac27af25ea4bb205`;
`tests/fixtures/lc3/portable-oracle-manifest.json`
`81cd9c09f7321eb3a928c763bee467405c1a7c470e22dbd2dcbbd7b371189b44`) and the
exact generator/compiler flags
`-O3 -std=c11 -ffast-math -Wall -Wextra -Wdouble-promotion -Wvla -pedantic
-Werror`. Raw evidence existed on both hosts at
`/tmp/opencode/pb031-calibration-intel-d4c321c-i3-6100u/` with per-file
SHA-256 values `preflight.txt`
`e34de4cc75a0bf979c6803d14f001c5ef8a61e42fa00ae5bc7e3e835d9667d4b`,
`intel-provenance-run-1.json`
`bc967d5bd1d6a5f6a63041ffb1c9d9db202b8d533ed2ed4a7225bf59a2de26d0`,
`intel-provenance-run-2.json`
`434efadc73befbdb33ac989278e8a1b383dfc83bb84d65ac09bd3c330d21366a`,
`validation.txt`
`43123f4c8c1b48049cf0d298ad56a184c7e65026970b4bf0c2f8db956e9de7da`. The
validation passed with exactly 22 equal parsed metric records across both
runs: `PB031_INTEL_VALIDATION_PASS metrics=22 repeat_equal=true` (the
two-run equality validator source is embedded in the Intel handoff, and the
handoff quotes the exact structural assertions run one and run two must
match, including stable `calibration_inputs`, `liblc3`, flags, and metrics
fields; the validator itself was adapted in later phases and was not rerun
in the preservation comparison).

Environment identity: `thomas-nuc`, bare-metal NixOS x86_64, raw `lscpu`
`GenuineIntel`, `Intel(R) Core(TM) i3-6100U CPU @ 2.30GHz`, family 6, model
78, stepping 3, microcode `0xf0`, VT-x capability, no hypervisor vendor,
kernel NixOS Linux 7.0.11. Repository and liblc3 Git bundles were
transferred without installing software; both runs used clean repository
HEAD `d4c321c390a915e89b4b28711dc8e535367f9c13`, exact liblc3 revision
`48bbd3eacd36e99a57317a0a4867002e0b09e183`, Clang 21.1.8 through `cc`, and
both compilations emitted no diagnostic.

Per-stream Intel schema-1 valid values:

| Stream | Max error | RMS | Correlation Q15 |
| --- | ---: | ---: | ---: |
| 10 ms left | 1862 | 389 | 32757 |
| 10 ms right | 1977 | 393 | 32759 |
| 7.5 ms left | 1902 | 426 | 32757 |
| 7.5 ms right | 1883 | 426 | 32757 |

Intel diagnostic separation: channel swap max 65535, RMS 21698 or 23112,
correlation -200 or 105; prior/next shifts max 65535, RMS 21612..23151,
correlation 77..301; dead channel max 32768, RMS 15283..16420, correlation
0; synthetic max 65535, RMS 36158..36656, correlation -9..-2. The CPU
identity cannot be claimed from runner labels (P0 handoff line
"Run local command twice ... Do not infer threshold from AMD-only data";
Intel handoff requires raw `lscpu` identity for exactly this reason), and
22 records were diagnostic, not the full mandatory threshold-bound control
set.

## P0b frozen policy (commit 94684b6f / review c7f64aa, handoff p0b-threshold-handoff.md)

Reviewed stop gate on 2026-09-16. Frozen schema-2 policy:

```text
max_abs_error=2048
max_rms_error=512
min_correlation_q15=32750
```

Headroom derivation (measured envelopes at that phase were AMD
`0/0/32767`, Intel `1977/426/32757`, ARM `1/1/32767`): maximum error 2048 is
the next power-of-two boundary above 1977, 71 samples or 3.59 percent
headroom; RMS 512 is the next power-of-two boundary above 426, 86 samples or
20.19 percent headroom; correlation floor 32750 is seven Q15 counts below
the measured floor 32757. A temporary real-liblc3 probe established the
one-byte mutation shape before freezing: XOR bit 2 (`0x04`) of byte 0 in
frame 0 of `bsim_48k_10ms_120b_l`; all 128 frames still decode. Temporary
corruption-probe metrics (clearly diagnostic and never thresholds): AMD/GCC
maximum 65535, RMS 2339, correlation 32389; Intel/Clang maximum 65535, RMS
2388, correlation 32372; both evaluate `max-error` under the candidate
policy. Closest ordinary mutation then available had maximum 32768 and RMS
15283. These probes exist only in the P0b handoff; they are neither
threshold values nor threshold exceptions.

Per-report evidence preserved:

- AMD run files: `/tmp/opencode/pb031-calibration/amd-p0b-c7f64aa-run-1.json`
  SHA-256 `5a1f2988791ed5662f01e32b2401b38997f54c84fad1175f5341304b2e4cbb0c`,
  run-2 SHA-256
  `ad02292d4e6532184f0f4bd063a4dd24e89e9aaa56a39fcf0922d2b6d347cabc`;
  envelope `0/0/32767`.
- Intel root `/tmp/opencode/pb031-calibration-intel-c7f64aa-i3-6100u/`, run 1
  SHA-256 `744bbcb053b454d4bc403de734b48342c514faf994b34a9a90462ed23008792f`,
  run 2 SHA-256
  `a8a7c66b17176b6c018f2c0f3ecedd21f202e812066e4eeeeab20135b249b15d`,
  `validation.txt` SHA-256
  `583af03d56304476ca5e8e3c40195875bc5d80f8cc34395befe479c4ec3b84e2`;
  envelope `1977/426/32757`.
- ARM build `/tmp/opencode/pb031-p0b-arm-calibration-c7f64aa`: FLASH
  `593904 B/1428 KiB` linked, `593900` bytes flashed and verified; identity
  log
  `/tmp/opencode/pb031-calibration/arm-p0b-c7f64aa-identity-preflash-full.log`
  SHA-256
  `ecb17806b8811a2b13aec31fb843d3b307abfe93bbfd270c3d6d6994e8218bf5`; run
  consoles SHA-256 `69b9bb069a7e733b84293a807071d16669db347cd154a4b637ae55dc510b4486`
  and `20e727914494e46cadfd8f12b8ca3d321f9887d694d0a3598fce55bf32498565`;
  stack `2888/8192` (35 percent, 5304 unused); envelope `1/1/32767`;
  validation `arm-p0b-c7f64aa-validation.txt` SHA-256
  `acf42f5372508df8dc06b9a1b496c9892e40067e693319fc68c81e17a4879102`.
- Cross-platform validation
  `/tmp/opencode/pb031-calibration/p0b-c7f64aa-cross-platform-validation.txt`
  SHA-256
  `fb399a964d45ebeca7fbd6441806c7b2fa82052b8f7f8b54cbe9a5665cf36ade`, exact
  final marker
  `PB031_P0B_CROSS_PLATFORM_PASS environments=3 runs=6 records_per_run=26 identity_order_equal=true evaluations_expected=true`.

P0b protocol change kept for provenance: host and ARM calibration extended
from 22 to 26 ordered records (adds LC3 byte-0 XOR `0x04` corruption over
all 128 frames of the 10 ms left stream with every `lc3_decode()` call
required to return zero, plus maximum-error, RMS-error, and correlation
threshold-boundary controls evaluating exact distinct results `max-error`,
`rms-error`, and `correlation` with recomputed values 2049 single-sample,
513 distributed, and a negated +-256 frame with correlation `INT16_MIN`).
Policy passes as validated argv to the host binary and as CMake-derived
generated build info to ARM: no compile-time copied constants.
Restoration evidence per the P0b handoff: production console
`/tmp/opencode/pb031-calibration/production-p0b-c7f64aa-restore-console.log`
SHA-256
`749e221c26b00ab0fac2a0c6f45d26b43ec8a35faf67b4076e251071712b5738` (cpuapp
536948 bytes, FLPR 32604 bytes flashed and verified, boot markers present,
only documented watchdog and `__ASSERT()` diagnostics), and repair-review
results raw suite 54 tests, PCM oracle 12 PASS, calibration Python 18 PASS,
BSim parser 94 PASS, unit gate `71 PASS / 0 FAIL / 71 TOTAL`, plus
`backlog doctor` and `git diff --check`.

Source-compare limit and explicit headroom record: 3.59 percent (maximum)
and 20.19 percent (RMS) with 7 Q15 counts of correlation headroom is the
explicitly bounded headroom from measured Intel-vs-frozen policy deltas.
Never widened since freezing; no per-CPU exception or per-run exception
exists anywhere in this family.

## P0c payload identity and stateful calibration (commit 262805e, handoff p0c-handoff.md)

New grounding facts that changed the design of P2:

- `bt_bap_stream_send()` forwards the host PSN, but the receiver's
  `bt_iso_recv_info.seq_num` is populated from the receiver controller
  ISOAL/HCI output. PSN is not sender fixture identity. A wrong fixture
  sequence would silently alias.
- Every stream has 128 distinct LC3 byte strings; same-duration left/right
  streams have zero byte-identical overlap (unique, disjoint payload
  identity).
- Local liblc3 probes (wrong-reference controls) measured maximum errors
  around 25k (wrong lossless indexing after startup PLC), 38k (malformed
  history), and 26k (one-CIS-loss wrong recipes); correct startup PLC
  followed by fixture sequence zero was exact on local AMD.
- Local probes falsified one P0c premise: symmetric Mode A 7.5 ms startup
  (PLC 13 on both channels followed by corpus frames 0 through 99).
  The measured truth (confirmed later by P0d diagnostics) is an asymmetric
  113-action history; P0d corrected recipes five and six. The P0c handoff's
  symmetric recipe rows are retained as the dated falsified premise, not
  current recipe truth.

First valid frame calls `lc3_plc_suspend()`; fresh-decoder startup PLC
produces silence, so startup PLC count is exact lifecycle contract but not a
numerical control by itself. Recipe table created in
`tests/support/lc3_stateful_recipes.h/.c` with the pure validator
(`lc3_stateful_recipes_validate()`), stateful manifest schema 1 binding
portable manifest schema 2 (`source_portable_manifest`), and the strict
regeneration script. Generated references store only source-valid outputs;
PLC advances state but is never stored or compared.

Accepted cross-platform evidence at `262805e` (review clean tree
`/tmp/opencode/pb031-p0c-review-262805e`): six schema-3 reports (two each
AMD GCC 14.3.0, Intel Clang 21.1.8, ARM), 38 records each, identity/order
equal across environments, repeats equal per environment. Report hashes:
AMD `989da5425a69dc7a541a9bc3630d890ff0168be2e5bd8805118f03fff8d58d0e` and
`1db0e14626ab4fc2ea4deab2333d327a6df8b883700342a51883c7b1f51bb124`; Intel
`630c49794f64adfde3963cf2b0d0890bb37933ac8e7a68aa1e87cdeaa408b53b` and
`68b0cf8fe82d84616754fb74d37201137c1e7e874a5d9a0c2e453f8180042b2c`; ARM
`c0ffc50baf7084f70c6c7b93f21d0e9aa3217595ffefad0fa54b19d8e4b1c8d2` and
`f9915a5b5c67d1a8a9cc1cfaeab2a7c83b11cfeb0fc25b011a47d3794b208457`, both
ARM reports `PB031_ARM_PASS metrics=38`. Stateful-valid envelope AMD
`0/0/32767`, Intel `1977/425/32756`, ARM `1/1/32767`; frozen limits
unchanged. Focused checks at review: strict stateful generator passed with
no original-corpus diff; calibration Python 32/32; native `pcm_oracle`
12/12; ARM build passed; `backlog doctor` and `git diff --check` passed;
compile-command links restored; detached status clean.

ARM resources at P0c: identity logs (three files, each SHA-256
`8cb5e27fffc8fb0f985ea0fc076582d7555c1b2dd85ee30c1bee4e4c8dee1334`);
flash log SHA-256
`a643d1c836d1f281ece9659016f43b709bba69a2be06aade706049f2fffab1c8` with
772472 bytes downloaded/verified; exact-ELF size text 771132, data 1340,
bss 26476; static RAM 27816; full RRAM headroom 689800; main stack
2920/8192 (35 percent, unused 5272). The calibration image exceeds the
production 664 KiB slot0 by 92536 bytes exactly because
`CONFIG_USE_DT_CODE_PARTITION=n`; production partitions unchanged.
Production restoration: CPUAPP SHA-256
`33738ab087f87d42849d604e7d50032b52d4dc8ee691f060d898521cf4a44da7`, FLPR
SHA-256 `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`,
536948 + 32604 bytes verified; boot log SHA-256
`b6cd19ecd8e5af374c1b5e925cfccef9059c071c7c36c56b7a3e543562dfacff` reached
banner `262805eb5173`, BLE ready, `settings_load() OK`, timing/I2S, FLPR
READY/rings/runtime, advertising, no boot warning or error. The Nix
eval-cache SQLite busy line `error (ignored)` occurred once because reader
and flash entered Nix concurrently (flash verification and boot succeeded),
and the dirty-tree warning came only from build-generated tracked
compile-command symlink targets in the detached worktree, restored
afterward. Both are recorded as dated one-off qualifications, not current
warning waivers.

## P0d Mode A 7.5 ms correction (commit 4fbe9bc, handoff p0d-handoff.md)

Measured at `/tmp/opencode/pb031-p2-modea7p5-diag-t5H2hQ`: normalized trace
SHA-256 `eb0cf50a49d11bd694ffd5c860c1e8b227cb48dc16083d427b8ae5119b506f6f`,
with `run1/p2diag.trace` and `run2/p2diag.trace` byte-identical, plus
retained `trace-analysis.txt`, `trace-compare.txt`, and
`restoration-post-rebuild-checks.txt`. Left channel: PLC actions 0 through
11, corpus frame 0 at action 12, corpus 1 through 100. Right: PLC 0
through 9, corpus frame 0 at action 10, PLC actions 11 and 12, corpus 1
through 100. Both channels: 113 actions, 101 source-valid outputs, 12 PLC
actions; asymmetric. The Mode-A history asymmetry is the recorded P0d
correction of the falsified symmetric premise.

Replaced `start13_7p5ms_l`/`start13_7p5ms_r` with `modea_start_7p5ms_l`
(portable reference) and `modea_start_7p5ms_r` with the 72720-byte
generated trace `stateful_48k_7p5ms_modea_start_r.pcm`
(`101 * 360 * 2`); `--rebase-stateful` explicitly permits exactly this one
missing manifest-declared generated file and stages a rollback-protected
replacement transaction (normal non-executable permissions, new-file
rollback on later failure, no partial old/new set, reminder to update README
and stateful manifest). Temporary placeholder-hash bootstrap of a new trace
through explicit rebase was an allowed historical execution detail; no
placeholder remained in the committed diff.

Accepted 2026-09-17 with two schema-3 reports per environment (38 records;
identity/order equal; repeats equal per environment); Mode A 7.5 ms values:
AMD `0/0/32767` and `0/0/32767`, Intel `1902/424/32761` and
`1883/426/32760`, ARM `1/1/32767` and `1/1/32767`; eight-stateful-valid
envelope `1977/426/32756`. ARM calibration image verification covered 845316
bytes; full RRAM use 845316/1462272 (headroom 616956); RAM 27808/192512
(headroom 164704); stack 2920/8192 both runs. Production restore: CPUAPP
SHA-256 `a3700e9314814e60b48ecf539d28c31e635d61f9bb29bbe43410506c8fba0c8e`,
FLPR `45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`,
536948 + 32604 bytes, boot banner `4fbe9bc135d9`, required markers, no UART
warning or error; only documented watchdog and `__ASSERT()` diagnostics.
Clean detached-tree checks: strict generation, original corpus no-diff,
37 Python tests, native `pcm_oracle` 12/12, pristine calibration build,
`backlog doctor`, `git diff --check` all passed. Evidence root
`/tmp/opencode/pb031-p0d-acceptance-4fbe9bc` with
`acceptance-report.md`.

## P0e reconnect correction (commit b38cfec, handoff p0e-handoff.md)

Measured at P2 (25 of 26 runs, root `/tmp/opencode/pb031-p2-bsim-stage1.PKts1b`):
reconnect segment 2 reported `trans2=7`, `pushes2=100`, `total2=107`, then
failed because the configured recipe required PLC metadata at action 7 (the
accepted `start8_10ms_l` mapping was the wrong count, measured value is
seven). Appended `start7_10ms_l` as the then-ninth recipe (one-based position
nine, now zero-based index 8) without changing any other recipe, reference,
corpus,
mutation record, schema, or frozen policy. Schema-3 record 34: 107 actions,
100 valid frames, 48000 samples; values AMD `0/0/32767`, Intel
`1862/389/32756`, ARM `1/1/32767`. Mutation reference indices remain
`{0U, 6U, 7U, 0U}`.

Accepted 2026-09-18 with two 39-record reports per environment; all nine
stateful-valid records passed, all four mutations returned `max-error`; the
nine-recipe envelope stayed `1977/426/32756`. ARM: verification 845428
bytes, RRAM 845428/1462272 (headroom 616844), RAM 27808/192512 (164704),
stack 2920/8192 both runs; identity `8EE9B3FF`/`0x6ba02477`/PART
`0x00054b15`/VARIANT `0x41414330`. Production restore: CPUAPP SHA-256
`111f40757ea986b03bd405aacf23e6f9f0acd0a91099bb1d03008895f2e2f323`, FLPR
`45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2`,
536908 + 32604 bytes, boot banner `b38cfecebe68`, markers present, no UART
warning or error, only documented watchdog and `__ASSERT()` diagnostics.
Evidence root `/tmp/opencode/pb031-p0e-acceptance-b38cfec` with
`acceptance-report.md`. Exact old-SDK pins used then: nrf
`ba167d9f3db4abbdc9b67887ca3ea66c64f2d956`, Zephyr
`fd9204a02d52630660ce8d729945a4dd743feabf`, liblc3
`48bbd3eacd36e99a57317a0a4867002e0b09e183`.

## P1 transport, loss placement, and TX synchronization (commits 63e8181, d4c321c; handoffs p1, p1-loss-*, p1-send-cap-*)

Key measured/negative results preserved (P4-era final matrix facts first
then the loss chain):

- Exact TX hash contract: unsigned FNV-1a (offset `0x811C9DC5`, prime
  `0x01000193`) over four little-endian logical-sequence bytes plus exact
  final SDU bytes per successful `bt_bap_stream_send()`; candidate hash
  computed before send while caller owns the buffer, published only inside
  the same successful generation/stream commit as count and sequence; failed
  or stale sends never affect audit state; audit retained keyed by logical
  stream pointer so reconnect preserves stream 0 evidence. `txcN` must equal
  `sendsN`. Ownership commit point: send count and 16-bit sequence commit
  under `tx_lock`; `tx_lock` is never held across allocation or send.
- Transport mutation rejection owned by the parser (strict manifest-derived
  hashes; reject duplication, omission, reorder, wrong channel, corruption,
  count drift, exhaustion). Malformed scenario derived from the corpus:
  sequence 20 valid left frame with its final byte removed (119 bytes), no
  synthetic bytes.
- Historical temporary malformed gap: old runtime TX skipped encoder history
  for logical frame 20; corpus TX encodes frame 20 before truncating that
  SDU. First fixture run kept all counts (`pushes1=100`, `total1=108`,
  `derr1=1`, `txc0=101`, `txh0=0xDD9C9459`) while the receiver diagnostic
  changed from `0x0C61918D` to `0x09513FCD`. Repinning either CPU-specific
  PCM value was forbidden; `known.full` was removed for this one bounded
  gap and P2 restored portable numerical PCM acceptance for the path. The
  gap was a historical P1 temporary state; current source has no such
  exclusion.
- Warning-class repairs preserved: six warning repairs landed during P1
  (buffer-count alignment to controller-reported 3, deterministic
  random-static identity pre-`bt_enable()`, test-local volatile settings
  store with no cross-process persistence, no-op `stopped`/`released`
  client callbacks, endpoint-state observation before Start, and
  lifecycle-closure error handling that treats post-teardown send errors as
  expected closure without logging or advancing state). Sink Start
  synchronization correction: `bt_bap_stream_start()` is wrong for SINK ASEs
  (installed contract applies Start Ready to SOURCE ASEs; `-EINVAL` while
  ENABLING is not readiness); replaced with bounded endpoint-state
  synchronization first, then final shape with no timing coupling
  (`start_streams()` never calls Start, returns `-EBADMSG` for unexpected
  states, preserves the established start-relative window origin via TX's
  public-state gate plus scenario `wait_for_sends()` readiness).
- One-CIS loss chain (recorded in the three loss handoffs): warning-clean
  17-completion candidate produced exactly 18 concealments but placed the
  gap after 51 valid right frames (candidate hashes `h1=0x134C23B0`,
  `rh1=0xAE966FB0`); old fixed-200 ms wall-clock reference and one 190 ms
  probe both failed at the 19th peer concealment
  (`concealed pushes 19 > 18 — unexpected losses`) and were rejected as
  phase-sensitive, never retuned, never used as repair. Final placement
  proved by a read-only i386 host model at `/tmp/opencode/pb031-hash-model/`
  (same installed liblc3, `-m32 -O3 -ffast-math -fshort-enums -DLC3_PLUS=0
  -DLC3_PLUS_HR=0`, eight startup PLC, volume 195 scaling, exact four-byte
  little-endian push-index + sample FNV framing) reproducing both hash
  pairs: 48 valid right pushes before gap gives `0x30D6BAF0`/`0x32777D65/`
  `0x9859F1D8`, 51 gives `0x134C23B0`/`0xAE966FB0`. Accepted placement is
  exactly 48 valid right pushes before an 18-event gap, resuming at fixture
  frame 48. The model is evidence of exact placement, not air proof and not
  an independently implemented codec.
- One-completion refill lead measured, not assumed: after resume, no right
  submission happened until the next free completion; fixed slot traversal
  (slot 0 before slot 1) with both streams sharing the three-buffer
  `tx_pool` produces one absent-right event during sender refill. Peer-loss
  target stays 18 with a 17-completion pause window plus the measured
  refill lead of one. The send-cap design (`MODEA_ONE_CIS_PRE_GAP_SENDS 48`)
  removed direct polling; completion drain plus 17-completion window plus
  refill lead is event-count accounting from captured controller/TX order,
  not wall-clock tuning (refill-lead measurement trace preserved in the
  loss-repair handoff).
- Exact accepted loss-run pins (both strict runs byte-identical):
  `pushes1=100 trans1=8 szero1=8 splc1=16 plc1=34 total1=216 derr1=0
  mal1=0`, receiver hashes `0x30D6BAF0`/`0x32777D65`/`0x9859F1D8`, TX
  hashes `0x8980C79D`/`0xDD25CC21`. No `LOSS_*` output. Scenario 17
  allowlisted warning `Invalid operation in state: releasing` remained the
  sole runtime warning at P1 time; the current native target adds the
  exactly-one `Unexpected seq_num diff between 47 and 66 for <stream
  pointer>` one-CIS-loss client warning as the second exact allowlist entry
  (PB-034 record, target-native negative-path observation, not a blanket
  exception).
- Semaphore defect and condvar replacement: slot-embedded reusable binary
  semaphore rejected because `k_sem_reset()` aborts a pending
  `k_sem_take()` with `-EAGAIN` which was mapped to `-ETIMEDOUT`, and
  re-registration could clear/reinitialize while a waiter was pending,
  violating documented `-ESTALE` semantics. Replacement: process-lifetime
  per-slot `k_condvar` array, broadcast on cap-reach, limit change,
  unregister, and register while holding `tx_lock`; waiters revalidate
  association, generation, limit, count, and pause state after wake.
  Review-fix evidence root
  `/tmp/opencode/pb031-p1-send-cap-wait-fix-20260916`; parser `94 PASS /
  0 FAIL`; strict matrix 17 scenarios/26 runs; unit `71/0/71`; source
  review found no remaining send-limit `k_sem_*` operations.
- Race-coverage boundary: no standalone `bsim_tx` unit race harness exists;
  the current matrix passing does not by itself exercise a concurrent
  pending-waiter race. Source-based review plus strict matrix is the
  declared proof for the condition-variable lifetime contract; the missing
  standalone race harness is a declared source-review plus matrix proof
  boundary, not a claim of dynamic race coverage.
- Diagnostic boundary evidence (temporary, removed after resolution; only
  the quoted rows remain): loss-diagnostics captured
  `LOSS_BOUNDARY pause right_sends=51 right_done=48 left_sends=51
  left_done=48` / `right_drained=51 left_done_start=49` / `resume
  left_done=67 target=67 right_done=51` with ordered TX submission and
  completion rows; receiver `LOSS_RX` flags `0x0c`/`0x09` raw rows, 184 RX
  rows and 75 Mode A emit rows quoted, no malformed records; the 19th
  concealment fail-fast fired before any PASS. All final source removed
  every `LOSS_*` diagnostic; no retained receiver diagnostics.
- P1 unit-phase acceptance at `71 PASS / 0 FAIL / 71 TOTAL`; parser
  `94 PASS / 0 FAIL`; `backlog doctor`; `git diff --check`. P4-era
  totals are the P4 gate `74 PASS / 0 FAIL / 74 TOTAL`.

## P2 payload-aware receiver oracle (commit 2c0c3ba, handoff p2-handoff.md)

Accepted 2026-09-18 at implementation commit
`2c0c3ba4209d1966388bc532d4eb9a58b416d351` (also the clean P3 start base).
Full Stage 1 passed all 17
scenarios and 26 runs; payload and recipe awareness measured maximum error
257, maximum RMS 182, and minimum correlation Q15 32767 (within frozen
`2048/512/32750`). All 52 receiver/client logs inspected at the dated root
`/tmp/opencode/pb031-p2-bsim-stage1.C6FBBY`; only the exact scenario-17
receiver warning appeared. Focused results: strict generator hashes
unchanged; calibration tests 38/38; parser `140 PASS / 0 FAIL`; native
`pcm_oracle` 12/12; native `audio_stream_session` 48/48; `backlog doctor`;
`git diff --check`. Reconnect segment 2 at that acceptance bound
`start7_10ms_l` on both channels, 107 actions, 100 valid frames, seven PLC
actions, 48000 compared samples per channel; the current native-target
scenario mapping has since changed the binding to start0-family recipes
with both reconnect segments (PB-034 record; see the maintained contract).

Retained P2 mechanism facts (source truth lives in
`tests/bsim/src/audio_sink_stub.c`, `tests/bsim/src/bsim_observer*`, and
`scripts/bsim_stage1_parse.py`):

- One-shot bounded payload snapshot (valid half: payload length exactly
  1..120 non-null; invalid half: null, zero length), taken in the
  serialized RX call path, no allocation, no caller-pointer retention, no
  lock (single receiver process, one scenario); stale/missing snapshot
  fails closed.
- Per-channel recipe cursors advance for every accepted sink push; PLC
  requires source-invalid metadata and advances total plus PLC count
  without entering the comparator; CORPUS requires source-valid metadata,
  exact payload identification (byte-exact, exactly one match across both
  same-geometry streams), exact source stem and frame index match, then
  volume-195-scaled accumulation; cursors advance only after every check
  and accumulation succeeded.
- The malformed frame creates no recipe action (it is rejected before
  decode, Mode A store, observer pre-push, volume, and sink push); the
  sink retains exact per-half compressed bytes in Mode A event storage
  only at the immediate test-only pre-push call for mono/Mode B; the
  bounded copied snapshot is sufficient and no production state changed.
- `SINK_SEG` human diagnostics use `pcm_oracle_result_name()`; parser
  enforces exact receiver-emitted limits equal to the manifest, recipe
  identity/order/completion, all accounting equations, results, and the
  fixed 17-scenario/26-run matrix; decoded-PCM hash CLI options and
  baseline mode were removed (`BSIM_BASELINE=1` fails before toolchain
  setup), while runner output ownership, locking, and timeout semantics
  were retained per the handoff; no tolerance widening or source-valid
  exclusion is permitted after failures.

Intermediate P2 repair facts preserved as diagnostics (not full
acceptance): the first P2 attempt failed because it used controller
sequence as identity and lossless references for post-PLC histories; the
reconnect repair initially mapped both segments to `start8` recipes and
failed 25 of 26; the asymmetric Mode A 7.5 ms prefix (one source-valid and
12 PLC each) and reconnect binding were corrected in the same phase; the
historical P2 mapping tables (start8/start11/modea/start7) and the
historical recipe action totals (108 loss recipes, 107 reconnect) are
retained as the dated calibration/legacy
mapping, superseded for the current native target by the PB-034 target
native record (which separately measured native prefix totals 68 for
first-stop and 55 for the other partial-lifecycle segments).

## P3 real-decoder migration (commit 4957a1f, doc repair d14a1b9, handoff p3-handoff.md)

Accepted 2026-09-18 on clean base
`2c0c3ba4209d1966388bc532d4eb9a58b416d351`. `tests/unit/decode` moved from
byte/CRC equality to the shared comparator at the real
`audio_decode_sdu()` boundary with immutable manifest limits; keep exact
fixtures/hashes, one frame/exact dimensions, per-channel independent
evaluation both strides, guards, stats, error contracts, and decoder state.
Historical byte/CRC observations remain historical only.

Focused P3 verification (historical run; the named external build root is
not available now): strict generator reported legacy and portable
hashes unchanged; native decode run at the dated root
`/tmp/opencode/pb031-p3-decode` reported 43/43 passing; `backlog doctor`
and `git diff --check` passed.

Historical execution-context note retained as conflict: the P3 handoff's
verification section says the focused verification commands ran in a plain
shell with active NCS v3.3.0 `ZEPHYR_BASE` because the repository flake
then had an unrelated `x86_64-darwin` attribute failure, so
`nix develop -c` was explicitly avoided for those focused commands. This
was the SDK state of that date; it was not repeated in later phases, and
current verification commands use the active v3.4.1 dev shell. No old-SDK
selection carries into current work.

## P4 full acceptance (commit d8f2a8e at tested HEAD; handoff p4-handoff.md)

First P4 attempt (root `/tmp/opencode/pb031-p4.BTF2Vc`, untracked handoff
made the worktree dirty): canonical gate `72 PASS / 2 FAIL / 74 TOTAL`,
because coverage correctly refused baseline enforcement and the matrix had
no `coverage.json`. All 71 unit children, BSim, both firmware builds, and
build contract 96/96 passed. The child-count blocker (74, not 72: 41
Twister + 5 exec-only + 25 Python) was resolved by repository evidence and
documented, with historical hosted 72-child results retained unchanged.

Second attempt (root `/tmp/opencode/pb031-p4.lnNZ6l`, worktree clean at
checkpoint `56472e2c`): coverage passed unchanged (population 37,
4969/5427 lines, 2177/2984 branches, 380/380 functions), BSim passed
17/26, matrix alone failed with

```text
error: invented witness: src/audio_decode.c: 'test_golden_mono_10ms'
```

(caused by P3 renaming the successful witness to `test_fixture_mono_10ms`
while the matrix kept the sole old name). Repair renamed only that witness
(commit `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22`, message
`test: repair decoder matrix witness`), passing focused matrix tests 42/42;
`check-test-matrix` reported 0 errors and 0 notes.

Accepted full gate at tested commit `d8f2a8e4d5eb0d6a7af2310a2c29e21b08542f22`
(evidence root `/tmp/opencode/pb031-p4.XgKVNM`, seven raw-log digests in
its `SHA256SUMS`; the evidence root's raw logs are no longer available on
disk as of 2026-10-08, the values below are the retained record):

- Canonical gate `74 PASS / 0 FAIL / 74 TOTAL` (41 Twister, 5 exec-only,
  25 Python, coverage, matrix, BSim).
- Coverage population 37: 4969/5427 lines, 2177/2984 branches, 380/380
  functions. Note for reader: this P4 coverage summary records 4969/5427
  lines and 2177/2984 branches while the later PB-040 clean canonical
  record at `daf7cd9` records 4971/5427 lines and 2203/3008 branches for
  population 37; both are dated records of their own commits (P4
  2026-09-18 versus PB-040 2026-09-25) and neither is rewritten here.
- BSim 17 scenarios/26 runs with exact TX pins (mono 10 ms `0xC5C840B0`,
  mono 7.5 ms `0x2CE69E65`, Mode A 10 ms L/R `0x8980C79D`/`0xDD25CC21`,
  Mode A 7.5 ms L/R `0x7D1EAC0F`/`0x001D6366`, Mode B 10 ms `0xE5D37A85`,
  Mode B 7.5 ms `0x4D9A9ED7`, zero-stream `0x811C9DC5`) and portable PCM
  `257/182/32767` within `2048/512/32750`.
- Both pristine receiver builds passed; resolved build contract 96/96;
  nRF5340 emitted only the documented warning classes including the
  `BT_CTLR_ADVANCED_FEATURES` CMake diagnostic (classified in
  `STATUS.md` before the rerun because the required peripheral-ISO SW Split
  overlay enables it to expose subordinate reservation controls;
  removing it would hide required controls); nRF54L15 emitted only the
  documented watchdog no-sources and `__ASSERT()` CMake diagnostics. No
  compiler or Kconfig assigned-value warning appeared. Test-only entropy
  and failed-injection banners are fixture evidence, not production
  diagnostics.
- `backlog doctor`; `git diff --check`; clean final status. No production
  behavior, liblc3 revision/flags, decoder code, fixture bytes, manifest
  limits, coverage baseline, or firmware feature changed.

Lifecycle closure: PB-031 moved to Review then Done, PR #13 titled
`PB-031: Make LC3/PCM test oracle platform-independent` human-merged into
main at `b59e1d8f99b8f4e7435c7086bfe81700007b221d`. Required PR checks
passed (`test-unit`, `test-heavy (coverage)`, `test-heavy (bsim)`,
aggregate `tests`, `firmware`), `release` was skipped on the pull request.
Human merge is official acceptance.

## Post-PB-031 continuity records

Two later records changed current recipe/mapping truth (retained dated
evidence, not part of PB-031):

- PB-034 (`21ff2f0`): introduced the target-native start0 recipe family,
  added two generated zero-start traces (`stateful_48k_10ms_skip20_start0_l.pcm`
  and `stateful_48k_10ms_loss48x18_start0_r.pcm`, hashes in the maintained
  contract), hardened the generator against generated-recipe output-alias
  reuse, extended calibration to 46 records including the
  `stateful-startup-history` PRNG negative control, recorded the PRNG
  correction (startup PLC retains seed across valid frames), documented the
  native Stage 1 totals 68 (first-stop) and 55 (release, disconnect,
  reconnect segment 1, duplicate-release), the exactly-one PSN 47 to 66
  client warning, and the parser suite at `146 PASS / 0 FAIL` at that
  dirty-tree state. It also recorded an earlier full unit-phase result
  `71 PASS / 1 FAIL / 72 TOTAL` from a stale one-CIS recipe mirror
  (108/26 instead of 100/18), corrected before continuing.
- PB-040 (`daf7cd9`): clean `80 PASS / 0 FAIL / 80 TOTAL` canonical gate
  with strict BSim Stage 1 unchanged oracle and limits; calibration tests
  40 pass; strict regeneration retained original v3.3.0 corpus bytes and
  provenance; the ARM calibration image build (296 build steps) completed
  with no ARM execution, flash, or physical audio claimed.

Current counts verified this session from checked files: 15 recipes in
`tests/support/lc3_stateful_recipes.c`, 15 recipes in the stateful
manifest, five generated trace files existing with the exact hashes above,
46 expected records (`expected_metric_records()` measured; ARM
`METRIC_RECORD_COUNT 46U`), 40 calibration Python tests
(`tests/unit/lc3_pcm_calibrate`), 18 parser tests
(`tests/unit/bsim_runner`), 45 matrix-witness checker tests
(`tests/unit/test_matrix`) and `check-test-matrix.py` reporting
`0 error(s), 0 note(s)`. The portable manifest remains byte-identical to
its committed record (`f82c85fe...`), and the stateful manifest hash is
`2c931ef6c3afc81081583c73bf429543c876cebf2e2166e0d43f4b4674b2519a`.

## Rights, conformance, and non-claims (consolidated)

- No license, EULA, patent, redistribution grant, or
  independent-codec-conformance grant appears in any of the sixteen
  handoffs. The checked project test source carries Apache-2.0 SPDX
  headers; that is a project-source license statement only. It is not
  rights approval for external reference tools, corpus, patents,
  qualification, or fixture redistribution.
- Host replay and generated references use the same pinned liblc3
  (semantic label 1.1.2, revision
  `48bbd3eacd36e99a57317a0a4867002e0b09e183`). The oracle is
  portability/regression evidence for the production decoder build, not
  independent decoder conformance. LC3plus is excluded.
- No air-proof claim: `sent` completions and the i386 placement model
  synchronize or model fixture state; the receiver peer count remains the
  peer-delivery truth.
- No numeric PCM threshold exception exists anywhere in this family, and
  numerical thresholds were never changed after freezing on 2026-09-16.
  Warning-class exceptions are exactly the two allowlisted negative-path
  warnings; each older `sole exception` wording is dated.
- Historical report counts (22, 26, 38, 39, 46 at their commits) were
  never rewritten; the current calibration population was measured anew
  this session (46 expected records) and the maintained contract derives
  it from current source.
- External evidence availability: the external roots
  (`/tmp/opencode/pb031-*`, `/tmp/opencode/pb034-arm-calibration-evidence/`,
  `/tmp/opencode/pb034-*`, `/tmp/opencode/pb040-*`) are not available now,
  and the retired plan/handoff records were removed from the tree after this
  preservation (content citable only at Git revision
  `a94f010de00e25d4a2433f7b4c56b31a5377446e`).
  The retention basis for this consolidated document is the quoted content
  preserved here, the completed PB-031 task, and these dated
  records; no generic availability promise is made and no raw rerun is
  possible today without the original host and hardware sessions.

## Source index (plan plus sixteen handoffs, inspected and quoted verbatim in the preservation comparison)

1. `portable-lc3-pcm-oracle-plan.md`
2. `portable-lc3-pcm-oracle-p0-handoff.md`
3. `portable-lc3-pcm-oracle-p0-arm-handoff.md`
4. `portable-lc3-pcm-oracle-p0-intel-handoff.md`
5. `portable-lc3-pcm-oracle-p0b-threshold-handoff.md`
6. `portable-lc3-pcm-oracle-p0c-handoff.md`
7. `portable-lc3-pcm-oracle-p0d-handoff.md`
8. `portable-lc3-pcm-oracle-p0e-handoff.md`
9. `portable-lc3-pcm-oracle-p1-handoff.md`
10. `portable-lc3-pcm-oracle-p1-loss-comparison-handoff.md`
11. `portable-lc3-pcm-oracle-p1-loss-diagnostic-handoff.md`
12. `portable-lc3-pcm-oracle-p1-loss-placement-repair-handoff.md`
13. `portable-lc3-pcm-oracle-p1-loss-repair-handoff.md`
14. `portable-lc3-pcm-oracle-p1-send-cap-wait-review-fix-handoff.md`
15. `portable-lc3-pcm-oracle-p2-handoff.md`
16. `portable-lc3-pcm-oracle-p3-handoff.md`
17. `portable-lc3-pcm-oracle-p4-handoff.md`

Items 1 through 17 (plan plus sixteen handoffs) map 1:1 to the
preservation comparison's 16 tracked handoff paths (all
`a94f010:docs/development/portable-lc3-pcm-oracle-*-handoff.md` paths) plus
`a94f010:docs/development/portable-lc3-pcm-oracle-plan.md` (17 paths total).
Canonical
sections, per-path facts, negative findings, external-availability, and
uninspected-path rows were drawn from the read-only preservation evidence
JSON at `/tmp/opencode/repo-wrapup-updated-20261008/pcm-preservation-evidence.json`
(SHA-256 `a24507a8e09bcc0f71aeb3c50beee3f273150dc587fd43a60841d27c3ea89e37`,
126 facts: 48 covered, 46 partly covered, 29 not captured, 3 historical
conflicts) and its fact packets. This consolidated report preserves the
substantive unique facts those rows captured; session-only coordination
content was not carried over.