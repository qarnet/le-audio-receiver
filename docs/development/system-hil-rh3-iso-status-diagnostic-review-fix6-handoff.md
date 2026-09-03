# RH3 ISO-status diagnostic remaining matrix indentation fix handoff

Status: mechanical review correction. `audio_stream_session_recv_count` was an
existing outcome whose witness changed during this phase. Its object remains
misindented at current `tests/test-matrix.json` lines 1177-1181.

## Scope and edit

Only `tests/test-matrix.json` may change. Reindent that one
`audio_stream_session_recv_count` object so opening brace and closing brace use
four spaces and keys use five spaces, matching adjacent outcomes. Preserve
every key, value, and order.

## Verification

```bash
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
git diff --check
```

No other edit, test rewrite, build, hardware action, commit, or cleanup.
