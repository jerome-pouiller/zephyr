/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_SOC_SILABS_SIWX91X_SIWG917_HAL_RAM_H_
#define ZEPHYR_SOC_SILABS_SIWX91X_SIWG917_HAL_RAM_H_
#include <zephyr/kernel.h>
#include <stdint.h>

struct device;
struct net_buf;

struct net_buf *siwx91x_nwp_send_frame(const struct device *dev,
				       struct net_buf *buf,
				       uint16_t command,
				       int queue_id,
				       k_timeout_t wait_duration,
				       uint8_t flags);

void siwx91x_nwp_isr(const struct device *dev);
void siwx91x_nwp_thread(void *arg1, void *arg2, void *arg3);

#endif
