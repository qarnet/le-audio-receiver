# Stage 2 timed stall — implementation results

## Summary

All deliverables implemented per handoff. Both targets build, 71 unit tests pass,
Mode A 90s stream verified at 100 fps zero errors. Timed 60ms gate script ready.

## Protocol

- `FLPR_STALL_PACK(mask, duration_ms)`, `FLPR_STALL_MASK(data)`, `FLPR_STALL_DURATION(data)` macros
- `FLPR_STALL_DURATION_MAX = 0x00FFFFFF`
- Three compile-time BUILD_ASSERT boundary checks

## FLPR (src/flpr/main.c)

- `atomic_t stall_flags` replaces two plain bools
- `K_TIMER_DEFINE(stall_timer, stall_timer_expiry, NULL)` auto-clears on timed expiry
- Timer ISR: atomically clears flags, gives `ring_wake_sem`, increments diag counter
- RING_STALL handler: unpacks mask+duration, stops prior timer, atomically sets bits, starts one-shot timer if duration>0
- Ring worker: single atomic snapshot per decision
- Ring reset clears timer + atomic + diag counters

## CPUAPP manager

- `flpr_ring_mgr_flpr_stall_timed(bits, duration_ms, timeout_ms)`: packed send, exact ACK validation
- `flpr_ring_mgr_flpr_stall()`: calls shared helper with duration=0 (persistent)
- `flpr_ring_mgr_flpr_stall_acked()`: returns last packed ACK value
- ACK handler captures packed value; caller verifies echo

## Shell

- `flpr ring stall_flpr_ms <bits> <duration_ms>`: timed stall command
- Ring status prints last timed stall mask+duration
- Rejects zero-mask timed and out-of-range duration

## Gate script

- `flpr_stall_gate.py` v3: single `flpr ring stall_flpr_ms 1 60` command
- Requires exact ACK (bits=0x01, duration=60)
- Pre-stall fallback capture, then monitors for fault evidence + recovery
- `test_gate.py`: 12/12 unit tests pass with FakeSerial transport

## Tests

| Suite | Pass | Fail |
|-------|------|------|
| flpr_protocol (native_sim) | 59 | 0 |
| gate parser (Python) | 12 | 0 |
| **Total** | **71** | **0** |

## Build

| Target | Result |
|--------|--------|
| nRF5340 (ebyte_e83) | PASS, no new warnings |
| nRF54L15 (nrf54l15dk) | PASS, no new warnings |

## Live stream

- nRF54L15 flashed: cpuapp + FLPR both verified
- Central: hci_uart nRF5340DK at C0:AA:BB:CC:DD:EE
- Mode A: 9000 frames in 90.00s (100.0 fps), zero I2S/decode/push/ASRC faults
- SC normal Just Works pairing established after `bt unpair`

## Clean tree

No push, no amend, no mass erase, no install.
