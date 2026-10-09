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

## Specification and provenance notes

Normative background used when this lane was specified (2026-10-04) is
ASCS v1.0, adopted 2021-09-14, read from the official HTML at
https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/23166-ASCS-html5/out/en/index-en.html;
that is a dated-citation label, not a claim about a newer revision or
formal qualification. The Zephyr BSim lane negotiates MTU 65 and does
not reproduce the exact-MTU-64 client ordering case from the BlueZ
survey context that motivated the lane. Release-to-Idle recovery was
chosen because production has no `.reconfig` callback; ASCS v1.0 also
permits a Cached Codec Configured completion, so this lane is not a
universal conformance claim. Every negative encoded request is a real
GATT write over the wire; a high-level helper's local refusal is not
wire proof. The receiver exposure in force is the existing profile
(2 sink, 0 source, metadata capacity 16) enforced by the current
policy; this lane does not duplicate that configuration.

The original refinement document is preserved only in Git history at
revision `a94f010de00e25d4a2433f7b4c56b31a5377446e`
(`docs/development/pb-051-encoded-ascs-refinement-20261004.md`).

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

Before any build the runner verifies exact Zephyr and
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
enumerates at run start; the suite rejects changes to that captured population
within the run. It does not compare population across separate runs.

Each family gets three independent actor processes (receiver, client, PHY)
through a supervised worker cohort with pidfd-checked cleanup. Every actor
must exit zero with a clean owned-record (no timeout, no cancellation, no
cleanup faults, no unexpected live descendants) before the separate
protocol checker runs. Eighteen zero-exit actors per full matrix and six
clean cohort scopes are the acceptance baseline; a single unhealthy actor
fails the suite without any fabricated or laundered partial record.

## Cohort caller contract

The supervised worker cohort (`run_cohort` in `scripts/ascs_bsim_run.py`)
holds one explicit contract for every caller, verified by the runner unit
suite (`tests/unit/ascs_runner`) and used unchanged by `public_main`:

- Public entry point arguments are a new absolute external output path,
  the caller-supplied policy digest and one `--timeout` for the whole
  cohort; the accepted per-cohort range is a 30 to 600 second integer
  (the `ascs-bsim-run.sh` wrapper passes no `--timeout` and the runner
  default is 300; any value outside 30..600 is an argparse error).
- After all three leader workers exit, inherited pipe ends stay
  selectable; the cohort bounds the drain of any inherited stdout for two
  more seconds. A still-readable inherited pipe after that bound is a
  failure so the cohort's `DescendantScope` can clean adopted detached
  descendants.
- On error the cleanup ladder is SIGTERM with a six-second grace, then
  SIGKILL with one further second, followed by bounded per-worker waits
  and captured cleanup faults; nothing escapes unrecorded.
- Each cohort runs inside a `DescendantScope`, and worker birth identity
  is checked against the runner's pid plus a pidfd handle, so a reaped or
  recycled pid can never substitute for a real worker.

## Result interpretation limits

Local procedure IDs in the client logs are run-local diagnostics for
correlating `ASCS_PROCEDURE_BEGIN`/`END` pairs, not ASCS wire transaction
IDs. ASCS notifications carry no nonce or sequence echo, so two
byte-identical delayed replies cannot be cryptographically distinguished
by any listener; pairing a local procedure ID with a notification is an
ordering-plausibility judgment, not proof of causation. The same limit
applies to the sealed execution records: they are trusted local evidence
of what this host produced, not cryptographic attestation; synthetic
unit execution records never qualify as actual BabbleSim evidence.

The independent checker parses only timestamped, prefixed Zephyr BSim
peer lines (`d_NN: @HH:MM:SS.microseconds`), requiring monotonic BSim
time per role. The checker hashes the original raw bytes, not cleaned
text, and strictly decodes UTF-8 for analysis; it strips only ANSI SGR
escape sequences and rejects any remaining ESC byte in the cleaned text.

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

## Retained TX-audit ownership (`bsim_tx_forget_result`)

The public test-fixture API `bsim_tx_forget_result(stream)` in
`tests/bsim/client/src/bsim_tx.c` releases exactly one caller-owned,
already drained retained audit result. Its refusal contract: null stream
returns `-EINVAL`, an unknown stream (no retained audit) returns
`-ENODATA`, and a stream that is still active or that still has an
in-flight send returns `-EBUSY`. It never releases an active or in-flight
audit, and it only touches slots whose `in_flight` count is zero under
the audit lock; the caller must have unregistered (drained) first. The
unchanged canonical Stage 1 cases never call this API. Additive ASCS
generation retirement calls it explicitly after draining: each generation
copies and logs the actual retained send count/FNV *before* forgetting
and checks its two streams independently, whichever state each stream is
in (a never-used stream must return `result=-ENODATA, forget=-ENODATA`;
a used stream must match the retained values, then show `forget=0` and
`result=-ENODATA` again). The ASCS release path never relies on address
recycling: there is no pool growth, no automatic eviction on
re-registration, and no private pointer identity acceptance. The forget
step retires audit ownership after the CP/IO drain but before the
generation drops its held connection reference.

