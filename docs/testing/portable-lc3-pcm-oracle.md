# Portable LC3/PCM oracle contract

Owner item: PB-031 (completed, `docs/product/backlog/completed/`). This
document is the maintained current-state contract. Consolidated dated
evidence: `docs/development/pb-031-portable-pcm-results.md`. The retained
historical plan and phase handoff records that fed that results document
were retired after preservation and survive as quoted content at the exact
Git revision `a94f010de00e25d4a2433f7b4c56b31a5377446e` (cited as
`a94f010:<path>` in the results document); current truth lives here and in
the completed PB-031 task itself.

## Purpose and scope

The oracle enforces a calibrated portable PCM regression envelope across
different host CPU and compiler instruction paths while the transport stays
exact. liblc3 is compiled with `-O3 -ffast-math`, and generated native x86
code uses reciprocal-square-root instructions in liblc3 LTPF/SNS paths, so
identical liblc3 source can produce different decoded PCM bytes on different
CPU instruction paths. Byte-identical decoded-PCM pins are therefore not a
portable acceptance boundary.

Boundary summary:

- Checked-in LC3 encoded bytes, exact transport (SDU order, frame geometry,
  send counts), lifecycle, PLC placement, malformed rejection, routing, and
  statistics remain byte-exact pass/fail contracts.
- Decoded PCM is accepted through fixed integer metrics (maximum absolute
  error, integer RMS, Q15 correlation), never byte hashes.
- Historical decoded-PCM hashes and CPU-specific pins remain diagnostic
  evidence only. They are never pass/fail values and are never used as
  fallback.

Non-claims: this oracle is same-library portability and regression evidence,
not independent codec-conformance proof. Host replay and generated references
use the same pinned liblc3 source as production. No license, EULA, patent,
redistribution, or independent-decoder-conformance grant exists in the
retained PB-031 evidence (verified across the full retired record set before
consolidation), and project-source SPDX files do not grant
external rights. LC3plus is excluded. No claim is made about RF
transmission-error recovery, audio quality, or hardware behavior from these
fixtures alone.

## Immutable numerical limits and sole owner

`tests/fixtures/lc3/portable-oracle-manifest.json` schema 2 is the sole owner
of the calibration policy:

```text
max_abs_error     = 2048
max_rms_error     = 512
min_correlation_q15 = 32750
```

Rules:

- These limits are frozen. No widening, per-CPU exception, per-scenario
  exception, or source-valid-output exclusion is authorized. Excluding a
  source-valid output from numerical comparison is a failure, not an
  optimization.
- No test suite hard-codes these numbers. Host and ARM calibration paths pass
  them as validated values from the manifest (Python argv to the host
  calibrator; CMake `string(JSON)` configure-time macros into the ARM image).
- Calibration provenance checks reject manifest relabeling: manifests retain
  historical corpus origin `ncs_version: v3.3.0` and liblc3 semantic label
  `1.1.2` with exact west revision
  `48bbd3eacd36e99a57317a0a4867002e0b09e183`, while calibration workspace
  checks demand the active NCS v3.4.1 revisions and a separate
  `fixture_ncs_version` field records the original corpus origin. Relabeling
  either side fails. The unchanged codec SHA is not a new independent
  reference implementation.
- Threshold selection history (for provenance, not an invitation to recalc):
  valid envelopes were measured as AMD Ryzen 9 5950X GCC 14.3.0
  `0/0/32767`, Intel Core i3-6100U Clang 21.1.8 `1977/426/32757`, and nRF54L15
  ARM `1/1/32767` (maximum/RMS/correlation). Chosen headroom: 2048 is the
  next power-of-two boundary above 1977 (71 samples, 3.59 percent); 512 is
  the next power-of-two boundary above 426 (86 samples, 20.19 percent);
  32750 is seven Q15 counts below the measured floor 32757. Numerical
  thresholds were never changed after freezing (P0b, 2026-09-16).

