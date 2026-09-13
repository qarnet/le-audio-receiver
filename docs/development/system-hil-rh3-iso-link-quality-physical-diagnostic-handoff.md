# RH3 nRF54L15 ISO link-quality physical diagnostic handoff

Status: one fresh direct diagnostic row. This captures controller-reported ISO
link-quality counters during known Mode A loss behavior. It is not an RH3 matrix
retry, acceptance run, root-cause conclusion, or audio-health claim.

## Goal

Run exactly one runner-owned fresh `rh3.fresh_mode_a_48_4_1` row with the
reviewed nRF54L15 ISO link-quality image. Retain normal strict runner evidence,
two structured ISO link-quality snapshots, and both Mode A receiver summaries.

The `bt iso quality` query runs only in the existing post-`scored_complete`
active-stream tail. It records `crc_error` and `rx_unreceived` as evidence. No
nonzero link-quality counter changes existing pass/fail policy.

## Grounding

The immutable predecessor `rh3-20260820-04-iso-status` passed its direct row,
but receiver callback-status summaries showed mostly `BT_ISO_FLAGS_LOST`:

```text
slot 0: rx_valid=119, rx_error=0, rx_lost=14300, rx_unknown=0, rx_no_ts=50
slot 1: rx_valid=0,   rx_error=0, rx_lost=14390, rx_unknown=0, rx_no_ts=14390
```

That evidence proves callback delivery of HCI NOP-derived lost status. It does
not distinguish controller `crc_error_packets` from `rx_unreceived_packets`.

Installed NCS v3.3.0 confirms SoftDevice Controller support for HCI LE Read ISO
Link Quality (`0x2075`) whenever ISO is enabled:

- `nrfxlib/softdevice_controller/include/sdc_hci_cmd_le.h` declares opcode
  `SDC_HCI_OPCODE_CMD_LE_READ_ISO_LINK_QUALITY`;
- `nrf/subsys/bluetooth/controller/hci_internal.c` exposes and forwards it under
  `CONFIG_BT_CTLR_ISO`;
- current nRF54L15 app configuration resolves `CONFIG_BT_CTLR_ISO=y`.

Current implementation uses only public BAP, ISO, and HCI APIs. It reacquires
each active sink CIS handle, verifies response length, status, and echoed
handle, then records stable application slot plus all seven HCI counters.

Software verification on current dirty worktree passed:

- native `audio_shell_nrf54`: `45` pass, `0` fail, no compiler warning;
- `tests/hil/rh2_test.py`: `141` pass;
- `scripts/check-test-matrix.py --repo-root .`: `0` errors, `0` notes;
- `fw-build-5340` and `fw-build-54l15`: passed with documented baseline
  diagnostics only;
- build contract: `96` assertions, `0` failed;
- Python compile and `git diff --check`: passed.

Existing user authorization covers remaining USB/HIL work. Matrix runner remains
sole owner of hardware actions.

## Exact image identities

Use existing outputs only. Do not rebuild. Before hardware action, all four
SHA-256 values must match exactly:

```text
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
244e864d985b75ae7e335a0410408d2cc423fddae6901ac978e435162dbd1232  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
```

Matching these identities proves only that this diagnostic runs reviewed local
images. It does not prove RH3, transport, audio, analog, release, or root-cause
acceptance.

## Frozen scope

In scope:

1. Recheck fresh output destinations.
2. Validate fixture and local binding without hardware mutation.
3. Verify four exact image hashes.
4. Run one direct runner-owned fresh Mode A row with a new immutable ID.
5. Read retained evidence after runner exit, including structured link-quality
   records and raw console transcript.

Out of scope:

- full RH3 matrix, child retry, old-ID reuse, second diagnostic row, threshold
  change, or acceptance conclusion;
- source, receiver, runner, fixture, binding, row, parser, Kconfig,
  devicetree, RF, power, or pairing-policy change;
- rebuild, unit/Python test, canonical gate, source-fixture build, or concurrent
  HIL command;
