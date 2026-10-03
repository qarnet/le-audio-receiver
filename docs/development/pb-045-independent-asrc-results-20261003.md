# PB-045: independent ASRC arithmetic and caller continuity

## Scope and current evidence boundary

This iteration adds an independent rational, full-waveform arithmetic oracle and
a native production sink/offload/processor integration lane. It repairs the
negative-ppm quantization defect exposed by that oracle. The item remains In
Progress until clean-commit repository gates and hosted PR checks pass.

No LC3 reference tooling or LC3plus is used. No 360-frame FLPR feature is added.
Spectral quality, closed-loop PI control, physical FLPR execution, mailbox
transport, RF, physical I2S timing, DAC fidelity and presentation remain separate
proof boundaries. Frozen coverage baselines, canonical BSim 17/26, PCM and HIL
limits are unchanged. PB-013 user-owned edits remain unstaged.

## Independent model and public boundaries

`tests/unit/asrc_oracle/test_asrc_oracle.py` evaluates authored stereo input at
global source coordinates with exact `Fraction` arithmetic. Nominal rate ratio
and signed ppm delta are independently rounded into Q32 ticks; PCM interpolation
uses nearest rounding with half ties away from zero. Expected output never
calls production code or reads DUT phase. Zero tolerance applies to this defined
quantized arithmetic contract, not an arbitrary alternative resampling filter.

Four rate pairs, six signal families and eight ppm settings produce 192 matrix
cases, each processed in regular and irregular partitions. Every produced
stereo sample and both consumed/produced counts are checked. A common global
control schedule, signed extrema, boundary impulses, distinct channels, cold
and live-history rejection/retry, reset and processor handover are included.
Output guards and raw count checks reject capacity overrun/reporting defects.
Eight separately compiled mutants target signed ppm rounding, PCM rounding sign,
ppm direction, boundary history, channel routing, frame advance, capacity writes
and produced-count reporting. Compiler failures cannot satisfy negative controls.

`tests/unit/audio_i2s_asrc_oracle/` links real `audio_i2s.c`, `audio_offload.c`,
`audio_asrc.c`, `flpr_audio_process.c`, rate converter and NONE actuator. Expected
vectors are generated at configure time solely from the independent model.
The remote endpoint invokes the real processor on submitted PCM and opaque
pre-state; it never returns oracle vectors. In-process transport and fake I2S
hardware isolate the arithmetic/caller boundary, not physical transport.

The actual sink owns CPU fallback and state commit. Fixed-duration 360-frame
streams prove unsupported-offload CPU continuity. Supported 480-frame streams
exercise remote success, timeout, CPU fallback, recovery, rejected post-state
and resumed remote success from established fractional history. Every submitted
startup-silence and data word is inspected before simulated DMA release. Public
offload counters prevent silent CPU fallback from masquerading as remote success.

Read-only review first identified missing production-caller coverage, hidden raw
counts and cold-only rejection tests. Those gaps were repaired; a second review
found no substantive correctness issue. Neither review substitutes for gates.

## Uncovered defect and repair

`src/audio_asrc.c:compute_step` formerly added positive 500000 before C signed
division for both ppm signs. Negative division truncates toward zero, so this
did not implement its documented nearest/ties-away rounding rule. At identity
ratio and -1 ppm, the old delta was -4294 Q32 ticks; the correct delta is -4295.
The fix selects the half-unit sign from the product before division.

The independent native waveform regression failed before repair and passed
afterward. No tolerance was widened. `audio_asrc.h` also now describes zero-output
tiny downsampling blocks, carry phase beyond one frame and partial output writes
on transactional capacity failure accurately. `flpr_audio_process.c` explicitly
marks an already-validated unused identity-path argument unused, resolving the
new host `-Wextra -Werror` diagnostic without suppressing warnings.

## Physical CPUAPP before/after isolation

External immutable successful diagnostic root:
`/tmp/opencode/pb045-physical-r3`. The same authored witness and configuration
ran on the same freshly identified XIAO CPUAPP; only the signed rounding
expression differed between sealed source variants. This is a standalone
`asrc-diagnostic` role, not an inferred source/receiver role or production stream.
The unused second board was not flashed. Capture opened before each flash/reset;
OpenOCD loaded and byte-verified each image. No RF or I2S application was enabled.

Five retained raw identity checkpoints (initial, before/after each image) agreed:
DPIDR `0x6ba02477`, AP0 `0x84770001`, AP1 `0x84770001`, AP2 `0x32880000`,
AP3 `0x00000000`, FICR PART `0x00054b15`, VARIANT `0x41414330` (`AAC0`).
USB evidence bound the selected live probe to its console through VID:PID
`2886:0066`, interface `02`, serial and `ID_PATH`. The logs own session-specific
probe/tty association; no permanent mapping is asserted here.

