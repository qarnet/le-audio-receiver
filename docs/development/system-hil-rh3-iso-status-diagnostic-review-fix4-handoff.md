# RH3 ISO-status diagnostic API-doc and parser-edge fix handoff

Status: focused review correction. Current behavior and focused verification
already pass. Fix only two review gaps before accepting phase.

## Findings

1. `src/audio_stream_session.h` uses Doxygen `@p VALID`, `@p ERROR`,
   `@p LOST`, and `@p UNKNOWN`, but none are formal parameter names. These must
   be enum-constant references, not invalid parameter references.
2. Parser grammar promises rejection of reordered diagnostic suffix fields, but
   fake-lab tests prove only a partial suffix. Add one public parser boundary
   test for reordering.
3. Align indentation of newly added `tests/test-matrix.json` outcome objects
   with surrounding entries. Do not alter content.

## Scope

Only these files may change:

- `src/audio_stream_session.h`
- `tests/hil/rh2_test.py`
- `tests/test-matrix.json`
- this handoff document if factual correction is needed

## Exact changes

### API documentation

In the `audio_stream_session_rx_status_record()` Doxygen contract, replace
invalid `@p VALID`-style text with exact enum references:

```text
@ref AUDIO_STREAM_RX_STATUS_VALID
@ref AUDIO_STREAM_RX_STATUS_ERROR
@ref AUDIO_STREAM_RX_STATUS_LOST
@ref AUDIO_STREAM_RX_STATUS_UNKNOWN
```

Keep every behavioral statement, signature, and API unchanged.

### Parser edge test

In `tests/hil/rh2_test.py`, add one `TestReceiverParsing` test containing an
otherwise complete stream summary whose suffix orders `rx_error` before
`rx_valid`. Assert `receiver.parse_stream_summary()` returns `[]`. Do not
change parser code or accepted grammar.

### Matrix formatting

Reindent only the three newly added session `public_outcomes` objects so their
indentation matches adjacent JSON objects. Preserve every key/value, including
the quoted `rx_stats_get` outcome.

## Verification

Run from repository root in existing dev shell:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
python3 -m py_compile scripts/hil/receiver.py tests/hil/hil_fakes.py tests/hil/rh2_test.py
```

No native rebuild needed: no C implementation, Kconfig, or C test change.
No full gate, firmware build, HIL, hardware command, commit, cleanup, source
behavior, parser behavior, threshold, warning policy, or unrelated change.
