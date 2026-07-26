# Phase 4b.1 Lifecycle Wiring Fix

Status: required after review of `79a14b8`

## Bug

`lc3_config()` calls `stream_lifecycle_sink_configured(idx, ch)` before parsing
channel allocation and assigning `sinks[idx].decode.chan_count`. Fresh entries
therefore register as zero/unconfigured; audio-path gate never opens.

## Fix

1. Move lifecycle registration after both channel-allocation branches assign
   final nonzero `chan_count`.
2. Pass final `sinks[idx].decode.chan_count` directly.
3. Add bounded configuration log or assertion showing lifecycle receives index
   and final channel count; no permanent verbose spam.
4. Add/adjust production API test if needed to cover that zero means absent and
   nonzero configured entries gate correctly. Do not copy integration logic.
5. Build both targets and rerun lifecycle tests.
6. Mark this handoff implemented, commit scoped fix.

```bash
fw-build-5340
fw-build-54l15
west build -b native_sim tests/unit/lifecycle -d /tmp/le-audio-lifecycle-test --pristine
/tmp/le-audio-lifecycle-test/lifecycle/zephyr/zephyr.exe
git diff --check
git status --short
```

No hardware, PI, RADIO, push, merge, PR, amend, force, attribution.
