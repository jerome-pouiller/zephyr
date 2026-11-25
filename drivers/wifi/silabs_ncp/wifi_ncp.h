/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_SILABS_NCP_WIFI_NCP_H_
#define ZEPHYR_DRIVERS_WIFI_SILABS_NCP_WIFI_NCP_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <drivers/driver_zephyr.h>

#include "sl_cpc.h"
#include "sli_nwp.h"

/* CPC endpoints of the Wi-Fi station. Provisional: the identifiers are not
 * assigned yet.
 */
#define WIFI_NCP_DATA_EP_ID 12
#define WIFI_NCP_MGMT_EP_ID 11

/* Every frame exchanged with the network processor, on both endpoints,
 * starts with this descriptor. On the data endpoint the only command is
 * "data", 0. The positions follow the SiWx91x host driver and the
 * descriptions of sli_nwp.h.
 */
struct wifi_ncp_nwp_hdr {
	/* Bits 0-11: bytes following the descriptor, bits 12-15: queue */
	uint16_t len_queue;
	uint8_t command_id;
	uint8_t flags;
	/* Bytes between the descriptor and the payload */
	uint8_t ext_desc_size;
	uint8_t reserved1[2];
	/* DW1.B3: SLI_WIFI_STA_MODE or SLI_WIFI_AP_MODE */
	uint8_t iface;
	/* DW2.B0, received frames */
	uint8_t channel;
	uint8_t reserved2[3];
	/* DW3.B0-B1: status of a confirmation, token of a TX status */
	uint16_t status;
	uint8_t reserved3;
	/* DW3.B3: subtype of a TX status */
	uint8_t sta_id;
} __packed;

BUILD_ASSERT(sizeof(struct wifi_ncp_nwp_hdr) == 16);

#define WIFI_NCP_NWP_CMD_DATA 0

/* Starts the network processor, which then reports it is ready. No payload,
 * no response. Provisional, until sli_nwp.h declares it.
 */
#ifndef SLI_WIFI_CMD_NWP_INIT
#define SLI_WIFI_CMD_NWP_INIT 0x0D
#endif

/* Flag of a sent frame: report its transmission with a TX status */
#define WIFI_NCP_NWP_FLAG_CONFIRM BIT(2)

/* Queues of the network processor */
#define WIFI_NCP_NWP_QUEUE_MGMT 4
#define WIFI_NCP_NWP_QUEUE_DATA 5

static inline void wifi_ncp_nwp_set_len(struct wifi_ncp_nwp_hdr *hdr, size_t len, uint8_t queue)
{
	__ASSERT_NO_MSG(len <= BIT_MASK(12));
	hdr->len_queue = sys_cpu_to_le16(len | (queue << 12));
}

static inline uint16_t wifi_ncp_nwp_status(const struct wifi_ncp_nwp_hdr *hdr)
{
	return sys_le16_to_cpu(hdr->status);
}

/* Largest Ethernet frame with its descriptor, as a CPC payload */
#define WIFI_NCP_DATA_MTU                                                                          \
	ROUND_UP(sizeof(struct wifi_ncp_nwp_hdr) + NET_ETH_MAX_FRAME_SIZE, SL_CPC_BUF_MIN_ALIGNMENT)
/* Largest frame exchanged with the network processor */
#define WIFI_NCP_MGMT_MTU 2048

