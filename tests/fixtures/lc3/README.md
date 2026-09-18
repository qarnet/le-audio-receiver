# LC3 test fixtures

Checked-in, reproducible LC3 bitstream + expected-PCM pairs for the
receiver's legacy decode fixtures and portable BSim diagnostic corpus. Legacy
fixture binaries are embedded by
`tests/unit/decode` (see `docs/testing/t2-audio-pipeline-tests.md`) and
are never regenerated during normal test runs.

## Legacy decode fixtures

| Fixture | Shape | `.lc3` size | `.pcm` size (samples) |
|---------|-------|-------------|-----------------------|
| `mono_48k_7p5ms_60b` | mono, 48 kHz, 7.5 ms, one 60-byte LC3 frame | 60 B | 1440 B (720 stereo samples) |
| `mono_48k_10ms_60b` | mono, 48 kHz, 10 ms, one 60-byte LC3 frame | 60 B | 1920 B (960 stereo samples) |
| `modeb_48k_7p5ms_60b` | Mode B, 48 kHz, 7.5 ms, `[L 60 B][R 60 B]` | 120 B | 1440 B (720 stereo samples) |
| `modeb_48k_10ms_60b` | Mode B, 48 kHz, 10 ms, `[L 60 B][R 60 B]` | 120 B | 1920 B (960 stereo samples) |

`.pcm` files hold interleaved stereo `int16_t` little-endian: `[L][R][L][R]…`.
Mono expected output duplicates each decoded sample; Mode B expected
output interleaves independently decoded left/right channels.  Left and
right Mode B channel hashes differ.

## Portable BSim corpus

P0a provides four logical streams with 128 continuous LC3 codec frames each.
P1 embeds only their `.lc3` files in the BabbleSim client. Each file is raw
concatenated per-frame LC3 data. The `.pcm` file remains a diagnostic
comparison anchor, not a decoded-output acceptance threshold.

| Stem | Duration | Channel | LC3 bytes/frame | PCM samples/frame | `.lc3` size | `.pcm` size |
|------|----------|---------|----------------:|------------------:|------------:|------------:|
| `bsim_48k_10ms_120b_l` | 10 ms | left | 120 | 480 | 15360 B | 122880 B |
| `bsim_48k_10ms_120b_r` | 10 ms | right | 120 | 480 | 15360 B | 122880 B |
| `bsim_48k_7p5ms_90b_l` | 7.5 ms | left | 90 | 360 | 11520 B | 92160 B |
| `bsim_48k_7p5ms_90b_r` | 7.5 ms | right | 90 | 360 | 11520 B | 92160 B |

The corpus source matches former BSim TX arithmetic. For logical sequence `seq`,
frame-local sample `i`, and channel `ch` (`left = 0`, `right = 1`):

```c
v = seq ^ ((i + 1U) * 747796405U) ^ ((uint32_t)ch << 24);
v = bsim_tx_hash_mix(v);
sample = (int16_t)(v & 0xFFFFU);
```

`bsim_tx_hash_mix_seq_i_ch_v1` identifies this formula in
`portable-oracle-manifest.json`. Each stream keeps one encoder and one decoder
alive across all 128 frames, preserving codec history.

The manifest is schema version 2. It records NCS `v3.3.0`, liblc3 semantic
label `1.1.2`, west revision
`48bbd3eacd36e99a57317a0a4867002e0b09e183`, exact generator flags, geometry,
binary SHA-256 values, and the sole calibration-policy object: maximum absolute
error `2048`, maximum RMS error `512`, and minimum correlation Q15 `32750`.
Calibration validates this policy without changing BSim or decoder acceptance;
P2 owns its first gate use.

### P1 BSim transport contract

`tests/bsim/client/CMakeLists.txt` uses Zephyr's
`generate_inc_file_for_target()` for exactly these four `.lc3` files. The
client has no runtime LC3 encoder. It validates 48 kHz geometry, accepts only
10 ms/120-byte or 7.5 ms/90-byte frames, and rejects a sequence at or above
128 instead of wrapping or reusing corpus data.