## Wrapper same-object scope limits

`src/bt_audio_ltv_guard.c` is linked through GNU ld `--wrap` on
`bt_audio_data_parse`. It validates each LTV entry's full logical length in
the caller's buffer and forwards only individually valid entries to the
real installed SDK parser. GNU ld `--wrap` redirects undefined references
to `bt_audio_data_parse` in the input object files to the wrapper; the
final ELF contains the SDK `bt_audio_data_parse` symbol and
`__wrap_bt_audio_data_parse`. The source-level
`__real_bt_audio_data_parse` call resolves to the SDK symbol.
Same-object internally resolved SDK calls
are not intercepted and are outside this guard's scope. There is no
SDK-wide interception guarantee; the guard holds only for the link units
where the wrap flag is actually applied.
`tests/unit/ltv_bounds/` compiles the wrapper through the public audio.h
API with guard on by default; test-only `PB051_LTV_GUARD=OFF` keeps the
unmodified SDK parser baseline, and the production guard remains always
linked in the receiver builds. The suite's five added public parser
methods cover, through the `bt_audio_data_parse` API only, these boundary
types: a missing type byte at the logical end, a missing value after an
already delivered prefix, a callback cancellation before an invalid
suffix, a full 255-length entry versus a logically short padded buffer,
and null/empty/valid retry after the invalid input.

## Native BSim link library resolver (`scripts/bsim_link_env.py`)

The lane resolves its native ELF32 runtime-link directories with a fixed,
bounded public contract in `scripts/bsim_link_env.py`:

- Query only the two fixed GCC `-m32 -print-file-name=` runtime library
  names, `libc.so` and `libgcc_s.so.1`. Every query runs through the
  existing `bluez_host_guest.run_bounded_command` generic process owner
  with a fresh `TemporaryDirectory`, separate 64 KiB stdout and stderr
  caps and a 10-second production deadline; successful stdout is read
  only after the command exits.
- Reject: any stderr output, a nonzero exit, a stdout/stderr cap breach,
  a deadline expiry, and a path that is empty, relative, or contains
  whitespace or control characters (the compiler output must name exactly
  one absolute path; a shell-quoted assignment alone cannot preserve
  spaces in the subsequent `NIX_LDFLAGS` flag list).
- `libc.so` may legitimately be a linker script, so the reported `libc.so`
  is only anchored: the resolved directory is validated by checking four
  ELF32 i386 runtime siblings (`libc.so.6`, `libm.so`, `libdl.so`,
  `libpthread.so`) separately.
- For `libgcc_s.so.1` the compiler-reported file may be 64-bit; the
  resolver therefore tests these candidates in order,
  `reported`, `reported.parent / '32' / name`,
  `reported.parent.parent / 'lib' / name`,
  `reported.parent.parent / 'lib32' / name`, and selects the first
  verified ELF32 match.
- Each candidate is validated as real ELF32 i386 little-endian content:
  the candidate symlink is resolved, opened with
  `O_NONBLOCK | O_NOFOLLOW`, `fstat` requires a regular file on that
  descriptor, then the read is bounded to the 20-byte ELF header
  checking the ELF magic, the ELFCLASS32 class, the little-endian byte
  order, and the EM_386 machine fields only. This header check is not an
  `e_type`/DSO format validation and proves no full shared-object
  closure. No nonregular or unbounded read ever happens.
- Printed flags are `-L<libc_dir> -L<gcc_dir>` prefixed onto the existing
  `NIX_LDFLAGS` so previously set flags are preserved, then exported
  through a shell-quoted assignment.
- No invented source-closure claim: this resolver pins link-time library
  directories only; it neither freezes nor verifies the full dependency
  closure of any image.

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

