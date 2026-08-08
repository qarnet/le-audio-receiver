/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 *
 * Internal cpuapp ring-manager context accessors (R8).
 *
 * Production-neutral seam shared between src/flpr_ring_mgr.c and the
 * acceptance module src/flpr_acceptance.c.  The acceptance module needs
 * the SAME bulk-op mutex (ring_data_lock) and the raw ring bases so it
 * can hold the single lock authority over ring memory (stale-test slot
 * production, the FLPR-stall transaction) without duplicating locks.
 * This header is internal to src/; external production headers stay
 * clean.  Under FLPR_RING_MGR_NATIVE_TEST the ring bases are the host
 * test arrays; production builds return the devicetree addresses.
 */

#ifndef FLPR_RING_MGR_INTERNAL_H_
#define FLPR_RING_MGR_INTERNAL_H_

#include <stdint.h>

#include <zephyr/kernel.h> /* struct k_mutex */

#ifdef __cplusplus
extern "C" {
#endif

/** Bulk-op mutex serializing ring memory/header operations vs reset. */
struct k_mutex *flpr_ring_mgr_data_lock(void);

/** Input ring base (CPUAPP -> FLPR). */
uint8_t *flpr_ring_mgr_input_ring(void);

/** Output ring base (FLPR -> CPUAPP). */
uint8_t *flpr_ring_mgr_output_ring(void);

#ifdef __cplusplus
}
#endif

#endif /* FLPR_RING_MGR_INTERNAL_H_ */