## Comparator (tests/support/pcm_oracle.h/.c)

Test-only shared engine. Integer-only acceptance math, incremental
accumulation for multi-frame streams.

- `pcm_oracle_accumulate(oracle, actual, actual_stride, reference,
  reference_byte_stride, sample_count)` accumulates per-sample squared error,
  both scaled energies (`sample / 256` squares), dot product, and running
  maximum absolute error. All accumulator adds are checked; overflow returns
  `-EOVERFLOW` without mutating output metrics.
- `pcm_oracle_finalize()` derives integer RMS as
  `ceil_sqrt_u64(ceil(squared_error / samples))`: the squared-error mean is
  first rounded up to the integer ceiling, then the ceiling integer square
  root is taken (so RMS is never understated by a fractional mean or a
  floor-sqrt truncation). Correlation stays scale-safe and signed: energies
  use `floor_sqrt_u64` on the /256-scaled sums, the dot product is multiplied
  by 32768, the quotient truncates toward the smaller magnitude, and the Q15
  result clamps to `INT16_MIN`/`INT16_MAX`; zero-energy inputs
  short-circuit.
- `pcm_oracle_evaluate()` ranks `INSUFFICIENT_SAMPLES`, then
  `MAX_ERROR`, then `RMS_ERROR`, then `CORRELATION` (including zero-energy),
  then `PASS`.
- Callers pass actual samples as stride-counted `int16_t` and references as
  little-endian bytes. Each caller passes its own strides; no caller uses
  both strides equal to 2 in the BSim sink sense:
  - BSim receiver sink (`tests/bsim/src/audio_sink_stub.c`, accumulation at
    the stateful corpus comparison): actual stride `2U` over the interleaved
    stereo buffer, reference byte stride `sizeof(int16_t)` (`2`) over the
    already volume-scaled per-channel reference copy.
  - Host calibration (`tests/fixtures/lc3/calibrate.c`): mono per-channel
    replay accumulates actual stride `1U` from the decoded mono buffer with
    a reference byte stride of `2U`; the standalone boundary controls build
    per-channel frames directly and use the same `1U`/`2U` shape against
    mono little-endian references.
  - Real-decoder suite (`tests/unit/decode/src/test_decode.c`): actual
    stride `2U` into the interleaved decoded stereo output, reference byte
    stride `4U` into the interleaved checked-in PCM anchor.
- Comparator unit tests prove result precedence, rounding direction,
  overflow transactions, and isolation of each rejection reason through the
  public suite `tests/unit/pcm_oracle` (12 tests).

Reference source note: the comparator algorithm is described here for
reference only; the executable truth is the checked-in source
`tests/support/pcm_oracle.c` at the current repository revision. This
documentation is not a copy of the algorithm and never overrides it.

## Fixture corpus and manifests

Checked-in under `tests/fixtures/lc3/`:

- Four legacy mono/Mode B fixture pairs (60-byte frames), plus their
  documented SHA-256 table.
- Four portable corpus streams (10 ms/120 B and 7.5 ms/90 B, left and
  right), 128 continuous frames each, formula
  `bsim_tx_hash_mix_seq_i_ch_v1`, one encoder and one decoder alive across
  all frames per stream. Left/right patterns are distinct; calibration proved
  all 128 payloads unique per stream and zero byte-identical payload overlap
  between same-duration left/right streams.
- Five generated stateful PCM traces backing the recipes whose histories
  change decoder state (see below).
- `portable-oracle-manifest.json` (schema 2) owns geometry, file hashes,
  flags, and the numerical limits above.
- `stateful-reference-manifest.json` (schema 1) binds the ordered recipes to
  backing files and to the portable manifest (`source_portable_manifest`
  size 2831, SHA-256
  `f82c85fed3097b6943b2d75733f7a377d0beb71a0a79fc5566ac8ed76bc7ba11`).
  It retains historical corpus-origin SDK label; it is not relabeled when the
  active SDK changes.

