# Stage 4A reset-release ordering fix

## Root cause hypothesis grounded in register semantics

Current code releases NDMRESET before reload/INITPC/CPURUN. sQSPI sequence is
shutdown-only and resets DEBUGIF by setting DMACTIVE disabled. Next experiment
holds system reset active through preparation and releases it as final launch
edge while DEBUGIF remains active.

## Exact sequence replacement

1. Deregister endpoint.
2. `nrf_vpr_cpurun_set(vpr, false)`.
3. Set DMCONTROL `NDMRESET=Active, DMACTIVE=Enabled`; do not release yet.
4. While reset asserted: copy 64 KiB, flush, barriers, CRC verify.
5. Set INITPC.
6. Register CPUAPP endpoint.
7. Set CPURUN true.
8. Final launch: set DMCONTROL `NDMRESET=Inactive, DMACTIVE=Enabled`.
9. Wait bound and new READY.

Never write DMACTIVE disabled in runtime restart. No separate VEVIF reset.

Add runtime stage enum/status and HAL getter snapshots for CPURUN, INITPC,
NDMRESET, DMACTIVE after assert/config/release. Shell prints them and exact failed
stage. Use only nrfx getter APIs.

Add mocked operation-order tests covering success and every failed stage. Keep
source CRC before stop if hardware requires it; execution CRC remains after
copy. Test failure cleanup leaves reset asserted/core stopped and endpoint state
known, allowing next restart to deregister/re-register cleanly.

Hardware: first one idle restart. If bound+new READY succeeds, run total three
with distinct epochs, CRC, stress/ring gates, then Mode A 60 s. If first fails,
stop and return stage/readbacks/log—do not try alternate sequence. Both builds,
tests, docs, new commit. No live audio recovery/security/direct registers/WDT/
HPF/BabbleSim/push/amend/mass erase/install.
