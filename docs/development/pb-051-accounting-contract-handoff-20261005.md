# PB-051: independently authored encoded-wire accounting policy

## Policy source and limits (2026-10-05)

`tests/ascs_bsim/cases.json` is reviewed input to a future strict runner,
not a result exported by the receiver/client under test. This phase authors
only the fixed versioned policy for the six existing test IDs. It does not
implement a parser, execute test firmware, or infer missing requirements from
passing logs. Installed NCS v3.4.1, Zephyr revision
`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`, two sink ASEs, zero
source ASEs, metadata capacity 16, negotiated MTU 65 and valid 48-kHz,
10-ms, 120-octet LC3 regression stimulus are explicit; no formal
qualification, RF, analog or independent codec claim follows.

Six ordered families: control framing 7 cases / 7 renders / 21 raw exchanges
and records; metadata 13 / 14 / 67 / 67; codec/QoS 7 / 7 / 27 / 27;
lifecycle 19 / 23 / 91 / 91; dual 10 / 10 / 42 / 52; reconnect 4 / 4 /
11 / 11. Total **60 cases, 65 fresh render phases, 259 raw checked
exchanges and 269 ordered per-ID response records**, zero exclusions.
Each case specifies ordered raw request descriptors and ordered CP response
records, render and generation delta, and exact expected runtime diagnostics.
Control responses are five-byte global error records (count FF, ID 00).
Dynamic `{A}`/`{B}` placeholders require newly discovered sink ASE IDs;
QoS descriptors must copy actual public state-2 payload including CIG/CIS,
never guess a private endpoint. The fixed Codec Config stimulus is separately
declared and edited only in the named field. High-level *valid* setup
notifications are not falsely counted as raw action transactions.

The eventual independent result checker must anchor this file's exact raw
SHA-256 after human/orchestrator review; never derive its required inventory
from DUT CASE markers or pin the digest in parser code before review. A
passing family process alone is insufficient: verify each actual encoded
request and ATT write completion, ordered CP bytes, complete both-ASE
public before/after preservation and accepted state, corrected retry,
two 30-send stream audits, independent receiver-rendered phases, Release
Idle, generation/CP ownership and scoped process exits. Missing/duplicate
cases, skipped or partial steps, stale generation, wrong response/order,
missing state/readback/render, nonzero child, timeout, profile drift and
unknown warning must reject. Exact case-local warning templates are not a
generic regex waiver. Build-profile experimental controller notices remain
visible; compiler, assigned-value, linker-architecture and unrelated
warnings fail. No synthetic BlueZ result substitutes real receiver traffic.

## This phase

Add only policy JSON, this record and PB-051 implementation notes. Verify
strict JSON parsing, types, uniqueness, authored action and record totals,
phase/generation deltas, and source case-name consistency without executing
firmware or modifying SDK, production, fixtures or prior evidence. Retain
actual file SHA-256 for review without claiming the future runner trusts it
yet. No stage, commit or push.

## Actual policy-only verification

Strict JSON load and offline checks passed on final file. Six family IDs and
all 60 case IDs match authored client case declarations (the two standalone
metadata cases were checked alongside its 11-entry table). Family and grand
totals match independently counted action arrays and ordered record arrays:
60 cases / 65 render phases / 259 raw exchanges / 269 response records.
The only phase-delta-two cases are metadata_update_streaming plus the four
initial Streaming lifecycle cases; only the four reconnect cases have
generation delta one. Action and response numeric/enum types, unique case
IDs, nonempty ordered actions, bounded exact-hex templates, request opcode
echo and zero exclusions/client diagnostics were checked. A separate
post-authorship offline comparison with retained immutable per-family raw
logs matched all 60 case-local warning expectations and all 259 ordered CP
response byte strings; 236 exact-hex request templates matched actual
traffic. This comparison is consistency review, **not** a trusted runner,
new firmware execution, formal qualification or permission to derive policy
from later DUT results.

Initial ad-hoc verification commands failed on checker mistakes, not policy:
one compared the `cases` list with its integer count, another looked only
in the metadata case array and omitted its two standalone authored cases.
Corrected read-only checks passed. Initial candidate policy SHA-256:
`737e5c5014e98a3e2821fa9212a63fd65f09d4a9e987c705bb96f379d0650a38`.
This is an **unapproved candidate review digest**; it has not been pinned
in a checker or accepted by a hosted/physical gate. `git diff --check`
passed. No SDK/test execution, code edits, stage, commit or push in this
phase.

## Reviewed state-obligation refinement (2026-10-05)

Preliminary digest above was never pinned or approved. Review found that a
correct rejection code alone cannot establish the named *precondition*.
The same 56 negative actions now carry exact typed `pre_states` mappings
for both dynamic ASEs A/B. The ten two-ASE partial-result batch actions
also carry exact `post_states`: accepted A changes where required, rejected
B retains its original public state. No positive action gained a state
precondition, and no original request, CP response, diagnostic, case name,
phase/generation delta or count was changed. Four reconnect cases retain
their separately declared `pre_disconnect` states and fresh Idle obligation.

A future independent checker must read complete public ASE byte snapshots
from the *same step and connection generation* before each negative
request, require both state bytes to equal `pre_states`, then read both
complete ASE values after its CP response. For single-ASE rejection both
full values must remain byte-identical unless the named action explicitly
accepts an ASE. For partial batches it must confirm `post_states` for both,
rejected B's full value byte-for-byte unchanged, and accepted A's separately
required state and metadata/QoS as declared by the case. A QoS snapshot
request copies fifteen fields from the current step/generation's observed
public QoS value, never stale previous-case data. Private endpoint fields,
CP status alone, CASE flags or callback counts cannot supply these proofs.
Unexpected or missing readbacks remain failure, not a skip.

Strict JSON load, source case-name matching, action and record types/counts,
phase/gen arithmetic and exact request/response template consistency
remain 60/65/259/269. Read-only comparison of the independently authored
56 negative `pre_states` obligations against retained public ASE reads
matched all 112 state bytes; ten dual batch `post_states` matched 20
publicly observed state bytes. This offline check was run after authoring,
not used to choose expected states or as a new firmware test. Zero exclusions,
zero accepted client runtime warnings and existing case-specific receiver
warning counts remain unchanged. `git diff --check` passed.

New raw policy SHA-256:
`addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`. Treat this as a
candidate for orchestrator review only. Do not pin it in a parser until
reviewed; no test, runner, production, SDK, hardware, commit or push in
this policy refinement.
