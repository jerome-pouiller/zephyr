/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr port of the CPC assertion: the invariants of the core are checked
 * by __ASSERT() when CONFIG_ASSERT is enabled. The condition is still
 * evaluated otherwise, the core relies on it in a few places.
 */

#ifndef CPC_ASSERT_H
#define CPC_ASSERT_H

#include <stdbool.h>

#include <zephyr/sys/__assert.h>

#define SLI_CPC_ASSERT(condition)                                                                  \
	__ASSERT_EVAL((void)(condition), bool __cpc_assert_ok = (condition), __cpc_assert_ok,      \
		      "CPC: " #condition)

#endif /* CPC_ASSERT_H */
