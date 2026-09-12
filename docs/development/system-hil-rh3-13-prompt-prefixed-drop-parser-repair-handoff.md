# RH3-13 prompt-prefixed drop parser repair handoff

Status: software/documentation repair only. This handoff grants no HIL
authorization. Do not run physical HIL.

## Evidence and root cause

RH3-13 evidence in
`/tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace`
is immutable and must not be retried or edited. Its retained trace contains:

```text
malformed dropped-message record: uart:~$ --- 276 messages dropped ---
```

The exact root cause has two parts: relative names in `SHA256SUMS` are resolved
from the current directory, and the exact `uart:~$ ` prompt prevented the
dropped-message fullmatch. Do not infer a cause for absent `0x206f` completion
records from this evidence.

## Repair contract

Only candidate dropped-message records may remove one exact leading
`RECEIVER_PROMPT` value, `"uart:~$ "`, for matching. All other parser matching
is unchanged. Records retain the original VT100-stripped prompt-prefixed line
and original raw byte offsets. Classification still follows the first arm
marker: pre-arm drops are `before_arm`; post-arm drops are `after_arm` and fail
with `dropped messages after arm: N`. The existing exact
`--- N messages dropped ---` grammar remains strict. Foreign prefixes and
malformed text remain parser errors.

Tests must prove parser acceptance and retained evidence for the exact
prompt-prefixed pre-arm form, successful opt-in runner trace output, parsed
prompt-prefixed post-arm rejection with `dropped messages after arm: 1`, and
rejection of a foreign prefix. Existing failure-boundary and warning tests stay
strict.

## Scope

In scope: `scripts/hil/receiver.py`, `tests/hil/rh2_test.py`, and this new
internal handoff.

Out of scope: firmware, Kconfig, CMake, fixtures, runner behavior, public docs,
historical RH3-12/RH3-13 handoffs, result documents, `STATUS.md`, immutable
evidence, checksum files, HIL, hardware, serial, flashing, OpenOCD, Bluetooth,
RF, reset, power, staging, commits, pushes, resets, stashes, cleans, or restores.

Use this read-only checksum proof from the run directory:

```bash
RUN_DIR=/tmp/opencode/hil-runs/rh3-20260823-13-callback-timed-hci-remove-iso-path-trace
(
  cd "$RUN_DIR"
  sha256sum --check SHA256SUMS
)
```
