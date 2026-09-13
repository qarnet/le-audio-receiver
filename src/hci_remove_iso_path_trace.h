/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HCI_REMOVE_ISO_PATH_TRACE_H_
#define HCI_REMOVE_ISO_PATH_TRACE_H_

#include <stdint.h>

struct hci_remove_iso_path_trace_ops {
	int (*source_id_get)(const char *name);
	uint32_t (*filter_set)(int source_id, uint32_t level);
};

struct hci_remove_iso_path_trace_result {
	int core_source_id;
	int driver_source_id;
	uint32_t core_level;
	uint32_t driver_level;
};

int hci_remove_iso_path_trace_arm(const struct hci_remove_iso_path_trace_ops *ops,
				  uint32_t debug_level,
				  struct hci_remove_iso_path_trace_result *result);

#endif /* HCI_REMOVE_ISO_PATH_TRACE_H_ */
