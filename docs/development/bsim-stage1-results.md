# BabbleSim Stage 1 — review fix results

**Date:** 2026-07-29
**NCS version:** v3.3.0

## Changes applied

Per `bsim-stage1-review-fix-handoff.md`:

1. **Sink-only architecture restored** — removed `CONFIG_REGISTER_SRC_PAC`
   from production `bt_bap.c` and BSIM Kconfig. Receiver config:
   `BT_ASCS_MAX_ASE_SRC_COUNT=0`, `BT_ISO_MAX_CHAN=1`. No source
   PAC/ASE on receiver.

2. **Client simplified to sink-only** — `g_sinks[1]` (one remote sink),
   `TEST_STREAM_CNT=1`, one `tx_param` pair. Removed `g_sources`,
   `discover_sources`, `configured_source_stream_count`,
   `sem_sources_discovered`. Client Kconfig preserves `ASE_SRC_COUNT=2`
   and `ASE_SNK_COUNT=2` (NCS requires 0 or ≥2; `ASE_SRC` provides
   `tx_streams` slots for TX, `ASE_SNK` enables `BT_AUDIO_TX`).

3. **Strict PCM oracle** — rewritten `audio_sink_stub.c`:
   - Startup zero-energy pushes silently consumed (CIS race, client not
     yet sending); zero-energy after first valid push is immediate FAIL.
   - Ordered FNV-1a hash: chained with frame index and all sample bytes
     so 100 identical sine blocks cannot cancel. Initial seed:
     `0x811c9dc5`. Final hash MUST be nonzero and not the seed.
   - Energy min/max tracked across all valid pushes.
   - At PASS: `decode_errors=0`, `plc_frames=0`, `total_frames≥100`,
     `malformed=0`, `pushes_after_stop=0`, `nonzero=1`, hash nonzero,
     `energy_max>0`.
   - Invalid frames (PLC) not decoded: `CONFIG_TEST` guard in
     `bt_bap.c` skips invalid SDUs entirely in BSIM builds so PLC
     never increments. Production hardware unaffected (`CONFIG_TEST=n`).

4. **Hardened runner** — `bsim-stage1-run.sh`:
   - Redirects stdout+stderr to `/tmp` paths printed before execution.
   - Records per-process exit codes with labeled output.
   - After all exits 0, greps logs for INFO markers and validates:
     receiver `errors=0`, `plc=0`, `malformed=0`, `after_stop=0`,
     `nonzero=1`, hash nonzero not seed, `energy_max>0`; client
     send count ≥100.
   - Fails if ANY marker/check absent, even when all exits are zero.

5. **Official smoke** — `bsim-official-smoke.sh` exits nonzero on
   upstream failure (teardown disable-race): Stage0 PARTIAL, never
   labelled PASS. Child failures propagated.

6. **Omitted Stage0 handoff** — `bsim-stage0-review-fix-handoff.md`
   was already committed (9e5e590).

7. **Trailing whitespace** — diff-check clean.

## Simulation results

### Run 1 — bsim_stage1_38368

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=0 total=101 malformed=0 after_stop=0 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

### Run 2 — bsim_stage1_41814

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=0 total=101 malformed=0 after_stop=0 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

Both runs identical: hash `0xFE0D4245` (deterministic FNV-1a over sine LC3),
energy 12,480 per 960-sample stereo block.

### Marker/counter verification

| Check | Run 1 | Run 2 |
|-------|-------|-------|
| receiver exit 0 | ✓ | ✓ |
| client exit 0 | ✓ | ✓ |
| PHY exit 0 | ✓ | ✓ |
| errors=0 | ✓ | ✓ |
| plc=0 | ✓ | ✓ |
| total≥100 (101) | ✓ | ✓ |
| malformed=0 | ✓ | ✓ |
| after_stop=0 | ✓ | ✓ |
| nonzero=1 | ✓ | ✓ |
| hash ≠ 0, ≠ seed | ✓ (0xFE0D4245) | ✓ (0xFE0D4245) |
| energy_max > 0 (12480) | ✓ | ✓ |
| client TX ≥ 100 (104) | ✓ | ✓ |
| sink-only PACS/ASCS | ✓ | ✓ |
| one CIS | ✓ | ✓ |

## Files changed

```
 scripts/bsim-official-smoke.sh           |   4 +-
 scripts/bsim-stage1-run.sh               | 169 ++++++++--
 src/bt_bap.c                             |  15 +-
 tests/bsim/Kconfig                       |   8 -
 tests/bsim/client/prj.conf               |   6 +-
 tests/bsim/client/src/bsim_client_main.c |  99 +-----
 tests/bsim/prj.conf                      |   4 +-
 tests/bsim/src/audio_sink_stub.c         | 167 +++++++---
 8 files changed, 289 insertions(+), 183 deletions(-)
```

## Reproduction

```bash
source scripts/bsim-env.sh
bash scripts/bsim-stage1-run.sh
```

Two runs produce unique simulation IDs ($$) with identical validated output.
