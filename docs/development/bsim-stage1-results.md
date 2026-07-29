# BabbleSim Stage 1 — final fidelity + production cleanup results

**Date:** 2026-07-29
**NCS version:** v3.3.0

## Changes applied

Per `bsim-stage1-final-fidelity-handoff.md` and
`bsim-stage1-production-cleanup-handoff.md`:

1. **CONFIG_TEST decode bypass removed** — `bt_bap.c` no longer skips invalid SDUs
   in BSIM builds.  Invalid frames flow through the same PLC/decode path as
   production hardware.  Startup transient produces PLC frames during CIS sync
   (7 PLC frames, all occurring before first nonzero PCM).

2. **Startup-zero/PLC oracle** — `audio_sink_stub.c` rewritten:
   - `startup_zero` tracks zero-energy pushes during CIS startup (before first
     nonzero PCM) using local file-static counters.
   - Each zero-energy push snapshots current `audio_stats.plc_frames` via
     `audio_stats_get()` into a local `startup_plc` snapshot.
   - After first nonzero PCM, zero-energy push = immediate FAIL.
   - At PASS (100 nonzero pushes):
     - `decode_errors == 0`
     - `malformed == 0`, `after_stop == 0`
     - `plc_frames == startup_plc` (all PLC in startup phase)
     - `total_frames == nonzero_pushes + startup_zero`
     - Ordered FNV-1a hash nonzero, not FNV seed
     - Energy min/max positive and deterministic
   - Reports `startup_zero`, `startup_plc`, `plc`, `total`, `hash`, `energy`.

3. **Production audio_stats cleaned** — `audio_stats.h/.c` restored to pre-BSim
   shape.  `startup_zero` and `startup_plc` fields removed; `audio_stats_startup_zero()`
   and `audio_stats_startup_plc_snapshot()` functions removed.  All startup
   accounting is local to the sink stub.  Real-target public stats/API unchanged.

4. **Client ASE counts** — `ASE_SNK_COUNT=2`, `ASE_SRC_COUNT=2`.  Attempted
   SRC_COUNT=0 (compiles but `stream_tx_register` returns -ENOMEM on zero-element
   `tx_streams[]` sized by `CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT`).
   Attempted SRC_COUNT=1 (upstream `BUILD_ASSERT` at `bap_unicast_client.c:63`
   rejects values between 1 and 1).  Minimum viable is 2.

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

## Simulation results — two consecutive runs (post-cleanup, 2026-07-29)

### Run 1 — bsim_stage1_93558

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=7 total=108 malformed=0 after_stop=0 startup_zero=8 startup_plc=7 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

### Run 2 — bsim_stage1_97012

| Process | Exit | Key output |
|---------|------|------------|
| receiver | 0 | `100 pushes — nonzero=1 errors=0 plc=7 total=108 malformed=0 after_stop=0 startup_zero=8 startup_plc=7 energy_min=12480 energy_max=12480 hash=0xFE0D4245` |
| client | 0 | `104 successful sends (>= 100)` |
| PHY | 0 | — |

## Invariant verification (post-cleanup)

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
hash, and energy.  Identical values to pre-cleanup baseline (hash=0xFE0D4245,
energy=12480), confirming no functional change from moving startup accounting
from production audio_stats to local sink-stub counters.

## Real-target build regression (post-cleanup)

| Target | Build | Flash/RAM |
|--------|-------|-----------|
| nRF5340 (ebyte_e83) | ✓ | FLASH 364036 B / 1008 KB (35.27%), RAM 136464 B / 448 KB (29.75%) |
| nRF54L15 (nrf54l15dk) | ✓ | FLASH 502904 B / 1428 KB (34.39%), RAM 152244 B / 160 KB (92.92%) |

Production `audio_stats` cleanup has zero effect on production hardware.
No warnings, no regressions.

## Official smoke

- **Exit:** non-zero (teardown disable-race)
- **Label:** Baseline PARTIAL
- Upstream unicast_client/unicast_server full lifecycle: 185 frames streamed
  before the known disable-race triggers ISO receive lost.

## Files changed (final fidelity + cleanup)

```
docs/development/bsim-stage1-results.md          | updated (post-cleanup runs)
src/audio_stats.c                                | -13 lines (startup fields removed)
src/audio_stats.h                                | -8 lines (startup API removed)
tests/bsim/client/prj.conf                       | updated (SRC_COUNT rationale)
tests/bsim/src/audio_sink_stub.c                 | startup counters now local
5 files changed
```

## Reproduction

```bash
source scripts/bsim-env.sh
bash scripts/bsim-stage1-run.sh
```

Two runs produce identical validated output: `hash=0xFE0D4245`,
`energy=12480`, `startup_zero=8`, `startup_plc=7`, `plc=7`, `total=108`.

BSIM Stage 1 accepted as regular local gate.  Official upstream smoke
remains PARTIAL (teardown disable-race in upstream BAP unicast test).
Scope stops here — reconnect/Mode A/B/error injection duplicate hardware
coverage and add low value under unmodeled I2S/FLPR.
