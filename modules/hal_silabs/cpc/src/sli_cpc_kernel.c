/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CMSIS RTOS v2 functions used by the CPC core that the Zephyr port does not
 * implement.
 */

#include <cmsis_os2.h>

osKernelState_t osKernelGetState(void)
{
	return osKernelRunning;
}
