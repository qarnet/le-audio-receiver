/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>

#include "hci_remove_iso_path_trace.h"

int hci_remove_iso_path_trace_arm(const struct hci_remove_iso_path_trace_ops *ops,
				  uint32_t debug_level,
				  struct hci_remove_iso_path_trace_result *result)
{
	if (ops == NULL || ops->source_id_get == NULL || ops->filter_set == NULL ||
	    result == NULL) {
		return -EINVAL;
	}

	result->core_source_id = -1;
	result->driver_source_id = -1;
	result->core_level = 0U;
	result->driver_level = 0U;

	result->core_source_id = ops->source_id_get("bt_hci_core");
	result->driver_source_id = ops->source_id_get("bt_sdc_hci_driver");
	if (result->core_source_id < 0 || result->driver_source_id < 0) {
		return -ENOENT;
	}

	result->core_level = ops->filter_set(result->core_source_id, debug_level);
	result->driver_level = ops->filter_set(result->driver_source_id, debug_level);
	if (result->core_level != debug_level || result->driver_level != debug_level) {
		return -ERANGE;
	}

	return 0;
}
