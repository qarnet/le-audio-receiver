# Phase 6 Stage 3 — hardware acceptance results

All gates passed.

## Build status

| Target    | Status  | Kconfig                       |
|-----------|---------|-------------------------------|
| nRF54L15  | CLEAN   | CPUAPP 152020 B RAM, FLPR 43200 B RAM |
| FLPR      | CLEAN   | 1 MHz cycles, soft-float      |
| nRF5340   | CLEAN   | 136464 B RAM, 364036 B FLASH  |

- Verification build: `fw-build-54l15 -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y` → VERIFY=y confirmed on cpuapp, 1 MHz cycles on both cores.
- Production build: `fw-build-54l15` (VERIFY unset).
- nRF5340: `fw-build-5340` — clean.

## Verification build (VERIFY=y, nRF54L15)

### Mode A — 120 s, 2 mono ASEs (stereo_a)

| Metric           | Value                     |
|------------------|---------------------------|
| Central frames   | 12,000 @ 100.0 fps        |
| Offload submits  | 12,030 / 12,030 success   |
| Fallback         | 0                         |
| Timeout/full/stale/seq/frame/CRC/payload faults | 0/0/0/0/0/0/0 |
| verify_fault     | 0                         |
| state_fault      | 0                         |
| RTT min/max/avg  | 1826 / 2040 / 1874 cyc    |
| FLPR cyc min/max/avg | 989 / 1052 / 1009 cyc  |
| Audio errors     | 0 decode, 0 I2S, 0 resets |
| Push failures    | 0                         |
| ASRC cap fail    | 0                         |
| Repeat fb        | 1 (initial PREPARING)     |

### Mode B — 120 s, stereo single ASE (stereo_b)

| Metric           | Value                     |
|------------------|---------------------------|
| Central frames   | 12,000 @ 100.0 fps        |
| Offload submits  | 12,031 / 12,031 success   |
| Fallback         | 0                         |
| All faults       | 0 (all categories)        |
| verify_fault     | 0                         |
| state_fault      | 0                         |
| RTT min/max/avg  | 1817 / 2094 / 1859 cyc    |
| FLPR cyc min/max/avg | 988 / 1052 / 1008 cyc  |
| Audio errors     | 0                         |
| Push failures    | 0                         |
| Repeat fb        | 0                         |

### Shadow verification

All 24,061 CPU-vs-FLPR ASRC shadow comparisons: **zero mismatches** across return code, frame count, every sample, and post-state continuity.

## Production build (VERIFY=n, nRF54L15)

### Mode A — 600 s, 2 mono ASEs (stereo_a)

| Metric           | Value                     |
|------------------|---------------------------|
| Central frames   | 60,000 @ 100.0 fps        |
| Offload submits  | 60,022 / 60,022 success   |
| Fallback         | 0                         |
| All faults       | 0 (all categories)        |
| verify_fault     | 0                         |
| state_fault      | 0                         |
| RTT min/max/avg  | 1822 / 2082 / 1887 cyc    |
| FLPR cyc min/max/avg | 988 / 1052 / 1005 cyc  |
| Audio errors     | 0                         |
| Push failures    | 0                         |
| Repeat fb        | 12 (initial PREPARING window) |
| PI ppm range     | 2144–2434 (from diag output) |
| Perf iso_recv max| 4105 cyc (41.0% deadline) |
| Perf sink_push max| 2366 cyc (23.6% deadline) |

### Mode B — 600 s, stereo single ASE (stereo_b)

| Metric           | Value                     |
|------------------|---------------------------|
| Central frames   | 60,000 @ 100.0 fps        |
| Offload submits  | 60,032 / 60,032 success   |
| Fallback         | 0                         |
| All faults       | 0 (all categories)        |
| verify_fault     | 0                         |
| state_fault      | 0                         |
| RTT min/max/avg  | 1818 / 2148 / 1867 cyc    |
| FLPR cyc min/max/avg | 986 / 1052 / 1006 cyc  |
| Audio errors     | 0                         |
| Push failures    | 0                         |
| Repeat fb        | 1 (initial PREPARING)     |
| PI ppm range     | 1831–2197 (from diag output) |
| Perf iso_recv max| 5292 cyc (52.9% deadline) |
| Perf sink_push max| 2431 cyc (24.3% deadline) |

### Combined production summary (1,200 s, 120,000 frames)

- **ASRC offload**: 120,054 successful submits, 0 fallback, 0 faults of any category
- **FLPR ASRC cycles**: max 1,052 cyc (well under 8,000 limit)
- **RTT (round-trip)**: max 2,148 cyc (well under 8,000 limit)
- **Audio**: 0 decode errors, 0 I2S underruns, 0 stream resets
- **I2S output**: 120,052 blocks pushed to I2S, 0 push failures, 0 ASRC capacity failures
- **Offload state**: ACTIVE after initial PREPARING (≤13 repeat fallbacks), no recovery/relapse/exhaustion

## Acceptance gate summary

| Gate                                | Result     |
|-------------------------------------|------------|
| Central 100 fps, exact frame count  | PASS       |
| verify_fault = 0                    | PASS       |
| All integrity/category faults = 0   | PASS       |
| FLPR processing status = 0          | PASS       |
| No I2S/decode/push/capacity faults  | PASS       |
| Initial PREPARING fallback allowed  | PASS (counted, steady stops) |
| Offload success after preparation   | PASS       |
| Zero timeout/full/stale/seq/frame/CRC/state faults | PASS |
| Zero recovery/exhaustion            | PASS       |
| FLPR ASRC cycles max < 8,000        | PASS (1,052) |
| RTT max < 8,000 cycles              | PASS (2,148) |
| nRF5340 rebuild (E83 absent)        | PASS (build clean, HW unavailable) |

## Commands

```bash
# Verification build + flash
fw-build-54l15 -DCONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y
fw-flash-54l15

# Central: normal BlueZ discovery
python3 scripts/bap_central.py --duration 120       # Mode A
python3 scripts/bap_central.py --stereo --duration 120  # Mode B

# Query commands (before disconnect)
flpr offload
audio status
audio perf

# Production build + flash
fw-build-54l15
fw-flash-54l15

# Production runs
python3 scripts/bap_central.py --duration 600       # Mode A
python3 scripts/bap_central.py --stereo --duration 600  # Mode B

# Regression
fw-build-5340

# Bond cleanup (when needed)
bt unpair                         # receiver shell
sudo bluetoothctl -- remove <addr>  # central side
```
