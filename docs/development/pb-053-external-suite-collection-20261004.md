# PB-053 external pytest suite collection checkpoint

This external lane runs only when selected explicitly. Nine inventory cases
cover three real public guest phases, daemon restart and state retention/reset,
48 transport frames, host-runner signals, host timeout and a new guest boot.
No physical RF, codec decode, I2S, analog or PB-051 claim follows from this lane.

Collection writes actual pytest-selected IDs to an exclusive external
`collection.json` independent of the reviewed inventory. Missing local
runtime/pytest version fails readiness; tests never skip. Execution and
PB-052 report accounting require later review. No VM run or acceptance is
claimed from collection alone.

Static-only collection (2026-10-04): pinned pytest 8.4.2, prepared r26
manifest SHA-256 `27429fcffbb1c84779a1170ecafdf0e11647d98fb074023a17e6cd79d602aef1`,
exclusive `/tmp/opencode/pb053-suite-collection-r1/collection.json`.
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python3 -m pytest --collect-only -q
tests/host_bluez/test_host_lane.py` collected nine named cases, with no
fixtures executed. Inventory SHA-256
`1de02a5083330284600ac740ab21a1f35ff76e5411377beb029ea251d324f276`.
Full VM suite, JUnit emission and external accountant invocation remain unrun.

Reviewed static revision: normal CLI now has owned Popen timeout handling;
normal and hold paths signal only their host-runner PID on failure, allow its
handler 15 seconds, then kill only that PID if needed. Cleanup checks recorded
or directly observed QEMU process group and preserves an earlier failure.
Successful normal output is validated again against host run ID/scenario and
pinned transport stimulus through `validate_guest`. State reset requires
nonempty hashes and original/preserved binding; unchanged cache hashes are
not rejected. Fresh/repeated boot UUID and exact frame checks remain.
New exclusive `/tmp/opencode/pb053-suite-collection-r2/collection.json`
records nine unchanged IDs under pytest 8.4.2; no fixtures or VM executed.
Inventory bytes and SHA-256 remain unchanged.