Mono and Mode A append one selected frame. Mode B appends the left frame then
the right frame for one logical sequence. The malformed-SDU scenario sends
sequence 20 as the valid 120-byte left frame with its final byte removed. It
does not use a synthetic payload.

For each successful `bt_bap_stream_send()`, client evidence updates unsigned
32-bit FNV-1a from offset basis `0x811C9DC5` and prime `0x01000193`. The hash
input is four little-endian logical-sequence bytes followed by the exact final
SDU bytes. Client `PASS` records emit retained `txc0`/`txh0` and
`txc1`/`txh1`; audit records survive unregister so reconnect evidence retains
stream 0 and proves stream 1 restarts at sequence zero.

`scripts/bsim_stage1_parse.py` independently calls the strict manifest loader
before deriving expected hashes. Scenario schema 2 identifies each transport
layout and fixture stem. The parser rejects wrong channel selection, Mode B
ordering, malformed-byte substitution, omission, duplication, reordering,
payload corruption, count drift, and corpus exhaustion without using
CPU-dependent decoded-PCM hashes.

`invalid_sdu_resume_10ms` is one bounded temporary PCM gap. Its schema omits
`known.full`, but retains `known.total = 108`, exact client transport hash and
101-send count, one malformed observer event, one decoder error, 100 resumed
pushes, and all lifecycle checks. Former runtime TX skipped LC3 encoder history
for logical frame 20; corpus TX encodes frame 20 before truncating that SDU to
119 bytes, so later valid decoder history changes. P2 owns portable numerical
PCM acceptance for this path.

| File | SHA-256 |
|------|---------|
| `bsim_48k_10ms_120b_l.lc3` | `c16222f9d0e107488a1aec502d1bbb5a4c6e3944ce28b7f886c55415f51130be` |
| `bsim_48k_10ms_120b_l.pcm` | `b42fd31158d24d255214580a63261087d57322266563cec778abc1d9f7f830a7` |
| `bsim_48k_10ms_120b_r.lc3` | `0b31725f64b9857c2b5c32e2370ef67e6d0623c43662cd66ce4c93042286fde0` |
| `bsim_48k_10ms_120b_r.pcm` | `7383216bd256165633da98a8a3e8af31bd2e1c4903001cf7ad749f87db18c8ca` |
| `bsim_48k_7p5ms_90b_l.lc3` | `2a10e889d1b460d06b75391c8ba7c4bab76ad124b366a8d184e6c37e1ff6264e` |
| `bsim_48k_7p5ms_90b_l.pcm` | `1e720b1f4f782f50b16b1e173a3fde0ba0bcd972a550d5a8b2a70282a37ffac6` |
| `bsim_48k_7p5ms_90b_r.lc3` | `5d5a8fcb2b573804a27d81c01d07756431b22592725e7ee126b23db07c0e3a7e` |
| `bsim_48k_7p5ms_90b_r.pcm` | `112a32ac37ab26ae50b7dfc3db082d06f3d0c5c866532306112e2d94a45914dd` |

## Stateful decoder-history references

`stateful-reference-manifest.json` is schema version 1. It binds exact ordered
decoder-history recipes to `portable-oracle-manifest.json` schema version 2,
including source-manifest provenance, source geometry, action counts, reference
kind, backing-file size, and SHA-256. The portable manifest remains sole owner
of numerical PCM limits.

Each recipe replays its measured Stage 1 decoder history. PLC outputs advance
decoder history but are not stored or compared numerically. Corpus outputs are
stored in recipe order. Mode A 7.5 ms has 113 actions per channel: left has PLC
actions 0 through 11, corpus frame 0 at action 12, then corpus frames 1 through
100; right has PLC actions 0 through 9, corpus frame 0 at action 10, PLC
actions 11 and 12, then corpus frames 1 through 100. First fully source-valid
sink push is action 13.

