# PB-051 ASCS results evidence (2026-10-05)

This document records the retained, read-back evidence for the PB-051
ASCS lane as of 2026-10-05: the native parser baselines, the physical
pure-parser review, the earlier failed or cancelled matrix roots, and the
final accepted full six-family matrix r3. Nothing here is inferred; every
value below was read back from the retained record files at the cited
paths. Clean-commit and hosted-CI gates are explicitly **pending** and no
hardware was used for this document.

## Native public parser baselines (host, no board action)

- Baseline before the guard (unchanged installed SDK parser): the earlier
  pre-option state of `tests/unit/ltv_bounds` had no `PB051_LTV_GUARD`
  CMake option at all (the option was added together with the guard), so
  the baseline ran with the plain unmodified SDK parser; 6 pass / 1 fail /
  0 skip / 7 total,
  `METADATA_VALIDATION ret=0 delivered_entries=1 value=aa` (the off-by-one
  entry passes through the raw parser); west exit 1 expected and observed.
  Raw owner log `/tmp/opencode/pb051-ltv-native-resume-r1/build-and-run.log`,
  SHA-256 `fc23ad805a4d492149a99dcd2c3293deab7a965349603f33b8d4f43328e66b1b`.
  Today the same comparison shape is reproduced with the test-only
  `PB051_LTV_GUARD=OFF`, which must not be read as implying the option
  existed at baseline time.
- After the repository-owned per-entry LTV guard:
  12 pass / 0 fail / 0 skip / 12 total,
  `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`;
  `TESTSUITE ltv_bounds succeeded`, PROJECT EXECUTION SUCCESSFUL.
  Raw owner log `/tmp/opencode/pb051-ltv-native-guard-r1/build-and-run.log`,
  SHA-256 `784bdfa23ca5be117d307ab1be9fa916aad64d3da0cda4de2dfb4deda61ade6b`.
  Source under test: `src/bt_audio_ltv_guard.c`
  (`__wrap_bt_audio_data_parse` forwarding valid entries to
  `__real_bt_audio_data_parse`), linked via `-Wl,--wrap=bt_audio_data_parse`.

## Physical reviewed pure-parser run r4 (CPUAPP diagnostic image)

Board side: explicit candidate `158D8E1D` (VID 2886 / PID 0066, interface 2,
deepest matching USB sysfs parent, current tty `/dev/ttyACM3`), fresh CMSIS-DAP
identities before and after (DPIDR `0x6ba02477`; AP0/1 `84770001`, AP2
`32880000`, AP3 `00000000`; FICR PART `0x00054b15`, VARIANT `0x41414330`),
identical at posttest; retained proofs under
`/tmp/opencode/pb051-ltv-physical-guard-execution-r4/`
(`initial-identity.json`, `pre-reset-identity.json`,
`posttest-identity.json`, `posttest-readonly-proof.json`). The tty and USB
paths in this section are DATED raw identity evidence from that 2026-10-05
capture only, not a current probe-to-board mapping; never reuse a serial or
tty from old evidence, always re-resolve with fresh identity checks.

Image artifacts (prepared in `/tmp/opencode/pb051-ltv-physical-guard-r1/`,
owner log SHA-256 `17928e9db13046891d84a45e5d2b4ecaed64aab4abf45c60714eb875c8f9ec44`):

- `build/zephyr/zephyr.hex` 165910 bytes, SHA-256
  `54c236f21759d6326db427c149825599be7a5507c231655d100ff69c8439177c`.
- `build/zephyr/zephyr.elf` (ARM) SHA-256
  `f3514949805634306ed81ffdc4db8cd2954b04cfb0edebbb94836cdb4fc29425`.

Capture: raw unmodified UART file `uart.log`, 3823 bytes, SHA-256
`7f9c4f4e26379a1b8dea12bdff90c7ef1c715262b6dc51cfabd14af9aeaef639`. The
whole file contains a 64-byte prior-run tail (`[0,64)` SHA-256
`82fd810705a3f7de7169e78456d50ed0e0bdc485b262d926bbae2239a120edcb`); the
fresh boot window is `[64,3823)`, 3759 bytes, SHA-256
`64ff257f5e9f992343d5133fcffa60328f299c05e127a88befd1b0796da8f03a`.

