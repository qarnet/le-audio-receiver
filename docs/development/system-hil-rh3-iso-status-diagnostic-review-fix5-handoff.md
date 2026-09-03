# RH3 ISO-status diagnostic matrix indentation fix handoff

Status: mechanical review correction. Prior fix4 claimed to align three new
`public_outcomes` objects, but current `tests/test-matrix.json` still indents
their opening braces and fields one space deeper than adjacent objects.

## Scope

Only `tests/test-matrix.json` may change.

## Exact edit

At session outcomes around `audio_stream_session_recv`, align these three
objects exactly with adjacent four-space-indented array objects:

- `audio_stream_session_rx_status_record`
- `audio_stream_session_rx_stats_get`
- `audio_stream_session_recv_reset`

Their opening `{`, keys, and closing `}` must have same indentation as adjacent
`audio_stream_session_recv` and `audio_stream_session_configured` objects.
Do not alter any key, value, order, quote escaping, or any other file.

## Verification

```bash
nix develop --command python3 scripts/check-test-matrix.py --repo-root .
git diff --check
git status --short
```

No test rewrite, build, HIL, hardware, commit, or cleanup.