Detailed per-file sizes, formulas, and hashes live in
`tests/fixtures/lc3/README.md`, the fixture owner. Generation is
reproducibility-only: `generate.sh` (portable corpus) and
`generate_stateful_references.sh` (stateful traces) validate against
manifests in default mode, never rewrite checked-in files, and expose
explicit rebase modes for reviewed intentional regeneration. Generator
compiles with `-O3 -std=c11 -ffast-math -Wall -Wextra -Wdouble-promotion
-Wvla -pedantic -Werror` against the pinned liblc3 sources.

## Stateful recipes and decoder history

`tests/support/lc3_stateful_recipes.h/.c` own the immutable ordered recipe
table. Fifteen recipes in exact order:

| # | ID | Steps summary | Reference kind |
|---|----|---------------|----------------|
| 0 | `start8_10ms_l` | PLC 8; corpus 0..99 | portable PCM |
| 1 | `start8_10ms_r` | PLC 8; corpus 0..99 | portable PCM |
| 2 | `start11_7p5ms_l` | PLC 11; corpus 0..99 | portable PCM |
| 3 | `start11_7p5ms_r` | PLC 11; corpus 0..99 | portable PCM |
| 4 | `modea_start_7p5ms_l` | PLC 12; corpus 0..100 | portable PCM |
| 5 | `modea_start_7p5ms_r` | PLC 10; corpus 0; PLC 2; corpus 1..100 | generated trace |
| 6 | `skip20_10ms_l` | PLC 8; corpus 0..19; corpus 21..100 | generated trace |
| 7 | `loss48x18_10ms_r` | PLC 8; corpus 0..47; PLC 18; corpus 48..81 | generated trace |
| 8 | `start7_10ms_l` | PLC 7; corpus 0..99 | portable PCM |
| 9 | `start0_10ms_l` | corpus 0..99 | portable PCM |
| 10 | `start0_10ms_r` | corpus 0..99 | portable PCM |
| 11 | `start0_7p5ms_l` | corpus 0..99 | portable PCM |
| 12 | `start0_7p5ms_r` | corpus 0..99 | portable PCM |
| 13 | `skip20_start0_10ms_l` | corpus 0..19; corpus 21..100 | generated trace |
| 14 | `loss48x18_start0_10ms_r` | corpus 0..47; PLC 18; corpus 48..81 | generated trace |

Population distinction (both retained, never merged or relabeled):

- Recipes 0 through 8 are the historical measured nRF5340-BSim-era Stage 1
  histories (startup PLC counts eight, eleven, twelve/two asymmetric, skip 20,
  loss after 48, reconnect seven). They remain the calibration population and
  are exercise targets of the calibration report and its mutation controls.
- Recipes 9 through 14 model the current nRF54L15 BSim native mapping, where
  native startup recipes have zero startup PLC. These six recipes are
  appended after the immutable historical population and are the recipes the
  canonical Stage 1 scenario matrix binds today (verified from
  `tests/bsim/stage1-scenarios.json`).

Decoder-history rules baked into the recipe semantics:

- A fresh decoder's startup PLC actions produce silence, and the first valid
  frame calls `lc3_plc_suspend()`. Startup PLC recipes reference existing
  portable PCM directly from the first corpus frame; no duplicate bytes are
  checked in for them.
- Startup PLC count is an exact recipe/lifecycle contract, but it is not a
  numerical mutation control by itself.
