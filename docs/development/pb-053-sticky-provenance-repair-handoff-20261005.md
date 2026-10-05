# PB-053 sticky provenance repair (2026-10-05)

Scope: `scripts/bluez_guest_limits.py`, `scripts/bluez_host_guest.py`, their focused unit suites, and this handoff. No VM, guest preparation, PB-051 execution, fixture edit, or release claim.

## Failure boundaries

- `EventLedger.append()` currently raises on serialization or quota without retaining that failure. A callback can return a D-Bus error while later, smaller cleanup events succeed. A final ledger containing those cleanup events alone must not be reported complete.
- Host preparation checks fixed LC3 stimulus size and hash before staging, then uses an unchecked `shutil.copy2`. Mutation between check and copy, or after copy before sealing, can make archived bytes disagree with fixed manifest provenance.

## Repair

Within existing ledger `RLock`, catch `Exception` from serialization, quota check, JSON deep copy, list append and byte accounting; latch first `TypeName: message`, then re-raise original exception. Later successful appends never clear error. Preserve existing event and byte limits and `safe_append` nonthrowing cleanup semantics.

Stage stimulus through existing bounded regular-file `snapshot` with fixed `STIMULUS_SIZE` cap and `STIMULUS_SHA` pin; require copied byte count equals fixed size. Verify staged size and hash immediately before archive creation and again before manifest write. Keep stimulus separate from nine frozen source keys, fixed manifest source/purpose, emulator mode `0755`, and SDK/vendor read-only.

## Observable regression and verification

- Oversized ordinary append raises, leaves rejected event absent, allows smaller cleanup append, yet `record_ledger_failure` rejects result. Real socketpair leases still close both peers to EOF after prior ordinary append failure. JSON NaN/type failure latches first error even after later append success or failure.
- Real temporary stimulus files exercise copied mismatch rejection, mutation after staging rejection before sealing, and valid exact copy success through snapshot and staged-verification boundaries. No fixture changes or environment mocks serve as acceptance.
- Run `python3 -W error::ResourceWarning -m unittest discover -s tests/unit/bluez_guest_limits -p 'test_*.py' -v`, equivalent `bluez_host_guest` and `bluez_host_results` commands, then `git diff --check`. These tests are component-boundary proof, not VM or physical acceptance.
