# PB-051 explicit BSim TX audit retirement for immutable generations

## Source-grounded failure boundary

Current independently owned ASCS streams use different addresses in
successive generations. `tests/bsim/client/src/bsim_tx.c` holds two
active slots **and** only two retained result audits. Registration
finds an audit by stream address or requires an empty audit entry;
unregister clears an active slot, not the retained result identity.
The single full procedure matrix r1 retained five independently
accepted families and stopped at reconnect gen-3 with client `-12`
before TX/render. This result must remain `accepted=false`; old
reused-address successes and r2 evidence remain immutable.

Expose an explicit public test-fixture `bsim_tx_forget_result(stream)`
that refuses null/unknown/active/in-flight use and releases only a
caller-owned, drained retained audit. Canonical cases do not call it:
existing retained results and 17/26 recipes/hashes/limits remain
unchanged. An ASCS generation copies and logs actual retained send
count/FNV *before* forgetting, checks both never-used and used
streams independently, and retires audit ownership after the CP/IO
drain but before releasing its connection ref. No address recycling,
pool-growth, auto eviction or private pointer identity acceptance.
The normal streaming phase must demonstrate `-EBUSY` while active,
30 actual sends per full stream, successful unregister, retained
result and subsequent explicit forget. Later acceptance still needs
new-gen public state and real receiver-rendered recovery.

This phase adds fixture API/client lifecycle, independent log checker
and tests. ResourceWarning-as-error parser/runner suites and diff
check precede **one** separately owned full six-family matrix in a
new external root. No SDK, production, receiver, policy, hardware,
canonical case, commit or release change.

## Execution record 2026-10-05 (bounded TX audit retirement repair)

Prior context from this session's procedure matrix r1 (immutable,
accepted=false): five families accepted, reconnect generation 3 failed
with client error -ENOMEM before TX/render because both retained
tx_audits entries were held by generation 2 immutable owned streams.
Delegator analysis confirmed shared fixture tests/bsim/client must
retain results until explicit retirement, and canonical clients never
call the new API.

A scheduled full matrix launch into /tmp/opencode/pb051-owned-audit-matrix-r1
was cancelled by signal 15 while staging its first three families. The
runner sealed suite-record.json with accepted=false,
cancelled_signal=15 and errors ValueError: operation cancelled by
signal 15 (three entries). Families control_frame_validation
(7/7/21/21), metadata_length_validation (13/14/67/67) and codec_qos
(7/7/27/27) carry trace verdicts; lifecycle, dual and reconnect never
started. The cancelled root is residue: it is not an accepted matrix
and it is not reusable per the exclusive-root rule. Its sealed
suite-record.json SHA-256 is
8ce12f41cd757eaac8f1fc764e0012d4470e75464cf79cb529c8dc73d32b2746
(direct sha256sum result; the value 0a796105...cffab5784 cited in this
item's backlog notes belongs to the earlier
pb051-owned-procedure-matrix-r1, a different sealed record).

Cancellation left the repository status inventory unchanged: the
`git status --porcelain=v1 -uall` SHA-256
7380c2a4b024a5ba7cdf9a2560562c3f28594107d984fd1aedf20cb2a02c814f is a
digest of the pathname/status listing only, not of file bytes, so it
proves only that no tracked path changed status; it does not by itself
prove byte-identical content. Source-byte invariance for the five
allowed files is established separately by the listed per-file SHA-256
values below (the cancelled run's own source-snapshots directory also
captured content-level copies at stage time).

Repair applied after cancellation, to
tests/ascs_bsim/client/client.c only: retire() previously published
ctx->retired before calling retire_tx_audits. Now the audit drain runs
first while ctx->closing is already true and before retire() publishes
anything; the retired ownership flag is set only after
retire_tx_audits returns 0, then the connection ref is dropped and
ASCS_CLEANUP retired=1 is printed. On any drain failure retire_tx_audits
returns the error, retired never publishes, closing stays true and the
held connection ref is kept, so a later retire can still finish the
partially cleaned generation and stale callbacks stay gated by closing
alone. No change to bsim_tx.c, bsim_tx.h, canonical bsim_client_main.c
or any canonical scenario.

Verified state of the other directed repairs before edit (already
present, unchanged by this session): bsim_tx_forget_result rejects
null (-EINVAL), unknown stream (-ENODATA), still-registered or
in-flight association (-EBUSY) and clears only an inactive drained
audit; the checker _tx_audits active stage compares the real CLI
forget_ret value against the literal -16 with no dead conditional
expression. The synthetic ControlTrace unit fixture already renders
per-phase ASCS_TX_REGISTER(ret=0), stage=active forget_ret=-16,
stage=retained with sends=30 and the phase/index derived FNV after
unregister, and after ASCS_CP_CLOSE a final retire (result_ret=0,
matching the last retained sends/FNV), stage=forget ret=0 and
stage=forgotten result_ret=-61 before ASCS_CLEANUP, all through real
CLI/pure-checker inputs with negative controls for missing/duplicate
audit, altered count/hash, absent forget, lingering result,
never-used unexpected result and wrong generation/window. Negative
control "unified forget semantics" already rejects a synthetic active
success path that forgets instead of EBUSY.

Post-repair focused verification, all PASS:

- python3 -W error::ResourceWarning -m unittest discover
  -s tests/unit/ascs_results -p 'test_*.py': 13/13
- tests/unit/ascs_runner: 16/16, tests/unit/bsim_link_env: 7/7,
  tests/unit/bluez_host_process: 11/11,
  tests/unit/bluez_host_descendants: 4/4 (same ResourceWarning mode)
