# PB-051: independent encoded ASCS trace checker

## Trust boundary and frozen inputs (2026-10-05)

Implement stdlib-only parsing against independently reviewed raw policy
`tests/ascs_bsim/cases.json`, approved SHA-256
`addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`.
The CLI takes that digest from its caller; neither client CASE markers nor
passing process exit determine required cases. The pure trace function has
no OS execution claim. CLI acceptance additionally needs a caller-owned
execution record binding three actual bounded process owners, raw logs,
ELF32 images, exact run ID/argv, restored descendant scope and full
source/profile identities. This is not cryptographic attestation; synthetic
unit execution records never qualify as actual BabbleSim evidence.

Parse only timestamped prefixed Zephyr BSim peer lines. Hash original bytes
before strict UTF-8 decoding and remove only ANSI SGR sequences for event
analysis. Inventory fixes 60 cases, 65 receiver render phases, 259 ordered
raw CP exchanges and 269 ordered response records, with negative pre-state
and dual partial-batch post-state obligations. A valid CP response alone is
insufficient. Check actual encoded request bytes independently against the
policy descriptors and same-generation public QoS observations, exact ATT
completion/ordered CP bytes, public complete ASE before/after values,
state/metadata continuity, 30 sends on both channels and matching rendered
receiver pushes, legal Idle cleanup and owned reconnect generation changes.
Case-local exact expected warnings are neither suppressed nor a general
waiver. Reject missing, duplicate, stale, skipped, unowned or malformed
evidence and any unexplained warning/error.

## Boundaries and verification

Implement only `scripts/ascs_results.py`, `scripts/check-ascs-results.py`,
`tests/unit/ascs_results/test_ascs_results.py`, this record and PB-051
notes. No policy/client/receiver/SDK/build/fixture modification, simulator
or hardware execution, stage, commit or push. Test real stdlib CLI using
synthetic authored bytes/ELF32 files and valid/mutated records with actual
process invocation. Run Python unit tests with ResourceWarning as error and
`git diff --check`. Historical six per-family logs may be inspected by
the *pure* trace checker only as an offline consistency diagnostic; they
have no matching authenticated execution record for hosted acceptance.

## Focused checker result (2026-10-05)

Implemented `scripts/ascs_results.py` and `scripts/check-ascs-results.py`.
The CLI requires caller-supplied policy SHA-256, run ID, family, three raw
file paths and a separate schema-1 execution record. It rejects unsafe or
oversized files, unowned/nonzero/timed-out participants, mismatched exact
argv, non-ELF32-i386 images, log or image identity drift and unclosed
descendant scopes. The pure checker separately counts policy-ordered cases,
encoded requests/responses, ATT completion, public state snapshots, exact
case-local diagnostics, rendered receiver phases, per-stream sends, Release
Idle and reconnect lifecycle. A synthetic owner record is only a CLI
contract fixture; it cannot authenticate actual process execution.

`python3 -W error::ResourceWarning -m unittest discover -s tests/unit/ascs_results -p 'test_*.py' -v`
passed 6/6. Included complete independently authored seven-case synthetic
control trace through real CLI, plus malformed-policy, altered public bytes,
missing runtime evidence, timed-out/nonzero/unowned participants, wrong argv,
log/image hashes, run ID and unsafe symlink negatives. `git diff --check`
passed. Historical six-family logs passed final **pure** checks (control,
metadata, codec/QoS, lifecycle, dual and reconnect). First post-change
diagnostic found a dual Release checker error: its latest post-CP read had
already advanced into valid setup. Checking the first public post-CP read
fixed this; all six passed on recheck. No historical
family has an authenticated execution record; no new simulator run, SDK
build, physical action, staging, commit or hosted gate occurred. PB-051
remains In Progress, not accepted.

## Direct-review corrections before runner (2026-10-05)

Each successful raw Enable/Update record now requires an actual same-case,
same-generation public ASE read after its CP and before the next raw request
or case end. State E3 for Enable, E3 or S4 for the two declared Update cases,
complete wire metadata and CIG/CIS from same-case public QoS must match.
`ASCS_METADATA` remains an optional diagnostic marker, not acceptance
authority; when present it must match the owned public read. Helper CP
responses now reject repeated ASE IDs.

Three owned process intervals must have strictly positive shared overlap,
and the descendant-scope owner must have a distinct PID. Every adopted
descendant needs one clean zero-exit wait record with matching PID and birth
time; signal cleanup, missing or duplicate reaps fail. The six ELF/log
snapshots retain `(st_dev, st_ino)` from their actual bounded-read file
descriptors, rejecting cross-role hardlinks and parent-symlink aliases,
even with correct byte hashes. PHY output containing warning/error/fatal
markers also fails; empty output or benign INFO remains allowed.

`python3 -W error::ResourceWarning -m unittest discover -s tests/unit/ascs_results -p 'test_*.py' -v`
exercises real CLI on authored synthetic files and targeted metadata parser
cases including removed optional markers and changed/missing public readback,
as well as boundary-touching intervals, reaps, inode aliases and PHY
diagnostics. Historical control, metadata, codec/QoS, lifecycle, dual and
reconnect pairs all passed final read-only **pure** trace recheck under the
approved unchanged policy anchor. This neither verifies historical process
execution nor supplies fresh source/image/run identities. No SDK/BSim or
hardware action, policy edit, stage, commit or push occurred in this repair.