- manual serial, `serial-mcp`, extra serial reader, manual flashing, reset,
  recovery, erase, pairing, Bluetooth control, `btattach`,
  `scripts/bap_central.py`, manual FLPR command, OpenOCD, probe control, RF, or
  power action;
- deleting, overwriting, moving, or repairing prior evidence;
- repository edits, commit, push, merge, PR, tag, release, or `STATUS.md` edit.

Preserve intentionally dirty worktree. Runner-created evidence under
`/tmp/opencode/hil-runs` is expected. No repository file may change during
execution.

## New immutable diagnostic identity

Use exactly:

```text
run ID:     rh3-20260821-01-iso-link-quality
run dir:    /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality
JUnit path: /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality.junit.xml
row:        rh3.fresh_mode_a_48_4_1
```

The destination does not exist at handoff creation. This is a new direct row,
not a retry of any `rh3-20260815-*` or `rh3-20260820-*` evidence.

## Preflight

Run sequentially from repository root. Do not start hardware action after any
preflight failure.

1. Require both destinations absent and not symlinks:

```bash
test ! -e /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality && \
  test ! -e /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality.junit.xml && \
  test ! -L /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality.junit.xml
```

2. Validate fixture and local binding. This performs no hardware mutation:

```bash
nix develop --command ./scripts/hil-runner.py validate \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json
```

Expected JSON:

```json
{"capture_capability": "none", "fixture_id": "local-nrf54l15-receiver"}
```

3. Verify exact local images:

```bash
sha256sum --check <<'EOF'
b40d03d1db49c990c87cd9406834e898a360bd581dd36453d2e40bdfaf3f3317  build/hil-source/app/zephyr/zephyr.hex
4e4b82f5de3e4789d85912db34b54a06a53e439bea14634ac59efe4e641f8f48  build/hil-source/hci_ipc/zephyr/zephyr.hex
244e864d985b75ae7e335a0410408d2cc423fddae6901ac978e435162dbd1232  build/nrf54l15/le-audio-receiver/zephyr/zephyr.hex
45ab8d15e1656ed82f0e5d20d2b0f39abad77b3f220b2d6bfe2a2378d9bddee2  build/nrf54l15/flpr/zephyr/zephyr.hex
EOF
```

4. Record whitespace and intentional dirty baseline:

```bash
git diff --check
```

## One execution

Use terminal-tool outer timeout `1800000` ms. Do not use shell `timeout`. Run
exactly once:

```bash
nix develop --command ./scripts/hil-runner.py run \
  --fixture tests/hil/fixture.json \
  --binding tests/hil/fixture.local.json \
  --output-root /tmp/opencode/hil-runs \
  --run-id rh3-20260821-01-iso-link-quality \
  --junit /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality.junit.xml \
  --row rh3.fresh_mode_a_48_4_1
```

Runner exclusively owns discovery, flash, reset, serial capture, fresh pairing,
Bluetooth connection, streaming, cleanup, and evidence collection. Do not start
any other hardware, serial, Bluetooth, RF, source-build, test, or HIL command
while it runs. Do not interrupt except on direct user cancellation.

## Post-run evidence extraction

After runner exits with a retained run directory, whether exit status is `0`,
`1`, or `130`, preserve evidence and inspect it read-only. First verify retained
payload hashes:

```bash
(
  cd /tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality &&
  sha256sum --check SHA256SUMS
)
```

Then run this read-only report. It does not alter runner verdict. It prints
missing/malformed evidence explicitly rather than treating diagnostic counters
as thresholds.

