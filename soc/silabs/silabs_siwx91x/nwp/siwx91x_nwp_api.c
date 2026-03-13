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

/* Contains low level helpers for NWP. Each function will run one (and only one) NWP command. This
 * is a first layer of abstraction for the driver.
 *
 * The function tries to expose only the relevant arguments to the upper layers. In some case,
 * several functions may be mapped to the same NWP API. It happens when API has several mode and
 * some argument are only meaningful in some mode (typically, siwx91x_nwp_fw_upgrade_write vs
 * siwx91x_nwp_fw_upgrade_start or siwx91x_nwp_flash_write vs siwx91x_nwp_flash_erase).
 *
 * We prefer to expose NWP agnostic parameters, however, for the complex API, rather than exposing
 * ridiculous number of parameters, we may expose the NWP structure to the caller.
 */


/* sli_si91x_driver_send_command */
static struct net_buf *siwx91x_nwp_send_buf(const struct device *dev,
					    const void *data_buf, size_t data_len,
					    uint16_t command, int queue_id, uint8_t flags)
{
	/* FIXME: net_buf are not supposed to be allocate outside of a mempool. However, in this is
	 * really helpful to allocate in the stack.
	 */
	struct siwx91x_frame_desc desc_buf;
	struct net_buf *reply;
	struct net_buf data_container = {
		.__buf = (uint8_t *)data_buf,
		.data = (uint8_t *)data_buf,
		.size = data_len,
		.len = data_len,
		.ref = 1,
		.frags = NULL,
		.flags = NET_BUF_EXTERNAL_DATA,
	};
	struct {
		struct net_buf buf;
		void *user_data;
	} desc_container = {
		.buf.__buf = (uint8_t *)&desc_buf,
		.buf.data = (uint8_t *)&desc_buf,
		.buf.size = sizeof(desc_buf),
		.buf.len = sizeof(desc_buf),
		.buf.ref = 1,
		.buf.frags = &data_container,
		.buf.flags = NET_BUF_EXTERNAL_DATA,
		.buf.user_data_size = sizeof(void *),
	};

	__ASSERT(!(flags & SIWX91X_FRAME_FLAG_ASYNC), "Invalid call");
	__ASSERT(!(flags & SIWX91X_FRAME_FLAG_NO_HDR_RESET), "Invalid call");
	reply = siwx91x_nwp_send_frame(dev, &desc_container.buf, command, queue_id, flags);
	__ASSERT(data_container.ref == 1, "Corrupted state");
	__ASSERT(desc_container.buf.ref == 1, "Corrupted state");
	return reply;
}

static struct net_buf *siwx91x_nwp_send_buf2(const struct device *dev,
					     const void *data1_buf, size_t data1_len,
					     const void *data2_buf, size_t data2_len,
					     uint16_t command, int queue_id, uint8_t flags)
{
	/* FIXME: net_buf are not supposed to be allocate outside of a mempool. However, in this is
	 * really helpful to allocate in the stack.
	 */
	struct siwx91x_frame_desc desc_buf;
	struct net_buf *reply;
	struct net_buf data2_container = {
		.__buf = (uint8_t *)data2_buf,
		.data = (uint8_t *)data2_buf,
		.size = data2_len,
		.len = data2_len,
		.ref = 1,
		.frags = NULL,
		.flags = NET_BUF_EXTERNAL_DATA,
	};
	struct net_buf data1_container = {
		.__buf = (uint8_t *)data1_buf,
		.data = (uint8_t *)data1_buf,
		.size = data1_len,
		.len = data1_len,
		.ref = 1,
		.frags = &data2_container,
		.flags = NET_BUF_EXTERNAL_DATA,
	};
	struct {
		struct net_buf buf;
		void *user_data;
	} desc_container = {
		.buf.__buf = (uint8_t *)&desc_buf,
		.buf.data = (uint8_t *)&desc_buf,
		.buf.size = sizeof(desc_buf),
		.buf.len = sizeof(desc_buf),
		.buf.ref = 1,
		.buf.frags = &data1_container,
		.buf.flags = NET_BUF_EXTERNAL_DATA,
		.buf.user_data_size = sizeof(void *),
	};

	__ASSERT(!(flags & SIWX91X_FRAME_FLAG_ASYNC), "Invalid call");
	__ASSERT(!(flags & SIWX91X_FRAME_FLAG_NO_HDR_RESET), "Invalid call");
	reply = siwx91x_nwp_send_frame(dev, &desc_container.buf, command, queue_id, flags);
	__ASSERT(data2_container.ref == 1, "Corrupted state");
	__ASSERT(data1_container.ref == 1, "Corrupted state");
	__ASSERT(desc_container.buf.ref == 1, "Corrupted state");
	return reply;
}

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
	uint16_t status;

	__ASSERT(ssid, "Invalid arguments");
	strncpy(params.ssid, ssid, sizeof(params.ssid));
	if (bssid) {
		memcpy(params.join_bssid, bssid, sizeof(params.join_bssid));
	}

	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_JOIN,
				     SLI_WLAN_MGMT_Q, 0);
	__ASSERT(reply != NULL, "Corrupted state");
	/* FIXME: Simplify this expression */
	status = *(uint16_t *)(&((struct siwx91x_frame_desc *)reply->data)->reserved[8]);
	net_buf_unref(reply);

	// see components/common/inc/sl_additional_status.h for status code
	switch (status) {
	case 0x0000:
		return 0;
	case 0x0019:
		return -ENOENT;
	case 0x0021:
		return -EBADFD;
	default:
		return -EINVAL;
	}
}