- The PLC PRNG seed persists across valid frames: `plc.c` resets the seed
  only at decoder setup; `lc3_plc_suspend()` resets count and attenuation,
  not the seed. Good-frame output equality after startup PLC therefore does
  not prove all later behavior: later loss bursts and recovery outputs carry
  different decoder state. Proof by measurement: the zero-start loss
  reference `stateful_48k_10ms_loss48x18_start0_r.pcm` SHA-256
  `40204f38b2a359c3d4348131b5900bc1d065fda4423173c6eee5839c1ddf3fbd` differs
  from the legacy start8 loss reference
  `stateful_48k_10ms_loss48x18_r.pcm` SHA-256
  `19087061a5f3d74d6d9631b7c5ba100fce358615cbffde322692ae65cc6e91be` despite
  identical later SDUs. The `stateful-startup-history` negative control
  (startup-history wrong-reference rejection) exists exactly to catch a
  wrong reference of this shape.
  Historical statements that suggest universal startup-state erasure are
  falsified dated claims retained in the retired records (quoted at
  `a94f010:docs/development/portable-lc3-pcm-oracle-p0c-handoff.md`,
  grounding section, and
  preserved in the results document), superseded by this contract and by
  PB-034 record
  `docs/development/pb-034-primary-repair-results.md`.
- PLC actions advance the recipe cursor and decoder history but are never
  stored or numerically compared. Only source-valid decoded outputs enter
  metrics. This valid-only policy is the declared contract; it is not
  permission to omit any source-valid output.
- A malformed exact-shape SDU rejection happens before decode, before any
  store or observer observation, and creates no recipe action and no cursor
  advance. The malformed scenario resumes at its next valid corpus frame
  (current native mapping binds `skip20_start0_10ms_l` and resumes at corpus
  frame 21; the historical start8 binding resumes at frame 21 with the same
  no-action semantics for frame 20).

Generated traces on disk (sizes and current hashes verified against checked
files):

| File | Size | SHA-256 |
|------|-----:|---------|
| `stateful_48k_7p5ms_modea_start_r.pcm` | 72720 B | `d76724f3392321a4ce959a00867ae40d82bcb93854099ec5d9abc8c01239d858` |
| `stateful_48k_10ms_skip20_l.pcm` | 96000 B | `cead2e59efb32c78cb87818c710ca727082fd9bb9137bb8255b4f1e37d9be024` |
| `stateful_48k_10ms_loss48x18_r.pcm` | 78720 B | `19087061a5f3d74d6d9631b7c5ba100fce358615cbffde322692ae65cc6e91be` |
| `stateful_48k_10ms_skip20_start0_l.pcm` | 96000 B | `cead2e59efb32c78cb87818c710ca727082fd9bb9137bb8255b4f1e37d9be024` |
| `stateful_48k_10ms_loss48x18_start0_r.pcm` | 78720 B | `40204f38b2a359c3d4348131b5900bc1d065fda4423173c6eee5839c1ddf3fbd` |

Note on traces: `skip20_start0_10ms_l` shares its byte content with
`skip20_10ms_l` because skipping one frame between two pure-corpus runs
(without a mid-history PLC burst) leaves the later valid frames
byte-identical; the loss trace differs because the 18-frame PLC burst changes
later decoder state through the PLC PRNG seed. Manifest recipes 6 and 13
therefore point at two distinct files whose contents are identical, and both
files exist and keep their own declared SHA-256 records; the generator
remains byte-identical across strict re-runs, which is the proof that this
pairing is intentional rather than a stale copy.

## Calibration protocol and current record population

Public CLI: `python3 scripts/lc3_pcm_calibrate.py --output
ABSOLUTE_NEW_FILE.json`. Stdlib-only Python driver; compiles the host
calibrator, shared comparator, and pinned liblc3 sources with the exact
generator flags; every compile diagnostic is fatal; report is written
atomically with mode 0644, refuses overwrite, and no output file exists on
any failure; provenance captures UTC timestamp, raw `lscpu`, `uname`,
compiler version, architecture, liblc3 revision, flags, fixture hashes,
calibration-input hashes, repository HEAD, and bounded raw
`git status --porcelain=v1`.

Current expected record population is exactly 46 ordered records
(`expected_metric_records()` in the driver and `METRIC_RECORD_COUNT 46U` in
the ARM image both enforce this):