/* Receive buffer handed to CPC */
struct wifi_ncp_data_rx_slot {
	sl_cpc_buf_t buf;
	uint8_t data[WIFI_NCP_DATA_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

/* Received management frames are queued until the work queue handles them */
struct wifi_ncp_mgmt_rx_slot {
	void *fifo_reserved;
	sl_cpc_buf_t buf;
	uint8_t data[WIFI_NCP_MGMT_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

/* Transmit context, alive until the CPC send-done event */
struct wifi_ncp_data_tx_slot {
	sl_cpc_buf_t buf;
	sl_cpc_frame_t frame;
	uint8_t data[WIFI_NCP_DATA_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

struct wifi_ncp_mgmt_tx_slot {
	sl_cpc_buf_t buf;
	sl_cpc_frame_t frame;
	uint8_t data[WIFI_NCP_MGMT_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

/* Command waiting for its confirmation. One at a time: the lock serializes
 * the callers, the confirmation is matched on the work queue while the caller
 * waits.
 */
struct wifi_ncp_cmd {
	struct k_mutex lock;
	struct k_sem done;
	bool pending;
	uint8_t id;
	uint16_t status;
	/* Payload of the confirmation: capacity on entry, length on return */
	uint8_t *resp;
	size_t resp_size;
};

/* Beacon or probe response kept from the last scan */
struct wifi_ncp_scan_entry {
	bool used;
	uint8_t bssid[NET_ETH_ADDR_LEN];
	uint8_t channel;
	uint16_t beacon_int;
	uint16_t caps;
	uint16_t ie_len;
	uint8_t ie[CONFIG_WIFI_SILABS_NCP_SCAN_IE_MAX];
};

/* Largest RSN element the supplicant may ask to associate with */
#define WIFI_NCP_WPA_IE_MAX 64

enum wifi_ncp_sta_state {
	WIFI_NCP_STA_IDLE,
	/* Join sent, waiting for its deferred confirmation */
	WIFI_NCP_STA_JOINING,
	WIFI_NCP_STA_ASSOCIATED,
};

/* Station state driven by the supplicant */
struct wifi_ncp_sta {
	struct k_mutex lock;
	bool scanning;
	/* Set when the scan was requested through the Wi-Fi management API
	 * rather than by the supplicant
	 */
	scan_result_cb_t scan_cb;
	struct wifi_ncp_scan_entry scan[CONFIG_WIFI_SILABS_NCP_SCAN_RESULTS];
	/* Results not kept during the current scan, the array being full */
	unsigned int scan_dropped;
	/* Regulatory region reported by the co-processor, sl_wifi_region_code_t */
	uint8_t region;
	enum wifi_ncp_sta_state state;
	/* The BSS being joined or joined */
	uint8_t bssid[NET_ETH_ADDR_LEN];
	uint8_t ssid[WIFI_SSID_MAX_LEN];
	size_t ssid_len;
	uint8_t channel;
	uint16_t beacon_int;
	unsigned int key_mgmt;
	uint8_t wpa_ie[WIFI_NCP_WPA_IE_MAX];
	size_t wpa_ie_len;
	/* Scan result handed to the supplicant or join information sent to
	 * the co-processor, built from one entry
	 */
	uint8_t scratch[MAX(sizeof(struct wpa_scan_res) + CONFIG_WIFI_SILABS_NCP_SCAN_IE_MAX,
			    sizeof(sli_wifi_sta_join_ap_info_t))] __aligned(4);
};

/* State shared with the Zephyr supplicant */
struct wifi_ncp_supp {
	struct zep_drv_if_ctx *if_ctx;
	struct zep_wpa_supp_dev_callbk_fns cb;
};

struct wifi_ncp_config {
	const struct device *parent;
	struct net_eth_mac_config mac;
	struct k_mem_slab *data_tx_slab;
	struct k_mem_slab *mgmt_tx_slab;
};

struct wifi_ncp_data {
	const struct device *dev;
	struct net_if *iface;
	/* Address reported by the co-processor, zero when unknown */
	uint8_t mac[NET_ETH_ADDR_LEN];
	/* Both endpoints are connected once the device is ready */
	sl_cpc_ep_t data_ep;
	sl_cpc_ep_t mgmt_ep;
	struct k_sem connected;
	/* Given by the ready indication of the network processor */
	struct k_sem card_ready;
	/* Management frames are handled out of the CPC context */
	struct k_work_q wq;

	K_KERNEL_STACK_MEMBER(wq_stack, CONFIG_WIFI_SILABS_NCP_WQ_STACK_SIZE);

	struct k_work mgmt_rx_work;
	struct k_fifo mgmt_rx_pending;
	struct wifi_ncp_cmd cmd;
	struct wifi_ncp_sta sta;
	struct wifi_ncp_supp supp;
	struct wifi_ncp_data_rx_slot data_rx[CONFIG_WIFI_SILABS_NCP_DATA_RX_COUNT];
	struct wifi_ncp_mgmt_rx_slot mgmt_rx[CONFIG_WIFI_SILABS_NCP_MGMT_RX_COUNT];
};

/* Queue a frame on the management endpoint: the descriptor, then the two
 * optional parts of the payload.
 */
int wifi_ncp_nwp_send(const struct device *dev, const struct wifi_ncp_nwp_hdr *hdr,
		      const void *head, size_t head_len, const void *body, size_t body_len);

#endif /* ZEPHYR_DRIVERS_WIFI_SILABS_NCP_WIFI_NCP_H_ */