void siwx91x_nwp_disconnect(const struct device *dev)
{
	sli_wifi_disassociation_request_t params = {
		.mode_flag = SL_WIFI_CLIENT_VAP_ID,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_DISCONNECT,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_ap_config(const struct device *dev, sli_wifi_ap_config_request *params)
{
	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WIFI_REQ_AP_CONFIGURATION,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

int siwx91x_nwp_ap_start(const struct device *dev, const char *ssid, int security_type)
{
	sli_wifi_join_request_t params = {
		/* I believe it does not make sense */
		// .join_feature_bitmap = SL_WIFI_JOIN_FEAT_LISTEN_INTERVAL_VALID,
		.power_level = 0x80 | FIELD_PREP(0x5C, 31),
		/* FIXME: This is already defined in ap_config */
		.security_type = security_type,
		.ssid_len = strlen(ssid),
	};
	struct net_buf *reply;
	uint8_t status;

	__ASSERT(params.ssid_len < sizeof(params.ssid) - 1, "Corrupted argument");
	strcpy(params.ssid, ssid);

	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_JOIN,
				     SLI_WLAN_MGMT_Q, 0);
	__ASSERT(reply != NULL, "Corrupted state");
	status = ((struct siwx91x_frame_desc *)reply->data)->data[0];
	net_buf_unref(reply);

	if (status != 'G') {
		return -EINVAL;
	}
	return 0;
}

void siwx91x_nwp_ap_stop(const struct device *dev)
{
	sli_wifi_disassociation_request_t params = {
		/* FIXME: the parameter seems ignored, but it would make sense to use
		 * SL_WIFI_AP_VAP_ID
		 */
		.mode_flag = SL_WIFI_CLIENT_VAP_ID,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_AP_STOP,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_sta_disconnect(const struct device *dev,
				const uint8_t remote_addr[WIFI_MAC_ADDR_LEN])
{
	sli_wifi_disassociation_request_t params = {
		.mode_flag = SL_WIFI_AP_VAP_ID,
	};

	memcpy(&params.client_mac_address, remote_addr, WIFI_MAC_ADDR_LEN);
	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_DISCONNECT,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_get_bss_info(const struct device *dev, sl_wifi_operational_statistics_t *info)
{
	struct net_buf *reply;
	struct siwx91x_frame_desc *desc;

	reply = siwx91x_nwp_send_buf(dev, NULL, 0, SLI_WIFI_REQ_GET_STATS, SLI_WLAN_MGMT_Q, 0);
	desc = (struct siwx91x_frame_desc *)reply->data;

	memcpy(info, desc->data, sizeof(sl_wifi_operational_statistics_t));
	net_buf_unref(reply);
}

void siwx91x_nwp_ps_disable(const struct device *dev)
{
	sli_wifi_power_save_request_t params = { };

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_PWRMODE,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}


void siwx91x_nwp_ps_enable(const struct device *dev, sli_wifi_power_save_request_t *params)
{
	__ASSERT(params->power_mode != 0, "Prefer siwx91x_nwp_disable_ps()");
	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WIFI_REQ_PWRMODE,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

void siwx91x_nwp_twt_params(const struct device *dev, sl_wifi_twt_request_t *params)
{
	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WIFI_REQ_TWT_PARAMS,
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

void siwx91x_nwp_set_ht_caps(const struct device *dev, bool enabled)
{
	sli_wifi_request_ap_high_throughput_capability_t params = {
		.mode_11n_enable = true,
		.ht_caps_bitmap = SL_WIFI_HT_CAPS_NUM_RX_STBC |
			          SL_WIFI_HT_CAPS_SHORT_GI_20MHZ |
				  SL_WIFI_HT_CAPS_GREENFIELD_EN,
	};

	__ASSERT(enabled == true, "Not supported");
	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WIFI_REQ_HT_CAPABILITIES,
			     SLI_WLAN_MGMT_Q, SIWX91X_FRAME_FLAG_NO_REPLY);
}

int siwx91x_nwp_sock_create(const struct device *dev, sli_si91x_socket_create_request_t *params,
			    sli_si91x_socket_create_response_t *resp)
{
	struct net_buf *reply;

	reply = siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WLAN_REQ_SOCKET_CREATE,
			     SLI_WLAN_MGMT_Q, 0);
	net_buf_unref(reply);
	return 0;
}

int siwx91x_nwp_sock_close(const struct device *dev, sli_si91x_socket_close_request_t *params,
			   sl_si91x_socket_close_response_t *resp)
{
	struct net_buf *reply;

	reply = siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WLAN_REQ_SOCKET_CLOSE,
			     SLI_WLAN_MGMT_Q, 0);
	net_buf_unref(reply);
	return 0;
}

int siwx91x_nwp_sock_recv(const struct device *dev, sli_si91x_req_socket_read_t *params,
			  void *resp)
{
	struct net_buf *reply;

	reply = siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WLAN_REQ_SOCKET_READ_DATA,
			     SLI_WLAN_MGMT_Q, 0);
	net_buf_unref(reply);
	return 0;
}

int siwx91x_nwp_sock_accept(const struct device *dev, sli_si91x_socket_accept_request_t *params,
			    sli_si91x_rsp_ltcp_est_t *resp)
{
	struct net_buf *reply;

	reply = siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WLAN_REQ_SOCKET_ACCEPT,
			     SLI_WLAN_MGMT_Q, 0);
	net_buf_unref(reply);
	return 0;
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
			  sl_wifi_system_boot_configuration_t *params)
{
	__ASSERT(params->coex_mode != SL_SI91X_WLAN_MODE, "You mean SL_SI91X_WLAN_ONLY_MODE");
	__ASSERT(params->oper_mode != SL_SI91X_CONCURRENT_MODE, "Not supported");

	siwx91x_nwp_send_buf(dev, params, sizeof(*params), SLI_WIFI_REQ_OPERMODE, SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}

void siwx91x_nwp_dynamic_pool(const struct device *dev, int tx, int rx, int global)
{
	sl_wifi_system_dynamic_pool_t params = {
		.tx_ratio_in_buffer_pool = tx,
		.rx_ratio_in_buffer_pool = rx,
		.global_ratio_in_buffer_pool = global,
	};

	siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WLAN_REQ_DYNAMIC_POOL,
			     SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}

/* This is a redefintion of sli_si91x_request_ta2m4_t, but without allocating the the full
 * input_data field.
 */
struct si91x_request_ta2m4 {
  uint8_t sub_cmd;
  uint32_t addr;
  uint16_t input_buffer_length;
  uint8_t flash_sector_erase_enable;
  uint8_t input_data[];
} __packed;

int siwx91x_nwp_flash_erase(const struct device *dev, uint32_t dest, size_t len)
{
	struct si91x_request_ta2m4 params = {
		.sub_cmd = SL_SI91X_WRITE_TO_COMMON_FLASH,
		.flash_sector_erase_enable = 1,
		.input_buffer_length = len,
		.addr = dest,
	};
	struct net_buf *reply;
	int status;

	__ASSERT(len <= FLASH_SECTOR_SIZE, "Corrupted argument");
	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_COMMON_REQ_TA_M4_COMMANDS,
				     SLI_WLAN_MGMT_Q, 0);
	status = ((struct siwx91x_frame_desc *)reply->data)->reserved[8];
	__ASSERT(!status, "Not supported");
	net_buf_unref(reply);
	return 0;
}

int siwx91x_nwp_flash_write(const struct device *dev, uint32_t dest, const void *data_buf, size_t data_len)
{
	struct si91x_request_ta2m4 params = {
		.sub_cmd = SL_SI91X_WRITE_TO_COMMON_FLASH,
		.flash_sector_erase_enable = 0,
		.input_buffer_length = data_len,
		.addr = dest,
	};
	struct net_buf *reply;
	int status;

	__ASSERT(data_len <= MAX_CHUNK_SIZE, "Corrupted argument");
	reply = siwx91x_nwp_send_buf2(dev, &params, sizeof(params), data_buf, data_len,
				      SLI_COMMON_REQ_TA_M4_COMMANDS, SLI_WLAN_MGMT_Q, 0);
	status = ((struct siwx91x_frame_desc *)reply->data)->reserved[8];
	__ASSERT(!status, "Not supported");
	net_buf_unref(reply);
	return 0;
}

int siwx91x_nwp_fw_upgrade_start(const struct device *dev, const void *hdr)
{
	/* FIXME: The structure below use > 1024 byte on the stack. */
	sli_si91x_req_fwup_t params = {
		.type = SL_FWUP_RPS_HEADER,
		.length = SLI_RPS_HEADER_SIZE,
	};
	struct net_buf *reply;
	int status;

	memcpy(params.content, hdr, params.length);
	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WLAN_REQ_FWUP,
				     SLI_WLAN_MGMT_Q, 0);
	/* FIXME: Simplify this expression */
	status = *(uint16_t *)(&((struct siwx91x_frame_desc *)reply->data)->reserved[8]);
	net_buf_unref(reply);
	if (!status)
		return 0;
	if (status == SL_STATUS_SI91X_FW_UPDATE_FAILED)
		return -EIO;
	return -EINVAL;
}

int siwx91x_nwp_fw_upgrade_write(const struct device *dev, const void *buf, size_t len)
{
	/* FIXME: The structure below use > 1024 byte on the stack. */
	sli_si91x_req_fwup_t params = {
		.type = SL_FWUP_RPS_CONTENT,
		.length = len,
	};
	struct net_buf *reply;
	int status;

	if (len > sizeof(params.content)) {
		return -EINVAL;
	}
	memcpy(params.content, buf, len);
	reply = siwx91x_nwp_send_buf(dev, &params, sizeof(params), SLI_WLAN_REQ_FWUP,
				     SLI_WLAN_MGMT_Q, 0);
	/* FIXME: Simplify this expression */
	status = *(uint16_t *)(&((struct siwx91x_frame_desc *)reply->data)->reserved[8]);
	net_buf_unref(reply);
	if (!status)
		return 0;
	if (status == SL_STATUS_SI91X_FW_UPDATE_DONE)
		return 1;
	if (status == SL_STATUS_SI91X_FW_UPDATE_FAILED)
		return -EIO;
	return -EINVAL;
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

void siwx91x_nwp_soft_reset(const struct device *dev)
{
	siwx91x_nwp_send_buf(dev, NULL, 0, SLI_COMMON_REQ_SOFT_RESET,
			     SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY | SIWX91X_FRAME_FLAG_NO_LOCK);
}

void siwx91x_nwp_force_assert(const struct device *dev)
{
	siwx91x_nwp_send_buf(dev, NULL, 0, SLI_COMMON_REQ_ASSERT,
			     SLI_WLAN_MGMT_Q,
			     SIWX91X_FRAME_FLAG_NO_REPLY);
}
