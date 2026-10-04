# PB-053: local emulator preparation checkpoint (2026-10-04)

## Scope and verdict

Local-only pinned BlueZ emulator preparation **PASS**. PB-053 isolated host
lifecycle lane **Blocked**, not Done. No guest runner, emulator controller,
host adapter or VM was executed in this checkpoint. No codec, independent
decoder, physical RF, I2S, analog or release acceptance follows.

`scripts/bluez_host_prepare.py` rejects existing/symlink output and output
inside home, repository, vendor source, `/nix/store` or retained
`/tmp/opencode/pb053-*` directories. It checks source HEAD equals the pin and
vendor status is clean before and after build. It hashes source bytes before
compilation, records each real compiler dependency file, then rehashes every
source and recorded dependency path before success; changes fail and retain a
failed record. These checks describe trusted local inputs, not independent
verification of vendor provenance. It hashes resolved compiler, retains
compiler version and exact command/log per operation, and writes outcome,
completed commands and binary identity to exclusive output. Compiler and link
operations run in owned sessions with scoped cancellation and bounded TERM/KILL
group cleanup. No warnings suppressed; `-Wall -Werror` remains enabled.

## Real preparation and public-boundary checks

```text
python3 -m unittest discover -s tests/unit/bluez_host_prepare -p 'test_*.py' -v
5 tests OK (real compiler GNU-source macro and header-change checks; actual
harmless Python child/descendant timeout and external SIGTERM cleanup).

python3 scripts/bluez_host_prepare.py --source /tmp/opencode/bluetooth-test-resources-20261003/bluez --output /tmp/opencode/pb053-emulator-build-r3
PASS, version-only 5.87, binary SHA-256 06611569862825010327200c354377428e996dade36d96c1a4872eb20ed0083c.

git -C /tmp/opencode/bluetooth-test-resources-20261003/bluez status --porcelain
empty; source HEAD 4dc15be8ee3f7422d447087f1893d215575cb2c8.
git diff --check
PASS.
```

Retained exclusive output `/tmp/opencode/pb053-emulator-build-r3/` contains
23 compiler logs and 23 `PB053_DEP` dependency files, link/version/compiler
version logs, objects, binary and `build-record.json`. All 23 compile logs and
link log empty; no compiler warning/error. All 26 recorded commands completed;
23 translation units and 262 distinct resolved dependency paths hashed. Compiler
resolved to `/nix/store/iwf80230xr0z8pqh1jk3z8rgw67ydagm-gcc-wrapper-14.3.0/bin/gcc`,
SHA-256 `fe8abeb3ca4a0c898801e9beb0a19651e700c95356629b7525e1ebd577f935ec`.
Binary size 233248 bytes. Record SHA-256
`84f6d2cbf4a1f5144091526ab66fc0bb39cc0ebec90e8be4e51968532ecb8448`.
This is preparation, not guest-lane acceptance. No commit or push.

### Review follow-up: portable tests and final r4 preparation

R3 output and record above remain immutable. Review removed the test-only
`/tmp/opencode` directory requirement: all five tests now use platform
`tempfile.TemporaryDirectory()` default, while forbidden `/tmp/opencode/pb053-*`
paths remain explicit, uncreated rejection controls. Real header tests also
change an authored dependency and source after recording hashes and require
the final input check to reject both changes. Final clean HEAD, clean status,
source rehash and recorded dependency rehash run before the success outcome.

```text
python3 -m unittest discover -s tests/unit/bluez_host_prepare -p 'test_*.py' -v
5 tests OK, no skips.

python3 scripts/bluez_host_prepare.py --source /tmp/opencode/bluetooth-test-resources-20261003/bluez --output /tmp/opencode/pb053-emulator-build-r4
PASS, version-only 5.87, binary SHA-256 06611569862825010327200c354377428e996dade36d96c1a4872eb20ed0083c.

git -C /tmp/opencode/bluetooth-test-resources-20261003/bluez status --porcelain
empty; pinned source HEAD 4dc15be8ee3f7422d447087f1893d215575cb2c8.
git diff --check
PASS.
```

Fresh exclusive `/tmp/opencode/pb053-emulator-build-r4/` contains 23 `.d`
files, 23 empty compile logs, empty link log, version/compiler-version logs,
objects, binary and record. `-Wall -Werror` unchanged; no compiler/link warning
or error. Record outcome success; all 26 commands completed, 23 source hashes,
262 distinct recorded dependency paths, 233248-byte binary. Compiler resolved
to `/nix/store/iwf80230xr0z8pqh1jk3z8rgw67ydagm-gcc-wrapper-14.3.0/bin/gcc`,
SHA-256 `fe8abeb3ca4a0c898801e9beb0a19651e700c95356629b7525e1ebd577f935ec`.
New `build-record.json` SHA-256
`2dd42c1c73ae7fb64e299923c8e01b7fba9a166db1b5b03e2faefe593e7d332d`.
R3's different record hash remains historical, not rewritten. No guest lane,
controller, hardware, CI, commit or push from this follow-up.

## Retained diagnostic guest evidence and blocker

Earlier r15 guest checkpoint delivered 16 exact 120-byte ISO frames with five
public cases true; raw `/tmp/opencode/pb053-guest-public-r15/serial.log`
SHA-256 `bfe5d568f0a638c852f4755c252273be7eac00ec1f8869b004df31a21792391a`.
It did not prove restart or retained-state lifecycle.

R16 and r17 both failed mandatory retained-state cleanup. Fresh child passed;
retained daemon restarted with preserved bonds and transferred 16 exact frames,
but after public Disconnect both Device1 `Connected` values remained true
through the bounded 10-second deadline. R17 sent both Disconnect requests
before awaiting replies; both replied successfully, yet both public states
remained connected. Fresh2 never started. Retained daemon reported MGMT
`Failed to add device ... Failed (0x03)` and ASE registration diagnostics;
underlying cause remains unknown. No timeout, response or acceptance check
was weakened. Raw records:

| Run | `serial.log` SHA-256 | `run-record.json` SHA-256 |
| --- | --- | --- |
| `/tmp/opencode/pb053-guest-state-r16/` | `9b21b991cb0c3b5de862ad3035bf13b2c99643844886411b42aee7fb38444812` | `14db8c454f538fcd097a0a11a3264e680689fff2038557ecdaa77e9b4547cb35` |
| `/tmp/opencode/pb053-guest-state-r17/` | `6b2106733a876f2a57dbd3b394fdd5a39c8960f64d2f36bd209c38c9b69f7eb7` | `801e8368583d4fe989b65971028670d2e8471021963cc1a6d4196f92f2eba59d` |

Guest diagnostic scaffolding has unresolved signal ownership, process cleanup
and result-validation/accounting review defects. It stays unstaged and is not
accepted as safe host-lane tooling. Further source-matched host-stack diagnosis
and runner safety/accounting refinement needed before resuming PB-053. This
blocker does not change PB-051 owner pause or any codec/physical proof boundary.