Result: 12 unique `START`/`PASS` lines, summary
`SUITE PASS - 100.00% [ltv_bounds]: pass = 12, fail = 0, skip = 0,
total = 12`, `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`,
single terminal `PROJECT EXECUTION SUCCESSFUL`, no fault or warning in the
raw window. Review verdict
`/tmp/opencode/pb051-ltv-physical-guard-execution-r4/reviewed-physical-verdict.json`
SHA-256 `4f986206b060e90bd035309675cd36fb38a21f7d52edff122d44e193f9f1f5db`,
`accepted: true`, six negative controls (missing-case, fail-one,
wrong-guard, extra-boot, truncated-terminal, fault) all `rejected: true`
with semantic reasons. The original failed verdict
(`native-capture-verdict.json` SHA-256
`11e6b4d2eb32ff379c6969b9fcfcb56dd4b73c053b9bb69bc31e86c86b2ad22a`,
accepted false because a generic FAIL regex matched the valid
`fail = 0` summary) is preserved unmodified, not rebaselined.

Acceptance boundary: physical CPUAPP public parser API 12/0/12 only. No
physical ASCS, RF, I2S, codec quality, analog or FLPR acceptance is claimed.
The diagnostic parser image remains on the board; any later physical work
requires a fresh identity and role provisioning.

## Earlier matrix roots (all immutable, none supersede r3)

| Root | Suite-record SHA-256 | accepted | cancelled | run_id |
|---|---|---|---|---|
| `pb051-owned-full-matrix-r1` | `e2cf1f6dd9eecc4446f39c5f5daa9b8b85a09f9b8dd50945a7a46ccba6acbe2b` | false | none | `03bfe0ebac654fe883441a8f1a4ce8a6` |
| `pb051-owned-full-matrix-r2` | `a1ece653b093e4056ed1396205be1fb6dd529a168f74740937d6a936fb7080eb` | true | none | `0f6e3024ced843f481dcca2633dca6f4` |
| `pb051-owned-procedure-matrix-r1` | `0a79610553d9078f6df5f7aa8582c8dc751164fed8c4804cf49b863cffab5784` | false | none | `04c79016b24f4c7fbf537bcfc46f23b8` |
| `pb051-owned-audit-matrix-r1` | `8ce12f41cd757eaac8f1fc764e0012d4470e75464cf79cb529c8dc73d32b2746` | false | 15 | `bc13cb555c85444a9895a7359c772df8` |
| `pb051-owned-audit-matrix-r2` | `1ac67c47bddf9d1a697c4afd655f898150b187c538d54f345958b0168f676d6a` | true | none | `9c6654340a5d4c4a9cc51caec4c928c7` |

- `pb051-owned-full-matrix-r1`: failed before any family with
  `receiver: unlisted compiler/Kconfig/CMake/link warning`; no families
  recorded, runtime capture never reached.
- `pb051-owned-full-matrix-r2`: accepted full matrix (exact 60/65/259/269,
  18 zero-exit actors) on the runner source before the runtime-boundary
  review and before the later stream-ownership/audit runner and client
  repairs. Earlier source and earlier accounting contract: historical
  evidence of that pre-ownership code state only, not the current client
  or runner source and not current acceptance proof.
- `pb051-owned-procedure-matrix-r1`: five families accepted
  (control 7/7/21/21, metadata 13/14/67/67, codec 7/7/27/27, lifecycle
  19/23/91/91, dual 10/10/42/52) then reconnect generation 3 failed with
  client `-ENOMEM` before TX/render; the retained-audit allocation gap was
  later fixed by explicit retirement, not by any canonical change; old
  `suite-record.json` accepted=false, raw reconnect client log
  SHA-256 `202dd5f128eee536926b6f7cefde5755a2e0ebf60c4f38125ef9924532a03189`.
- `pb051-owned-audit-matrix-r1`: cancelled by signal 15 while staging its
  first three families; runner sealed one terminal record
  (accepted=false, `ValueError: operation cancelled by signal 15` three
  times) with control/metadata/codec_qos trace verdicts; not an accepted
  matrix and not reused per the exclusive-root rule.
- `pb051-owned-audit-matrix-r2`: accepted full matrix after the TX audit
  retirement repair and before the runtime-boundary call-site review and
  the post-r3 worker-cohort race repair; earlier than the current final
  runner source and labeled evidence of that intermediate source, not
  final-source acceptance.

## Final matrix r3 (2026-10-05, one authorized run)

Command (repo root, exact):
`env -u ZEPHYR_BASE nix develop -c bash -c 'ASCS_OUTPUT_ROOT=/tmp/opencode/pb051-owned-audit-matrix-r3 bash scripts/ascs-bsim-run.sh'`;
target root verified absent before launch (no clobber of any previous
root). Owner log `/tmp/opencode/pb051-owned-audit-matrix-r3.log`, SHA-256
`7c83232682276bee9053e3d7958f3398fb9bec4b0eb71f7bb3d033526dadf5b0`;
runner exit 0.

