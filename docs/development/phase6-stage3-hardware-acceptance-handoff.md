# Phase 6 Stage 3 hardware acceptance

## Verification build

1. Pristine nRF54 build with temporary
   `CONFIG_AUDIO_OFFLOAD_ASRC_VERIFY=y`; confirm resolved option y and both
   CPUAPP/FLPR `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=1000000`.
2. Flash CPUAPP+FLPR. Clean bonds only when needed. Use normal BlueZ discovery
   and Pair-before-ACL; never `--peer-addr`.
3. Mode A 120 s, then true Mode B (`--stereo`) 120 s. Capture console before
   reset and query `flpr offload`, `audio status`, `audio perf`, ring and FLPR
   status before each disconnect.
4. Require verify_fault=0, every integrity/category fault zero, processing
   status zero, no I2S/decode/push/capacity fault, central exactly 100 fps.
   Initial PREPARING cpu fallback is allowed and counted; steady ACTIVE fallback
   must stop increasing.

## Production build

1. Pristine normal nRF54 build with VERIFY unset/n; flash both images.
2. Mode A 600 s and true Mode B 600 s, normal BlueZ path. Query full status at
   start, midpoint, and before disconnect.
3. Require each central run 60,000 frames/600 s/100 fps; offload success resumes
   every block after preparation; zero timeout/full/stale/seq/frame/CRC/state/
   verify faults; zero recovery/exhaustion; zero audio faults.
4. FLPR ASRC cycles max and total RTT max must stay below 8,000 cycles (1 MHz =
   microseconds). Record min/max/avg/count, output frame range from audio perf,
   PI ppm range, queue depth, CPU callback/sink timings, CPUAPP+FLPR RAM.

## Regression

- Rebuild nRF5340 normally; no FLPR symbol/path, clean build. Hardware absent is
  recorded as unavailable, not passed.
- Update Stage 3 results with raw logs/counters and exact commands. Commit docs
  only after gates pass. No security/recovery-policy/code changes, push, mass
  erase, HPF, BabbleSim, or analog claim.
