/*
 * Copyright (c) 2023 Antmicro
 * Copyright (c) 2024-2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT silabs_siwx91x_wifi

#include <zephyr/net/wifi_mgmt.h>
#include "siwx91x_nwp_bus.h"
#include "siwx91x_nwp_api.h"
#include "siwx91x_wifi.h"
#include "siwx91x_wifi_ps.h"
#include "siwx91x_wifi_ap.h"
#include "siwx91x_wifi_sta.h"
#include "siwx91x_wifi_scan.h"

LOG_MODULE_REGISTER(siwx91x_wifi, CONFIG_SIWX91X_WIFI_LOG_LEVEL);

BUILD_ASSERT(SLI_WIFI_SSID_LEN >= WIFI_SSID_MAX_LEN,
	     "SSID lengths mismatch");
BUILD_ASSERT(SLI_WIFI_PSK_LEN == WIFI_PSK_MAX_LEN,
	     "PSK lengths mismatch");
BUILD_ASSERT(SLI_WIFI_HARDWARE_ADDRESS_LENGTH == WIFI_MAC_ADDR_LEN,
	     "Hardware address lengths mismatch");

static int siwx91x_wifi_send(const struct device *dev, struct net_pkt *pkt)
{
	const struct siwx91x_wifi_config *config = dev->config;

	__ASSERT(net_buf_headroom(pkt->buffer) >= 16, "No supported");
	net_buf_push(pkt->buffer, 16);
	siwx91x_nwp_send_frame(config->nwp_dev, pkt->buffer,
			       SLI_SEND_RAW_DATA, SLI_WLAN_DATA_Q,
			       SIWX91X_FRAME_FLAG_ASYNC);
	net_pkt_unref(pkt);
	return 0;
}

static void siwx91x_wifi_on_rx(const struct siwx91x_nwp_wifi_cb *ctxt, struct net_buf *buf)
{
	struct net_if *iface = net_if_get_first_wifi();
	const struct net_linkaddr *ll = net_if_get_link_addr(iface);
	struct net_pkt *pkt;
	int ret;

	__ASSERT(buf->frags == NULL, "Corrupted data from NWP driver");
	net_buf_pull(buf, sizeof(struct siwx91x_frame_desc));

	/* Multicast Tx frames are echoed back by the AP. This breaks IPv6 DAD algorithm. */
	/* FIXME: use sl_wifi_configure_multicast_filter() to filter this */
	if (!memcmp(((const struct net_eth_hdr *)buf->data)->src.addr,
		    ll->addr, WIFI_MAC_ADDR_LEN)) {
		return;
	}
	if (CONFIG_NET_BUF_DATA_SIZE >= SIWX91X_MAX_PAYLOAD_SIZE) {
		/* In this case, NWP driver allcate data on network RX pool. Not need for
		 * duplication.
		 */
		pkt = net_pkt_rx_alloc_on_iface(iface, K_NO_WAIT);
		if (!pkt) {
			goto pkt_alloc_fail;
		}
		net_pkt_frag_add(pkt, net_buf_ref(buf));
	} else {
		pkt = net_pkt_rx_alloc_with_buffer(iface, buf->len, AF_UNSPEC, 0, K_NO_WAIT);
		if (!pkt) {
			goto pkt_alloc_fail;
		}
		ret = net_pkt_write(pkt, buf->data, buf->len);
		if (ret) {
			goto rx_fail;
		}
	}

	ret = net_recv_data(iface, pkt);
	if (ret) {
		goto rx_fail;
	}
	return;

rx_fail:
	net_pkt_unref(pkt);
pkt_alloc_fail:
	LOG_WRN("Dropped frame");
}

static int siwx91x_wifi_mode(const struct device *dev, struct wifi_mode_info *mode)
{
	const struct siwx91x_wifi_config *config = dev->config;
	struct siwx91x_wifi_data *data = dev->data;
	int ret;

	switch (mode->oper) {
	case WIFI_MGMT_GET:
		mode->mode = data->operating_mode;
		return 0;
	case WIFI_MGMT_SET:
		ret = siwx91x_nwp_reset(config->nwp_dev, mode->mode, false, 0);
		if (ret) {
			return ret;
		}
		siwx91x_nwp_set_band(config->nwp_dev, SL_WIFI_BAND_MODE_2_4GHZ);
		siwx91x_nwp_wifi_init(config->nwp_dev);
		/* FIXME: This command is rejected in AP mode */
		siwx91x_nwp_set_device_region(config->nwp_dev, SL_WIFI_REGION_EU);
		siwx91x_nwp_set_config(config->nwp_dev, SLI_WIFI_CONFIG_RTS_THRESHOLD, 2346);
		siwx91x_nwp_set_sta_config(config->nwp_dev);
		siwx91x_nwp_set_region(config->nwp_dev);
		/* FIXME: Set max Tx Power for scan and join */
		data->operating_mode = mode->mode;
		return 0;;
	default:
		__ASSERT(0, "Corrupted argument");
	}
}

