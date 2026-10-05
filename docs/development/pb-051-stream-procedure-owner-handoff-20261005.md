# PB-051: immutable stream and encoded CP procedure ownership

## Intended repair boundary

Current ASCS client evidence has two independently identified ownership
gaps. Canonical included helper code keeps three static stream objects,
which can let an old callback be attributed to an active generation after
reconnect. Transaction-zero CP notifications can also be accepted without
a currently owned high-level procedure. Neither historical r2 evidence
nor frozen 60/65/259/269 policy is rewritten to hide those gaps.

This phase changes only the additive ASCS client and independent trace
checker. Eight existing generation contexts each own two immutable stream
objects for their whole lifetime. ASCS callback ownership derives from
the containing stream, not the mutable active-context pointer; expected
old-generation teardown remains separately identified. All raw and valid
high-level CP procedures receive a contiguous local procedure ID and
single pending owner. Every CP notification must match that owner's
exact opcode, count, ordered dynamic IDs and response bytes once.
Higher-level helpers require both public SDK callback completion and CP
completion before ownership ends. Local procedure IDs are diagnostics,
not wire transaction IDs: byte-identical delayed replies cannot be
cryptographically distinguished by ASCS notifications.

Unit/CLI fixtures must prove the public logged procedure ledger rejects
missing, extra, stale, reordered and wrong-generation CP records. This
host-only phase performs no SDK build, BabbleSim matrix, hardware action,
commit, push, acceptance rebaseline, or full PB-051 completion. The next
real reconnect run is separately authorized only after code and parser
review.

## Focused implementation checkpoint

ASCS now renames only the **included** canonical three-stream array to
`unselected_canonical_streams`. Each existing generation context owns
two `owned_stream` objects, each containing immutable `owner` and index;
`init_context` registers the separately defined ASCS stream callbacks
once for those persistent addresses. Current ASCS setup, group, enable,
connect, TX registration/send accounting and partial reconnect paths
use those objects. Canonical `client_setup` still registers its old
array/handlers for unselected canonical installers; no canonical C
file changed. Installed `bap_stream.c:105-109` confirms that
`bt_bap_stream_cb_register` stores the supplied ops pointer and returns
void. Installed `bap_unicast_client.c:4752-4765` appends the callback
structure itself, not a copy, so ASCS sets listener wrappers after
canonical `client_setup` and before the first connection. The installed
release path (`bap_unicast_client.c:818-839`) resets the stream before
its released callback; both Release observers therefore use immutable
containing ownership rather than `stream->conn`. Other normal callbacks
check current generation/connection/retirement before forwarding existing
semaphores. An old late Release cannot set a new generation's release
mask; expected closing-old-generation events identify their original
generation explicitly. No allocation/identity assertion substitutes
for the public release barrier, Idle read or rendered recovery.

Every raw action and high-level successful Configure, paired QoS or
individual Enable now opens one locally numbered, mutually exclusive
procedure before invoking SDK or raw GATT. Expected CP bytes are bounded
to eight, including exact opcode, count, dynamic ordered IDs and 00/00
helper results. Incoming CP notifications without a current procedure,
duplicates, wrong bytes or wrong owner latch a wire error rather than
satisfying any later semaphore. Raw transaction numbers still advance
only for the unchanged raw policy actions. High-level Config and QoS
wait for their original SDK callback completions plus the owned CP;
each Enable gets its own 00/00 one-ID CP, enabled semaphore and public
E-state observation before its procedure closes. No newly claimed
over-the-wire transaction ID exists: the local `procedure` field can
bound callbacks but cannot disambiguate byte-identical delayed replies
cryptographically.

The independent parser now requires contiguous BEGIN/END procedure IDs,
one matching in-window CP per owner, raw ATT and policy ownership,
ordered successful helper records and closure before context retirement.
R2's pre-owner logging remains retained **historical** evidence and is
not silently accepted under the strengthened current grammar. The
authored seven-case control trace now carries explicit raw/helper
procedure traffic. Copied mutation tests reject missing, duplicated,
out-of-window, wrong-opcode/ID/order/code/procedure/generation helper
CP as well as unowned raw traffic through the pure checker; one
failure is also checked through the real CLI with matching file/owner
hashes. No SDK build or actual new ASCS matrix was performed in this
phase, so C compile and real callback sequencing await separately
authorized verification.