- bash -n scripts/ascs-bsim-run.sh OK
- git diff --check OK

Post-edit source SHA-256: client.c
220aacc8223508d964af1c3c823cda110d925345865ad02352b8cf0d45fda0fb;
bsim_tx.c 3bd1c448f28551fd2a29082baed8bd147016ea45fa5fd20c73c0ce41e3a9185c
and bsim_tx.h
e5bea4d50f2762235d4fe8f641f6b3f3045a38c6dec715c2b64dfd3c2c61bc51
unchanged this phase; ascs_results.py
45626cd3dfd8ef46bcc6786d99a04359b444f7e611ee50904c504f7c94ad8561 and
test_ascs_results.py
3c6c89b4a7a5f7131cc9c346ea81dc250ab479b7780eaeb4aac1f8d501b68640
unchanged this phase; policy digest stays
addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c with
totals 60 cases/65 renders/259 raw/269 records.

BLOCKED for the one authorized matrix: its mandated target root
/tmp/opencode/pb051-owned-audit-matrix-r1 already exists as cancelled
residue and the handoff forbids clobbering. Resolution needs either a
fresh exclusive root (for example ...-r2) with the rerun explicitly
authorized, or an explicit owner disposition of the residue root; the
new-generation allocation path also has no simulated proof yet because
reconnect never ran, so acceptance still requires the eventual full
run to show generation 3 register succeeding after explicit retirement.

## Authorized full matrix r2 result (2026-10-05)

After direct review confirmed the retire publication fix, the checker's
exact -16 comparison, and the digest/hash-semantics corrections above,
one fresh full matrix was authorized into
/tmp/opencode/pb051-owned-audit-matrix-r2 (verified absent first; r1
and all other roots preserved). Command:
`env -u ZEPHYR_BASE nix develop -c bash -c 'ASCS_OUTPUT_ROOT=/tmp/opencode/pb051-owned-audit-matrix-r2 bash scripts/ascs-bsim-run.sh'`,
exit 0, owner log SHA-256
7c83232682276bee9053e3d7958f3398fb9bec4b0eb71f7bb3d033526dadf5b0.

Result: sealed suite-record.json SHA-256
1ac67c47bddf9d1a697c4afd655f898150b187c538d54f345958b0168f676d6a with
accepted=true, policy digest
addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c and
exact totals 60 cases / 65 render phases / 259 raw exchanges / 269
response records. All six families accepted by the unchanged
independent checker: control_frame_validation 7/7/21/21,
metadata_length_validation 13/14/67/67, codec_qos 7/7/27/27, lifecycle
19/23/91/91, dual 10/10/42/52, reconnect 4/4/11/11. Run ID
9c6654340a5d4c4a9cc51caec4c928c7; every cohort accepted=true with
scope ok, no unexpected live descendants, no cancelled signal; all
nine process records rc=0 with empty cleanup_errors.

Reconnect proof from families/reconnect/client.log (SHA-256
fa943a4dc02c9047c6deed46cc076f6e6f7f306e7d0ec614e5e18a246dee0178):
generation 1 teardown emits stage=unused result_ret=-61 forget_ret=-61
for both indices (real ENODATA for never-used streams); each later
generation registers ret=0 and immediately exercises stage=active
forget_ret=-16 (EBUSY guard), sends exactly 30 (10 for the partial
streaming stream, fnv ab61d129), unregisters with retained result
matching, then stage=retire result_ret=0 with the exact retained
sends/FNV, stage=forget ret=0, stage=forgotten result_ret=-61, and
only then ASCS_CLEANUP retired=1; generations 2, 3, 4 and 5 each
successfully re-register fresh owned streams after the previous
generation's explicit retirement, proving the earlier
procedure-matrix -ENOMEM allocation failure is resolved by explicit
retirement with no pool growth, eviction or address reuse. Receiver
rendered output present for all four reconnect phases
(families/reconnect/receiver.log SHA-256
90782b1f4d83820d6888df081ab120ced2e596a8b86ff489d44b99923ea4c16a).

Warnings retained within the approved set only: builds show exactly 2x
BT_LL_SW_SPLIT, 2x BT_CTLR_SET_HOST_FEATURE, 1x BT_CTLR_CENTRAL_ISO,
1x BT_CTLR_PERIPHERAL_ISO experimental notices plus the 2x native SoC
unsupported-SoC CMake notice; zero compiler, assigned-value or linker
diagnostics in both cmake/ninja logs. Runtime receiver warnings are
exactly the pinned case-local rejection sets in each family (control 5,
metadata 19, codec_qos 0 with the four exact "Codec config rejected:
code 0x08 reason 0x02" info lines and 0x0903/0904/0905 QoS responses,
lifecycle 11 including Unknown ase 0x00, dual 6, reconnect 0); no new
or unlisted warning. Images: client.elf
7df3a4fb387a55b102b25af9fc1d40db30ee211cbc8a44241333cfb475e9eb73 built
from client.c 220aacc8223508d964af1c3c823cda110d925345865ad02352b8cf0d45fda0fb,
receiver.elf
9150c148de088065320cc3eacde23c9e52ca575ec191406af65c8bfe36d1e991,
phy.elf
5a6919e710a8811e70d10c9beb619a776cd797e23a943697893fe212f70bbda6.
No post-matrix source edit; the six-family software-model matrix scope
is complete on this evidence. Physical boundaries, hosted CI on PR 16
and Done remain separate unclaimed work.
