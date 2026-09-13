# RH3-36 foreign-flush proof repair handoff

Status: focused host-test repair. RH3-36 implementation and trace linkage are
otherwise complete. This repair proves a foreign `k_work_flush()` cannot alter
tracked lifetime stage, rather than only checking snapshot values captured
before that foreign call. No hardware action is authorized.

## Defect

Current
`test_iso_rx_lifetime_disposition_tracks_progression` invokes a foreign direct
`__wrap_k_work_flush()` after it already captured a disable snapshot, then
asserts cached observation values. That proves forwarding but does not observe
post-call slot stage. The implementation's active/thread guard appears correct,
but test must prove public diagnostic outcome after the foreign flush.

## Scope

In scope:

- `tests/unit/sdc_hci_remove_iso_path_trace/src/test_sdc_hci_remove_iso_path_trace.c`
- this handoff only for factual correction.

Out of scope:

- trace source, Kconfig, CMake, parser, RH2 tests, trace image build, normal
  build, hardware, HIL runner, flash, serial, RF, source image, production
  behavior, `STATUS.md`, unrelated dirty work, commit, stage, push, reset,
  stash, clean, or broad formatting.

## Exact change

In existing
`test_iso_rx_lifetime_disposition_tracks_progression`, replace final
foreign-flush assertion block with a fresh independent session:

1. Call existing test reset and lifetime-observation reset.
2. Open ISO RX lifetime session.
3. Allocate exactly one tracked ISO RX buffer through `__wrap_bt_buf_get_rx()`.
4. Do not invoke `__wrap_bt_conn_recv()`.
5. Set fake real flush return to `true`, then invoke direct
   `__wrap_k_work_flush(&fake_flush_work, &fake_flush_sync)` outside outer
   receive context.
6. Call existing disable snapshot after that foreign flush.
7. Assert exact observable result:

```text
undispatched=1
host_dispatched=0
tx_notify_flush_entered=0
tx_notify_flush_returned=0
host_returned=0
app_callback_seen=0
unclassified=0
```

Also retain exact foreign wrapper forwarding assertions for `work`, `sync`, and
Boolean return, plus no lifetime tracking error. This proves foreign flush did
not advance a tracked slot.

Do not add a test method, alter native test count, alter fake behavior outside
this test, or weaken existing assertions.

## Verification

Run only:

```bash
nix develop --command env NIX_HARDENING_ENABLE="" west build --no-sysbuild \
  -b native_sim/native/64 \
  -d /tmp/sdc-hci-iso-rx-tx-notify-flush-rh3-36-foreign-unit \
  tests/unit/sdc_hci_remove_iso_path_trace -p -t run
git diff --check
```

Expected native result: `15/15`. Report exact output and stop on any mismatch.
No trace or normal build is needed because no trace implementation changes.

## Return report

Return changed paths, exact native result, `git diff --check`, final status,
and explicit no-hardware/no-commit confirmation.
