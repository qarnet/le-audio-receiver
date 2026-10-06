# ASCS protocol regression matrix (PB-051 test lane)

Public entry point: `scripts/ascs-bsim-run.sh` plus
`scripts/ascs_bsim_run.py`. One command runs the complete six-family
encoded-ASCS regression against the production receiver integration in a
dedicated two-peer BabbleSim lane. No subset execution exists; the legacy
`ASCS_CASE` variable is rejected with an explicit diagnostic.

## What the runner proves

Each family exercises the production receiver (`tests/ascs_bsim/receiver`:
real BAP/ASCS/PACS integration with the per-entry LTV guard wrapper) from an
independently authored client (`tests/ascs_bsim/client`). The client writes
raw encoded ASCS control-point requests over real GATT writes, collects the
real notification responses, compares full public ASE read bytes before and
after each rejection, and then drives a fresh valid stream with exactly 30
sends per stream plus a legal release back to Idle. Encoded traffic is
authored from the frozen inventory, never copied from device-under-test
logs; the independent checker re-derives every expected request/response
from the policy file and rejects any drift.

## Independent inventory and digest

The policy lives in `tests/ascs_bsim/cases.json` with a caller-supplied
trust digest accepted only when it equals
`addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c`
(SHA-256 of the exact frozen file). The checker refuses any other digest,
recomputes the file hash with the same bounded read helpers and re-verifies
every declared case name, request descriptor, response record, diagnostic
text, phase delta and generation delta from the file itself. Test code and
docs never quote device output as expected data.

## Source, tool, image and actor accounting

Before any build the runner records SHA-256 pins for the exact Zephyr and
nrf SDK HEADs (`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`,
`b20f8619ba9a5530f8c34b0a130d829947cfe55d`), the toolchain bundle
`8285d8ad56`, cmake/ninja/gcc binaries, repo HEAD and full `git status`
inventory. A bounded source population (at most 512 files, 2 MiB per file,
32 MiB total) is hashed and copied into the output root; the check repeats
after the build and at the final seal. The population covers selected
repository integration, build and helper sources, the consumed Zephyr
Bluetooth audio surfaces,
`nrf/cmake/device_support.cmake`, `zephyr/cmake/modules/FindBabbleSim.cmake`
and the 15 consumed component headers (`libUtilv1/src/*.h` 11 files plus
`libPhyComv1/src/*.h` 4 files) from the pinned components root. This is a
bounded lane-only source pin of the selected surface above, not an
SDK-wide or whole-components freeze and not a proved complete dependency
closure; the frozen set is exactly what `source_paths()` currently
enumerates and the suite fails if it grows or shrinks silently.

Each family gets three independent actor processes (receiver, client, PHY)
through a supervised worker cohort with pidfd-checked cleanup. Every actor
must exit zero with a clean owned-record (no timeout, no cancellation, no
cleanup faults, no unexpected live descendants) before the separate
protocol checker runs. Eighteen zero-exit actors per full matrix and six
clean cohort scopes are the acceptance baseline; a single unhealthy actor
fails the suite without any fabricated or laundered partial record.

## Exact families and counts

Frozen totals, enforced at every family and the suite seal, are 60 cases,
65 rendered phases, 259 checked raw exchanges and 269 response records:

| Family | Cases | Render phases | Raw exchanges | Records |
|---|---|---|---|---|
| control_frame_validation | 7 | 7 | 21 | 21 |
| metadata_length_validation | 13 | 14 | 67 | 67 |
| codec_qos | 7 | 7 | 27 | 27 |
| lifecycle | 19 | 23 | 91 | 91 |
| dual | 10 | 10 | 42 | 52 |
| reconnect | 4 | 4 | 11 | 11 |

Every negative case is followed by public state preservation proof and a
valid recovery render with receiver rendered-output confirmation. For
single-ASE negatives the rejected ASE's public reads are bytewise
identical before and after; for the ten dual partial-result positives the
rejected B stream is preserved bytewise while the accepted A stream
transitions to its expected new state in the same step. Every full
recovery stream sends exactly 30 frames per stream; retention semantics
pin the retained TX result, explicitly forget it (with a real EBUSY guard
while the stream is active) and verify the result is gone.

## Wrapper same-object scope limits

