/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_NWP_API_H_
#define SIWX91X_NWP_API_H_
#include <zephyr/net/ethernet.h>
#include <zephyr/net/wifi.h>
#include "wiseconnect/components/device/silabs/si91x/wireless/inc/sl_wifi_device.h"
#include "wiseconnect/components/protocol/wifi/inc/sl_wifi_constants.h"
#include "wiseconnect/components/protocol/wifi/inc/sl_wifi_types.h"
#include "wiseconnect/components/sli_wifi/inc/sli_wifi_types.h"

struct device;

/* NWP APIs */
void siwx91x_nwp_soft_reset(const struct device *dev);
void siwx91x_nwp_force_assert(const struct device *dev);
void siwx91x_nwp_opermode(const struct device *dev,
			  sl_wifi_system_boot_configuration_t *params);
void siwx91x_nwp_feature(const struct device *dev, bool enable_pll);
void siwx91x_nwp_dynamic_pool(const struct device *dev, int tx, int rx, int global);
void siwx91x_nwp_set_device_region(const struct device *dev,
				   sl_wifi_region_code_t region_code);
void siwx91x_nwp_get_firmware_version(const struct device *dev,
				      sl_wifi_firmware_version_t *version);
int siwx91x_nwp_flash_erase(const struct device *dev, uint32_t dest, size_t len);
int siwx91x_nwp_flash_write(const struct device *dev, uint32_t dest, const void *buf, size_t len);
int siwx91x_nwp_fw_upgrade_start(const struct device *dev, const void *hdr);
int siwx91x_nwp_fw_upgrade_write(const struct device *dev, const void *buf, size_t len);

/* Wifi APIs */
void siwx91x_nwp_wifi_init(const struct device *dev);
void siwx91x_nwp_scan(const struct device *dev, uint16_t channel_list, const char *ssid,
		     bool passive, bool internal);
int siwx91x_nwp_join(const struct device *dev, const char *ssid,
		     const uint8_t *bssid, int security_type);
void siwx91x_nwp_disconnect(const struct device *dev);
void siwx91x_nwp_ap_config(const struct device *dev, sli_wifi_ap_config_request *params);
int siwx91x_nwp_ap_start(const struct device *dev, const char *ssid, int security_type);
void siwx91x_nwp_ap_stop(const struct device *dev);
void siwx91x_nwp_sta_disconnect(const struct device *dev,
				const uint8_t remote_addr[WIFI_MAC_ADDR_LEN]);
void siwx91x_nwp_get_bss_info(const struct device *dev, sl_wifi_operational_statistics_t *info);
void siwx91x_nwp_ps_disable(const struct device *dev);
void siwx91x_nwp_ps_enable(const struct device *dev, sli_wifi_power_save_request_t *params);
void siwx91x_nwp_twt_params(const struct device *dev, sl_wifi_twt_request_t *params);
void siwx91x_nwp_set_region(const struct device *dev);
void siwx91x_nwp_set_psk(const struct device *dev, const char *psk);
void siwx91x_nwp_set_sta_config(const struct device *dev);
void siwx91x_nwp_set_band(const struct device *dev, sl_wifi_band_mode_t band);
void siwx91x_nwp_set_ht_caps(const struct device *dev, bool enabled);
void siwx91x_nwp_set_config(const struct device *dev, uint16_t type, uint16_t value);
void siwx91x_nwp_get_mac_address(const struct device *dev, uint8_t mac[NET_ETH_ADDR_LEN]);

/* Socket APIs */
int siwx91x_nwp_sock_create(const struct device *dev, sli_si91x_socket_create_request_t *req,
			    sli_si91x_socket_create_response_t *resp);
int siwx91x_nwp_sock_close(const struct device *dev, sli_si91x_socket_close_request_t *req,
			   sl_si91x_socket_close_response_t *resp);
int siwx91x_nwp_sock_recv(const struct device *dev, sli_si91x_req_socket_read_t *req,
			  void *resp);
int siwx91x_nwp_sock_accept(const struct device *dev, sli_si91x_socket_accept_request_t *req,
			    sli_si91x_rsp_ltcp_est_t *resp);

#endif
