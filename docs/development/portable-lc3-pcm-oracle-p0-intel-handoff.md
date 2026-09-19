# PB-031 P0 handoff: Intel x86_64 host calibration

Status: Ready for execution on an identified Intel x86_64 Linux host. This is
diagnostic evidence collection only. It does not select thresholds or change
repository files.

Parent plan: `docs/development/portable-lc3-pcm-oracle-plan.md`.

## Goal

Run the checked-in 128-frame LC3 corpus and shared integer comparator twice on
one identified Intel x86_64 Linux environment. Retain provenance-complete JSON
reports proving CPU identity, compiler, repository inputs, NCS/liblc3 revision,
fixture identity, metrics, and repeatability.

This closes only the missing Intel measurement in the cross-platform
calibration set. Threshold selection remains a later orchestrator decision.
The current 22-record calibrator covers valid decode, channel swap, prior/next
frame shift, dead channel, and low-correlation diagnostics. It does not cover
the parent plan's one-byte LC3 corruption or threshold-bound maximum/RMS
controls, so successful Intel reports alone do not complete the P0 stop gate.

## Required execution environment

- Identified Intel x86_64 Linux CPU. Raw `lscpu` must contain vendor
  `GenuineIntel` and a non-empty model name. A hosted runner label is not CPU
  evidence.
- Repository checkout containing accepted P1 commit
  `d4c321c390a915e89b4b28711dc8e535367f9c13` as an ancestor. Worktree must be
  clean before each calibration run.
- NCS v3.3.0 at `$NCS` or `$HOME/ncs/v3.3.0`.
- `modules/lib/liblc3` must be a Git checkout at exact revision
  `48bbd3eacd36e99a57317a0a4867002e0b09e183` with no local changes.
- Python 3, Git, `lscpu`, `uname`, and a warning-clean C11 host compiler.
  Compiler version may differ from prior hosts but must be captured exactly.
- No package installation, source regeneration, repository edit, NCS edit, or
  compiler-warning suppression during this task.

Known repository flake issue may make `nix develop` fail with
`attribute 'x86_64-darwin' missing` at `flake.nix:29:29`. Do not edit the flake
as part of calibration. Use an already available host compiler through `CC`.
If no suitable compiler exists, stop and report the blocker.

## Scope

Read and execute only:

- `scripts/lc3_pcm_calibrate.py`
- `tests/fixtures/lc3/portable-oracle-manifest.json`
- corpus binaries under `tests/fixtures/lc3/`
- `tests/fixtures/lc3/calibrate.c`
- `tests/support/pcm_oracle.c`
- `tests/support/pcm_oracle.h`
- liblc3 sources under `$NCS/modules/lib/liblc3/`

Write only a new external evidence directory under `/tmp/opencode/`. Produce:

- `preflight.txt`
- `intel-provenance-run-1.json`
- `intel-provenance-run-2.json`
- `validation.txt`
- `sha256.txt`

## Non-scope

- No repository, manifest, fixture, production source, test, workflow, NCS, or
  liblc3 changes.
- No fixture regeneration.
- No tolerance constants, threshold proposal, scenario migration, hash repin,
  or pass/fail oracle change.
- No P2/P3 implementation.
- No commit, tag, push, PR, branch rewrite, stash, reset, or cleanup.
- No claim that current diagnostic mutation classes complete all mandatory
  controls.

## Pinned calibration identity

Reports must contain these exact stable inputs:

| Input | SHA-256 |
| --- | --- |
| `scripts/lc3_pcm_calibrate.py` | `4c2c3df5fbd879ee526d56ad9a4cc729e144e42b94a1dc03501c97b44255b5ba` |
| `tests/fixtures/lc3/calibrate.c` | `76030161fd1ea30c0eb5f82b63010fa51801b75f48d831a7440c316107ac83e1` |
| `tests/support/pcm_oracle.c` | `90a79b96d3e793d37f1579d5d0bfa43302bc83bfef48527c58b083396c7112eb` |
| `tests/support/pcm_oracle.h` | `49d898f1f93379b697e72c2e7c48cb76d462a25c8cc5eb94ac27af25ea4bb205` |
| `tests/fixtures/lc3/portable-oracle-manifest.json` | `81cd9c09f7321eb3a928c763bee467405c1a7c470e22dbd2dcbbd7b371189b44` |

