# PB-019: nRF54L15 HCI UART boundary and bonding evidence (2026-09-28)

This records a dirty-tree engineering diagnosis and two later **normal tracked
CLI** diagnostic repeats. It does not change PB-019 acceptance criteria or
claim a clean-commit gate, generic no-flow UART reliability, analog audio
qualification, RH4/FR4 exact-artifact acceptance, or release readiness.
Original failed runs and private external evidence remain immutable.

## Version and evidence boundary

Active SDK is NCS v3.4.1 (`~/ncs/v3.4.1`), nrf
`b20f8619ba9a5530f8c34b0a130d829947cfe55d`, Zephyr
`33fa6a7aac6a4401d16a67cb9f27a3483fa02dd6`; toolchain pin
`8285d8ad56`. See `ncs-3.4.1-upgrade-results.md` for SDK setup and the
**previous** clean software gate at `daf7cd9`: 80 PASS / 0 FAIL / 80 TOTAL,
36 coverage sources, 4971/5427 lines, 2203/3008 branches, 377/377 functions,
and 69/69 build-contract assertions. No numeric baseline update occurred here.
None of the new HCI/security edits has passed a new clean canonical gate.

The complete current receiver image pair in these diagnostics is CPUAPP HEX
SHA-256 `ac3ef3dcc5fbbc7ee70c432a7d26f69ec16492a9e0721255e5c8cde548d24e3b`
and FLPR HEX SHA-256
`c2197f4c664b11a28d499f527ec6d359ee122e9a9434b0a4e9f46b1e4239aa4a`.
The second XIAO is an **alternate** source or HCI role, never both at once.
Its firmware remains HCI after these runs. External fixture sessions revalidate
live USB and probe identity before target-changing actions; common-family
fingerprints do not bind roles: DP `0x6ba02477`, AP0/1 `0x84770001`, AP2
`0x32880000`, AP3 `0x00000000`, FICR PART `0x00054b15`, VARIANT
`0x41414330`. Only a fresh bound session resolves which board is receiver or
source. No static serial, tty or adapter mapping is recorded here.

## UART first-fault chronology and controls

| Diagnostic | Observed result and immutable private evidence |
| --- | --- |
| NCS v3.4.1 original AA generated HCI `ee0640e0de597aaec299f3a48ef74d37e2460b6ff92fb30f0de061556053c3b2` | First six-case attempt failed mono reconnect at 2,885 sent frames (`/tmp/opencode/pb019-migration-20260928-r2/audio/result.json`); exact-AA crossover failed Mode A reconnect at 11,283 sent (`/tmp/opencode/pb019-sentinelAA-crossover-20260928-r1/audio/result.json`). Whole-buffer sentinel initialization alone was **not** a fix. |
| Sentinel-only `0x55` perturbation | Exactly four Intel HEX address bytes changed, `0x21ffe`, `0x22004`, `0x22320`, `0x228e8` (`aa` to `55`); one six-case pass at `/tmp/opencode/pb019-sentinel55-20260928-r2/`. Exact original-AA crossover failed again. No `0x55` production repair was adopted. Parsed address comparison: `/tmp/opencode/pb019-sentinelAA-crossover-20260928-r1/intel-hex-comparison.json`. |
| AA swap witness | First six-case instrumented pass, then Mode A fresh failure at 11,470 sent: `/tmp/opencode/pb019-swap-witness-run-20260928-r1/` and `-r2/`. No-reset RAM and decoded records: `/tmp/opencode/pb019-swap-witness-core-20260928-r2/`. Mature old tails show sequence 54118 guessed limit 115 versus actual 114 and 54119 guessed 109 versus actual 110; the RAM ring had an inserted `aa` then a missing real byte. Witness writes RAM outside the collision critical window and can perturb timing. |
| Deferred-boundary v1 | Initially deferred only `prev_inc=1`. Extracted real-driver UART callback replay showed earlier AA RED 260 mismatches versus v1 GREEN 0 for the captured cuts. One six-case physical diagnostic passed (`/tmp/opencode/pb019-boundary-repair-run-20260928-r1/`); monitored repeat failed Mode A fresh at 7,110 sent (`/tmp/opencode/pb019-boundary-monitor-run-20260928-r1/`). Host ISO TX handle 2 / sequence 7102 had length byte `78`, while parser/ring and user DMA buffer contained `aa` at frame offset 6, with previous five frames exact and no host btsnoop drops/truncation: `/tmp/opencode/pb019-boundary-monitor-analysis-20260928-r1/summary.json`. A host monitor is **not** an independent wire capture. |
| Enriched v1 witness | One pass then Mode A fresh failure at 4,791 sent: `/tmp/opencode/pb019-boundary-witness-run-20260928-r1/` and `-r2/`. `/tmp/opencode/pb019-boundary-witness-core-20260928-r2/last-ten-compact.txt` records sequence 38740 with `new_count=7`, provisional old limit 113, mature actual cut 112. The immediate counter/PTR pair can be transient even outside the critical section; deferring only `prev_inc=1` misses this branch. |

