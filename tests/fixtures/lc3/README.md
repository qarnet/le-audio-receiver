# LC3 test fixtures

Checked-in, reproducible LC3 bitstream + expected-PCM pairs for the
receiver's four supported decode shapes.  These binaries are embedded by
`tests/unit/decode` (see `docs/testing/t2-audio-pipeline-tests.md`) and
are never regenerated during normal test runs.

## Files

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

## Generation command

```bash
bash tests/fixtures/lc3/generate.sh
```

Compiles `gen_fixtures.c` with the host C compiler and the liblc3 module
sources from NCS v3.3.0 (`~/ncs/v3.3.0/modules/lib/liblc3`), using the
same relevant flags as the Zephyr liblc3 module build:

```text
-O3 -std=c11 -ffast-math -Wall -Wextra -Wdouble-promotion -Wvla -pedantic
```

`-Wno-array-bounds` is intentionally NOT used: the generator compiles
warning-free without it and no suppression may be added without a
recorded compiler diagnostic.  The generator calls the installed liblc3
C API (`lc3_setup_encoder()` / `lc3_encode()`, and independently
`lc3_setup_decoder()` / `lc3_decode()` with one encoder and one decoder
instance per channel), writes the PCM explicitly little-endian, and
removes its temporary executable via `mktemp` + EXIT trap.  Generation
is deterministic and path-independent (verified by repeated runs from
clean copies producing identical hashes).

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
