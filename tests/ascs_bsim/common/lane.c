/* SPDX-License-Identifier: Apache-2.0 */
#include "lane.h"
#include <zephyr/sys/byteorder.h>
#include <errno.h>
static void open_lane(void)
{
	uint32_t self = bsim_args_get_global_device_nbr();
	uint32_t peer = 1 - self, number = 0;
	__ASSERT(self < 2, "Two peers only");
	__ASSERT(bs_open_back_channel(self, &peer, &number, 1), "Backchannel open failed");
}
NATIVE_TASK(open_lane, PRE_BOOT_3, 100);
int lane_send(uint8_t kind, uint32_t token, uint32_t value)
{
	uint8_t raw[12] = {1, kind};
	sys_put_le32(token, raw + 2);
	sys_put_le32(value, raw + 6);
	for (unsigned i = 0; i < 10; i++) {
		raw[10] ^= raw[i];
	}
	bs_bc_send_msg(0, raw, sizeof(raw));
	return 0;
}
int lane_receive(struct lane_message *message)
{
	if (!bs_bc_is_msg_received(0)) {
		return -EAGAIN;
	}
	uint8_t raw[12], sum = 0;
	bs_bc_receive_msg(0, raw, sizeof(raw));
	for (unsigned i = 0; i < 10; i++) {
		sum ^= raw[i];
	}
	if (raw[0] != 1 || raw[10] != sum || raw[11]) {
		return -EBADMSG;
	}
	*message = (struct lane_message){raw[1], sys_get_le32(raw + 2), sys_get_le32(raw + 6)};
	return 0;
}
int lane_wait(uint8_t kind, uint32_t token, uint32_t *value, uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;
	do {
		struct lane_message message;
		int ret = lane_receive(&message);
		if (!ret) {
			if (message.kind != kind || message.token != token) {
				return -EBADMSG;
			}
			*value = message.value;
			return 0;
		}
		if (ret != -EAGAIN) {
			return ret;
		}
		k_sleep(K_MSEC(1));
	} while (k_uptime_get() < deadline);
	return -ETIMEDOUT;
}
