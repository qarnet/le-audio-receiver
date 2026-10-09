# PB-051 native UART capture: one reset, failed verification boundary

Orchestrator explicitly authorized a new direct native termios capture
contract after the earlier serial-MCP base64 transfer could not be sealed.
The older `/tmp/opencode/pb051-ltv-physical-guard-execution-r1` evidence,
including its unaccepted manual-base64 candidate, was not edited.
No production source, SDK, codec, Bluetooth, FLPR or DAC implementation
changed. The configured target remains the standalone pure-parser
diagnostic image, not a streaming receiver/source role.

The new exclusive root is
`/tmp/opencode/pb051-ltv-physical-guard-execution-r2`. Before opening
UART, read-only `nix-nrf probes`, bounded explicit CMSIS-DAP fingerprint
and udev/USB resolution twice matched diagnostic candidate probe
`158D8E1D`: DPIDR `0x6ba02477`; AP0/1 `0x84770001`, AP2
`0x32880000`, AP3 `0x00000000`; FICR PART `0x00054b15`, VARIANT
`0x41414330` (AAC0). Its unique USB parent was VID `2886`, PID
`0066`, interface `02`, serial `158D8E1D`, on `/dev/ttyACM3` **in this
session only**. Seven prepared source identities and HEX/ELF/map hashes
matched: HEX SHA-256
`54c236f21759d6326db427c149825599be7a5507c231655d100ff69c8439177c`.
Raw checkpoints: `initial-identity.json` and `pre-reset-identity.json`.

The native owner opened only the matched tty (`O_RDWR|O_NOCTTY|O_NONBLOCK`,
`TIOCEXCL`), saved termios and configured raw 115200 8N1, CREAD/CLOCAL,
no HW/SW flow or CR/LF translation. No UART write, modem-line ioctl,
`tcflush`, or explicit DTR/RTS pulse. Pre-reset drain retained 0 bytes in
`pre-reset.log`. The direct binary reader thread and `uart.log` were armed
before the sole OpenOCD command. Mark was
`2026-10-05T14:44:37.212232+00:00` (monotonic ns
`3186954514418822`). The exact owned command and its process result are
in `reset-process.json`: `init`, `verify_image {<prepared HEX>}`,
`reset run`, `shutdown` with ports disabled and selected probe serial.
No `load_image`, RRAM write, second reset, erase, recovery or flash.

**Failure, not a physical parser verdict:** OpenOCD exited 0 and printed
`verified 58940 bytes in 0.841000s`, but `reset.log:20-23` printed four
real errors, two each of `[nrf54l.cpu] not halted (start target algo)` and
`[nrf54l.cpu] error executing cortex_m crc algorithm`. Byte verification
was therefore not accepted: process return 0 and a later `verified` line
cannot waive error diagnostics. Raw log SHA-256
`d16c91572e468dfb0f4f06f6137c46c304e7863ce51d8208d66cbf6a018a8f1f`.
Native UART reader retained **1280 actual bytes** before the owner stopped
after the verification error; `uart.log` SHA-256
`76f1fbe1817dc8fb9e1e34734b6fe47fbec5cac5829c83370df5dfff353036a4`.
It contains only a partial suite, no `PROJECT EXECUTION SUCCESSFUL` and
no accepted 12/0/12. No user-buffer cap overflow was observed;
the kernel's RX-loss count is unobservable from this fd capture and was
not fabricated as zero.

Reader thread joined; original termios was restored and checked equal,
exclusive mode released with `TIOCNXCL`, fd closed; descendant scope
restored its flag with no errors, adopted descendants or live child.
`native-capture-verdict.json` records `accepted=false`, SHA-256
`89eec78a03fe6da6a052e39fbf950a5783f23857c79b915a5c03ae807616f1ff`.
One separate **read-only**, post-failure SWD and USB checkpoint retained
`posttest-identity.json` and `posttest-readonly-proof.json` SHA-256
`802597049748d7399e9749b6a833c7056240d6440848a6e5e39dd9e115265c1d`;
DP/AP/FICR and current USB identity matched pre-reset values. It did
not reset, open UART or alter the diagnostic image.

Stop here. `verify_image` tried Cortex-M CRC with the target running;
the raw error specifies why this verification path cannot serve as
acceptance even though OpenOCD returned 0. A differently ordered
halt/verify/reset procedure or any new attempt needs a separate,
source-grounded authorization and fresh target identity. Do not replay
this partial UART as a passing test, alter r1, or relabel it as physical
ASCS, RF, codec, analog, FLPR, or PB-041 evidence.

## Separately authorized r3: halt before CRC, still failed

