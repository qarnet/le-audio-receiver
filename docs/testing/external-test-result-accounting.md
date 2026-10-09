# PB-052: external test result accounting, schema v1

## Scope and trust boundary

`scripts/check-external-test-results.py` validates one externally produced report
against a reviewed inventory and caller-owned execution record. Acceptance means
the submitted evidence is complete and consistent with the independently pinned
policy. It is **not** proof of authentic execution, freshness, firmware behavior,
Bluetooth qualification or conformance. A caller capable of forging matching
records and reports is outside this parser's trust boundary.

PB-053 owns real isolated execution, discovery/collection, prerequisite checks,
producer verification and immutable evidence sealing. Do not use a reconstructed
summary as raw execution evidence. No production external-suite inventory is
invented here; parser tests use explicitly authored synthetic inventories and
real local pytest emission.

## Public CLI

```sh
python3 scripts/check-external-test-results.py \
  --inventory /absolute/evidence/inventory.json \
  --expected-inventory-sha256 TRUSTED_REVIEWED_DIGEST \
  --run-record /absolute/evidence/run.json \
  --report /absolute/evidence/report
```

The expected digest must come from independently reviewed policy. Computing it
from an untrusted supplied inventory at validation time permits both inventory
and run record to shrink together and defeats the required-case anchor.

Exit 0 accepts accounting. Exit 1 rejects evidence; exit 2 rejects unsupported or
invalid inputs, invocation or I/O. Every nonzero exit fails the calling lane.
Output is deterministic JSON for the same input bytes, with acceptance, reason
code/detail, exact interpreted case IDs/outcomes and input size/SHA-256 provenance.
Missing/unexpected IDs and separate unselected exclusions are reported where
parsing reaches those checks. Callers must retain stdout, stderr and actual exit
status alongside raw inputs. The CLI writes no files and executes no child tool.

All inputs are bounded to 16 MiB, case universes/reports to 100000 cases. These
are resource guards, not modified firmware/audio acceptance limits. JSON rejects
duplicate keys, unsupported versions, unknown schema fields and nonfinite values.
Reports are UTF-8. XML DTD/entity declarations and unsupported nested structures
are rejected. Report hashing and parsing use the same already-read bytes.

## Supported report profiles

| Profile | Producer pin | Supported structure |
| --- | --- | --- |
| `bluez-tester-v1` | BlueZ `4dc15be8ee3f7422d447087f1893d215575cb2c8` | One complete `Test Summary`, per-case rows, reconciled aggregate and terminal execution time |
| `pytest-xunit2-v1` | pytest `8.4.2` | Native `<testsuites name="pytest tests">` wrapper with one `<testsuite>` and xunit2 testcases |

Source evidence for the BlueZ grammar is `src/shared/tester.c` at that pin:
`print_summary` lines 56-58, `tester_log` lines 146-168 and `tester_summarize`
lines 371-424. Case-name width is a minimum, not truncation. ANSI SGR sequences
are normalized for interpretation; raw bytes remain hashed. `Timed out` counts
as failure. `Not Run` can coexist with upstream exit 0, so it is always rejected
for required execution. Aggregate counts must exactly match rows; one-decimal
percentage permits only its half-unit formatting error, not a case-count waiver.

Source evidence for pytest is `_pytest/junitxml.py` 8.4.2, particularly outcome
emission at lines 190-250 and wrapper/count emission at lines 639-675. No outcome
element reports pass; empty `<failure/>` or `<error/>` still means failure.
Any `<skipped>` is rejected, including xfail. Duplicate IDs and phase-expanded
failure reports cannot be accepted. Failure reports can legitimately count
phases differently; rejection is not a claim that those files are invalid XML.

The pytest recorded command must use an explicit `pytest`/`py.test` launcher or
`python[3[.minor]] -m pytest`, with `--runxfail` **before** any `--` option
terminator. This removes expected-failure semantics: non-strict XPASS otherwise
looks like ordinary pass in JUnit. A real test directory named `--runxfail` after
`--` does not enable the option. Unsupported wrappers are rejected, not guessed.
Future producer versions or merged/nested JUnit dialects require explicit profile
review; they are not silently normalized into this profile.

## Inventory

Mandatory fields, with no others:

