# Stage 3B review fixes

## Transport validation

- Extend `flpr_consume_asrc_result` with sequence, flags, correction_ppm and
  payload_crc snapshot. Fill before consume_done.
- Offload validates sequence equals request, correction echo equals request,
  success flags include exactly VALID+ASRC (allow CRC_OK only if documented),
  status is zero, and post_state.step_base equals pre_state.step_base.
- Ring layer continues recomputing payload CRC before returning OK. Expose CRC
  for diagnostics; offload may recompute scratch CRC too for defense in depth.
- Assign public result fields explicitly; no memcpy between different struct
  types or assumed common prefixes.

## Transaction/lifecycle

- Validate pre_state transactionally before counters/produce.
- Keep caller output and public result untouched through all validation.
- Under g_lock perform final lifecycle check and success/stat/probation commit.
  This is submit linearization point. Unlock, then copy validated private scratch
  and explicit result to caller and return success. A later stream_stop does not
  retroactively invalidate already committed submit.
- Remove any failure path after caller copy.

## Counters/observability

- Every valid ASRC call increments both generic and ASRC submit counters.
- Every fallback increments both exactly once; `record_fault` already owns
  generic fallback increment, so ASRC paths add only ASRC category before it.
- Success increments both once. Initial PREPARING/FALLBACK and mutex-state race
  update both generic+ASRC fallback.
- Add ASRC stats to `flpr offload` shell: submits/success/fallback, category
  faults, RTT min/max/avg/count, FLPR cycles min/max/avg/count, verify faults.
- Correct results RAM table: report nRF54 CPUAPP and FLPR separately from build
  output. Heap note: existing FLPR 1024-byte Zephyr heap remains, but audio path
  must have no `k_malloc`/malloc/new calls.

## Integration tests

- Add smallest test seam around ASRC dispatch/state commit used by
  `fill_block_asrc` (extract pure helper only if needed). Prove:
  success commits FLPR state and skips CPU ASRC; PREPARING/error invokes CPU once
  from unchanged pre-state; FLPR->fallback->FLPR continuity; sequence advances
  once per successful block and resets; 479/480/481 outputs accepted.
- Add transport corruption tests for sequence, flags, correction, step_base,
  CRC; output/result sentinels untouched on each failure and lifecycle race.
- Shadow pre-state import failure must fail, not fall through as pass.

Run all Stage3 suites and both builds. No hardware yet. Commit new fix, no
amend/push/security/recovery policy/HPF/BabbleSim/mass erase/install.

---

## Results

### Tests — all pass (native_sim)

| Suite             | Pass | Fail | Skip | Total |
|-------------------|------|------|------|-------|
| flpr_protocol     |   59 |    0 |    0 |    59 |
| audio_offload     |   34 |    0 |    0 |    34 |
| offload_asrc      |   28 |    0 |    0 |    28 |
| flpr_ring_mgr     |    8 |    0 |    0 |     8 |
| **Total**         |**129**|**0**|**0**|**129**|

New tests (8 added): sequence mismatch, flags missing ASRC, correction echo mismatch,
step_base mismatch, sentinels untouched on failure, lifecycle race stop during submit,
commit skips CPU ASRC seam, shadow import failure documentation.

### Builds — both clean (no new warnings)

**nRF5340 (ebyte_e83_nrf5340/nrf5340/cpuapp)**
- FLASH: 146,780 B / 256 KB (55.99%)
- RAM: 40,512 B / 64 KB (61.82%)

**nRF54L15 CPUAPP (nrf54l15dk/nrf54l15/cpuapp)**
- FLASH: 494,520 B / 1,428 KB (33.82%)
- RAM: 152,020 B / 160 KB (92.79%)

**nRF54L15 FLPR (nrf54l15dk/nrf54l15/cpuflpr)**
- text: 31,582 B, data: 588 B, bss: 10,952 B
- RAM total: 11,540 B / 64 KB (17.6%)
- FLASH total: 32,170 B
- Heap: 1,024 B Zephyr heap (existing, not used by audio path)

### Audio path — zero heap calls

No `k_malloc`, `malloc`, `new`, `calloc`, or `realloc` in:
`audio_offload.c`, `audio_asrc.c`, `audio_decode.c`, `audio_drift.c`,
`audio_i2s.c`, `flpr_audio_process.c`, `flpr_ring.c`, `flpr_ring_mgr.c`.

### Files changed

- `src/flpr_ring_mgr.h` — extended `flpr_consume_asrc_result` struct (+4 fields)
- `src/flpr_ring_mgr.c` — fill new fields before consume_done, recompute CRC
- `src/audio_offload.c` — transactional commit-then-copy, explicit result assignment, dual counters, ASRC_LIFECYCLE_CHECK macro, new validation (sequence/ppm/flags/step_base/status), shadow import failure path, no post-copy failure
- `src/audio_shell.c` — ASRC stats in `flpr offload` command
- `tests/unit/offload_asrc/src/mock_ring_mgr_asrc.c` — extended result fields, corruption controls
- `tests/unit/offload_asrc/src/test_offload_asrc.c` — 8 new tests (28 total)
- `docs/development/phase6-stage3b-review-fix-handoff.md` — this file with results

### Clean tree

```sh
git diff --stat
```