The public PCM witness `test_negative_ppm_fractional_known_samples` uses ratio
1:2, -2000 ppm and alternating stereo extrema. At output frame 443, independent
global-coordinate arithmetic requires L=-29033 and R=29032. Old arithmetic
returned L=-29032 and R=29031. Physical before suite: 20 PASS / 1 FAIL / 0 SKIP;
failure specifically at L sample assertion. Physical after: 21 PASS / 0 FAIL /
0 SKIP, terminal `PROJECT EXECUTION SUCCESSFUL`. The diagnostic board remains
on the after numeric-test image; future streaming sessions must provision and
freshly identify their intended firmware roles. Consoles were closed.

| Retained artifact | SHA-256 |
| --- | --- |
| before CPUAPP HEX | `96ed72844fcc6768bae2583af1c4891a2116edb50a4d1cad8e1911f4d28bea43` |
| after CPUAPP HEX | `5b1b0bfb78560d1b69ddd8614bd1911f7a3f3b4362e575fe015ac16ca3ccc24c` |
| before raw UART | `fb575cd8d512a32c165a05fb647bcd402d09b970a460db90fa7b7db64c63763c` |
| after raw UART | `5bfd815af4417ce4f67cb6afadc0cbf977b33487181a90ac6e0699f796005c5a` |

Earlier attempts remain failures, not overwritten acceptance: r1 built under
implicit sysbuild, then failed to open the assumed HEX path after reset/halt;
r2 byte-verified its image but console capture remained empty with DTR false and
the outer command timed out. r3 used explicit `--no-sysbuild`, required image
existence before target action, and the SAMD11 bridge's DTR=true capture state.
Generic Nordic target configuration avoided the board config's automatic erase
hook. No erase/recovery was performed. Both physical builds and flash logs were
warning-free. Test-thread stack was explicitly 32768 bytes for legacy large
local arrays; no production stack setting changed.

## Focused and initial repository verification

- Independent oracle: 6 test methods, complete matrix and eight compiled controls
  passed. Reviewed run log `/tmp/opencode/pb045-oracle-reviewed-r1.log`, SHA-256
  `3029c04e848f897742ea5e81f26f0126cde27063fb5351147f6dd063626d1ce4`.
- Native sink integration: 2 PASS / 0 FAIL, log
  `/tmp/opencode/pb045-sink-run-r1.log`, SHA-256
  `0519343342175aee2a903399a6d3879a7334fc1e260bfebefe70defe3c66ffc9`.
- Receiver CPUAPP/FLPR, standalone source and HCI images built. Build-contract
  check passed using `--nrf54l15 build/nrf54l15`; raw log
  `/tmp/opencode/pb045-build-contract-r1.log`, SHA-256
  `7d3a5405fcc6ad499af34721b3e6ed77e46f15319252d7d9b21ebc96eeb3fb31`.
  The first combined command omitted this required argument after all builds;
  its exit 2 is retained separately, not represented as a build failure/pass.
- Dirty primary-tree canonical attempt: **80 PASS / 2 FAIL / 82 TOTAL**.
  All 79 unit children and canonical BSim passed. Coverage correctly refused a
  dirty worktree; dependent coverage-matrix check had no coverage JSON. This is
  not accepted canonical evidence. A detached clean candidate worktree is used
  next, preserving PB-013 and private untracked data in the primary repository.

Full logs retain the scoped native unsupported-SoC notice and receiver
experimental TX-notify-workqueue notice under existing recorded dispositions;
no new warning waiver, baseline change or skipped failing scenario is introduced.

## Clean-worktree runner repair

The first detached clean candidate at `9bb642e` again produced 80 PASS / 2 FAIL /
82 TOTAL, this time because `test-coverage.sh` required `.git` to be a directory
and rejected a valid linked worktree's `.git` file as "not a git checkout".
Raw failure log `/tmp/opencode/pb045-clean-canonical-r1.log` has SHA-256
`8623230f08c2593cbca8aa5f3d3f3ff2b34a16bdcd8c4546752499992fbddb92`.

The runner now asks `git rev-parse --is-inside-work-tree` rather than inferring
checkout validity from filesystem shape. Exact commit provenance and dirty-tree
rejection remain unchanged. Public CLI tests use real detached linked worktrees:
clean baseline write/enforcement records the real source SHA; dirty linked
worktrees still fail. The focused runner suite passes, raw log
`/tmp/opencode/pb045-linked-coverage-focused.log`, SHA-256
`021316fb62933ad07c106fa910aeb2a2fbdcfda566953d48a84e6979cb7ea0e0`.
This is a necessary validation-boundary repair, not permission to bypass clean
acceptance or replace the committed coverage baseline.
