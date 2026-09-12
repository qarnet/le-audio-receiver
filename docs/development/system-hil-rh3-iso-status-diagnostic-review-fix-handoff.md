# RH3 ISO-status diagnostic review-fix handoff

Status: focused software-only review correction for
`system-hil-rh3-iso-status-diagnostic-handoff.md`. Do not start a new
diagnostic phase or perform hardware work.

## Review findings

The implementation matches diagnostic behavior, but phase verification cannot
be accepted yet.

1. `tests/test-matrix.json` records
   `audio_stream_session_rx_stats_get` outcome as unquoted
   `valid=3,error=1,lost=1,unknown=1,no_ts=3`. The matrix checker accepts an
   exact free-form result only when it is a JSON string containing quoted text,
   such as `"valid=3,error=1,lost=1,unknown=1,no_ts=3"`. Current entry fails
   `scripts/check-test-matrix.py` outcome grammar.
2. New public session APIs have no API contract comments in
   `src/audio_stream_session.h`. New public behavior must document
   gate-independent recording, status classifications, return value, reset
   lifecycle, and invalid-slot snapshot semantics.
3. `src/audio_stream_session.c` mutex comment still says mutex guards only
   admission/generation/in-flight. New `rx_stats` is also guarded by it.
4. Focused native test emitted forbidden Kconfig assigned-value warnings:

   ```text
   CONFIG_BT_CONN_TX_MAX=7
   CONFIG_BT_ISO_TX_BUF_COUNT=6
   ```

   Exact NCS v3.3.0 Kconfig grounding:

   - `zephyr/subsys/bluetooth/host/Kconfig:377-386` defines
     `BT_CONN_TX_MAX` only under `if BT_CONN`.
   - `zephyr/subsys/bluetooth/Kconfig:338-355` defines
     `BT_ISO_TX_BUF_COUNT` only under `if BT_ISO`.
   - The unit test enables only `CONFIG_BT=y` because it includes Bluetooth
     audio headers but performs no Bluetooth connection or ISO operation.
   - `BT_CONN` and `BT_ISO` stay disabled, so both assignments are rejected.
   - NCS default `BT_BUF_EVT_RX_COUNT=10` and `BT_BUF_ACL_TX_COUNT=3` satisfy
     `buf.h` static assertions. No explicit buffer override is needed.

## Scope

Only these files may change:

- `src/audio_stream_session.h`
- `src/audio_stream_session.c`
- `tests/unit/audio_stream_session/prj.conf`
- `tests/test-matrix.json`
- this handoff document, if a factual correction becomes necessary

## Exact fixes

### 1. Matrix outcome grammar

In the audio-stream-session entry of `tests/test-matrix.json`, change only the
new `audio_stream_session_rx_stats_get` outcome value to exact JSON text:

```json
"outcome": "\"valid=3,error=1,lost=1,unknown=1,no_ts=3\""
```

Keep its API name and witness unchanged. Do not modify unrelated matrix entries
or ledger formatting.

### 2. Public API contracts

Add Doxygen-style declarations in `src/audio_stream_session.h` immediately
above both new APIs:

- `audio_stream_session_rx_status_record()`:
  - records one app-delivered ISO callback before admission/configuration gate;
  - classifies `VALID`, `ERROR`, `LOST`, `UNKNOWN`, and invalid enum values;
  - increments `no_ts` independently when timestamp absent;
  - returns post-record valid count; invalid slot returns zero and changes
    nothing;
  - must not claim decode, PLC, or audio output success.
- `audio_stream_session_rx_stats_get()`:
  - returns an atomic-by-mutex snapshot, never an internal pointer;
  - out-of-range returns an all-zero snapshot.

Retain API signatures exactly. Wrap surrounding long comment lines to existing
header style where needed. Do not add new public API or alter data structure.

### 3. Mutex ownership comment

Update the existing `session_mutex` comment in `src/audio_stream_session.c` so
it accurately says mutex serializes session admission/lifetime/configuration
and per-slot receive-status state, while never spanning decode, sink, logging,
or Bluetooth work. Do not change locking code.

### 4. Warning-free unit Kconfig

In `tests/unit/audio_stream_session/prj.conf`:

1. Preserve `CONFIG_BT=y`.
2. Remove all explicit Bluetooth buffer/count assignments at current lines
   14-21, including rejected `CONFIG_BT_CONN_TX_MAX` and
   `CONFIG_BT_ISO_TX_BUF_COUNT`.
3. Replace stale comment with concise exact statement: test needs Bluetooth
   public audio header declarations only; NCS defaults satisfy buffer static
   assertions; it enables no BT connection or ISO role.
4. Do not enable `BT_CONN`, `BT_ISO`, a role, controller, or production
   Bluetooth behavior merely to make obsolete assignments accepted.

## Required verification

Run from repository root inside existing NCS v3.3.0 dev shell. Use a fresh
outside-repository build directory and retain it.

```bash
build_dir="$(mktemp -d /tmp/le-audio-receiver-rx-status-review.XXXXXX)"
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 -d "$build_dir" tests/unit/audio_stream_session -p -t run
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
python3 -m py_compile scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
```

Acceptance requires no compiler, Kconfig, or test warning/error from focused
native build; all fake-lab tests pass; matrix checker passes; Python compile
passes; whitespace check passes. Do not run full gate, firmware build, HIL, or
hardware command.

## Constraints

- Preserve all unrelated dirty work, including prior RH3 host-tail and
  ISO-status implementation changes.
- No commit, push, merge, PR, reset, stash, cleanup, `STATUS.md` edit, or
  public documentation change.
- No HIL threshold, warning scanner, parser grammar, summary field, decoder,
  Mode A, timing, source, controller, Kconfig production, devicetree, or
  hardware behavior change.
- Stop and report if these narrow fixes need broader design or scope.
