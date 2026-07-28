# Stage 2 automated brief-stall gate

## Fixed decisions

- Remove `CONFIG_BT_SMP_SC_PAIR_ONLY=n` again. SC-only hardware already passed
  in `2dacc3e` using normal BlueZ-owned pairing. Never use raw pre-connect for
  this gate.
- Add `scripts/flpr_stall_gate.py`, using pyserial directly on nRF54 console.
  One process owns UART for whole injection; no serial-MCP polling latency.

## Script algorithm

1. Open configured console (`ttyACM0`, 115200), preserve raw log to caller path.
2. Send `flpr offload`; wait until line contains `State` and `ACTIVE`.
3. Send `flpr ring stall_flpr 1`.
4. Read continuously until ACK line contains
   `FLPR stall applied: 0x01 (cons_in=1 prod_out=0)`; record monotonic time.
5. Continue reading until first `offload recovery OK` line; this proves at least
   one real fault/reset. Immediately send `flpr ring stall_flpr 0` from same
   process, without polling/sleep.
6. Require clear ACK containing `0x00 (cons_in=0 prod_out=0)`.
7. Continue reading and periodically send `flpr offload` until status proves:
   ACTIVE, recovery attempts >=1, max exhaustion=0, probation active=0,
   probation cleared>=1, and successes increased by >=100 after clear.
8. Send audio/ring/FLPR status commands, close port, exit nonzero on timeout or
   missing predicate. Default total timeout 30 s.

Use regexes against current shell output, not fixed line offsets. Script must
never reset, flash, erase, or alter bonds.

## Hardware execution

- Build/flash SC-only firmware.
- Clear bonds, attach compiled-public dongle, normal discovery/async Pair path.
- Start receiver log/injection script and 90 s Mode A central concurrently.
- Gate script runs only after stream ACTIVE.
- Central: 100 fps. Audio faults: zero. Fault evidence remains after recovery.

## Completion

Add script unit tests using recorded/fake serial line streams for success,
missing ACK, exhaustion, timeout. Run Python compile, tests, both builds, live
gate. Update Stage 2 results and commit. No design changes.
