# FR4 mono fix handoff: reserve one ASE during BlueZ selection

Date: 2026-08-10

## Goal

Fix strict `--mono` negotiation after exact-artifact hardware exposed a
pre-configuration race: BlueZ 5.86 selects FL and FR concurrently before it
calls either SetConfiguration. Current limit counts only pending/acquired
transports, so both SelectProperties calls succeed; BlueZ assigns CIS 0 and
CIS 1, then the second SetConfiguration is rejected. The remaining first
transport belongs to a partial two-CIS CIG and never establishes.

Add one explicit mono selection reservation. Reject the second selection
before BlueZ creates a second setup/CIS. Preserve default Mode A, Mode B,
deferred Acquire, fd ownership, and all connection/writer behavior.

This handoff is code/tests only. Do not run hardware, download assets, reflash,
or touch release/tag state. FR4 remains blocked until Orchestrator reviews the
fix and resumes the approved exact-artifact hardware run.

## Grounding

Exact retained hardware evidence:

- run directory `/tmp/opencode/fr4-v0.1.0-OEp9Kh`;
- exact draft nRF5340 firmware flashed and verified;
- mono log: FL SelectProperties success, FR SelectProperties success, FL
  SetConfiguration accepted as CIG 0/CIS 0, FR SetConfiguration arrives as CIG
  0/CIS 1 and is rejected, first Acquire times out, receiver SDUs=0;
- one authorized default Mode A diagnostic on the same exact firmware passed:
  two CIS established, 3000 frames / 30 s / 100 fps, zero decode errors,
  underruns, resets, or warnings.

BlueZ 5.86 source at commit
`74770b1fd2be612f9c2cf807db81fcdcc35e6560`:

- `src/shared/bap.c` `bt_bap_select()` lines 6148-6210 computes FL/FR
  allocations and invokes local endpoint selection for each ASE before
  configuration;
- `profiles/audio/bap.c` `select_cb()` lines 1968-1988 drops an individual
  failed selection without setting global `select->err`; successful setup(s)
  still proceed when all callbacks finish;
- therefore rejecting the second SelectProperties leaves the first setup
  eligible for configuration and prevents CIS 1 from entering the CIG.

Do not solve this by ignoring a second accepted configuration, shortening
grace, acquiring early, changing receiver firmware, or changing BlueZ.

## Exact implementation

Modify `scripts/bap_central_endpoint.py` only.

### Reservation state

Add private endpoint state initialized before any callback can run:

```python
self._mono_selected_channel = None
```

Meaning:

- `None`: no successful mono SelectProperties is awaiting configuration;
- `0x01` or `0x02`: one FL/FR selection is reserved but not yet consumed by
  matching SetConfiguration.

D-Bus callbacks run on the existing serialized main-loop context. Add no lock,
counter, timer, or second endpoint.

### SelectProperties

For mono mode, before returning configuration:

1. reject with existing exact transport-limit diagnostic/error when either:
   - pending+acquired count is already one, or
   - `_mono_selected_channel is not None`;
2. retain exact FL/FR-only allocation validation;
3. construct/log exact current mono configuration and QoS unchanged;
4. only after successful return object construction, reserve exact requested
   channel in `_mono_selected_channel` immediately before returning.

The first FL or FR call succeeds and reserves. Concurrent second selection
sees reservation and receives `org.bluez.Error.Rejected`. It must not mutate
reservation, pending/acquired lists, config_done, or returned config bytes.

### SetConfiguration

Keep existing owned-transport limit and LTV parsing. For mono mode, after exact
FL/FR validation and before queue mutation:

- if reservation is present and parsed channel does not equal reserved channel,
  reject atomically with `org.bluez.Error.Rejected`; leave reservation and all
  ownership state unchanged;
- if reservation is absent, retain direct first-configuration compatibility
  for existing tests/callers and accept exact FL/FR;
- on matching/compatible acceptance, append existing pending record and set
  config_done as now, then consume reservation by setting it to `None` only
  after queue mutation succeeds.

After acceptance, pending ownership itself blocks any later selection/config.

### Cleanup

- `Release()` must clear `_mono_selected_channel` along with existing pending,
  acquired, and config_done state, so endpoint teardown cannot retain a stale
  reservation.
- `ClearConfiguration()` keeps current record-specific behavior. Accepted mono
  SetConfiguration already consumed reservation, so clearing the owned record
  restores capacity through empty ownership lists.
- Do not clear a pre-configuration reservation in response to an unrelated
  transport path.

## Tests

Modify
`tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`.

Add/adjust public-boundary tests proving:

1. exact hardware callback order: FL SelectProperties succeeds; FR
   SelectProperties immediately rejects before any SetConfiguration; matching
   FL SetConfiguration then queues exactly one; one deferred Acquire infers
   `stream_mode == "mono"`, one transport, 120-byte SDU;
2. symmetric FR-first then FL-rejected path;
3. second selection rejection leaves first reservation behavior intact and
   every ownership/config_done field unchanged;
4. matching SetConfiguration consumes reservation and queues one;
5. mismatched SetConfiguration rejects atomically, leaves reservation usable
   only by matching configuration, and matching retry succeeds;
6. Release before configuration clears reservation; a fresh first selection
   then succeeds;
7. Release after configuration retains existing cleanup/reuse behavior;
8. combined/unknown first selection rejects without creating reservation;
9. direct exact FL/FR SetConfiguration without prior SelectProperties remains
   accepted for backward-compatible test seams;
10. default Mode A still accepts FL+FR selection/configuration; stereo Mode B
    still accepts combined selection/configuration.

Assert public error name, exact config/QoS/mode/SDU, callback outcomes, queue
records, and successful cleanup. Private reservation may be inspected only to
pin atomic failure/cleanup state, not as sole acceptance proof.

Do not weaken or delete existing tests. Avoid literal fake fds; use existing
fake transport scripting/real pipe discipline.

## Scope

Touch exactly:

- `docs/development/firmware-release-fr4-mono-fix-handoff.md`
- `scripts/bap_central_endpoint.py`
- `tests/unit/bap_central_endpoint/test_bap_central_endpoint.py`

Do not modify CLI flags, procedure, session/writer/security/device modules,
firmware/build files, workflow, VERSION, inventory, coverage baseline, status,
plan, results, or retained `/tmp` evidence.

## Verification and commit

Run before commit:

```bash
python3 tests/unit/bap_central_endpoint/test_bap_central_endpoint.py
python3 tests/unit/bap_central_session/test_bap_central_session.py
python3 -m py_compile scripts/bap_central_endpoint.py
git diff --check
```

Inspect status, full diff, and recent log. Stage exactly three files. Commit:

```text
fix: reserve mono ASE during BlueZ selection
```

Require clean worktree, then run full canonical gate:

```bash
./scripts/test-all.sh
```

Expected 65 PASS / 0 FAIL / 65 TOTAL with unchanged BSim pins. If post-commit
gate fails, do not amend; stop and report for another fix.

Do not push, merge, open PR, amend, run hardware, download assets, reflash, or
touch remote state. Return files, behavior, focused/full results, commit hash,
blockers/deviations, and exact hardware resume step.
