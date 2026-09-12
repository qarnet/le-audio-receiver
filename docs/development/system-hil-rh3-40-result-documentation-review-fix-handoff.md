# RH3-40 result documentation review fix handoff

Status: one documentation correction. Do not run builds or touch hardware.

## Review finding

`docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md` records H40
post-stop FLPR `submit=0 success=0`, but does not say that this is expected for
the tested 7.5 ms profile. A reader could mistake expected CPUAPP ASRC fallback
for an H40 offload fault.

Repository truth is at `docs/known-limitations.md:15-21`: nRF54L15 FLPR accepts
480 input frames, while 7.5 ms streams use 360 frames and fall back to CPUAPP
ASRC.

## Exact change

Touch only:

```text
docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md
```

Immediately after its post-stop FLPR bullet, add one short factual sentence:

```text
The `48_3_1` 7.5 ms row has the 360-frame input shape. Existing nRF54L15
behavior therefore uses CPUAPP ASRC fallback, so zero FLPR submits are expected
here and are not an H40 offload fault.
```

Do not change evidence values, scope claims, H40 outcome, the resume state,
handoffs, code, configuration, tests, or public documentation. Do not stage or
commit.

## Constraints

- No build, test, Nix, HIL, serial, debugger, flash, reset, or cleanup action.
- No deletion or garbage collection. Reuse existing files only.
- Preserve all pre-existing dirty work.
- Use no em dash character.

## Verification

```bash
git diff --check
! rg -n '—' docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md
git diff -- docs/development/system-hil-rh3-40-tx-notify-workqueue-result.md
git status --short
```

Return changed path, verification result, no-build/no-hardware/no-commit
confirmation, and blockers or deviations.
