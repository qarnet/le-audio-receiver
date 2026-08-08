/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake transport controls for the FLPR-image acceptance native suite.
 */
#ifndef FAKE_FLPR_DEPS_H_
#define FAKE_FLPR_DEPS_H_

#include <stdint.h>

#include "flpr_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_DEPS_MAX_SENT 128U

/* Dependency-table callbacks (see acceptance.h). */
int fake_deps_send(const struct flpr_msg *msg);
void fake_deps_wake(void);

/* Controls / observation. */
void fake_deps_reset(void);
uint32_t fake_deps_sent_count(void);
const struct flpr_msg *fake_deps_sent_at(uint32_t i);
uint32_t fake_deps_sent_type_count(uint8_t type);
uint32_t fake_deps_wake_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_FLPR_DEPS_H_ */