- 26 original prefix: four valid, two channel-swap, four
  prior-frame/next-frame/dead-channel/low-correlation-synthetic per stream in
  manifest order, one LC3 byte corruption (byte 0 XOR `0x04` of frame 0 of
  the 10 ms left stream; all 128 frames must still decode), and the
  maximum-error, RMS-error, and correlation threshold-boundary controls.
- Fifteen `stateful-valid` records, one per recipe in table order, all
  expected `pass`.
- Five mutation records, all expected `max-error`:
  `stateful-payload-off-by-one`, `stateful-skip-ignored`,
  `stateful-loss-burst-omitted`, `stateful-wrong-channel`, and
  `stateful-startup-history` (recipe 14 replayed against recipe 7 reference;
  PRNG-history rejection).

Historical record counts (22 schema-1, 26 schema-2, 38, and 39 schema-3
records) remain dated evidence in the consolidated results document and in
`docs/development/pb-034-primary-repair-results.md` (46 records at the
2026-09-24 target-native repair). They describe their own commits and are not
rewritten to the current population.

Calibration identity requirements (from the accepted reports):

- Environments must be identified by raw CPU identity (vendor strings
  directly observed from the host, for example `AuthenticAMD` or
  `GenuineIntel`), never by CI runner labels. The full accepted per-vendor
  model/stepping details live only in the results document.
- Report schemas require exact manifest hashes, input hashes, flags,
  geometry, per-record identity, order, geometry, and evaluation
  recomputation. Repeat reports must match within an environment; record
  identity and order must match across environments.
- ARM protocol: `PB031_ARM_BEGIN schema=3 ...`, one `PB031_ARM_SOURCE`
  line, 46 `PB031_METRIC` lines, thread-analyzer lines (not protocol
  records), then `PB031_ARM_PASS metrics=46`. First error emits one
  `PB031_ARM_FAIL` line and no PASS line.

## BSim Stage 1 binding (current native mapping)

- Client (`tests/bsim/client/`) embeds the four corpus `.lc3` files, has no
  runtime LC3 encoder, and transmits exact checked-in bytes. Client evidence
  carries unsigned 32-bit FNV-1a (offset `0x811C9DC5`, prime `0x01000193`)
  over four little-endian sequence bytes plus the exact final SDU bytes per
  successful send; the audit survives unregister so reconnect retains stream
  0 evidence and proves stream 1 restarts at sequence zero. The parser
  independently derives expected hashes from the corpus, layout, counts, and
  injection, and rejects duplication, omission, reorder, wrong channel,
  corruption, count drift, and exhaustion.
- Receiver (`tests/bsim/src/audio_sink_stub.c` plus `bsim_observer*`) takes
  bounded exact payload snapshots (valid half: non-null payload length 1 to
  `BSIM_OBSERVER_MAX_PAYLOAD_BYTES` (120); invalid half: null/zero), advances
  independent per-channel recipe cursors, identifies each payload by exact
  byte comparison against both same-geometry source streams (exactly one
  match required; payload hash is never identity; receiver controller
  sequence is diagnostic data, never fixture identity), scales the selected
  reference frame by BSim volume 195 with C integer truncation toward zero,
  and accumulates only source-valid outputs. Accounting invariants
  (`recipe_actions == transients + pushes`, `recipe_valid ==
  compared_frames`, `recipe_plc == recipe_actions - recipe_valid`,
  `pre_valid + pre_plc == transients`,
  `post_boundary_excluded == recipe_plc - pre_plc`,
  `recipe_valid == pre_valid + pushes - post_boundary_excluded`,
  `compared_samples == compared_frames * samples_per_channel`) are enforced
  by the strict parser for every segment.