static int siwx91x_wifi_get_config(const struct device *dev,
				   enum ethernet_config_type type,
				   struct ethernet_config *config)
{
	if (type == ETHERNET_CONFIG_TYPE_EXTRA_TX_PKT_HEADROOM) {
		config->extra_tx_pkt_headroom = sizeof(struct siwx91x_frame_desc);
	}
	/* FIXME we could also manage ETHERNET_CONFIG_TYPE_RX_CHECKSUM_SUPPORT and
	 * ETHERNET_CONFIG_TYPE_TX_CHECKSUM_SUPPORT
	 */
	return 0;
}

static void siwx91x_wifi_ethernet_init(struct net_if *iface)
{
	struct ethernet_context *eth_ctx;

	if (IS_ENABLED(CONFIG_WIFI_SILABS_SIWX91X_NET_STACK_NATIVE)) {
		eth_ctx = net_if_l2_data(iface);
		eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;
		ethernet_init(iface);
		net_if_dormant_on(iface);
	}
}

static void siwx91x_wifi_iface_init(struct net_if *iface)
{
	const struct siwx91x_wifi_config *config =  iface->if_dev->dev->config;
	struct siwx91x_wifi_data *data = iface->if_dev->dev->data;
	uint8_t mac_addr[NET_ETH_ADDR_LEN];

	data->iface = iface;
	siwx91x_nwp_set_band(config->nwp_dev, SL_WIFI_BAND_MODE_2_4GHZ);
	siwx91x_nwp_wifi_init(config->nwp_dev);
	siwx91x_nwp_set_device_region(config->nwp_dev, SL_WIFI_REGION_EU);
	siwx91x_nwp_set_config(config->nwp_dev, SLI_WIFI_CONFIG_RTS_THRESHOLD, 2346);
	siwx91x_nwp_set_sta_config(config->nwp_dev);
	siwx91x_nwp_get_mac_address(config->nwp_dev, mac_addr);
	siwx91x_nwp_set_region(config->nwp_dev);
	net_if_set_link_addr(iface, mac_addr, sizeof(mac_addr), NET_LINK_ETHERNET);
	siwx91x_wifi_ethernet_init(iface);
}

static int siwx91x_wifi_init(const struct device *dev)
{
	const struct siwx91x_wifi_config *config = dev->config;
	struct siwx91x_wifi_data *data =  dev->data;

	if (!device_is_ready(config->nwp_dev)) {
		return -ENODEV;
	}
	siwx91x_nwp_register_wifi(config->nwp_dev, &data->nwp_ops);

	return 0;
}

static const struct wifi_mgmt_ops siwx91x_wifi_mgmt = {
	.mode = siwx91x_wifi_mode,
	.scan = siwx91x_wifi_scan,
	.connect = siwx91x_wifi_connect,
	.disconnect = siwx91x_wifi_disconnect,
	.ap_enable = siwx91x_ap_enable,
	.ap_disable = siwx91x_ap_disable,
	.ap_sta_disconnect = siwx91x_ap_sta_disconnect,
	.ap_config_params = siwx91x_ap_config_params,
	.set_twt = siwx91x_wifi_set_twt,
	.set_power_save	= siwx91x_wifi_set_power_save,
	.get_power_save_config = siwx91x_wifi_get_power_save_config,
};

static const struct net_wifi_mgmt_offload siwx91x_wifi_api = {
	.wifi_iface.iface_api.init = siwx91x_wifi_iface_init,
	.wifi_iface.get_config = siwx91x_wifi_get_config,
	.wifi_iface.send = siwx91x_wifi_send,
	.wifi_mgmt_api = &siwx91x_wifi_mgmt,
};

static const struct siwx91x_wifi_config siwx91x_wifi_config = {
	.nwp_dev = DEVICE_DT_GET(DT_INST_PARENT(0)),
};

static struct siwx91x_wifi_data siwx91x_wifi_data = {
	.nwp_ops.on_scan_results = siwx91x_wifi_on_scan_results,
	.nwp_ops.on_rx = siwx91x_wifi_on_rx,
	.ap_idle_timeout = UINT8_MAX,
	.ap_max_num_sta = 4,
	.ps_exit_strategy = WIFI_PS_EXIT_EVERY_TIM,
	.ps_wakeup_mode = WIFI_PS_WAKEUP_MODE_DTIM,
};

ETH_NET_DEVICE_DT_INST_DEFINE(0, siwx91x_wifi_init, NULL, &siwx91x_wifi_data, &siwx91x_wifi_config,
			      CONFIG_WIFI_INIT_PRIORITY, &siwx91x_wifi_api, NET_ETH_MTU);