Generator/compiler flags remain exact:

```text
-O3 -std=c11 -ffast-math -Wall -Wextra -Wdouble-promotion -Wvla -pedantic -Werror
```

The calibration command fails on any compiler diagnostic. Do not suppress,
filter, or normalize one.

## Execution

Run from repository root in a fresh shell. Set `CC` to one already installed
compiler command, for example `gcc`. Do not put shell metacharacters in `CC`.

```bash
set -euo pipefail
export LC_ALL=C
export NCS="${NCS:-$HOME/ncs/v3.3.0}"
export CC="${CC:-gcc}"
read -r -a CC_ARGV <<<"$CC"
test "${#CC_ARGV[@]}" -gt 0

git merge-base --is-ancestor d4c321c390a915e89b4b28711dc8e535367f9c13 HEAD
test -z "$(git status --porcelain=v1)"
test "$(uname -m)" = x86_64
lscpu | grep -F 'Vendor ID:' | grep -F GenuineIntel
lscpu | grep -F 'Model name:'
test "$(git -C "$NCS/modules/lib/liblc3" rev-parse HEAD)" = \
  48bbd3eacd36e99a57317a0a4867002e0b09e183
test -z "$(git -C "$NCS/modules/lib/liblc3" status --porcelain=v1)"

EVIDENCE_ROOT="/tmp/opencode/pb031-calibration-intel-$(date -u +%Y%m%dT%H%M%SZ)"
test ! -e "$EVIDENCE_ROOT"
mkdir -p "$EVIDENCE_ROOT"

{
  date -u +'%Y-%m-%dT%H:%M:%SZ'
  git rev-parse HEAD
  git status --porcelain=v1
  printf 'NCS=%s\n' "$NCS"
  git -C "$NCS/modules/lib/liblc3" rev-parse HEAD
  git -C "$NCS/modules/lib/liblc3" status --porcelain=v1
  uname -a
  lscpu
  "${CC_ARGV[@]}" --version
} >"$EVIDENCE_ROOT/preflight.txt"

python3 scripts/lc3_pcm_calibrate.py \
  --output "$EVIDENCE_ROOT/intel-provenance-run-1.json"
test -z "$(git status --porcelain=v1)"
python3 scripts/lc3_pcm_calibrate.py \
  --output "$EVIDENCE_ROOT/intel-provenance-run-2.json"
test -z "$(git status --porcelain=v1)"
```

If `CC` needs multiple argv elements, export that exact command before running,
for example `CC='ccache gcc'`. The report records parsed compiler argv and raw
version output.

## Evidence validation

Run this from repository root with `EVIDENCE_ROOT` still set. It validates
identity and stable report fields, then prints every metric for orchestrator
review. Do not add numerical acceptance assertions based on AMD or ARM values.

