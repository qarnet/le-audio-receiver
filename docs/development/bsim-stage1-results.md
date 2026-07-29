# BabbleSim Stage 1 — final fidelity results

**Date:** 2026-07-29
**NCS version:** v3.3.0

## Changes applied

Per `bsim-stage1-final-fidelity-handoff.md`:

1. **CONFIG_TEST decode bypass removed** — `bt_bap.c` no longer skips invalid SDUs
   in BSIM builds.  Invalid frames flow through the same PLC/decode path as
   production hardware.  Startup transient produces PLC frames during CIS sync
   (7 PLC frames, all occurring before first nonzero PCM).

2. **Startup-zero/PLC oracle** — `audio_sink_stub.c` rewritten:
   - `startup_zero` tracks zero-energy pushes during CIS startup (before first
     nonzero PCM).  Each zero-energy push snapshots current `audio_stats.plc_frames`
     as `startup_plc`.
   - After first nonzero PCM, zero-energy push = immediate FAIL.
   - At PASS (100 nonzero pushes):
     - `decode_errors == 0`
     - `malformed == 0`, `after_stop == 0`
     - `plc_frames == startup_plc` (all PLC in startup phase)
     - `total_frames == nonzero_pushes + startup_zero`
     - Ordered FNV-1a hash nonzero, not FNV seed
     - Energy min/max positive and deterministic
   - Reports `startup_zero`, `startup_plc`, `plc`, `total`, `hash`, `energy`.

3. **Audio stats extended** — `audio_stats.h` gains `startup_zero` and
   `startup_plc` fields, plus `audio_stats_startup_zero()` /
   `audio_stats_startup_plc_snapshot()` functions.  Reset clears both fields.

4. **Client ASE count** — `ASE_SNK_COUNT=2` preserved (required for
   `BT_AUDIO_TX` enable).  `ASE_SRC_COUNT=2` preserved — attempted 0 but
   NCS v3.3.0 BAP client requires ≥1 Source ASE for unicast group `tx_param`
   binding: zero Source ASEs breaks TX stream creation.

5. **Runner hardened** — `bsim-stage1-run.sh`:
   - Removed `plc=0` check (PLC during startup is valid).
   - Added `plc == startup_plc` invariant.
   - Added `total_frames == pushes + startup_zero` invariant.
   - `startup_zero` and `startup_plc` parsed and reported.
   - Stale `NCS_ROOT` double-dirname removed.

6. **Official smoke** — `bsim-official-smoke.sh`:
   - Duplicate exit-code echo removed.
   - Stale `NCS_ROOT` double-dirname removed.
   - Exits nonzero on upstream teardown disable-race → **Baseline PARTIAL**.

## Simulation results — two consecutive runs

### Run 1 — bsim_stage1_63394

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=7 total=108 malformed=0 after_stop=0 startup_zero=8 startup_plc=7 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

### Run 2 — bsim_stage1_66843

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=7 total=108 malformed=0 after_stop=0 startup_zero=8 startup_plc=7 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

## Invariant verification

| Invariant | Run 1 | Run 2 |
|-----------|-------|-------|
| startup_zero | 8 | 8 |
| startup_plc | 7 | 7 |
| final plc_frames | 7 | 7 |
| plc == startup_plc | ✓ (7=7) | ✓ (7=7) |
| total == pushes + startup_zero | ✓ (108=100+8) | ✓ (108=100+8) |
| decode_errors | 0 | 0 |
| malformed | 0 | 0 |
| after_stop | 0 | 0 |
| nonzero | 1 | 1 |
| energy_min | 12480 | 12480 |
| energy_max | 12480 | 12480 |
| hash (FNV-1a) | 0xFE0D4245 | 0xFE0D4245 |
| receiver exit 0 | ✓ | ✓ |
| client exit 0 | ✓ | ✓ |
| PHY exit 0 | ✓ | ✓ |
| client TX ≥ 100 | ✓ (104) | ✓ (104) |

Both runs fully deterministic: identical startup_zero, startup_plc, PLC, total,
hash, and energy.

## Real-target build regression

| Target | Build | Flash/RAM |
|--------|-------|-----------|
| nRF5340 (ebyte_e83) | ✓ | FLASH 146780 B / 256 KB (55.99%), RAM 40512 B / 64 KB (61.82%) |
| nRF54L15 (nrf54l15dk) | ✓ | FLASH 502952 B / 1428 KB (34.40%), RAM 152252 B / 160 KB (92.93%) |

CONFIG_TEST removal has zero effect on production hardware (CONFIG_TEST=n on
both targets).  No warnings, no regressions.

## Official smoke

- **Exit:** non-zero (teardown disable-race)
- **Label:** Baseline PARTIAL
- Upstream unicast_client/unicast_server full lifecycle: 185 frames streamed
  before the known disable-race triggers ISO receive lost.

## Files changed

```
docs/development/bsim-stage1-results.md       | 102 ++++++++----
scripts/bsim-official-smoke.sh                |   7 +-
scripts/bsim-stage1-run.sh                    |  41 +++--
src/audio_stats.c                             |  17 ++-
src/audio_stats.h                             |   9 +-
src/bt_bap.c                                  |  12 --
tests/bsim/src/audio_sink_stub.c              | 131 ++++++++++++-----
7 files changed, 215 insertions(+), 104 deletions(-)
```

## Reproduction

```bash
source scripts/bsim-env.sh
bash scripts/bsim-stage1-run.sh
```

Two runs produce identical validated output: `hash=0xFE0D4245`,
`energy=12480`, `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`.
