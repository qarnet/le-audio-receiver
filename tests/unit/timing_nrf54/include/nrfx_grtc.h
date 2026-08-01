/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Test-owned shadow of the installed NCS v3.3.0 <nrfx_grtc.h>.
 * Declares exactly the API surface used by src/audio_timing_nrf54.c;
 * the mock implementation lives in src/mock_hal.c.  Never part of a
 * production build.
 */

#ifndef NRFX_GRTC_H__
#define NRFX_GRTC_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * GRTC driver instance compare handler type.
 * @param[in] id        Channel ID.
 * @param[in] cc_value  Compare value.
 * @param[in] p_context User context.
 */
typedef void (*nrfx_grtc_cc_handler_t)(int32_t id, uint64_t cc_value, void *p_context);

/** GRTC capture/compare channel description structure. */
typedef struct {
	nrfx_grtc_cc_handler_t handler; /**< User handler. */
	void *p_context;                /**< User context. */
	uint8_t channel;                /**< Capture/compare channel number. */
} nrfx_grtc_channel_t;

int nrfx_grtc_channel_alloc(uint8_t *p_channel);
void nrfx_grtc_channel_callback_set(uint8_t channel, nrfx_grtc_cc_handler_t handler,
				    void *p_context);
int nrfx_grtc_channel_free(uint8_t channel);
int nrfx_grtc_syscounter_cc_absolute_set(nrfx_grtc_channel_t *p_chan_data, uint64_t val,
					 bool enable_irq);
int nrfx_grtc_syscounter_cc_disable(uint8_t channel);
uint64_t nrfx_grtc_syscounter_get(void);

#ifdef __cplusplus
}
#endif

#endif /* NRFX_GRTC_H__ */
