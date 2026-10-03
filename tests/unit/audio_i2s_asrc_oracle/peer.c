/* SPDX-License-Identifier: Apache-2.0 */
/* In-process remote transport endpoint. Real sink, offload manager and FLPR
 * processor remain linked. This is not shared-memory or physical FLPR proof. */
#include <string.h>
#include <zephyr/kernel.h>
#include "flpr_handshake.h"
#include "flpr_runtime.h"
#include "flpr_ring_mgr.h"
#include "flpr_ring.h"
#include "flpr_audio_process.h"

int peer_fault;
int32_t scripted_ppm;
static struct flpr_ring_slot_meta request;
static int16_t payload[960];

int audio_timing_init(void)
{
	return 0;
}
void audio_timing_reset(void)
{
}
int32_t audio_drift_controller_update(int free_blocks)
{
	(void)free_blocks;
	return scripted_ppm;
}
void audio_drift_reset(void)
{
}
void audio_stats_i2s_underrun(void)
{
}
void audio_stats_stream_reset(void)
{
}

void flpr_handshake_get_status(struct flpr_status *status)
{
	memset(status, 0, sizeof(*status));
	status->ready = status->acked = status->healthy = true;
}
void flpr_handshake_register_health_cb(flpr_health_transition_cb_t cb, void *user)
{
	(void)cb;
	(void)user;
}
int flpr_runtime_restart(uint32_t timeout_ms)
{
	(void)timeout_ms;
	return 0;
}
void flpr_runtime_get_status(struct flpr_runtime_status *status)
{
	memset(status, 0, sizeof(*status));
}
int flpr_ring_mgr_init(void)
{
	return 0;
}
int flpr_ring_mgr_remote_restarted(void)
{
	return 0;
}
int flpr_ring_mgr_coordinated_reset(uint32_t epoch, uint32_t timeout)
{
	(void)epoch;
	(void)timeout;
	return 0;
}
enum flpr_produce_result flpr_ring_mgr_produce_asrc(const int16_t *pcm, uint16_t frames,
						    uint32_t sequence, int32_t ppm,
						    const struct audio_asrc_state *pre_state)
{
	if (frames != 480) {
		return FLPR_PRODUCE_INVALID;
	}
	memcpy(payload, pcm, sizeof(payload));
	memset(&request, 0, sizeof(request));
	request.valid_frames = frames;
	request.sequence = sequence;
	request.flags = FLPR_SLOT_FLAG_VALID | FLPR_SLOT_FLAG_ASRC_LINEAR;
	request.correction_ppm = ppm;
	request.asrc_state = *pre_state;
	request.crc32 = flpr_ring_crc32((const uint8_t *)payload, sizeof(payload));
	return FLPR_PRODUCE_OK;
}
int flpr_ring_mgr_notify_producer(void)
{
	return 0;
}
int flpr_ring_mgr_wait_consume(uint32_t timeout)
{
	(void)timeout;
	if (peer_fault == 1) {
		peer_fault = 0;
		return -ETIMEDOUT;
	}
	return 0;
}
enum flpr_consume_result flpr_ring_mgr_consume_asrc_result(int16_t *pcm, uint16_t capacity,
							   struct flpr_consume_asrc_result *result)
{
	struct flpr_ring_slot_meta response;
	int ret = flpr_audio_process(&request, (const uint8_t *)payload, sizeof(payload), &response,
				     (uint8_t *)pcm, capacity * 4U);
	/* Match the production remote wrapper's status-finalization boundary. */
	flpr_ring_slot_set_processing(&response, 0, ret);
	memset(result, 0, sizeof(*result));
	result->sequence = response.sequence;
	result->output_frames = response.valid_frames;
	result->flags = response.flags;
	result->correction_ppm = response.correction_ppm;
	result->payload_crc = response.crc32;
	result->processing_status = response.processing_status;
	result->post_state = response.asrc_state;
	if (peer_fault == 2) {
		result->post_state.reserved[0] = 1;
		peer_fault = 0;
	}
	return FLPR_CONSUME_OK;
}
