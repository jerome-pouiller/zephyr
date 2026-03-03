/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "siwx91x_nwp.h"
#include "siwx91x_nwp_bus.h"
#include "siwx91x_nwp_api.h"
#include "wiseconnect/components/sli_wifi/inc/sli_wifi_types.h"
#include "wiseconnect/components/device/silabs/si91x/wireless/ble/inc/rsi_bt_common.h"

LOG_MODULE_DECLARE(siwx91x_nwp, CONFIG_SIWX91X_NWP_LOG_LEVEL);

#define SCAN_FEATURE_QUICK_SCAN       BIT(0)
#define SCAN_FEATURE_RESULTS_TO_HOST  BIT(1)

#define PASSIVE_SCAN_ENABLE           BIT(7)
void siwx91x_nwp_scan(const struct device *dev, uint16_t channel_list, const char *ssid,
		      bool passive, bool internal)
{
	sli_wifi_request_scan_t params = { };

	__ASSERT(strlen(ssid) < sizeof(params.ssid), "Corrupted data");
	if (ssid) {
		strcpy(params.ssid, ssid);
	}
	if (passive) {
		params.pscan_bitmap[3] |= PASSIVE_SCAN_ENABLE;
	}
	if (ssid && channel_list) {
		params.scan_feature_bitmap |= SCAN_FEATURE_QUICK_SCAN;
	}
	if (!internal) {
		params.scan_feature_bitmap |= SCAN_FEATURE_RESULTS_TO_HOST;
	}

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_SCAN,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_set_psk(const struct device *dev, const char *psk)
{
	sli_wifi_request_psk_t params = {
		.type = 1, /* PSK */
	};

	__ASSERT(psk, "Invalid arguments");
	__ASSERT(strlen(psk) < sizeof(params.psk_or_pmk), "Invalid arguments");
	strcpy(params.psk_or_pmk, psk);

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_HOST_PSK,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

int siwx91x_nwp_join(const struct device *dev,
		     const char *ssid,
		     const uint8_t *bssid,
		     int security_type)
{
	sli_wifi_join_request_t params = {
		.power_level = 0x80 | FIELD_PREP(0x5C, 31),
		.security_type = security_type,
		.ssid_len = strlen(ssid),
	};
	struct net_buf *reply;
	uint8_t status;

	__ASSERT(ssid, "Invalid arguments");
	strncpy(params.ssid, ssid, sizeof(params.ssid));
	if (bssid) {
		memcpy(params.join_bssid, bssid, sizeof(params.join_bssid));
	}

	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_JOIN,
				     SLI_WLAN_MGMT_Q, 0);
	__ASSERT(reply != NULL, "Corrupted state");
	status = ((struct siwx91x_frame_desc *)reply->data)->reserved[8];
	net_buf_unref(reply);

	// see components/common/inc/sl_additional_status.h for status code
	switch (status) {
	case 0x00:
		return 0;
	case 0x19:
		return -ENOENT;
	case 0x21:
		return -EBADFD;
	default:
		return -EINVAL;
	}
}

void siwx91x_nwp_disconnect(const struct device *dev)
{
	sli_wifi_disassociation_request_t params = { };

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_DISCONNECT,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_set_region(const struct device *dev)
{
	sli_wifi_set_region_ap_request_t params = {
		.set_region_code_from_user_cmd = SET_REGION_CODE_FROM_USER,
		.country_code = "EU ",
		.no_of_rules = 1,
		.channel_info[0].first_channel = 1,
		.channel_info[0].no_of_channels = 13,
		.channel_info[0].max_tx_power = 20,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_SET_REGION_AP,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_wifi_init(const struct device *dev)
{
	siwx91x_nwp_send_buf(dev, NULL, 0, SLI_WIFI_REQ_INIT,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_get_mac_address(const struct device *dev, uint8_t mac[NET_ETH_ADDR_LEN])
{
	struct net_buf *reply;
	int reply_len;

	reply = siwx91x_nwp_send_buf(dev, NULL, 0, SLI_WIFI_REQ_MAC_ADDRESS, SLI_WLAN_MGMT_Q, 0);
	__ASSERT(reply != NULL, "Corrupted state");
	reply_len = net_buf_linearize(mac, NET_ETH_ADDR_LEN,
				      reply, sizeof(struct siwx91x_frame_desc), SIZE_MAX);
	net_buf_unref(reply);
}

void siwx91x_nwp_set_sta_config(const struct device *dev)
{
	sli_wifi_rejoin_params_t params = {
		.max_retry_attempts = 1,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_REJOIN_PARAMS,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_set_band(const struct device *dev, sl_wifi_band_mode_t band)
{
	uint8_t params = band;

	__ASSERT(band == SL_WIFI_BAND_MODE_2_4GHZ, "Unsupported option");
	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_BAND,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_set_config(const struct device *dev, uint16_t type, uint16_t value)
{
	sli_wifi_config_request_t params = {
		.config_type = type,
		.value = value,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_CONFIG,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_set_device_region(const struct device *dev, sl_wifi_region_code_t region_code)
{
	sli_wifi_set_region_request_t params = {
		.set_region_code_from_user_cmd = SET_REGION_CODE_FROM_USER,
		.region_code = (uint8_t)region_code,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_SET_REGION,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_get_firmware_version(const struct device *dev,
				      sl_wifi_firmware_version_t *version)
{
	struct net_buf *reply;

	reply = siwx91x_nwp_send_buf(dev, NULL, 0, SLI_WLAN_REQ_FULL_FW_VERSION, SLI_WLAN_MGMT_Q, 0);
	__ASSERT(reply != NULL, "Corrupted state");

	net_buf_linearize(version, sizeof(sl_wifi_firmware_version_t),
			  reply, sizeof(struct siwx91x_frame_desc), SIZE_MAX);
	net_buf_unref(reply);
}

void siwx91x_nwp_opermode(const struct device *dev,
			  const sl_wifi_system_boot_configuration_t *params)
{
	__ASSERT(params->coex_mode != SL_SI91X_WLAN_MODE, "You mean SL_SI91X_WLAN_ONLY_MODE");
	__ASSERT(params->oper_mode != SL_SI91X_CONCURRENT_MODE, "Not supported");

	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WIFI_REQ_OPERMODE, SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}

void siwx91x_nwp_dynamic_pool(const struct device *dev,
			      const sl_wifi_system_dynamic_pool_t *params)
{
	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WLAN_REQ_DYNAMIC_POOL,
			     SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}

void siwx91x_nwp_feature(const struct device *dev, bool enable_pll)
{
	sli_si91x_feature_frame_request params = {
		.rf_type = RSI_INTERNAL_RF,
		.wireless_mode = SL_WIFI_HP_CHAIN,
		.enable_ppp = 0,
		.afe_type = 1,
		.feature_enables = SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE |
				   SLI_FEAT_FRAME_LP_CHAIN |
				   SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE,
		.pll_mode = enable_pll ? 1 : 0,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_COMMON_REQ_FEATURE_FRAME,
			       SLI_WLAN_MGMT_Q,
			       SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}
