/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ASCS_LANE_H
#define ASCS_LANE_H
#include <stdint.h>
#include <zephyr/kernel.h>
#include <bs_types.h>
#include <bs_pc_backchannel.h>
#include <bsim_args_runner.h>
#include <posix_native_task.h>

enum {
	ARM = 1,
	ARMED = 2,
	CHECK = 3,
	RENDERED = 4,
	FINISH = 5,
	FINISHED = 6
};
struct lane_message {
	uint8_t kind;
	uint32_t token, value;
};
int lane_send(uint8_t kind, uint32_t token, uint32_t value);
int lane_receive(struct lane_message *message);
int lane_wait(uint8_t kind, uint32_t token, uint32_t *value, uint32_t timeout_ms);
#endif