```bash
set -euo pipefail
python3 - "$EVIDENCE_ROOT" <<'PY' | tee "$EVIDENCE_ROOT/validation.txt"
import collections
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
reports = [
    json.loads((root / "intel-provenance-run-1.json").read_text()),
    json.loads((root / "intel-provenance-run-2.json").read_text()),
]

expected_inputs = [
    ("scripts/lc3_pcm_calibrate.py", "4c2c3df5fbd879ee526d56ad9a4cc729e144e42b94a1dc03501c97b44255b5ba"),
    ("tests/fixtures/lc3/calibrate.c", "76030161fd1ea30c0eb5f82b63010fa51801b75f48d831a7440c316107ac83e1"),
    ("tests/support/pcm_oracle.c", "90a79b96d3e793d37f1579d5d0bfa43302bc83bfef48527c58b083396c7112eb"),
    ("tests/support/pcm_oracle.h", "49d898f1f93379b697e72c2e7c48cb76d462a25c8cc5eb94ac27af25ea4bb205"),
]
expected_flags = [
    "-O3", "-std=c11", "-ffast-math", "-Wall", "-Wextra",
    "-Wdouble-promotion", "-Wvla", "-pedantic", "-Werror",
]
expected_counts = {
    "valid": 4,
    "channel-swap": 2,
    "prior-frame-shift": 4,
    "next-frame-shift": 4,
    "dead-channel": 4,
    "low-correlation-synthetic": 4,
}

for index, report in enumerate(reports, 1):
    report_path = root / ("intel-provenance-run-%d.json" % index)
    assert report_path.stat().st_mode & 0o777 == 0o644
    assert report["schema_version"] == 1
    assert report["architecture"] == "x86_64"
    assert "GenuineIntel" in report["lscpu_raw"]
    assert "Model name:" in report["lscpu_raw"]
    assert report["uname_raw"]
    assert report["repository"]["status_porcelain_v1_raw"] == ""
    assert report["ncs_version"] == "v3.3.0"
    assert report["liblc3"]["semantic_label"] == "1.1.2"
    assert report["liblc3"]["west_revision"] == "48bbd3eacd36e99a57317a0a4867002e0b09e183"
    assert report["liblc3"]["observed_git_revision"] == "48bbd3eacd36e99a57317a0a4867002e0b09e183"
    assert report["generator_flags"] == expected_flags
    assert report["manifest_sha256"] == "81cd9c09f7321eb3a928c763bee467405c1a7c470e22dbd2dcbbd7b371189b44"
    assert [(item["path"], item["sha256"]) for item in report["calibration_inputs"]] == expected_inputs
    assert report["compiler"]["command"]
    assert report["compiler"]["version_raw"]
    assert len(report["metrics"]) == 22
    assert collections.Counter(metric["comparison"] for metric in report["metrics"]) == expected_counts
    print("run%d head=%s compiler=%r" % (
        index, report["repository"]["head"], report["compiler"]["command"]
    ))

assert reports[0]["repository"]["head"] == reports[1]["repository"]["head"]
assert reports[0]["architecture"] == reports[1]["architecture"]
assert reports[0]["uname_raw"] == reports[1]["uname_raw"]
assert reports[0]["compiler"] == reports[1]["compiler"]
assert reports[0]["liblc3"] == reports[1]["liblc3"]
assert reports[0]["generator_flags"] == reports[1]["generator_flags"]
assert reports[0]["manifest_sha256"] == reports[1]["manifest_sha256"]
assert reports[0]["fixture_hashes"] == reports[1]["fixture_hashes"]
assert reports[0]["calibration_inputs"] == reports[1]["calibration_inputs"]
assert reports[0]["metrics"] == reports[1]["metrics"]

def lscpu_field(raw, name):
    prefix = name + ":"
    values = [line.split(":", 1)[1].strip() for line in raw.splitlines() if line.startswith(prefix)]
    assert len(values) == 1 and values[0]
    return values[0]

for field in ("Architecture", "Vendor ID", "Model name"):
    assert lscpu_field(reports[0]["lscpu_raw"], field) == lscpu_field(reports[1]["lscpu_raw"], field)

for metric in reports[0]["metrics"]:
    print(
        "comparison=%s stem=%s reference=%s frames=%d samples=%d max=%d rms=%d corr_q15=%d"
        % (
            metric["comparison"],
            metric["stem"],
            metric["reference_stem"],
            metric["frames"],
            metric["samples"],
            metric["max_abs_error"],
            metric["rms_error"],
            metric["correlation_q15"],
        )
    )

print("PB031_INTEL_VALIDATION_PASS metrics=22 repeat_equal=true")
PY

sha256sum \
  "$EVIDENCE_ROOT/preflight.txt" \
  "$EVIDENCE_ROOT/intel-provenance-run-1.json" \
  "$EVIDENCE_ROOT/intel-provenance-run-2.json" \
  "$EVIDENCE_ROOT/validation.txt" \
  >"$EVIDENCE_ROOT/sha256.txt"

test -z "$(git status --porcelain=v1)"
printf 'Evidence root: %s\n' "$EVIDENCE_ROOT"
```

## Success criteria

- Both calibration commands exit zero and create two new mode-0644 JSON files.
- Both reports identify `x86_64` and raw `GenuineIntel` CPU vendor/model.
- Repository and liblc3 worktrees remain clean.
- Both reports carry exact pinned input, manifest, NCS, liblc3, fixture, and
  compiler provenance.
- Each report contains exactly 22 records in expected diagnostic classes.
- Parsed metric records are structurally equal across both runs.
- Validation prints `PB031_INTEL_VALIDATION_PASS metrics=22 repeat_equal=true`.
- No compiler diagnostic, calibrator stderr, validation error, or repository
  change occurs.