| Recipe | Exact decoder action history | Reference |
|--------|------------------------------|-----------|
| `start8_10ms_l` | 8 PLC, corpus 0 through 99 | portable 10 ms left PCM, frame 0 |
| `start8_10ms_r` | 8 PLC, corpus 0 through 99 | portable 10 ms right PCM, frame 0 |
| `start11_7p5ms_l` | 11 PLC, corpus 0 through 99 | portable 7.5 ms left PCM, frame 0 |
| `start11_7p5ms_r` | 11 PLC, corpus 0 through 99 | portable 7.5 ms right PCM, frame 0 |
| `modea_start_7p5ms_l` | 12 PLC, corpus 0 through 100 | portable 7.5 ms left PCM, frame 0 |
| `modea_start_7p5ms_r` | 10 PLC, corpus 0, 2 PLC, corpus 1 through 100 | generated trace |
| `skip20_10ms_l` | 8 PLC, corpus 0 through 19, then 21 through 100 | generated trace |
| `loss48x18_10ms_r` | 8 PLC, corpus 0 through 47, 18 PLC, then 48 through 81 | generated trace |
| `start7_10ms_l` | 7 PLC, corpus 0 through 99 | portable 10 ms left PCM, frame 0 |

Fresh-decoder startup PLC produces silence in pinned liblc3 and first valid
decode resets that state. Therefore six normal-start recipes reference existing
portable PCM directly and do not duplicate its bytes. The Mode A 7.5 ms right
recipe decodes corpus frame 0 before two PLC actions, so later source-valid
output carries state that portable PCM cannot represent. It and other
state-changing histories have generated traces:

| File | Size | SHA-256 |
|------|-----:|----------|
| `stateful_48k_7p5ms_modea_start_r.pcm` | 72720 B | `d76724f3392321a4ce959a00867ae40d82bcb93854099ec5d9abc8c01239d858` |
| `stateful_48k_10ms_skip20_l.pcm` | 96000 B | `cead2e59efb32c78cb87818c710ca727082fd9bb9137bb8255b4f1e37d9be024` |
| `stateful_48k_10ms_loss48x18_r.pcm` | 78720 B | `19087061a5f3d74d6d9631b7c5ba100fce358615cbffde322692ae65cc6e91be` |

The three generated traces total 247440 B. Calibration proves every source stream
has 128 distinct LC3 payloads and same-duration left/right streams have no
byte-identical payload. Exact payload bytes select fixture sequence; receiver
controller sequence remains diagnostic only.

Regenerate or verify stateful traces from any directory with:

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh
```

Default mode generates all three traces into temporary files, validates all
checked-in source and stateful hashes, and requires byte-identical candidates.
It does not rewrite checked-in traces. Intentional stateful-reference review
uses:

```bash
bash tests/fixtures/lc3/generate_stateful_references.sh --rebase-stateful
```

Rebase mode still rejects source corpus or existing checked-in trace mismatch.
It stages all three generated traces before a rollback-protected replacement
transaction and prints a reminder to update this README and the stateful
manifest. Existing destinations retain their permissions and backups. Only the
manifest-declared Mode A 7.5 ms generated trace may be absent during explicit
rebase; it is created with normal non-executable permissions and removed if a
later transaction step fails. Neither mode edits a manifest.

## ARM calibration image

`tests/calibration/lc3_pcm_oracle` is a standalone diagnostic Zephyr image
for PB-031 ARM measurement. It embeds all eight portable-corpus files and three
generated stateful PCM traces in flash, uses the checked-in integer
`pcm_oracle` comparator unchanged, reads portable-manifest-owned limits at
configure time, validates the stateful manifest and backing hashes at configure
time, validates all nine recipes, and keeps bounded static work buffers plus one
liblc3 decoder state in RAM. It does not change production behavior.

The reviewed schema-1 calibration rerun uses `CONFIG_MAIN_STACK_SIZE=8192`.
The first ARM execution resolved its main stack to 1024 bytes and faulted in
liblc3 SNS spectral shaping before its first metric. The 8192-byte value is a
conservative repair configuration, not an accepted stack high-water mark.
Reviewed fault-free UART captures report remaining main-stack space before
threshold work proceeds. This repair does not disable stack-protection or
assertion settings.

The image enables `CONFIG_THREAD_ANALYZER=y`,
`CONFIG_THREAD_ANALYZER_USE_PRINTK=y`, and `CONFIG_THREAD_NAME=y`. It calls
`thread_analyzer_print(0U)` after all 39 metric records and immediately before
the PASS record. `CONFIG_THREAD_ANALYZER_AUTO` remains disabled, so no periodic
analyzer thread changes the measurement run. Thread names make the report's
`main` line identify the main-thread `unused` and `usage` values.

Build it from the repository root with the installed NCS v3.3.0 toolchain:

```bash
nix develop -c west build --no-sysbuild \
  -b xiao_nrf54l15/nrf54l15/cpuapp \
  -d /tmp/opencode/pb031-p0c-arm-calibration \
  tests/calibration/lc3_pcm_oracle -p
