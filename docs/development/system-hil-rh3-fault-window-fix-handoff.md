# System HIL RH3 fault-window warning fix handoff

Status: focused host-only review fix. RH3 matrix software phase remains open
until named hang row can pass without weakening raw-log warning policy.

## Goal

Make hang-row warning handling both executable and fail-closed. Record exact
receiver raw-byte boundaries around runner-owned fault injection/recovery. Allow
only documented hang-recovery warning payloads whose complete raw line lies in
that byte window. Reject same payload outside window and every other warning.

## Scope

Touch only:

- `scripts/hil/serial_io.py`;
- `scripts/hil/runner.py`;
- `scripts/hil/receiver.py`;
- `tests/hil/hil_fakes.py` and `tests/hil/rh2_test.py`;
- this handoff/status note if needed.

No hardware, source firmware, receiver firmware, matrix scheduling, release,
capture, fixture, or acceptance changes. No commit.

## Design

1. Add thread-safe non-destructive `SerialConsole.rx_offset()` returning exact
   `_bytes_received` under existing condition lock. It does not purge, consume,
   flush, or move parser state.
2. Fake console path must expose same public behavior through real
   `SerialConsole`; do not bypass byte accounting.
3. In `_run_fault_window`, capture `start_offset` immediately before sending
   exact fault command and `end_offset` immediately after recovery predicate
   succeeds. Retain offsets in `recovery.json`, `summary.json`, and
   `fault-baseline.json` only if semantically useful. Required recovery shape:
   `raw_window: {"start_offset": N, "end_offset": M}` with integers,
   `0 <= N <= M`.
4. Keep `recovery-window.txt` readable transcript unchanged.
5. During final raw receiver scan, process UTF-8 bytes with line start/end byte
   offsets. Normal warning scanner semantics remain source of warning matches.
6. Exempt line only when all conditions hold:
   - row fault is `hang`;
   - recovery evidence has valid raw window;
   - complete line byte range is contained in `[start_offset, end_offset]`;
   - stripped line matches one exact documented payload pattern:
     `offload: heartbeat supervisor → RECOVERING`,
     `offload recovery: handshake unhealthy, escalating to runtime restart`,
     or `offload recovery: short ring reset failed (<signed integer>), escalating to runtime restart`.
7. Keep raw evidence unchanged. Do not remove exempt lines from
   `recovery-window.txt` or logs.
8. Same payload before/after window fails. Partial-overlap line fails. Stall row
   gets no exemption. Any LOG_ERR, assertion, fault, I2S warning, different
   LOG_WRN, malformed UTF-8, or shell error fails regardless of window.
9. If offsets missing/malformed or end precedes start, fault row fails log scan.

## Tests

Public behavior tests must prove:

- `rx_offset()` increases by exact retained raw bytes and does not consume
  lines;
- expected hang warning wholly inside window passes final log scan;
- identical warning before start and after end fails;
- expected warning with line crossing boundary fails;
- unrelated warning inside window fails;
- stall row expected hang text fails;
- malformed/missing window fails hang row;
- healthy rows unchanged;
- recovery evidence contains exact monotonic offsets;
- prior RH2/RH3/matrix tests remain green.

## Verification

```bash
python3 tests/hil/rh3_matrix_test.py
python3 tests/hil/rh2_test.py
python3 -W error scripts/test_hil_runner.py
python3 -m pytest -q tests/hil/rh2_hardware_test.py
python3 -m compileall -q scripts/hil scripts/hil-runner.py tests/hil
git diff --check
git status --short
```

No hardware commands. Return files, behavior, tests, deviations, blockers, and
explicit no-commit status.