## Stop and escalate

Stop without modifying repository or rerunning with weakened checks if:

- raw CPU identity is absent, virtualized beyond usable vendor/model evidence,
  or not `GenuineIntel` x86_64;
- accepted P1 commit is absent;
- repository, liblc3, manifest, fixture, or calibration-input identity differs;
- NCS/liblc3 provenance cannot be verified from Git;
- compiler emits any diagnostic or calibration command fails;
- reports differ in metrics or stable provenance;
- output path already exists;
- obtaining dependencies would require installation or repository/flake edits.

Preserve all created external evidence. Return exact blocker, commands, raw
stdout/stderr, evidence path, and repository status.

## Required recap

Return:

1. Intel vendor, model, architecture, OS, and virtualization facts copied from
   raw evidence.
2. Repository HEAD and clean-status result.
3. Compiler command and full version identity.
4. NCS path and exact observed liblc3 revision.
5. Evidence-root path plus SHA-256 file.
6. Four valid metric triples `(max_abs_error, rms_error, correlation_q15)`.
7. Range by each diagnostic comparison class.
8. Repeat-equality result and validation PASS line.
9. Any warning, deviation, blocker, or missing provenance.

Do not propose thresholds or edit PB-031 notes in the execution session. The
orchestrator reviews Intel, AMD, and ARM evidence together before deciding next
phase.

## Reviewed orchestrator result

Execution completed on `thomas-nuc`, a bare-metal NixOS x86_64 host with raw
`lscpu` identity `GenuineIntel`, `Intel(R) Core(TM) i3-6100U CPU @ 2.30GHz`,
family 6, model 78, stepping 3, and microcode `0xf0`. `lscpu` reports VT-x CPU
capability and no hypervisor vendor. Kernel identity is NixOS Linux 7.0.11.

Exact repository and liblc3 Git bundles were transferred without installing
software. Both runs used clean repository HEAD
`d4c321c390a915e89b4b28711dc8e535367f9c13`, exact liblc3 revision
`48bbd3eacd36e99a57317a0a4867002e0b09e183`, and Clang 21.1.8 through `cc`.
The required flags remained `-O3 -std=c11 -ffast-math` plus the pinned warning
set. Both compilations emitted no diagnostic.

Raw evidence exists on both hosts at
`/tmp/opencode/pb031-calibration-intel-d4c321c-i3-6100u/`. SHA-256 values are:

- `preflight.txt`: `e34de4cc75a0bf979c6803d14f001c5ef8a61e42fa00ae5bc7e3e835d9667d4b`
- `intel-provenance-run-1.json`: `bc967d5bd1d6a5f6a63041ffb1c9d9db202b8d533ed2ed4a7225bf59a2de26d0`
- `intel-provenance-run-2.json`: `434efadc73befbdb33ac989278e8a1b383dfc83bb84d65ac09bd3c330d21366a`
- `validation.txt`: `43123f4c8c1b48049cf0d298ad56a184c7e65026970b4bf0c2f8db956e9de7da`

Validation passed with exactly 22 equal parsed metric records across both runs:
`PB031_INTEL_VALIDATION_PASS metrics=22 repeat_equal=true`. Valid metrics were:

| Stream | Maximum error | RMS error | Correlation Q15 |
| --- | ---: | ---: | ---: |
| 10 ms left | 1862 | 389 | 32757 |
| 10 ms right | 1977 | 393 | 32759 |
| 7.5 ms left | 1902 | 426 | 32757 |
| 7.5 ms right | 1883 | 426 | 32757 |

Diagnostic classes remained separated: channel swap had maximum error 65535,
RMS 21698 or 23112, and correlation -200 or 105; prior/next frame shifts had
maximum error 65535, RMS 21612 through 23151, and correlation 77 through 301;
dead channel had maximum error 32768, RMS 15283 through 16420, and correlation
0; low-correlation synthetic had maximum error 65535, RMS 36158 through 36656,
and correlation -9 through -2.

Combined valid envelope is now maximum error 1977, RMS error 426, and minimum
correlation Q15 32757 across identified Intel, AMD, and ARM reports. Candidate
P0b policy is maximum error 2048, RMS error 512, and minimum correlation Q15
32750. These values are not active until threshold-bound controls and updated
cross-platform reports pass.