```

The build config calculates the raw manifest SHA-256 and SHA-256 values for
the calibration `main.c`, `pcm_oracle.c`, `pcm_oracle.h`, stateful manifest,
and stateful recipe C/header at CMake configure time. It disables Zephyr logging
and boot banner output. Hardware execution is reviewed orchestration work: start
UART capture before any target action, then capture one complete record sequence.
Do not use this build command as a flash command.

Normal UART output is ASCII. PB-031 records have this order; Zephyr's
human-readable thread-analyzer report appears after the 39 metric records and
before PASS:

```text
PB031_ARM_BEGIN schema=3 manifest_sha256=<64 lowercase hex> ncs=v3.3.0 liblc3=48bbd3eacd36e99a57317a0a4867002e0b09e183 max_abs_error=2048 max_rms_error=512 min_correlation_q15=32750
PB031_ARM_SOURCE main_c_sha256=<64 lowercase hex> pcm_oracle_c_sha256=<64 lowercase hex> pcm_oracle_h_sha256=<64 lowercase hex> stateful_manifest_sha256=<64 lowercase hex> stateful_recipe_c_sha256=<64 lowercase hex> stateful_recipe_h_sha256=<64 lowercase hex>
PB031_METRIC {"record":"metric",...}
... exactly 39 PB031_METRIC lines, each ending with an `evaluation` string ...
Thread analyze:
 main                 : STACK: unused <bytes> usage <bytes> / 8192 (<percent> %); CPU: <percent> %
