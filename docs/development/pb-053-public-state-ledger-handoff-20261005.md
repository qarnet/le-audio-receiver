# PB-053 public state ledger repair (2026-10-05)

Scope: `scripts/bluez_guest_public.py`, `scripts/bluez_host_results.py`, their two focused test files, and this document. No preparation or VM run, PB-051 execution, stage, commit, or push.

## Problem and contract

Six successful case flags do not prove initial identity, registrations, discovery, pairing or disconnect. Old Device1/MediaTransport1 `repr` strings cannot be checked without evaluating private diagnostics. Advance public schema from 1 to 2; guest marker stays schema 1. Archived schema-1 public results remain historical evidence, not current success. Keep callback and SetConfiguration property repr strings as diagnostics, not parser authority.

Record structured six-field Device1 state at initialization, pairing and disconnect; structured five-field MediaTransport1 GetAll state; adapter-bound registrations, discovery before/after stop, actual Pair/Connect operation and reply-time disconnect timestamps. Sample both peers through existing 0.5 s stable window inside original 10 s deadline, including terminal sample, and emit actual start/end/count summary before successful disconnect cleanup. Preserve the existing cleanup and resource-release paths and all six case conditions.

Validate event cardinality, exact fields, identity and ordering for initialization through frame delivery and release through disconnect observation. Do not order pairing before SetConfiguration callbacks; these callbacks can arrive during Pair/Connect. Require shared adapter and peer identity across all three public guest phases; retained initial bonds must match fresh1 and fresh2 initial state must be unbonded. Failed public reports with `require_success=False` retain diagnostics without acceptance.

Component acceptance uses authored schema-2 encoded fixtures with whole-family removal and contradictory-state controls, plus pure property snapshots using authored dictionaries. No mocked D-Bus substitutes for VM evidence. Run ResourceWarning-as-error suites for `bluez_host_results`, `bluez_host_guest`, `bluez_guest_limits`, `bluez_guest_acquire`, `bluez_host_lane`, `bluez_host_process`, then `git diff --check`.

## Direct-review follow-up (2026-10-05)

Three exact-shape checks still need binding: every initial Device1 address/path must match the opposite known controller address rather than its own self-reported Address; each callback-derived source/sink transport path must descend from its respective paired peer; and peer-disconnect cleanup `ok` must be exact boolean true, not numeric `1`. Add valid-shaped wrong-address/path, consistently remapped unrelated transport paths, and numeric cleanup-flag negative fixtures. This is parser-only; no new image or VM acceptance.