Sealed record `suite-record.json`, SHA-256
`479a5237f591b30b97c0a73e6b75e3e40e6b90c70ee42a25a87e69f0e87477f7`,
`accepted: true`, `cancelled_signal: null`, `errors: []`,
`cleanup_errors: []`, run_id `480d5bd5f6204df69479cc862d0586f8`, policy
digest `addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`.

Exact totals: 60 cases / 65 render phases / 259 raw exchanges / 269
response records. Family matrix identical to the table above and every
family's independent trace accepted: control
(`verdict_sha256 3cfc37d643a1741295636f345cbeacfad8feab4514c10ab22fdc3c946058d1cb`),
metadata `b37e11ea20fde7ffa5e023a16c79b2f5f14bb1e68b4c741f688c5b5e161d8cbe`,
codec_qos `a7bb3fcb88b7946f27d43df1339d8a212bc2206efb52df80b770c62e5a627741`,
lifecycle `fcb2b77e3f511d3941a1fea511d5da19f0375cbdd84ff2c6cfffc403abed5c5e`,
dual `33fb0465911ff8b55ba7b199f5a74b89e067c32737062c96c595e9c629f20694`,
reconnect `9d637f2c02cdc90ff75afa47f489386e539330b8a8c8f3dc68b17739a919497a`.

Raw per-family peer log hashes from the retained files:

| Family | client.log SHA-256 | receiver.log SHA-256 |
|---|---|---|
| control_frame_validation | `4046ce2dddc8f61f1b6f6d846c2c2bbc2d51ab00c03720ff843b14ed1ab022d0` | `642c4e369ad569757f2142d2e44d4ba360aebfb9e385b67c6afb2775dad1635a` |
| metadata_length_validation | `0e176060820e83212e2343559d631ce8935662b7833749893725101a5504accc` | `279bf8e33c26fc1f97e975161bcacc98a8a158cfc7631ad2aff1e2b5cf212a57` |
| codec_qos | `6eac0423f51b4d771f3f174fdc07d0f1321293a6bbdc7971a4f05d8192d821c6` | `f11dab5ede702ae6484fc066686f796b623c88f06b7c15eae0f4215328b80d6b` |
| lifecycle | `655a276efd3b0095876b88703eff737688a776cd078bdf82620db971a69fe49b` | `4979e3375b314d27d1d993fb17ef4200ecaf5310752ed11e6491e499c01f09e3` |
| dual | `8b5f080ce85b3e818abd2842897f7e71e5021d111baccb214539ab53e9eed729` | `d6190d6f2fc49e58857958b2a898439639e3624ba920c325116ca73ccfedda85` |
| reconnect | `fa943a4dc02c9047c6deed46cc076f6e6f7f306e7d0ec614e5e18a246dee0178` | `90782b1f4d83820d6888df081ab120ced2e596a8b86ff489d44b99923ea4c16a` |

images: client.elf SHA-256
`991fab96396d25c5e10046a1e304db15b03447f2162fb75488cd9a786713ed6e`,
`images/receiver.elf`
`5cc32900878060b4baab0fe3e627c0e91f570c86f2333f03c5259149cd511c7b`,
`images/phy.elf`
`5a6919e710a8811e70d10c9beb619a776cd797e23a943697893fe212f70bbda6`.

Runner-source note: r3 ran on scripts/ascs_bsim_run.py SHA-256
`ca974e1c2c082fbfd3b4b6eecb4a9d4200f46763239185a50472ce210577339d`.
After r3 a worker-cohort race repair (failure attribution for fast-exiting
workers) changed the runner to SHA-256
`811da2c768b75df0e976fe136247904765bcbe74b96ea18aaa043fe709acd821`; r3 is
therefore evidence of the pre-repair runner, not final-source acceptance.
The upcoming clean canonical gate must rerun the full matrix on the
committed race-fix source.

Review-required properties confirmed by direct record reads after the run:

- All 18 actor process records (`*-process.json` across six families)
  returncode 0, `ok: true`, no timeout, no cancellation, empty
  `cleanup_errors`, `descendant_cleanup_required` false; every cohort
  result accepted with `scope.ok` true and no unexpected live descendants;
  final suite scope clean.
- Actual PHY argv in every family job carries literal `-nodump`
  (for example `reconnect/phy-job.json` has
  `["-D=2", "-nodump", "-sim_length=250e6"]` after device, `-v=2` and the
  `-s=ascs_480d5bd5f6204df69479cc862d0586f8_<family>` session).
