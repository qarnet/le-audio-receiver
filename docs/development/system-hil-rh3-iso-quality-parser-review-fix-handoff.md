# RH3 ISO link-quality transcript parser review fix handoff

Status: host-only repair after immutable direct diagnostic
`rh3-20260822-01-modea-tail-order-cleanup`. No hardware action.

## Review finding

The repaired tail order worked. Retained receiver transcript ran `bt iso quality`
before source teardown and contains valid shell output:

```text
--- ISO link quality ---
  Stream[0] handle=0x0001 tx_unacked=0 tx_flushed=0 tx_last_subevent=14323 retransmitted=0 crc_error=0 rx_unreceived=14317 duplicate=0
  Stream[1] handle=0x0006 tx_unacked=0 tx_flushed=0 tx_last_subevent=14250 retransmitted=0 crc_error=0 rx_unreceived=14241 duplicate=0
```

`scripts/hil/receiver.py::parse_iso_link_quality()` nevertheless marks the
transcript malformed because it treats every line containing `Stream[` as a
candidate. The prompt-bounded command transcript also retains asynchronous
Zephyr log lines before the shell header, including:

```text
uart:~$ [00:31:30.816,893] <inf> bt_bap: Stream[0] started: CIG 0 CIS 0
```

That log is not shell output and cannot match the exact shell grammar. The
result was strict failure at `receiver tail`; no acceptance claim exists.

## Scope

### In scope

1. Make ISO parser ignore unrelated log lines before its shell header.
2. Keep exact shell-line grammar strict after the header.
3. Add physical-transcript-shaped host regressions.
4. Correct inaccurate direct-run wording in resume/status documents.

### Out of scope

- all hardware, HIL, source/receiver firmware, shell formatting, controller,
  Kconfig, devicetree, warning policy, threshold, audio, RF, pairing, or image
  changes;
- ignoring malformed ISO shell records, relaxing expected slot/count checks, or
  treating nonzero counters as healthy;
- direct-row retry, full matrix, evidence mutation, `STATUS.md`, commit, push,
  merge, PR, tag, or release.

## Exact implementation

### Parser

In `scripts/hil/receiver.py`, add an exact shell-record candidate regex that
matches only an unprefixed line whose first non-whitespace token is `Stream[`:

```python
RE_ISO_LINK_QUALITY_STREAM_CANDIDATE = re.compile(r"^[ \t]*Stream\[")
```

In `parse_iso_link_quality()`:

1. retain duplicate-header failure exactly as now;
2. ignore every non-header line before first exact header;
3. after header, only invoke `RE_ISO_LINK_QUALITY_STREAM.fullmatch()` for lines
   matching `RE_ISO_LINK_QUALITY_STREAM_CANDIDATE`;
4. mark a candidate malformed when the exact record grammar does not match;
5. ignore unrelated prefixed Zephyr log lines both before and after the header.

Do not strip or normalize candidate whitespace. `RE_ISO_LINK_QUALITY_STREAM`
continues to require exactly two leading spaces, upper-case four-digit hex
handle, exact field order, and nonnegative decimal counters. An unindented or
otherwise malformed shell-like `Stream[` line after header must fail.

### Tests

In `tests/hil/rh2_test.py`, add or extend parser tests to prove:

1. transcript with a realistic pre-header `uart:~$ [timestamp] <inf> bt_bap:
   Stream[0] started...` log plus two valid ISO shell records parses and
   validates as two streams without `malformed`;
2. a header followed by an unindented or truncated shell-like `Stream[` record
   remains malformed;
3. existing duplicate/missing-slot and nonzero-counter behavior stays strict.

Use `hil_fakes.iso_link_quality_transcript()` for valid records where useful.
No fake firmware or source transcript change is needed.

### Documentation truth

Update only:

- `docs/development/system-hil-resume-state.md`;
- `docs/development/system-hil-rh3-software-status.md`.

Correct `rh3-20260822-01-modea-tail-order-cleanup` wording to say its
`result.json`/JUnit outcome is failed. Remove unsupported claim that runner
command exited `0`; do not infer an exit code absent retained process evidence.
Call it a direct runner diagnostic, not a matrix run. State parser repair is
host-only and no physical retry occurs in this phase.

## Verification

Run sequentially from repository root:

```bash
nix develop --command python3 tests/hil/rh2_test.py
nix develop --command python3 -m py_compile \
  scripts/hil/receiver.py \
  tests/hil/rh2_test.py
```

No hardware, build, flash, serial, Bluetooth, RF, source-build, HIL, commit,
