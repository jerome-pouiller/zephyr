/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_SOC_SILABS_SIWX91X_SIWG917_HAL_H_
#define ZEPHYR_SOC_SILABS_SIWX91X_SIWG917_HAL_H_
#include "sl_wifi_types.h"
#include "sl_wifi_device.h"
#include <zephyr/kernel.h>
#include <stdint.h>

struct device;
struct net_buf;
struct siwx91x_nwp_wifi_cb;
struct siwx91x_nwp_bt_cb;

int siwx91x_nwp_hal_init(const struct device *dev,
			 const sl_wifi_device_configuration_t *dev_config,
			 bool first_init);
void siwx91x_nwp_register_wifi(const struct device *dev, struct siwx91x_nwp_wifi_cb *val);
void siwx91x_nwp_register_bt(const struct device *dev, struct siwx91x_nwp_bt_cb *val);

int siwx91x_nwp_get_firmware_version(const struct device *dev, sl_wifi_firmware_version_t *version);

#endif