- Binding capacity is `BSIM_RECIPE_BINDING_MAX 15` (executable source value;
  the historical nine-recipe capacity statement and the P2 summary record the
  dated value 9 for that phase). All current scenario mappings bind only the
  six start0-family recipes; the historical nine recipes stay calibration
  population.
- Scenario mapping today (schema 3, from `tests/bsim/stage1-scenarios.json`):
  every audio scenario binds start0-family recipes exactly as listed in that
  file, including `invalid_sdu_resume_10ms` bound to
  `skip20_start0_10ms_l` (malformed frame creates no action, resume at frame
  21), `modea_one_cis_loss_10ms` bound to `start0_10ms_l` plus
  `loss48x18_start0_10ms_r` (18-loss burst after 48 valid right pushes on
  native zero-start history), and `reconnect_second_stream_10ms` binding
  `start0_10ms_l` on both channels for both segments (fresh recipe cursor
  state each segment). The historical P2 mapping (start8/start11/modea/start7
  recipes) remains the dated calibration mapping quoted in the consolidated
  results document (retired records at
  `a94f010:docs/development/portable-lc3-pcm-oracle-plan.md` and
  `a94f010:docs/development/portable-lc3-pcm-oracle-p2-handoff.md`);
  it is superseded for the current native target by PB-034 record
  `docs/development/pb-034-primary-repair-results.md`.
- Exact negative-path warnings currently allowlisted (checked, not
  blanket): the receiver duplicate-release warning `Invalid operation in
  state: releasing` (scenario 17), and exactly one client one-CIS-loss
  warning `Unexpected seq_num diff between 47 and 66 for <stream pointer>`
  from the deliberate 18-event PSN gap. The historical sole-scenario
  statement predating the PSN warning is dated evidence. Anything else,
  including wrong gap, wrong role, wrong count, or wrong scenario, is fatal.

## One-CIS loss placement and TX send-cap synchronization (retained boundary)

The loss scenario models an exact transport gap: right sends exactly 48 valid
frames, omits payloads for 18 CIS intervals while PSN advances, then resumes.
Mechanism facts retained from the historical phase:

- Scheduling is event-counted, not wall-clock tuned: pre-gap sends 48, loss
  count 18, final send limits 110. The current client builds this shape with
  `bsim_tx_schedule_gap(&streams[1], 48, 18)`: gap ticks omit payload bytes
  while still advancing the transport PSN, and the arm point is exactly
  send count 48. Wall-clock duration probes (200 ms,
  190 ms) were diagnostic-only and failed to place the gap; they are not and
  may not become a repair mechanism.
- The TX slot exposes an exact send-cap notification. First implementation
  used a reusable slot-embedded binary semaphore; review rejected it because
  `k_sem_reset()` aborts a pending `k_sem_take()` with `-EAGAIN` (mapped to
  timeout) and slot reuse/reinit cannot satisfy documented `-ESTALE`
  semantics for pending waiters. The retained design is process-lifetime
  per-slot condition variables (`k_condvar`), with init/unregister/register,
  cap reach, and limit-change broadcasts performed under `tx_lock`, and full
  snapshot revalidation (`association`, `generation`, limit, count, pause
  state) after every wake.
- `bt_bap_stream_ops.sent` (controller completion) and completion drains
  synchronize the fixture only. They are not proof of air delivery; the
  receiver concealment/count remains peer-delivery truth.
- No standalone `bsim_tx` unit race harness exists or is required: public
  acceptance is real encoded BAP traffic through all matrix clients, and
  condition-variable lifetime and state-transition semantics are verified by
  source review plus the strict matrix. A passing matrix does not by itself
  prove dynamic pending-waiter lifecycle coverage; that boundary is declared.

## Negative controls and their scope

Distinct control families, not interchangeable:

- Synthetic comparator controls in `tests/unit/pcm_oracle`: channel swap,
  frame shifts, dead channel, low-correlation synthetic, plus threshold
  boundary controls (2049 single-sample, 513-distributed RMS, negated
  +-256 frame), precedence and overflow/rounding checks. These isolate
  comparator reason ranking; they do not exercise liblc3.
