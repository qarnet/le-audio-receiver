# 0002: Per-entry audio LTV interposition guard

- Date: 2026-10-08
- Status: Proposed
- Approval: Pending human product-owner review on PR 16

## Context

The installed NCS v3.4.1 public API `bt_audio_data_parse` accepts an LTV
entry whose declared length exceeds the logical buffer contents by one byte
(a logical out-of-range value). Verified behavior on the installed SDK: the native public parser, fed such
an encoded LTV input, returned 0 and delivered that entry; the native
baseline suite was 6 pass / 1 fail with the out-of-range case passing
through the raw parser (`ret=0`). With the repository guard the same case
returns `-EINVAL` (observed `ret=-22`, 12 pass / 0 fail). The physical
pure-parser diagnostic image reproduces the acceptance on the board. The
real-wire ASCS lane accepted the overlong metadata request with the success
response `0301010000` before the guard and rejects it on the wire with
`0301010c00` (code `0x0c`, reason `0`) with the guard. The native API
result and the real wire ASE response are separate observations; the wire
result is not claimed as a native API `-22`. The SDK tree is read-only:
modifying or patching installed SDK sources is outside this repository's
authority.

## Options considered

- Modify or patch the installed SDK parser. Rejected: the SDK is read-only,
  and repository work must not mutate its source.
- Validate the whole LTV buffer once before calling the real parser. Rejected
  as superseded: a whole-buffer preflight would change the parser's own
  prefix and immediate-cancel semantics for otherwise valid entries instead
  of preserving them.

## Decision and rationale

The repository owns a per-entry guard in `src/bt_audio_ltv_guard.c`, linked
through GNU ld `--wrap` on `bt_audio_data_parse`. The wrap flag is applied
wherever the receiver and the native/ASCS fixtures link the audio API
(`CMakeLists.txt` link options), so the guard is always linked, not opt-in in
production paths. GNU ld `--wrap` intercepts undefined link references to
`bt_audio_data_parse`; same-object calls that resolve internally inside the
SDK's own translation units are not intercepted and remain outside the
guard's scope. For each entry, the guard's length check requires that the
length octet plus the declared type and value bytes fit inside the caller's
remaining buffer before any callback runs. A valid entry is forwarded to the
real installed SDK parser entry by entry, so both the valid prefix and the
real parser's per-entry early-return semantics are retained: callback-driven
cancellation still yields the immediate `-ECANCELED`, and an overlong entry
is rejected (`-EINVAL`, observed `ret=-22` on the native path). This is
per-entry behavior, not a whole-buffer preflight result.

## Consequences

Interposition applies only in link units where the wrap flag is applied;
same-object calls that resolve inside the SDK's own translation units remain
outside the guard's scope, and the guard makes no SDK-wide claim. After any
SDK upgrade, the guard's ARM link behavior, the actual wire path and the 12
public unit cases (`tests/unit/ltv_bounds/test_ltv.c`) must be revalidated
before the guard is changed or removed. The guard proves input-length
validation only; it does not establish RF error recovery or full LC3
conformance.

## References

- Guard implementation: `src/bt_audio_ltv_guard.c`, `CMakeLists.txt`
- Guard API tests: `tests/unit/ltv_bounds/test_ltv.c`
- Regression guide and same-object scope limits:
  [ASCS protocol regression matrix](../testing/ascs-protocol-regression.md)
  (Wrapper same-object scope limits section). The design phase also
  evaluated and rejected a whole-buffer preflight; that rejected option
  is preserved in the dated Git history of
  `docs/development/pb-051-encoded-ascs-refinement-20261004.md` at
  revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`.
- Completed item: [PB-051 in backlog completed](../product/backlog/completed/pb-051%20-%20ASCS-protocol-rejection-and-lifecycle-regression-matrix.md)
- Measured evidence: `docs/development/pb-051-ascs-results-20261005.md`
  (native baseline and guard-run counts),
  `docs/development/pb-051-native-probe-entry-results-20261006.md`
  (native probe entry observations)