- 15 component headers (`libUtilv1` 11 + `libPhyComv1` 4) plus
  `FindBabbleSim.cmake` are inside the frozen `source-hashes.json` (150
  source entries total, snapshot copies under `source-snapshots/`).
- Runtime identity recorded once at capture and unchanged at every
  boundary class of this runner source: `runtime-ready:pre` right after
  the capture, `runtime-ready` after the readiness preflight,
  `cohort:<family>:pre` and `cohort:<family>:post` around each of the six
  cohorts, `final` in the success tail, and `post-run` in the execute
  finally (the earlier procedure-matrix runner source had only the
  final success check). Suite record `runtime` equals the same three
  library hashes
  (`libCryptov1.so` 4043672 bytes `c6cff2c6...cbf3bf1`,
  `lib_2G4Channel_NtNcable.so` 24576 bytes `7e925234...abe0a`,
  `lib_2G4Modem_Magic.so` 27976 bytes `8a7c205e...98dbe`); no
  `post-run runtime integrity` error exists. A vanished root or missing
  library would be reported by this same finally check instead of being
  silently skipped.
- Reconnect generation proof from the raw log: gen 1 teardown emits
  `stage=unused result_ret=-61 forget_ret=-61` for both indices; every
  later generation registers `ret=0`, exercises `stage=active
  forget_ret=-16` (EBUSY guard), sends 30 (partial streaming stream sends
  10, retained `ab61d129`), unregisters with retained result matching,
  completes `stage=retire result_ret=0` with the exact retained sends/FNV,
  `stage=forget ret=0`, `stage=forgotten result_ret=-61`, then
  `ASCS_CLEANUP retired=1`; generations 2, 3, 4, 5 all re-register fresh
  streams after explicit retirement with no `-ENOMEM`.
- Receiver rendered output for all four reconnect phases present
  (`ASCS_RENDER` count 4, summary `phases=4`).
- Warning envelope: build logs contain exactly the approved experimental
  Kconfig notices (client `BT_CTLR_CENTRAL_ISO`, receiver
  `BT_CTLR_PERIPHERAL_ISO` plus shared `BT_LL_SW_SPLIT`,
  `BT_CTLR_SET_HOST_FEATURE`) and the documented native SoC CMake notice;
  no compiler or linker diagnostic. Runtime receiver warnings are exactly
  the pinned case-local rejection sets (control 5, metadata 19, codec_qos
  0 with the four exact `Codec config rejected: code 0x08 reason 0x02`
  info lines plus 0x0903/0904/0905 response handling, lifecycle 11
  including the policy-pinned `Unknown ase 0x00`, dual 6, reconnect 0);
  no unlisted warning.

SDK identity, toolchain bundle, repo HEAD `e289bc6e9e` and dirty-inventory
digest `7380c2a4b024a5ba7cdf9a2560562c3f28594107d984fd1aedf20cb2a02c814f`
(status-listing digest only, content proven by per-file hashes) all match
the record.

## Focused verification commands (host-only)

All under `python3 -W error::ResourceWarning -m unittest discover -s <dir>
-p 'test_*.py'`, all OK:

- `tests/unit/ascs_runner`: 21/21 (includes the real-`execute()` lifecycle
  boundary test with ready/cohort/vanish hooks and the fresh fake SDK;
  `verify_runtime_identity` never mocked).
- `tests/unit/ascs_results`: 13/13 (including `phy missing -nodump`
  execution-record negative and the full TX-audit control set).
- `tests/unit/bsim_link_env` 7/7, `tests/unit/bluez_host_process` 11/11,
  `tests/unit/bluez_host_descendants` 4/4.
- `bash -n scripts/ascs-bsim-run.sh` and `git diff --check` clean.

## Explicit boundaries and pending gates

- The matrix is a host-executed protocol and lifecycle lane against the
  production receiver integration inside BabbleSim. Not established:
  physical RF, physical I2S/DAC output, LC3 conformance against
  independent vectors, FLPR offload behavior, release acceptance.
- Clean-commit canonical gate (software) and latest hosted CI on PR 16 are
  **PENDING**; the r3 matrix ran on the dirty continuation tree with the
  pre-race-fix runner and does not claim them. Because the guard wrapper
  linkage (`-Wl,--wrap=bt_audio_data_parse` on the production app root)
  changes production link behavior, the clean canonical gate must also
  build the physical receiver CPUAPP/FLPR images (build only, no flash)
  and prove the linker wrap applies with zero new build diagnostics before
  any later physical use.
- No acceptance criterion checkbox is checked by this document; criteria
  move only through the PR gate after human product-owner merge.
- Earlier roots listed above remain immutable; no root was deleted,
  overwritten or reused.