These runs distinguish actual receiver delivery loss from an accepted stream.
They do not attribute every bad byte to the RF controller. In particular,
post-fault bounce RAM may have changed before the halt. First successful
no-reset capture required a private minimal CPUAPP-only OpenOCD configuration:
the SDK XIAO board configuration's unsupported `nrf5` flash bank caused GDB
auto-probe to read inapplicable FICR addresses; its examine-fail auto-recovery
hook is not suitable for RAM-only fault inspection. The first rejected GDB
attempt and warnings are retained at
`/tmp/opencode/pb019-migration-fault-core-20260928-r1/`; later minimal-config
full RAM at `-r2/` succeeded. This is not a recipe to use the flash-capable
auto-recover hook or to reset a fault before RAM capture. No SDK files changed.

## Reviewed v2 coherent cut and limits

The SDK source SHA-256 guard remains
`d68f45fbef9da8077efe6c9f94c609393fc3485bd1d486e4f710288f6d808bd3`;
the SDK tree remains untouched. The tracked generator retains full `0xAA`
initialization, provisionally defers **all** successful swaps, and requires
each of the counter and DMA pointer to remain **unchanged from its own prior
reading** across a 1 us quiet sample; count is not compared with pointer.
First-byte anomaly recovery is settled before copying. Each coherent sampler
is bounded to 128 attempts; a separate boundary-wait loop can make up to 128
iterations when no new pointer progress is visible. This is not a combined
128-attempt worst-case bound, and either exhausted cap fails closed. The 1 us
interval is an empirical **1 Mbaud 8N1 HCI-profile** window, not proof for
arbitrary baud rates or every peripheral. Reviewed
generated source SHA-256 is
`c16932ae1fba3047f70b5a9785ef0eb735b3a5b376b9fc60f9a349893f98bf2d`;
HCI HEX SHA-256 is
`52fca1feed89988a8e2693e3146a18e8810f2e81db17a389e9d0182560e70ea1`.
Pristine `fw-build-dongle` from the tracked generator reproduced **both**
exact bytes: `/tmp/opencode/pb019-coherent-integration-20260928-r1/dongle-pristine-build.log`.

Compiled actual driver functions and `UART_RX_RDY` callback bytes, not a
separate copied byte algorithm, drove the host replay. Prepare-only AA RED
had 263 mismatches, partial v1 RED had three, and coherent v2 GREEN had zero
in both runtime-configure branches; no new host compiler warning waiver.
The separate whole-sentinel exhaustive replay remained original 3/8192,
generated 0/8192. See `tests/unit/hci_uarte/` and the private replay under
`/tmp/opencode/pb019-coherent-boundary-20260928-r1/`.

Three exact v2 candidate six-case runs at
`/tmp/opencode/pb019-coherent-boundary-run-20260928-{r1,r2,r3}/` passed
**18 cases / 216,000 writer frames**, with nonzero receiver PLC **1,284**
aggregate. All three bounded kernel journals retained three
`unexpected SMP command 0x0b` messages, nine total. This was **not** warning-
free acceptance or proof of generic no-flow H4 reliability. Writer frame
counts do not count Mode A's two delivered CIS channels or Mode B's decoded
channels as extra source frames. The unchanged frozen transport criteria are
at least 90% valid SDUs, at most 5% PLC and zero decode/I2S/reset/error fields;
no limits, QoS, or baud rate were relaxed.

## SMP race and normal CLI continuation

Kernel 7.1.5 / BlueZ 5.87 diagnostics with proactive fresh `Device1.Pair()`
show MGMT Pair Device followed by the receiver's RX Security Request, TX
Pairing Request, RX Pairing Response and successful Encryption Change. The
kernel logged and dropped `0x0b` while an SMP context already existed. This
is not a blanket warning waiver; sanitized headers and timestamps (not key
values) are at
`/tmp/opencode/pb019-boundary-monitor-analysis-20260928-r1/smp-detail-final.txt`.

Normal fresh discovery now calls BlueZ `Device1.Connect()` without proactive
`Pair()`. The receiver still requests L2 security through the existing default
NoInputNoOutput agent; the central must observe both `Paired=True` and
`Connected=True` before proceeding to encrypted PACS/ASCS access. A partial
Connect is registered with the existing ordered cleanup owner **before**
attempting it. Existing preserve-bond and optional exact-peer/raw paths remain
distinct. The external strategy experiment passed six cases / 72,000 writer
frames, PLC 580, without `0x0b` warnings:
`/tmp/opencode/pb019-connect-bond-run-20260928-r1/`.

The **tracked normal CLI** then passed two separate exact-image sequences:
`/tmp/opencode/pb019-normal-cli-20260928-r1/` (passive host btmon) and
`/tmp/opencode/pb019-normal-cli-20260928-r2/` (unmonitored). Combined:
**12 cases / 144,000 writer frames**, PLC **1,005** (362 + 643), all unchanged
per-case transport limits met, zero receiver/source case alerts and zero
decode/I2S/reset/error fields. Both bounded kernel journals contained **zero**
SMP `0x0b` warnings, HCI hardware errors or opcode timeouts. r1 btsnoop had
194,043 records, zero drops/truncation; raw packet and keys remain private.
Summary: `/tmp/opencode/pb019-normal-cli-summary-20260928-r1/result.json`.
Owned adapter and root/child processes were absent after each run. These runs
are still on a dirty tree and are not a clean canonical software gate, clean
coverage baseline, analog qualification, RH4/FR4 exact-artifact acceptance,
PR acceptance or public release. Before any new hardware action, resolve both
live XIAO roles again through the fixture session; prior raw fingerprints are
evidence, **not** a reusable role mapping.