... other thread-analyzer lines ...
PB031_ARM_PASS metrics=39
```

Thread-analyzer lines are not PB-031 protocol records. Retain the complete
report and record the line labeled `main` when reviewing ARM stack headroom.

### Reviewed schema-1 ARM calibration evidence

The repaired nRF54L15 calibration image was reviewed with fresh identity
evidence at `/tmp/opencode/pb031-calibration/arm-identity-stack-fix.log`:
CMSIS-DAP serial `8EE9B3FF`, DPIDR `0x6ba02477`, AP IDRs `0x84770001`,
`0x84770001`, `0x32880000`, `0x00000000`, PART `0x00054b15`, and VARIANT
`0x41414330` (`AAC0`). The repaired flash log is
`/tmp/opencode/pb031-calibration/arm-stack-fix-flash.log`; it records 592680
bytes downloaded and verified.

Run 1 raw UART evidence is
`/tmp/opencode/pb031-calibration/arm-stack-fix-console.log`, SHA-256
`4d71bbfa7e10ebfad62edfa6cee862c4b10789a1af61717cd29ef1df5711bdb8`.
It contains exact BEGIN and SOURCE records, 22 metrics, and
`PB031_ARM_PASS metrics=22`, with no fault, FAIL, or error. Its main stack
used `2816 / 8192` bytes, leaving `5376` bytes (`34%` used).

Repeat identity and reset evidence are
`/tmp/opencode/pb031-calibration/arm-identity-repeat.log` and
`/tmp/opencode/pb031-calibration/arm-repeat-reset.log`. Run 2 raw UART
evidence is `/tmp/opencode/pb031-calibration/arm-repeat-console.log`, SHA-256
`fe6f2d7ff4889a929773b8e2a3e58784877bb913ac1a01b9603a72df67cde34c`.
It has the same 22 records, PASS result, and `2816 / 8192` main-stack usage.
Machine comparison reports `run1_metrics=22 run2_metrics=22 equal=True`.
CPU-cycle telemetry differs between runs and is not a calibration metric.

The four valid records report `max_abs_error=1`, `rms_error=1`, and
`correlation_q15=32767`. Current diagnostic controls remain strongly separated:
channel swap has max error `65535`, RMS at least `21686`, and correlation
`-202` or `108`; prior and next frame shifts have max error `65535`, RMS at
least `21604`, and correlation `79..300`; dead channel has max error `32768`,
RMS at least `15283`, and correlation `0`; low-correlation synthetic has max
error `65535`, RMS at least `36158`, and correlation `-9..-2`. These are P0a
diagnostic facts, not acceptance thresholds or complete parent-plan adversarial
coverage. At capture time, thresholds remained unset.

The original 1024-byte main-stack fault remains superseded diagnostic evidence.
Production receiver cpuapp and FLPR were rebuilt and restored afterward.
Evidence is retained at `/tmp/opencode/pb031-calibration/production-restore-identity.log`,
`/tmp/opencode/pb031-calibration/production-restore-build.log`,
`/tmp/opencode/pb031-calibration/production-restore-flash.log`, and
`/tmp/opencode/pb031-calibration/production-restore-console.log`, SHA-256
`e0e59ade36e08cfe24d38c4db050d5a7928a69d0274200f14c4a31609f750bf3`.
Boot reached BLE ready, `settings_load() OK`, audio timing and I2S ready, FLPR
READY with rings and runtime ready, then advertising. Restore-build output has
repository-known documented diagnostics only: dirty-worktree notice, nRF54L15
watchdog no-sources CMake diagnostic, and Zephyr `__ASSERT()` globally enabled
CMake diagnostic. It is not a warning-free build.

Metric JSON uses this field order: `record`, `comparison`, `stem`,
`reference_stem`, `squared_error`, `actual_energy_scaled`,
`reference_energy_scaled`, `dot_product_scaled`, `samples`, `frames`,
`max_abs_error`, `rms_error`, `correlation_q15`, and `evaluation`. Records are
four `valid`, two `channel-swap`, then `prior-frame-shift`,
`next-frame-shift`, `dead-channel`, and `low-correlation-synthetic` for each
manifest stream in order, followed by `lc3-byte-corruption`,
`max-error-boundary`, `rms-error-boundary`, and `correlation-boundary` for the
10 ms left stream. Schema-3 appends nine `stateful-valid` records in recipe
table order, then `stateful-payload-off-by-one`, `stateful-skip-ignored`,
`stateful-loss-burst-omitted`, and `stateful-wrong-channel`. The appended ninth
record is `start7_10ms_l`: 7 PLC actions followed by corpus frames 0 through
99, with portable 10 ms left PCM from frame 0. Every stateful valid record
evaluates `pass`; each stateful mutation evaluates `max-error`.

On first error, the image emits one line and no PASS line:

```text
PB031_ARM_FAIL stage=<token> stream=<stem-or-none> code=<integer>
```

## Generation modes

```bash
bash tests/fixtures/lc3/generate.sh
```

Default mode validates every checked-in portable file against
`portable-oracle-manifest.json` before compiler lookup. It then generates into
a temporary directory, verifies legacy output hashes and generated portable
manifest hashes, and copies only validated portable files. It never rewrites a
legacy fixture.

Reject conditions include malformed manifest data, missing or extra `bsim_*`
streams, unsafe paths, geometry mismatch, and size or SHA-256 mismatch. Failed
validation leaves checked-in files unchanged.

For reviewed intentional portable-corpus rebases only:

```bash
bash tests/fixtures/lc3/generate.sh --rebase-portable
```

`--rebase-portable` permits only generated portable SHA-256 mismatches. It does
not bypass checked-in portable validation, manifest/schema/path/size checks, or
legacy fixture protection. It copies candidate portable files and emits a loud
reminder to review and update SHA-256 records in this README and
`portable-oracle-manifest.json`. The script never updates those integrity
records itself.

Both modes compile `gen_fixtures.c` with the host C compiler and the liblc3
module sources from NCS v3.3.0 (`~/ncs/v3.3.0/modules/lib/liblc3`), using the
same relevant flags as the Zephyr liblc3 module build:

```text
-O3 -std=c11 -ffast-math -Wall -Wextra -Wdouble-promotion -Wvla -pedantic -Werror
```

`-Wno-array-bounds` is intentionally NOT used. `-Werror` requires the
generator to compile warning-free and no suppression may be added without a
recorded compiler diagnostic. The generator calls the installed liblc3 C API
(`lc3_setup_encoder()` / `lc3_encode()`, and independently
`lc3_setup_decoder()` / `lc3_decode()` with one encoder and one decoder
instance per legacy channel or portable stream), writes the PCM explicitly
little-endian, and removes its temporary executable via `mktemp` + EXIT trap.
Generation is deterministic and path-independent (verified by repeated runs
from clean copies producing identical hashes).

`generate.sh` records hashes for every legacy fixture and fails if any of those
eight binaries changes. It prints every legacy and portable `.lc3`/`.pcm`
SHA-256 after a successful run.

## Source PCM formulas

Deterministic integer-generated, distinct left/right patterns (sample
index `i`, 0-based).  All arithmetic is defined: the index converts to
`uint32_t` before multiplication and the constants are `UINT32_C`
(explicit unsigned wrap):

```text
L(i) = (int16_t)(((uint32_t)i * UINT32_C(1103515245) + UINT32_C(12345)) >> 12)
R(i) = (int16_t)(((uint32_t)i * UINT32_C(2654435761) + UINT32_C(67890)) >> 12)
```

Mono encodes `L` only (expected output duplicates the decoded sample).
Mode B encodes `L` and `R` with independent encoder instances.  The
defined arithmetic produces byte-identical fixtures to the original
formulation on the pinned toolchain (verified: two clean-copy runs,
all SHA-256/CRC-32 unchanged).

## SHA-256 (checked-in binaries)

| File | SHA-256 |
|------|---------|
| `mono_48k_7p5ms_60b.lc3` | `a92ba8c193b0376124beeefef984c48c11efbaeb6fbe0e16d5313ca7fdf35378` |
| `mono_48k_7p5ms_60b.pcm` | `8541ab2bb907c50accc7b58513f29561fcda7f9d96e64b61067f4cb7b3937763` |
| `mono_48k_10ms_60b.lc3` | `c944197208019d44c3f36fa7cb7cb194573fa38094ae7a0b6a6793c23c088104` |
| `mono_48k_10ms_60b.pcm` | `7740e597bbace677f108a60739663238b8d531babf70ac6cd4feda60bf4df4a3` |
| `modeb_48k_7p5ms_60b.lc3` | `35ca817cc0b079041a48c09308da6067bb3fbf4dbf36862e9b011cd266349733` |
| `modeb_48k_7p5ms_60b.pcm` | `a5df6d089798fe714edb9a4d5c8910f917e4132bb33aad2b9513a9805357f534` |
| `modeb_48k_10ms_60b.lc3` | `30d880e4792a4c3c6c04bbf55b2f59a7f355f77869a41569cb65186eec609829` |
| `modeb_48k_10ms_60b.pcm` | `a2c16351c1e7c4e7dc49bdf9dc6b91705099d4b9fcb6d87c27f0d8228ac12c7a` |

## CRC-32 (IEEE 802.3, `crc32_ieee()`) asserted by the decode tests

CRC-32 over the full interleaved PCM byte stream, over the left-channel
samples, and over the right-channel samples (int16 little-endian bytes):

| Fixture | full | left | right |
|---------|------|------|-------|
| `mono_48k_7p5ms_60b` | `E272CD4C` | `62AD330F` | `62AD330F` |
| `mono_48k_10ms_60b` | `D546D96C` | `A7D0F060` | `A7D0F060` |
| `modeb_48k_7p5ms_60b` | `446235E4` | `62AD330F` | `77673426` |
| `modeb_48k_10ms_60b` | `6669E859` | `A7D0F060` | `D0036A17` |

Mono left and right CRCs are equal by design (duplicated channel); Mode B
left and right CRCs differ (verified by the generator itself).

Golden PCM is exact for the pinned NCS v3.3.0 liblc3 1.1.2 and the
native_sim host toolchain.  No audio-quality claim is made from these
fixtures.