Orchestrator authorized exactly one new direct capture in
`/tmp/opencode/pb051-ltv-physical-guard-execution-r3`. External
`supervisor-derivation.json` pins the r2 native capture source SHA-256
`76fc21cf5e5f1e489daf3cbd36bb466d5741819500f011b3cd412c0e832e1593`
and proves only two reviewed source changes: exclusive external root
r2 to r3, and inserting `halt` between `init` and `verify_image` in
OpenOCD argv. New supervisor SHA-256
`568c92602029be8856ba12386972742978cf84a0da1b4c75851bf73730322573`.
No firmware rebuild, load_image, erase, SDK/source edit, alternate board,
or modification of r1/r2 evidence. Fresh initial/pre-reset DP/AP/FICR,
unique current candidate USB identity and prepared source/image hashes
matched before tty open. Direct raw reader and log were armed before
the one target command.

**r3 remains failed, not an accepted 12-test physical result.** Raw
`reset.log:20-22` shows `Warn : [nrf54l.cpu] target was in unknown state
when halt was requested`, `Error: timed out while waiting for target
halted`, and `Error: [nrf54l.cpu] error executing cortex_m crc
algorithm`. OpenOCD exited 0 and eventually printed `verified 58940
bytes in 20.736000s`, but that fallback does not erase warning/errors
or prove the halted-CRC verification contract. Reset log SHA-256
`99fe3800b6cfa4d8577304521e06bf33a393d1b5bee3ec924cc6c8c3f84d8e20`.
Actual unmodified r3 `uart.log` retained **2420 bytes**, SHA-256
`bb21d9bc94a8e1b2a2ecf330a1307b6e0bd0cef30b66a60d912fb60dbaa05013`.
It contains partial output, including a visible Zephyr fatal/MPU fault
before a later boot fragment; do not infer a parser or firmware defect
from that debugger-halt disturbance, and do not score partial PASS
lines. No terminal marker or accepted 12/0/12 in the captured bytes.

`native-capture-verdict.json` sets `accepted=false`, SHA-256
`0ae763a760126361988607c718ed6009509fbca8b988778bcf7afa78133b0f9a`.
Reader joined, termios was restored equal to original, `TIOCNXCL`
released exclusivity, fd closed, and owner scope had no error or adopted
live child. RX ring loss remains unobservable, not reported as zero. One
separate read-only post-failure DP/AP/FICR and USB check matches the
pre-reset diagnostic candidate; `posttest-readonly-proof.json` SHA-256
`fb317b8647d51efc6692503d067409145954ef645344f26f6da745d9a360ddad`.

Stop after this one run. Halt did not reach a trustworthy halted target
state; do not retry, waive OpenOCD errors, or infer the visible fault
was a source regression. Any changed debug sequence requires new
source-grounded authorization and fresh identity before target action.

## Separately authorized r4: clean halted verification, raw UART acceptance unresolved

Orchestrator authorized exactly one more direct capture with new external
root `/tmp/opencode/pb051-ltv-physical-guard-execution-r4`. Its
`supervisor-derivation.json` pins immutable r3 owner source SHA-256
`568c92602029be8856ba12386972742978cf84a0da1b4c75851bf73730322573`;
reviewed changes were only new r4 root, OpenOCD
`init; reset halt; wait_halt 2000; verify_image {<unchanged HEX>}; reset run;
shutdown`, and a positive halted-target log requirement. New owner source
SHA-256 `0fd868d5dc270084e107dbd69687988604b06df3ed84d98a69cbfc0665c01210`.
No load, erase, new image, SDK alteration or repeated r4 target action.

Fresh initial/pre-reset Nordic DP/AP/FICR and matching USB identity, seven
source hashes, HEX/ELF/map hashes passed before tty open. Raw UART reader
was armed before both reset commands. `reset.log:20-22` shows
`[nrf54l.cpu] halted due to debug-request` and clean
`verified 58940 bytes in 0.092000s` with **no warning/error**. OpenOCD
owner exited 0 without timeout or cleanup fault. Reset log SHA-256
`a954818a4db437e84ba3ad13030a2251cc80b48bbb6cec02060e8f0764389e75`.

Actual direct raw `uart.log` retains **3823 bytes**, SHA-256
`7f9c4f4e26379a1b8dea12bdff90c7ef1c715262b6dc51cfabd14af9aeaef639`.
No kernel RX-loss count exists for this native fd capture; no user buffer
cap/read error occurred. Its first **64 bytes** are an old-run tail after
the reader was armed but before the sole fresh boot marker. The original
3823-byte file remains untouched. A separate **read-only**
`read-only-raw-audit.json` (SHA-256
`b30f3fbf212c192efb58d3148b88fd7ed2f036a8a7b19c9554fa236030af8961`)
locates exactly one `*** Booting ...` marker at byte offset 64. The
following **3759 original bytes** (not rewritten or saved as substitute)
contain 12 uniquely named `START` and 12 corresponding `PASS`, suite
summary `pass = 12, fail = 0, skip = 0, total = 12`, expected
`METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`, one
`PROJECT EXECUTION SUCCESSFUL`, and no postboot fault/warning/error
diagnostic. Their read-only SHA-256 is
`64ff257f5e9f992343d5133fcffa60328f299c05e127a88befd1b0796da8f03a`.

