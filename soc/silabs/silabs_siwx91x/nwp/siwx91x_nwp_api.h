/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_NWP_API_H_
#define SIWX91X_NWP_API_H_
#include <zephyr/net/ethernet.h>
#include "wiseconnect/components/device/silabs/si91x/wireless/inc/sl_wifi_device.h"
#include "wiseconnect/components/protocol/wifi/inc/sl_wifi_constants.h"
#include "wiseconnect/components/protocol/wifi/inc/sl_wifi_types.h"

struct device;

/* NWP APIs */
void siwx91x_nwp_opermode(const struct device *dev,
			  const sl_wifi_system_boot_configuration_t *params);
void siwx91x_nwp_dynamic_pool(const struct device *dev,
			      const sl_wifi_system_dynamic_pool_t *params);
void siwx91x_nwp_feature(const struct device *dev, bool enable_pll);
void siwx91x_nwp_set_device_region(const struct device *dev,
				   sl_wifi_region_code_t region_code);
void siwx91x_nwp_get_firmware_version(const struct device *dev,
				      sl_wifi_firmware_version_t *version);

/* Wifi APIs */
void siwx91x_nwp_wifi_init(const struct device *dev);
void siwx91x_nwp_scan(const struct device *dev, uint16_t channel_list, const char *ssid,
		     bool passive, bool internal);
int siwx91x_nwp_join(const struct device *dev, const char *ssid,
		     const uint8_t *bssid, int security_type);
void siwx91x_nwp_disconnect(const struct device *dev);
void siwx91x_nwp_set_region(const struct device *dev);
void siwx91x_nwp_set_psk(const struct device *dev, const char *psk);
void siwx91x_nwp_set_sta_config(const struct device *dev);
void siwx91x_nwp_set_band(const struct device *dev, sl_wifi_band_mode_t band);
void siwx91x_nwp_set_config(const struct device *dev, uint16_t type, uint16_t value);
void siwx91x_nwp_get_mac_address(const struct device *dev, uint8_t mac[NET_ETH_ADDR_LEN]);

#endif
