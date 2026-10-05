/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/audio/audio.h>

int __real_bt_audio_data_parse(const uint8_t ltv[], size_t size,
			       bool (*func)(struct bt_data *data, void *user_data),
			       void *user_data);

int __wrap_bt_audio_data_parse(const uint8_t ltv[], size_t size,
			       bool (*func)(struct bt_data *data, void *user_data), void *user_data)
{
	if (ltv == NULL || func == NULL) {
		return -EINVAL;
	}

	for (size_t offset = 0; offset < size;) {
		const uint8_t len = ltv[offset];
		const size_t remaining = size - offset;

		/* The length octet precedes len bytes of type and value. */
		if (len < 1U || (size_t)len >= remaining) {
			return -EINVAL;
		}

		int ret =
			__real_bt_audio_data_parse(ltv + offset, 1U + (size_t)len, func, user_data);
		if (ret != 0) {
			return ret;
		}
		offset += 1U + (size_t)len;
	}

	return 0;
}