**Original owner verdict remains `accepted=false`.** Its validator
searched for bare `FAIL` anywhere in the whole decoded text; it matches
the normal summary field `fail = 0`, producing
`RuntimeError: unexpected parser diagnostic or marker` despite 12
distinct public test passes. The extra 64-byte preboot tail is real
postmark data and must not be erased or silently normalized. We have
**not** turned the read-only supplemental analysis into an accepted
physical verdict or rewritten `native-capture-verdict.json` (SHA-256
`11e6b4d2eb32ff379c6969b9fcfcb56dd4b73c053b9bb69bc31e86c86b2ad22a`).
Owner must decide whether explicitly boot-anchored parsing of the
retained whole raw file, with the preboot segment identified separately,
proves this diagnostic API result, or whether a new capture acceptance
shape is required. The existing raw file supports review without another
reset; do not infer independent codec, ASCS/RF, analog or DAC acceptance.

Thread joined; original termios restored equal, exclusive mode released,
fd closed and descendant scope clean. One separate read-only posttest
DP/AP/FICR/USB checkpoint matched pre-reset candidate with no target or
UART action. `posttest-readonly-proof.json` SHA-256
`a48dd52277f85d3c444591fc9f67324c93ccad8928caa66fad84933591353af7`.

## Reviewed r4 boot-window supplement: physical parser API 12/0/12

Orchestrator reviewed the **whole original r4 UART file** and resolved the
acceptance framing: its first 64 bytes are a partial prior-run test tail
and separator, followed by one exact current NCS v3.4.1 boot banner at
byte **64**. The failed original validator's bare `FAIL` pattern matched
the valid summary field `fail = 0`; this was an accounting bug, not a test
failure. No UART byte was edited or silently dropped. This supplement
explicitly accounts for bytes `[0,64)` as preboot, SHA-256
`82fd810705a3f7de7169e78456d50ed0e0bdc485b262d926bbae2239a120edcb`,
and analyzes bytes `[64,3823)` in the **same** retained `uart.log` as the
fresh execution, 3759 bytes, SHA-256
`64ff257f5e9f992343d5133fcffa60328f299c05e127a88befd1b0796da8f03a`.
Whole-file identity remains 3823 bytes, SHA-256
`7f9c4f4e26379a1b8dea12bdff90c7ef1c715262b6dc51cfabd14af9aeaef639`.
No alternate boot search, substituted suffix file, filtered error line,
or reconstructed serial content was used.

New **read-only** `review.py` pins the whole raw identity and byte-64
boot banner; rejects faults/warnings across the *entire* file; checks
one current Zephyr/NCS banner and all fresh-window lines; derives the
exact 12 method names from SHA-pinned `tests/unit/ltv_bounds/test_ltv.c`;
requires each one START, one execution PASS in order and one separately
named summary PASS, complete `pass = 12, fail = 0, skip = 0, total = 12`,
the single `METADATA_VALIDATION ret=-22 delivered_entries=0 value=00`,
and one final `PROJECT EXECUTION SUCCESSFUL` with no trailing data.
Real `FAIL` markers remain fatal; legitimate `fail = 0` is not treated as
an error. Six independent copied-input controls in new external
`review-controls/` reject missing case, nonzero fail count, altered guard
result, extra boot, truncated terminal, and injected HARD FAULT **without
relying on the pinned file digest to make those mutations fail**.

The exclusive supplemental
`/tmp/opencode/pb051-ltv-physical-guard-execution-r4/reviewed-physical-verdict.json`
is `accepted=true`, SHA-256
`4f986206b060e90bd035309675cd36fb38a21f7d52edff122d44e193f9f1f5db`.
It also independently rehashes the same physical HEX/ELF/map and seven
source inputs, checks the clean actual `reset halt`/58940-byte verify log,
the original owner's fd restoration/scope flags, and all three fresh
DP/AP/FICR plus USB checkpoints. External reviewer source SHA-256
`6e5ac0966b618a813ebd29cc353ab7c71d1ce3d3abe158e1d55299a56c294eeb`.
The earlier `native-capture-verdict.json` remains **unchanged and
accepted=false** (SHA-256
`11e6b4d2eb32ff379c6969b9fcfcb56dd4b73c053b9bb69bc31e86c86b2ad22a`),
as do all raw r1-r4 logs and failed MCP transfer evidence. This is
reviewed **physical CPUAPP public-parser API** evidence only. It is not
physical ASCS, RF, LC3 conformance, I2S/DAC/analog, FLPR, release,
or full PB-051 Done acceptance. Diagnostic image remains on the board.
No new board, UART, SDK, source, flash or reset action occurred in this
supplemental review.