Narrow recorded-purpose exception (2026-10-06): the configure-log scan in
`scripts/native_bsim_probes.py` recognizes exactly four diagnostic-bearing
CMake capability-probe events (`check_C__fuse_ld_bfd__static`,
`check_C__fuse_ld_bfd__Wl__N`,
`check_C__fuse_ld_bfd__Wl___orphan_handling_warn`,
`check_C__fuse_ld_bfd__Wl___orphan_handling_error`). Each recognition is
bounded to the full event envelope shape: exact required backtrace
entries, exact `CMAKE_C_FLAGS: "-m32"`, exact empty
`CMAKE_EXE_LINKER_FLAGS`, exact compile/link argv with `-Wl,--entry=main`,
exact per-shape diagnostic multiset, and a verified installed SDK source
pair (`linker_flags.cmake`/`extensions.cmake` exact SHA-256 pins). These
are purpose-test dispositions of Zephyr's own linker capability probes,
not a production warning waiver: every compiler or Ninja diagnostic
outside those exact events stays a hard failure, unrecognized diagnostics
inside or outside the envelope still reject with the strict error, failed
probes stay `supported: false`, and `cached: true` never implies success.
The runner retains per-role `capability-probes.json` records for the two
build roles (receiver, client) plus a top-level suite
`capability_probe_dispositions` map; family verdicts carry the unchanged
checker JSON shape.

## Reconnect generation caller contract

Reconnect cases accept a new generation only after both old stream
releases finish, the old unicast group is deleted through the public API
with only `-EBUSY` retried (bounded to five seconds from delete-retry
start), and the old generation context is safely retired. These
retirement-order facts complete that contract (from
`tests/ascs_bsim/client/client.c`, `retire`/`retire_tx_audits`, and
`tests/bsim/client/src/bsim_tx.c`):

- The five-second retirement deadline covers the GATT cleanup only: no
  pending discovery/read/write/subscribe operation and no live CP
  subscription (the unsubscribe is issued first when one exists) before
  `retire_tx_audits` runs. Expiry is a `retirement_expired` error, not an
  extension. The audit TX drain is a separate bound: each registered
  stream's unregister waits for slot idle with its own
  `BSIM_TX_IDLE_WAIT_MS` of 1000 ms per stream (`bsim_tx.c:64`,
  `tx_wait_idle`), not the five-second budget, and the source defines no
  recheck of the GATT deadline after the audit phase. The two bounds are
  separate and no atomic or hard wall-clock guarantee covers their sum.
- The retained result is read back and its send count/FNV logged before
  the forget call; each stream is checked in whichever state it is in: a
  never-used stream must return `result=-ENODATA, forget=-ENODATA`; a
  used stream must match the retained values, then show `forget=0` and
  `result=-ENODATA` again.
- Audit ownership retires after the CP/IO drain but before the held
  connection reference drop; on drain failure the closing generation
  keeps its held ref so a later retire can finish the partially cleaned
  generation.
- Every generation's extra `bt_conn_ref` is held through bounded
  retirement and released only after every outstanding operation, CP
  closure, and terminal callback completes: `bt_gatt_cancel` (`void`,
  `gatt.h`) neither frees params nor suppresses terminal callbacks, so
  the stable per-generation storage is retired only after terminal
  callbacks; old callbacks retain their originating generation and fail
  closed rather than being relabeled as current.

Before each fresh connection the client clears its old endpoint cache
(`sink_eps`) and resets the connection, MTU-exchange, security and
sink-discovery semaphores, then requires a fresh public
connect/security/discovery, a negotiated MTU of exactly 65, and both
newly read Idle ASE states. Eight generation contexts
(`WIRE_GENERATIONS 8`) back the four reconnect cases plus recovery
streams per case without any address-recycling assumption.

## Exclusive output and retention

The public entry point requires a new absolute external
`ASCS_OUTPUT_ROOT`; the root must not pre-exist and never overlaps the
repository, home, Nix store or preserved PB-05x evidence trees. Every
families/<name>/ directory retains the raw peer logs, per-actor process
records, worker/cohort records and the sealed execution record; hashes and
paths are inside `suite-record.json`. Failed runs keep all raw evidence the
same way; cancelled runs seal one corrective failure record. Evidence
directories are write-once: subsequent runs must use a new root; the
previous accepted matrix stays immutable for later comparison. All prior
exclusive roots are kept regardless of run outcome; nothing is deleted,
overwritten or reused.

When the gate runs this matrix under `scripts/test-all.sh`, the ASCS run
root is allocated first inside a fresh exclusive container (absent before
allocation, kept even when the run fails), and only then is an optional
retained-symlink published: when `TEST_OUTPUT_DIR` is set (the gate
requires it to be an existing absolute directory for the ASCS lane), the
uniquely named symlink is created under it pointing at that run path
*before* any child is launched. The approved pinned `upload-artifact`
action searches with `followSymbolicLinks: true`, so the hosted artifact
upload follows the retained link rather than copying; this is a trusted
pin, not a substitute for the retained raw evidence. When `TEST_OUTPUT_DIR`
is unset the run has no symlink at all; the run directory itself stays the
sole retained evidence root either way.

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
   the actual SHA-256 of the supplied frozen baseline file; a drifted anchor rejects.
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