`src/bt_audio_ltv_guard.c` is linked through GNU ld `--wrap` on
`bt_audio_data_parse`. It validates each LTV entry's full logical length in
the caller's buffer and forwards only individually valid entries to the
real installed SDK parser. `--wrap` works by leaving
`bt_audio_data_parse` undefined in the link unit, so only cross-object
references are redirected; same-object calls that resolve internally
inside the Zephyr build (any caller compiled into the same translation
unit/object as the SDK definition) are not intercepted and are outside
this guard's scope. There is no SDK-wide interception guarantee; the guard
holds only for the link units where the wrap flag is actually applied.
`tests/unit/ltv_bounds/` compiles the wrapper through the public audio.h
API with guard on by default (test-only `PB051_LTV_GUARD=OFF` retains th

## Raw warning policy

Unlisted warnings are errors. Builds must contain exactly the pinned
per-target experimental Kconfig notices (`BT_LL_SW_SPLIT`,
`BT_CTLR_SET_HOST_FEATURE` and receiver `BT_CTLR_PERIPHERAL_ISO` or client
`BT_CTLR_CENTRAL_ISO`) plus the documented native SoC CMake product notice;
anything else in cmake, ninja, configure logs or runtime diagnostics fails
the run. Runtime receiver warnings are pinned per case (each negative
exchanges has its exact rejection diagnostic with count), and a
build-warning checker retains the raw bounded logs plus the configure YAML
identity. Expected argparse diagnostics from negative CLI tests are
authored inputs, not suppressed warnings.

## Exclusive output and retention

The public entry point requires a new absolute external
`ASCS_OUTPUT_ROOT`; the root must not pre-exist and never overlaps the
repository, home, Nix store or preserved PB-05x evidence trees. Every
families/<name>/ directory retains the raw peer logs, per-actor process
records, worker/cohort records and the sealed execution record; hashes and
paths are inside `suite-record.json`. Failed runs keep all raw evidence the
same way; cancelled runs seal one corrective failure record. Evidence
directories are write-once: subsequent runs must use a new root; the
previous accepted matrix stays immutable for later comparison.

## Running the lane

Repo-root full-matrix command (new exclusive external root on every
execution):

```
env -u ZEPHYR_BASE nix develop -c bash -c \
  'ASCS_OUTPUT_ROOT=/tmp/opencode/UNIQUE_NEW_ASCS_RUN bash scripts/ascs-bsim-run.sh'
```

The wrapper rejects absolute paths that pre-exist and any overlap with the
repository, home, Nix store or preserved evidence trees.

A single accepted family's sealed record can be re-verified directly with
the independent accountant CLI (exact options as accepted by
`scripts/check-ascs-results.py`; all seven are required):

```
python3 scripts/check-ascs-results.py \
  --inventory tests/ascs_bsim/cases.json \
  --expected-inventory-sha256 addede19018af76f8ecb759b30f6297ad95501d2152b20c268423e4fdb8ea90c \
  --family control_frame_validation \
  --client-log /tmp/opencode/UNIQUE_NEW_ASCS_RUN/families/control_frame_validation/client.log \
  --receiver-log /tmp/opencode/UNIQUE_NEW_ASCS_RUN/families/control_frame_validation/receiver.log \
  --execution-record /tmp/opencode/UNIQUE_NEW_ASCS_RUN/families/control_frame_validation/family-execution-record.json \
  --expected-run-id <run-id-from-suite-record>
```

Argument errors come back as `{"accepted": false, ...}` JSON, not stack
traces: the CLI is fail-closed on every input.

## Result semantics

- accepted true: every family's independent checker returned the exact
  frozen counts with a fully clean actor/scope record; the final runtime,
  source and SDK identity checks all passed after the last cohort.
- accepted false with errors: the first detected cause plus all later
  closure-check findings are kept; original runtime errors are never
  replaced by post-run integrity entries, which append separately.
- cancelled: the first received signal is latched across scope and record
  publication; no family after the cancellation is started and a sealed
  record with the signal is published.

## Additive coverage sidecar

`tests/coverage-additions.json` (sole path; the gate reads exactly
`repo/tests/coverage-additions.json`, no environment override is offered)
adds a separately accounted strict non-regression contract for newly added
sources while the frozen `tests/coverage-baseline.json` stays byte-for-byte
unchanged. Field semantics:

- `schema_version`: exactly the int `1` (a JSON boolean is rejected).
- `frozen_baseline_sha256`: exactly 64 lowercase hex characters matching
  the actual SHA-256 of the supplied frozen baseline file; a drifted anchor rejects
  rejects.
- `files`: a nonempty object; keys are relative `src/*.c` paths (a leading
  `src/`, the `.c` suffix, no backslash, no empty, `.` or `..` component),
  and none may overlap the frozen population; each value holds exactly the
  metrics `lines`, `branches` and `functions` with `[covered, total]`
  integer pairs (`0 <= covered <= total`, `total > 0`).
- Decoding is strict: duplicate JSON keys and non-finite constants reject,
  and the gate performs exactly one bounded non-follow regular read
  (at most 64 KiB, an empty file/dangling symlink/directory/oversize are
  explicit errors, never an absent-sidecar fallback); only a genuinely
  missing path takes the legacy no-additions path.
- Enforcement shape: the required current population is the frozen
  baseline UNION the sidecar keys, so a missing original, a missing
  addition or a further unknown new source all fail; the frozen
  population's overall and per-file ratios are computed exclusively over
  current frozen-population records (a perfectly covered addition cannot
  mask a frozen regression, and missing records error rather than skip);
  each additive source must reach its exact reference totals and ratios
  by the same integer cross multiplication (the measured guard reference
  is 13/13 lines, 12/12 branches, 1/1 functions); no `0/0` pass and no
  automatic population growth.
- The sidecar's SHA-256 (or `null`) is recorded in `run-manifest.json`
  (`coverage_additions_sha256`, `coverage_additions`) and the current-run
  `numeric-summary.json` records the sidecar identity plus separate
  `frozen_population_totals` / `additive_sidecar_totals` groups, so the
  displayed combined totals remain truthfully attributable to both groups.

## Non-claims

Passing this matrix is a host-executed protocol and lifecycle regression
over real ASCS/GATT/ISO encoding inside the BabbleSim environment. It does
not establish physical RF behavior, over-the-air latency or packet-error
statistics, physical I2S/DAC audio output, LC3 decoder conformance against
independent vectors, FLPR offload behavior, release acceptance, or any
hardware identity claim. Physical boundaries remain separate work items.