- Calibration-level controls (host and ARM): the same classes plus the
  all-decodes-must-succeed XOR `0x04` byte-0 corruption over the 10 ms left
  corpus, and the fifth startup-history wrong-reference rejection.
- Parser-emitted-evidence mutations (`tests/unit/bsim_runner`): wrong
  hashes/counts/accounting in transport and receiver records must fail the
  strict parser; parser regressions cover wrong gap, count, role, and
  scenario cases.
- Transport mutations: corpus-derived malformed injection and TX hash
  mismatch; decoded-PCM hash CLI options and `BSIM_BASELINE` are removed and
  rejected.
- Real-linker hard decoder errors: `tests/unit/decode` injects `-1` via a
  test-only `-Wl,--wrap=lc3_decode` that delegates to the real implementation
  for all other calls.

None of these alone proves RF transmission-error recovery, independent codec
conformance, analog quality, or hardware delivery. A deliberately invalid
encoded input is not proof of RF transmission-error recovery.

## Real-decoder fixture lane (PB-031 P3)

`tests/unit/decode` drives the real public `audio_decode_sdu()` boundary
with the same checked-in fixtures and the same shared comparator, per
channel, with actual stride 2 and reference-byte stride 4, one frame and the
configured samples-per-channel count per call. Exact LC3 geometry,
integrity hashes, mono duplication semantics, Mode B `[L][R]` placement with
independent per-channel decoder state, guards, statistics, decoder state
preservation across repeat/reconfigure/reject calls, rejection behavior,
and the wrapped hard-error accounting remain exact contract assertions.
Decoded PCM bytes and CRC values are not pass/fail. Current suite: 43 tests
(historical T2/CODEC-017 record counts describe their dated suite; see
`docs/testing/coverage-matrix.md` row for the current matrix accounting).

## Calibration source provenance boundary

The accepted cross-platform calibration runs happened at specific, retired
repository revisions. Current checkout hashes differ naturally after later
work and cannot substitute for the calibration-time inputs, and current
checkout hashes are not calibration evidence by themselves.

- Full preserved historical identity (calibration-input and report hashes,
  raw CPU/vendor/model identities, Intel/AMD/ARM report triples,
  ARM image/stack/flash figures, external evidence-root paths, raw-run
  availability checks) is retained in
  `docs/development/pb-031-portable-pcm-results.md`, quoted verbatim from
  the retired records at exact Git revision
  `a94f010de00e25d4a2433f7b4c56b31a5377446e` (cited there as
  `a94f010:<path>`). The completed PB-031 task retains its dated notes.
- This current guide intentionally keeps only the durable current facts:
  frozen limits, the measured-envelope/headroom basis for them, executable
  source identity, and the current record population. It carries no
  per-run hardware digests.
- Non-claims stay as stated in Purpose: same-library portability and
  regression evidence only, no independent-codec conformance, no rights
  grant, LC3plus excluded, no RF/analog/audio-quality claims.
- Availability boundary: the external `/tmp/opencode/pb031-*` (and later
  PB-034/PB-040 external) evidence roots were verified absent on 2026-10-08
  (93 referenced literal paths plus 12 unresolved `$` template references).
  No raw rerun is possible today, none is promised, and references in dated
  documentation are not current evidence availability.

## Limits of this contract

This contract holds the current numerical/geometric/lifecycle policy only.
It does not replace:

- BSim scenario/lifecycle/count/warning checks (behavior contract, coverage
  matrix),
- the exact TX hash contract (fixture README),
- production decoder behavior (unchanged by this oracle),
- physical RF/analog/I2S/presentation delivery acceptance (separate
  boundaries), or
- independent LC3 conformance and rights decisions (PB-042; excluded scope,
  LC3plus explicitly excluded).