`python3 -W error::ResourceWarning -m unittest discover -s tests/unit/ascs_results -p 'test_*.py' -v`
passed 12/12. First `ascs_runner` verification had one intermittent
host-worker diagnostic race (`missing/failing worker` instead of the
test's expected `worker failed`); a repeat passed 16/16 without any
runner/test edit. Do not misreport that first failure or reclassify it
as evidence of ASCS wire success. `git diff --check` passed.

Final source checkpoint for this host-only review: ASCS client
`tests/ascs_bsim/client/client.c` SHA-256
`09fd627b6cad56dc9eac9449e3f6ae1649a5259baa98d4047ad7d07aa5deb824`;
checker `scripts/ascs_results.py` SHA-256
`a8f0b72cdf667903838bf1db6085e0abbbe4c85a0ed86d891ea1de480faa88d3`;
the independently approved raw policy `tests/ascs_bsim/cases.json`
remains SHA-256
`addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`.
Incoming CP while a GATT context is closing is now still logged as
unowned and latches `wire_error`; only the `NULL` teardown notification
has its separately owned closure rule. These hashes are source identity,
not a compiled-image or physical acceptance claim.

## One authorized real six-family procedure run, stopped on fixture limit

One fresh, exclusive matrix was run from this source, with parent owner
evidence `/tmp/opencode/pb051-owned-procedure-matrix-owner-r1/` and child
`/tmp/opencode/pb051-owned-procedure-matrix-r1/`. Terminal suite record
SHA-256 `0a79610553d9078f6df5f7aa8582c8dc751164fed8c4804cf49b863cffab5784`
has `accepted=false`, run ID `04c79016b24f4c7fbf537bcfc46f23b8`,
owner exit 1, no cancel/timeout/owner descendants. Both image builds
completed with reviewed exact build diagnostics; final SDK tracked statuses
were empty, source/retained-copy hash checking did not fail.

The first **five** public families independently accepted with actual
three-peer execution, in order: control 7/7/21/21, metadata
13/14/67/67, codec/QoS 7/7/27/27, lifecycle 19/23/91/91, dual
10/10/42/52 (cases/render phases/raw exchanges/response records).
Together these are 56/61/248/258 and **not** a complete 60/65/259/269
matrix. Logged procedure BEGIN counts in these five were 56, 113, 71,
234 and 86 respectively; helper counts 35, 46, 44, 143 and 44,
and raw counts remain 21, 67, 27, 91 and 42. Current owned client
image SHA-256 `4edd4e87d2e631a8123f9f6ca560f013fc1440ba6df30fd9d1a017a3e13ae5e0`,
receiver `2d76431521f2f437bedfe83a20863b9b079f668c748e1aaae27ddfa168ad0daa`,
PHY `5a6919e710a8811e70d10c9beb619a776cd797e23a943697893fe212f70bbda6`.
Historical successful r2 images and the old pre-ledger grammar are
unaltered evidence, not a fallback acceptance path.

Reconnect reached two successful old-owner release barriers and a
rendered gen-2 recovery; its gen-3 A/B Configure, ordered QoS and two
separate Enable CP procedures also closed exactly, with public Streaming
reads. At `client.log:174`, it failed **`error=-12`, `wire_error=0`,
assertions=2, phases=2**, before gen-3 TX registration/render.
`cohort-result.json` records `worker_failure`, client actor return 2,
not a stale callback/procedure parser acceptance. Raw client log SHA-256
`202dd5f128eee536926b6f7cefde5755a2e0ebf60c4f38125ef9924532a03189`;
cohort result SHA-256
`4fb827087e9eb2ccbde57ce85299a53904a3c26b5029c8c7378f828b875a2b11`.

Source-grounded fixture boundary: unchanged `tests/bsim/client/src/bsim_tx.h:22`
limits active streams to two, while `bsim_tx.c:170` also allocates
only two retained `tx_audits`. Registration
(`bsim_tx.c:640-655`) returns `-ENOMEM` with an empty TX slot when
no audit entry exists for a **new** stream address.
`bsim_tx_unregister` (`bsim_tx.c:712-732`) clears active TX slot
but intentionally retains the audit's stream pointer. After gen-2
used two distinct immutable owner stream addresses, gen-3 cannot
register another address under this canonical TX audit capacity.
This provides a source-grounded fixture explanation consistent with
the observed placement of `-12`; the individual failing call does not
have its own raw return marker in this log, so do not claim that call
site was independently measured. No production firmware or SDK defect
is established. No additional attempt, fallback
to old shared streams, canonical TX edit, warning waiver, or skipped
reconnect family was performed. Orchestrator must review an explicit
audit-lifecycle-compatible fixture repair before authorizing another
full matrix. This is neither full PB-051 completion nor a physical/RF
claim.
