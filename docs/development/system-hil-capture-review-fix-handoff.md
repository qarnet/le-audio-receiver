# System HIL capture software review-fix handoff

Status: host-only review correction. No live ALSA, USB, audio, probe, flash,
serial, Bluetooth, sudo, RF, hardware qualification, or acceptance run. No
commit.

## Goal

Close two review gaps in MA0/MA1 and SA0/SA1 capture foundation while keeping
public behavior and accepted external qualification contract unchanged.

## In scope

1. Make `CaptureSession` process cleanup retry-safe. A failed `stop()` must not
   cause registered `abort()` cleanup to become a no-op while an `arecord`
   process can still be alive.
2. Add public-boundary tests for failed SIGINT/process teardown, retry cleanup,
   retained partial evidence, and no false final WAV promotion.
3. Add synthetic analyzer happy-path coverage with non-default gain and bounded
   leading timing offset. Existing clean tests currently use only default gain
   and one fixed prefix despite handoff requirement.

## Out of scope

- No analyzer algorithm, metric names, numerical synthetic limits, production
  qualification schema, fixture schema, CLI, matrix schedule, verdict, source
  signal, firmware, or documentation-status redesign.
- No hardware numerical limits.
- No commit, push, merge, PR, hardware access, or destructive command.

## Grounding and required behavior

- `scripts/hil/runner.py:1671` registers `capture_session.abort` before source
  START. CleanupStack is final resource owner.
- `scripts/hil/capture.py:372-407` currently sets `_closed = True` before
  `_stop_process()`. If SIGINT or later process termination fails, `stop()`
  raises, then `abort()` at lines 410-416 returns immediately because `_closed`
  is already true. This violates no-owned-process-survives invariant and makes
  cleanup registration ineffective on process-stop failure.
- Partial WAV and process diagnostics must remain evidence on every failure.
  Final WAV promotion remains allowed only after process termination,
  post-stop identity validation, and strict WAV validation all succeed.
- `abort()` stays bounded and idempotent after confirmed process exit. It must
  retry cleanup when prior `stop()` failed before confirming process exit.
- Cleanup failure must still propagate so runner verdict becomes failed. Do not
  swallow termination errors or weaken existing stderr/xrun checks.
- Use injected fake process boundary. Tests must not spawn `arecord` or inspect
  live ALSA.
- `tests/hil/capture_analyzer_test.py:97-104` claims gain and bounded-prefix
  coverage but exercises defaults only. Add at least two clean public inputs
  using explicit non-default gain and distinct bounded prefix sample counts for
  mono/stereo. Keep limits explicitly synthetic-only.

## Decided implementation shape

Refactor `CaptureSession` state minimally:

- Track whether process exit has been confirmed separately from whether normal
  session finalization completed.
- Do not let a failed normal `stop()` suppress later cleanup retry while
  `poll()` still reports live process.
- `abort()` checks actual process state. If process already exited, it remains
  idempotent. If still live after failed stop, it retries bounded termination.
- Record process result after each termination attempt without deleting prior
  partial WAV. Preserve useful first-attempt diagnostic in raised error or
  evidence. Do not invent an unbounded loop.
- If SIGINT delivery fails and process is still live, attempt bounded kill as
  part of same cleanup path. If cleanup still cannot confirm exit, raise
  `CaptureError`; leave state retryable for CleanupStack invocation.
- Normal WAV validation or identity-validation failures occur after confirmed
  process exit, so repeated cleanup must not rerun analysis or promote files.
- Keep allowed normal arecord exit statuses `(0, -SIGINT, 130)` and existing
  warning/xrun failure rules.

Tests in `tests/hil/capture_runner_test.py` must prove observable behavior:

1. `stop()` termination failure leaves partial WAV, no final WAV, and does not
   make subsequent `abort()` a no-op while fake process remains live.
2. Retry can confirm fake process exit and no process remains live.
3. Already-finished successful/failed sessions keep idempotent cleanup.
4. Timeout/kill and existing exact argv tests still pass.

Tests in `tests/hil/capture_analyzer_test.py` must prove clean mono/stereo input
passes with explicit gain variation and different bounded leading prefixes.

## Verification

```bash
nix develop --command python3 tests/hil/capture_analyzer_test.py
nix develop --command python3 tests/hil/capture_runner_test.py
nix develop --command python3 tests/hil/capture_model_test.py
python3 tests/hil/rh4_artifact_test.py
python3 tests/hil/rh3_matrix_test.py
python3 tests/hil/rh2_test.py
python3 -W error scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/rh2_hardware_test.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
git diff --check
git status --short
```

Stop and report blocker after two materially different failed attempts. Do not
weaken assertions, normalize warnings, change architecture, touch hardware, or
commit. Return changed files, behavior, tests/results, deviations, blockers,
and final git status.