```json
{
  "schema_version": 1,
  "suite": "reviewed-host-suite",
  "profile": "bluez-tester-v1",
  "producer": {
    "name": "bluez",
    "revision": "4dc15be8ee3f7422d447087f1893d215575cb2c8"
  },
  "required": [["reviewed/case-one"], ["reviewed/case-two"]],
  "exclusions": [{"id": ["unselected/case"], "reason": "Reviewed non-required scope"}],
  "prerequisites": ["isolated-host-ready"],
  "prerequisite_rationale": "Actual lane checks readiness before executing cases"
}
```

Required IDs must be nonempty, unique and disjoint from exclusions. Exclusions
have nonempty reasons and are **unselected/absent from execution report**, never
permission to skip a selected case. Their IDs stay in the discovered universe.
No excluded/unreviewed reported case is accepted, even if it passes.

BlueZ ID is `[exact_case_name]`. Pytest ID is `[exact_classname, exact_name]`,
preserving decoded parametrization. The inventory suite field scopes those IDs
to this invocation; no ambiguous joined-string identity is used. Do not case-fold,
strip paths, match substrings or substitute cardinality for exact coverage.
Strings must be nonempty/control-free; BlueZ summary padding must remain
unambiguous. Pytest producer is exactly `{"name":"pytest","version":"8.4.2"}`.

Prerequisite IDs are unique. Empty prerequisite sets are supported only with
an explicit nonempty rationale; an environment check is not made optional by
omitting its result. Observed IDs must equal the declared set exactly.

## Caller run record

Mandatory fields, with no others:

```json
{
  "schema_version": 1,
  "run_id": "caller-owned-fresh-run",
  "suite": "reviewed-host-suite",
  "producer": {
    "name": "bluez",
    "revision": "4dc15be8ee3f7422d447087f1893d215575cb2c8"
  },
  "inventory_sha256": "<64 lowercase hex characters>",
  "discovered_cases": [["reviewed/case-one"], ["reviewed/case-two"], ["unselected/case"]],
  "child": {
    "argv": ["/absolute/pinned/test-binary", "--reviewed-selection"],
    "cwd": "/absolute/caller-work-directory",
    "started_at": "2026-10-03T01:00:00+00:00",
    "ended_at": "2026-10-03T01:00:01+00:00",
    "termination": "exited",
    "exit_code": 0
  },
  "prerequisites": [{"id": "isolated-host-ready", "termination": "exited", "exit_code": 0}],
  "report": {"bytes": 1234, "sha256": "<64 lowercase hex characters>"}
}
```

Example placeholders must be replaced with real values. Suite/producer must
match inventory. Discovered cases must exactly equal required plus excluded
universe, without duplicates. Discovery/list/collection must be independently
performed by the execution owner; a copied policy list is not execution proof.

Normal termination and integer exit 0 are mandatory for child and prerequisites.
Boolean `true` is not exit 1/0. Timeout, cancellation, signal, spawn failure,
skip and wrapper-only success cannot be accepted. Child argv is a nonempty
control-free string list, cwd absolute, timestamps timezone-aware and ordered.
Report byte count and lowercase SHA-256 must match the actual raw report.

An accepted run record does not prove command authenticity, monotonic clock
accuracy, unique run ID enforcement or replay prevention. Those require caller
ownership/sealing. A previous accepted report cannot be relabeled fresh evidence
for PB-053, firmware or physical tests merely by changing this metadata.

## Public-boundary regression checks

```sh
env -u ZEPHYR_BASE nix develop -c \
  python3 tests/unit/external_test_results/test_external_test_results.py
```

The canonical inventory discovers this one Python child automatically. Tests
invoke the real CLI with file-backed encoded inputs and retain command output in
their private fixture directories. They exercise both complete profiles and
missing/duplicate/unselected cases, zero execution, inconsistent counts, all
unhappy outcomes, bad hashes, independent inventory shrink, prerequisite and
child failure, malformed/schema/unsafe XML and actual pytest emission.

Actual pytest controls include pass, failure, skip, xfail, teardown error,
double failure, collection error, empty selection and positional `--runxfail`
XPASS. Negative controls must change inputs and fail the intended boundary,
not merely trigger an unrelated XML syntax error. Fixtures are parser tests;
they do not claim BlueZ, receiver, RF, codec, PCM, I2S or analog acceptance.
