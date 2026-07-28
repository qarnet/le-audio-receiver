/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * FLPR runtime restart manager — nRF54L15 only.
 *
 * CPUAPP-owned synchronous restart of the SRAM-executing FLPR VPR
 * using public nrfx VPR HAL + public IPC service lifecycle.
 *
 * Idle-only in Stage 4A — rejects restart when offload stream active.
 * Never reboot CPUAPP on FLPR failure.
 */

#ifndef FLPR_RUNTIME_H_
#define FLPR_RUNTIME_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime state. */
enum flpr_runtime_state {
	FLPR_RUNTIME_IDLE = 0,
	FLPR_RUNTIME_BUSY,
	FLPR_RUNTIME_UNAVAILABLE, /* FLPR stopped or unrecoverable */
};

/** Status snapshot (shell-readable). */
struct flpr_runtime_status {
	enum flpr_runtime_state state;
	uint32_t requests;          /* total restart attempts */
	uint32_t success_count;     /* successful restarts */
	uint32_t fail_count;        /* failed restarts */
	uint32_t busy_reject;       /* rejected due to busy/active stream */
	uint32_t previous_epoch;    /* epoch before last restart */
	uint32_t new_epoch;         /* epoch after last restart */
	uint32_t reload_bytes;      /* bytes copied from source to execution */
	uint32_t source_crc;        /* CRC-32 of source memory (last restart) */
	uint32_t execution_crc;     /* CRC-32 of execution memory after copy */
	int last_errno;             /* last error (0 on success) */
	uint32_t total_duration_ms; /* cumulative restart duration */
	uint32_t max_duration_ms;   /* longest single restart */
};

/**
 * @brief Initialise the FLPR runtime manager.
 *
 * Derives VPR register base, source-memory, execution-memory from DT.
 * Non-blocking; does not restart FLPR.
 *
 * @return 0 on success, negative errno on failure.
 */
int flpr_runtime_init(void);

/**
 * @brief Perform a full FLPR restart cycle.
 *
 * Synchronous, mutex-serialised.  Rejected if offload stream is active.
 * Never called from ISR/BT callback — shell or dedicated thread only.
 *
 * Sequence:
 *   1. Snapshot previous handshake epoch; mark manager busy.
 *   2. Deregister CPUAPP IPC endpoint (handshake disconnect).
 *   3. Stop VPR (nrf_vpr_cpurun_set false).
 *   4. Pulse NDMRESET via debugif DMCONTROL mask.
 *   5. Copy execution-memory from source-memory (exact exec size).
 *   6. Cache flush + full barrier.
 *   7. CRC-32 source + execution, require equality.
 *   8. Set INITPC to execution base.
 *   9. Re-register IPC endpoint (handshake reconnect).
 *  10. Start VPR (nrf_vpr_cpurun_set true).
 *  11. Wait bound, then wait new READY with epoch different from snapshot.
 *  12. Return success; on failure leave FLPR unavailable, report stage/error.
 *
 * @param timeout_ms  Maximum total time for the restart (bound+ready).
 * @return 0 on success, negative errno on failure.
 */
int flpr_runtime_restart(uint32_t timeout_ms);

/**
 * @brief Get a snapshot of the runtime manager status.
 */
void flpr_runtime_get_status(struct flpr_runtime_status *out);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RUNTIME_H_ */