```bash
nix develop --command python3 - <<'PY'
import json
import sys
from pathlib import Path

sys.path.insert(0, "scripts")
from hil.receiver import parse_stream_summary

root = Path("/tmp/opencode/hil-runs/rh3-20260821-01-iso-link-quality")

def load_json(name):
    path = root / name
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))

result = load_json("result.json")
if result is None:
    print(json.dumps({"result_error": "result.json missing"}, sort_keys=True))
else:
    print(json.dumps({
        "outcome": result.get("outcome"),
        "first_failed_boundary": result.get("first_failed_boundary"),
        "failure_detail": result.get("failure_detail"),
        "cleanup_failures": result.get("cleanup_failures"),
    }, sort_keys=True))

images = load_json("images.json")
if images is None:
    print(json.dumps({"images_error": "images.json missing"}, sort_keys=True))
else:
    print(json.dumps({"images": images.get("images")}, sort_keys=True))

raw_path = root / "receiver-console.bin"
if not raw_path.is_file():
    print(json.dumps({"raw_summary_error": "receiver-console.bin missing"}, sort_keys=True))
else:
    raw = raw_path.read_bytes().decode("utf-8", errors="strict")
    raw_summaries = parse_stream_summary(raw)
    print(json.dumps({
        "raw_summary_count": len(raw_summaries),
        "raw_summary_slots": [item.get("slot") for item in raw_summaries],
        "raw_summaries": raw_summaries,
    }, sort_keys=True))

summary_path = root / "summary.json"
if not summary_path.is_file():
    print(json.dumps({"summary_error": "summary.json missing"}, sort_keys=True))
    raise SystemExit(0)

summary = json.loads(summary_path.read_text(encoding="utf-8"))
tail = summary.get("receiver_tail", [])
quality = None
if len(tail) == 1 and isinstance(tail[0], dict):
    receiver_tail = tail[0].get("receiver")
    if isinstance(receiver_tail, dict):
        quality = receiver_tail.get("iso_link_quality")
stream_slots = [
    item.get("last", {}).get("slot")
    for item in summary.get("receiver_streams", [])
    if isinstance(item, dict)
]
quality_records = quality.get("streams", []) if isinstance(quality, dict) else []
quality_errors = []
if not isinstance(quality, dict) or quality.get("header_seen") is not True:
    quality_errors.append("ISO link-quality header missing")
if not isinstance(quality_records, list):
    quality_errors.append("ISO link-quality records malformed")
    quality_records = []
if [item.get("slot") for item in quality_records] != [0, 1]:
    quality_errors.append("ISO link-quality slots are not [0, 1]")
for record in quality_records:
    for field in (
        "handle", "tx_unacked", "tx_flushed", "tx_last_subevent",
        "retransmitted", "crc_error", "rx_unreceived", "duplicate",
    ):
        value = record.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            quality_errors.append("malformed %s" % field)
            break
print(json.dumps({
    "structured_summary_slots": stream_slots,
    "iso_link_quality": quality,
    "iso_link_quality_errors": quality_errors,
}, sort_keys=True))
PY
```

Inspect `result.json`, `summary.json`, `images.json`, external JUnit,
`receiver-status.txt`, `receiver-console.bin`, `source-console.bin`,
`commands.jsonl`, `MANIFEST.md`, and `SHA256SUMS`. Confirm `images.json`
contains exactly the four preflight hashes.

Interpretation boundary:

- link-quality values classify controller-reported counters for active CISes;
- callback status and link-quality counters remain distinct evidence planes;
- `crc_error=0` or `rx_unreceived=0` alone does not prove a healthy link;
- nonzero counters remain evidence, not acceptance thresholds;
- any strict runner warning, command/parser failure, or receiver-tail failure
  remains a failed diagnostic row.

## Stop and report

Stop after this one run. On passed, failed, cancelled, preflight failure, or
missing link-quality data:

1. Preserve evidence exactly. Do not retry, rebuild, modify hardware, or launch
   another HIL command.
2. Return preflight output, runner exit status, outcome, failure boundary/detail,
   cleanup failures, exact images, SHA-256 verification, structured
   link-quality JSON, parsed raw summaries, and relevant raw-log lines in
   timestamp order.
3. State whether two slots, `0` and `1`, were retained in both structured
   summary and ISO link-quality evidence. List every counter for each slot.
4. State that no acceptance, root-cause, audio, analog, or release claim follows.
5. Confirm runner was sole hardware owner and no repository file changed or
   commit was made.
