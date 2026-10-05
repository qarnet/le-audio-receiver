/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <string.h>

struct observed {
	uint8_t types[4], lengths[4], bytes[4];
	size_t entries;
	bool stop;
};
static bool collect(struct bt_data *data, void *user)
{
	struct observed *out = user;
	zassert_true(out->entries < 4);
	out->types[out->entries] = data->type;
	out->lengths[out->entries] = data->data_len;
	out->bytes[out->entries] = data->data_len ? data->data[0] : 0;
	if (!data->data_len) {
		zassert_is_null(data->data);
	}
	out->entries++;
	return !out->stop;
}
ZTEST(ltv_bounds, test_metadata_length_validation)
{
	/* Isolated input validation, not RF corruption or security acceptance.
	 * Padding keeps actual access in bounds; declared range has no value byte. */
	uint8_t padded_metadata[] = {2, 0xf0, 0xaa};
	struct observed out = {0};
	int ret = bt_audio_data_parse(padded_metadata, 2, collect, &out);
	printk("METADATA_VALIDATION ret=%d delivered_entries=%u value=%02x\n", ret,
	       (unsigned)out.entries, out.bytes[0]);
	zassert_equal(ret, -EINVAL, "Missing value at exact logical end must reject");
	zassert_equal(out.entries, 0, "Invalid encoding cannot expose out-of-range data");
}
ZTEST(ltv_bounds, test_valid_entries_and_cancel)
{
	uint8_t input[] = {1, 0xf0, 2, 0xf1, 0xaa};
	struct observed out = {0};
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 2);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	zassert_equal(out.types[1], 0xf1);
	zassert_equal(out.lengths[1], 1);
	zassert_equal(out.bytes[1], 0xaa);
	out = (struct observed){.stop = true};
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), -ECANCELED);
	zassert_equal(out.entries, 1);
}
ZTEST(ltv_bounds, test_empty_input_and_length_validation)
{
	uint8_t input[] = {4, 0xf0, 0xaa, 0xbb, 0xcc};
	struct observed out = {0};
	zassert_equal(bt_audio_data_parse(NULL, 0, collect, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(input, sizeof(input), NULL, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(input, 0, collect, &out), 0);
	zassert_equal(bt_audio_data_parse(input, 2, collect, &out), -EINVAL);
	zassert_equal(out.entries, 0);
	input[0] = 0;
	zassert_equal(bt_audio_data_parse(input, 1, collect, &out), -EINVAL);
}

ZTEST(ltv_bounds, test_length_error_then_valid_retry)
{
	/* Isolated input validation, not RF corruption or security acceptance. */
	uint8_t input[] = {4, 0xf0, 0xaa, 0xbb, 0xcc};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(input, 2, collect, &out), -EINVAL);
	zassert_equal(out.entries, 0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 3);
	zassert_equal(out.bytes[0], 0xaa);
}

ZTEST(ltv_bounds, test_zero_length_entry_then_valid_retry)
{
	uint8_t invalid[] = {0, 0xf0, 0xaa};
	uint8_t valid[] = {2, 0xf0, 0xaa};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(invalid, 1, collect, &out), -EINVAL);
	zassert_equal(out.entries, 0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(valid, sizeof(valid), collect, &out), 0);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 1);
	zassert_equal(out.bytes[0], 0xaa);
}

ZTEST(ltv_bounds, test_callback_cancellation_then_valid_retry)
{
	uint8_t input[] = {1, 0xf0, 2, 0xf1, 0xaa};
	struct observed out = {.stop = true};

	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), -ECANCELED);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 2);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	zassert_equal(out.types[1], 0xf1);
	zassert_equal(out.lengths[1], 1);
	zassert_equal(out.bytes[1], 0xaa);
}

ZTEST(ltv_bounds, test_final_entry_length_validation)
{
	/* Padded backing keeps invalid declared length inside allocation.
	 * Already-delivered prefix stays visible; no whole-buffer preflight assumed. */
	uint8_t input[] = {1, 0xf0, 4, 0xf1, 0xaa, 0xbb, 0xcc};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(input, 4, collect, &out), -EINVAL);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 2);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	zassert_equal(out.types[1], 0xf1);
	zassert_equal(out.lengths[1], 3);
	zassert_equal(out.bytes[1], 0xaa);
}

ZTEST(ltv_bounds, test_missing_type_at_logical_end)
{
	uint8_t padded[] = {1, 0xf0};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(padded, 1, collect, &out), -EINVAL);
	zassert_equal(out.entries, 0);
	zassert_equal(bt_audio_data_parse(padded, sizeof(padded), collect, &out), 0);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
}

ZTEST(ltv_bounds, test_missing_value_after_delivered_prefix)
{
	uint8_t padded[] = {1, 0xf0, 2, 0xf1, 0xaa};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(padded, 4, collect, &out), -EINVAL);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(padded, sizeof(padded), collect, &out), 0);
	zassert_equal(out.entries, 2);
	zassert_equal(out.bytes[1], 0xaa);
}

ZTEST(ltv_bounds, test_cancellation_before_invalid_suffix)
{
	uint8_t padded[] = {1, 0xf0, 2, 0xf1, 0xaa};
	struct observed out = {.stop = true};

	zassert_equal(bt_audio_data_parse(padded, 4, collect, &out), -ECANCELED);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	out = (struct observed){0};
	zassert_equal(bt_audio_data_parse(padded, 4, collect, &out), -EINVAL);
	zassert_equal(out.entries, 1);
}

ZTEST(ltv_bounds, test_max_length_entry_and_short_retry)
{
	uint8_t input[256] = {255, 0xf0};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(input, sizeof(input) - 1, collect, &out), -EINVAL);
	zassert_equal(out.entries, 0);
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 1);
	zassert_equal(out.types[0], 0xf0);
	zassert_equal(out.lengths[0], 254);
	zassert_equal(out.bytes[0], 0);
}

ZTEST(ltv_bounds, test_empty_and_null_argument_combinations)
{
	uint8_t input[] = {1, 0xf0};
	struct observed out = {0};

	zassert_equal(bt_audio_data_parse(NULL, 0, collect, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(NULL, sizeof(input), collect, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(input, 0, NULL, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(input, sizeof(input), NULL, &out), -EINVAL);
	zassert_equal(bt_audio_data_parse(input, 0, collect, &out), 0);
	zassert_equal(out.entries, 0);
	zassert_equal(bt_audio_data_parse(input, sizeof(input), collect, &out), 0);
	zassert_equal(out.entries, 1);
}
ZTEST_SUITE(ltv_bounds, NULL, NULL, NULL, NULL, NULL);
