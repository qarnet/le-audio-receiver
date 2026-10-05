# PB-053 live hold marker framing

R3 retained failure: `/tmp/opencode/pb053-accounted-lane-r3/pytest.log` reports
`test_signal_cancellation[SIGTERM]` reading JSON input `'{"r'` while polling
growing serial output. This is incomplete stream framing, not evidence of a
process cleanup failure. Preserve R3 and every earlier external evidence root.

Scope: `tests/host_bluez/test_host_lane.py`,
`scripts/bluez_host_results.py`,
`tests/unit/bluez_host_results/test_bluez_host_results.py`, and this note.
No VM, preparation, commit, push, skip, timing change or report rewrite.

Add pure `complete_marker_lines(content: bytes, prefix: bytes,
max_line_bytes=4096)` to results module. Require byte content, nonempty byte
prefix ending with space and positive exact-integer cap. Scan LF-terminated
rows only, stripping optional CR before selection. Select exact-prefix rows;
reject matching complete or trailing partial row over cap, but do not return
trailing partial marker even if its JSON appears closed. Return complete marker
row bytes without terminator. Leave malformed complete JSON and duplicate
rows visible to caller: `hold_run` still asserts at most one, parses JSON,
checks readiness and nonce, and verifies owned QEMU. No bad-JSON retries.

Regression: feed marker byte-by-byte and at every split; no row until LF,
then one exact row. Test closed JSON without LF, CRLF, complete malformed JSON,
duplicate rows, oversized partial and complete marker, and two-chunk growth
of a real temporary regular file (first chunk ends at `'{"r'`).

Verification: ResourceWarning-as-error `bluez_host_results`, `bluez_host_lane`,
and `bluez_host_guest` unittest discovery suites; `git diff --check`.
