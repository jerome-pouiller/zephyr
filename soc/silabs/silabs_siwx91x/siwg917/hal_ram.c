/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <zephyr/net_buf.h>
#include <zephyr/net/net_pkt.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "siwx91x_nwp.h"
#include "hal.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(siwx91x_nwp);

/* sli_si91x_driver_send_command */
struct net_buf *siwx91x_nwp_send_frame(const struct device *dev,
				       struct net_buf *buf,
				       uint16_t command,
				       int queue_id,
				       k_timeout_t wait_duration,
				       uint8_t flags)
{
	struct siwx91x_frame_auxdata *auxdata = net_buf_user_data(buf);
	struct siwx91x_frame_desc *desc = (void *)buf->data;
	struct siwx91x_nwp_data *data = dev->data;
	size_t len = net_buf_frags_len(buf);
	k_spinlock_key_t key;
	int ret;

	__ASSERT(queue_id < SI91X_CMD_MAX,
		 "Invalid queue id");
	__ASSERT(buf->len >= sizeof(struct siwx91x_frame_desc),
		 "Invalid buffer");
	__ASSERT(buf->user_data_size == sizeof(struct siwx91x_frame_auxdata),
		 "Not supported");

	/* FIXME: Should we leave that operation to the caller? */
	memset(desc, 0, sizeof(struct siwx91x_frame_desc));
	desc->command = command;
	desc->length_and_queue = FIELD_PREP(0x0FFF, len - sizeof(struct siwx91x_frame_desc));
	if (flags & SIWX91X_FRAME_FLAG_WLAN_DATA) {
		__ASSERT(queue_id == SLI_WIFI_WLAN_CMD, "Invalid argument");
	desc->length_and_queue |= FIELD_PREP(0xF000, SLI_WLAN_DATA_Q);
	} else if (queue_id == SLI_SI91X_BT_CMD) {
		desc->length_and_queue |= FIELD_PREP(0xF000, SLI_BT_Q);
	} else {
		desc->length_and_queue |= FIELD_PREP(0xF000, SLI_WLAN_MGMT_Q);
	}

	__ASSERT(buf->user_data_size == sizeof(struct siwx91x_frame_auxdata), "Not yet supported");
	/* FIXME: should we leave the caller take care of that? */
	memset(auxdata, 0, sizeof(struct siwx91x_frame_auxdata));
	k_sem_init(&auxdata->done, 0, 1);
	auxdata->flags = flags;

	k_fifo_put(&data->cmd_queues[queue_id].tx_queue, net_buf_ref(buf));
	if (auxdata->flags & SIWX91X_FRAME_FLAG_ASYNC)
		return NULL;
	ret = k_sem_take(&auxdata->done, wait_duration);
	if (!ret) {
		return auxdata->reply;
	}
	LOG_ERR("NWP command timeout");
	key = k_spin_lock(&data->cmd_queues[queue_id].tx_in_progress_lock);
	data->cmd_queues[queue_id].tx_in_progress = NULL;
	k_spin_unlock(&data->cmd_queues[queue_id].tx_in_progress_lock, key);
	return NULL;
}

/* Exemple */
static void siwx91x_nwp_add_to_bt_queue(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->bt && data->bt->on_rx)
		data->bt->on_rx(data->bt, buf);
}

/* Exemple */
static void siwx91x_nwp_add_to_wifi_queue(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_rx)
		data->wifi->on_rx(data->wifi, buf);
}

static void siwx91x_nwp_process_data_frame(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_add_to_socket_queue(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_add_to_net_queue(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_add_to_log_queue(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_rejoin(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_join(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_bg_scan(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_config(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_no_ram_retention(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_wlan_notification(const struct device *dev, struct net_buf *buf)
{
	LOG_INF("Unsupported");
}

static void siwx91x_nwp_client_disconnected(const struct device *dev, struct net_buf *buf)
{
	siwx91x_nwp_rejoin(dev, buf);
}

static void siwx91x_nwp_handle_card_ready(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	k_sem_give(&data->firmware_ready);
}

static const struct {
	uint8_t  fw_queue;
	uint8_t  cmd_queue;
	uint16_t cmd_id;
	void (*cb)(const struct device *, struct net_buf *);
} siwx91x_nwp_rsp_list[] = {
	{ SLI_BT_Q,        SLI_SI91X_BT_CMD,      -1 /* wildcard */,                         .cb = siwx91x_nwp_add_to_bt_queue     },
	{ SLI_LOG_Q,       SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_NWP_LOGGING,                .cb = siwx91x_nwp_add_to_log_queue    },
	{ SLI_WLAN_DATA_Q, SLI_WIFI_WLAN_CMD,     SLI_NET_DUAL_STACK_RX_RAW_DATA_FRAME,      .cb = siwx91x_nwp_process_data_frame  },
	{ SLI_WLAN_DATA_Q, SLI_WIFI_WLAN_CMD,     SLI_RECEIVE_RAW_DATA,                      .cb = siwx91x_nwp_process_data_frame  },
	{ SLI_WLAN_DATA_Q, SLI_WIFI_WLAN_CMD,     SLI_SI91X_WIFI_RX_DOT11_DATA,              .cb = siwx91x_nwp_add_to_wifi_queue   },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_DISCOVER_SERVICE,             .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_DNS_QUERY,                    .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_DNS_SERVER_ADD,               .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_EMB_MQTT_CLIENT,              .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_EMB_MQTT_PUBLISH_PKT,         .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_HTTP_ABORT,                   .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_HTTP_CLIENT_GET,              .cb = siwx91x_nwp_add_to_net_queue    },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_HTTP_CLIENT_POST,             .cb = siwx91x_nwp_add_to_net_queue    },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_HTTP_CLIENT_POST_DATA,        .cb = siwx91x_nwp_add_to_net_queue    },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_HTTP_CLIENT_PUT,              .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_IPCONFV4,                     .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_IPCONFV6,                     .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_IPV4_CHANGE,                  .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_MDNSD,                        .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_MQTT_REMOTE_TERMINATE,        .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_MULTICAST,                    .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_NAT,                          .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_OTA_FWUP,                     .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_PING_PACKET,                  .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_SET_SNI_EMBEDDED,             .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_NETWORK_CMD, SLI_WLAN_RSP_SNTP_CLIENT,                  .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_CONN_ESTABLISH,               .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_REMOTE_TERMINATE,             .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SELECT_REQUEST,               .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SOCKET_ACCEPT,                .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SOCKET_CLOSE,                 .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SOCKET_CONFIG,                .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SOCKET_CREATE,                .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_SOCKET_READ_DATA,             .cb = siwx91x_nwp_add_to_socket_queue },
	{ SLI_WLAN_MGMT_Q, SLI_SI91X_SOCKET_CMD,  SLI_WLAN_RSP_TCP_ACK_INDICATION,           .cb = NULL /* TODO */ },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_DEBUG_LOG,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_ENCRYPT_CRYPTO,             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_FEATURE_FRAME,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_GET_CONFIG,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_GET_EFUSE_DATA,             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_GET_RAM_DUMP,               },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_GET_RTC_TIMER,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_NWP_LOGGING,                },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_PWRMODE,                    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_SET_CONFIG,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_SET_RTC_TIMER,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_SOFT_RESET,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_TA_M4_COMMANDS,             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_COMMON_RSP_ULP_NO_RAM_RETENTION,       .cb = siwx91x_nwp_no_ram_retention    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_SI91X_FW_FALLBACK_RSP_FROM_HOST,       },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_WIFI_RSP_ANTENNA_SELECT,               },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_WIFI_RSP_CARDREADY,                    .cb = siwx91x_nwp_handle_card_ready    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_COMMON_CMD,   SLI_WIFI_RSP_OPERMODE,                     },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_11AX_PARAMS,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_AP_CONFIGURATION,             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_AP_STOP,                      .cb = siwx91x_nwp_rejoin              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_BAND,                         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_BEACON_STOP,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_BG_SCAN,                      .cb = siwx91x_nwp_bg_scan             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_CONFIG,                       .cb = siwx91x_nwp_config              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_DISCONNECT,                   .cb = siwx91x_nwp_rejoin              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_EAP_CONFIG,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_EXT_STATS,                    .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_FILTER_BCAST_PACKETS,         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_GAIN_TABLE,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_GET_STATS,                    .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_HOST_PSK,                     },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_HT_CAPABILITIES,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_INIT,                         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_JOIN,                         .cb = siwx91x_nwp_join                },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_MAC_ADDRESS,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_QUERY_GO_PARAMS,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_QUERY_NETWORK_PARAMS,         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_REJOIN_PARAMS,                },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_RESCHEDULE_TWT,               },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_ROAM_PARAMS,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_RSSI,                         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_RX_STATS,                     .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_SCAN,                         .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_SCAN_RESULTS,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_SET_MAC_ADDRESS,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_SET_MULTICAST_FILTER,         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_SET_TRANSCEIVER_MCAST_FILTER, },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TRANSCEIVER_CONFIG_PARAMS,    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TRANSCEIVER_FLUSH_DATA_Q,     },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TRANSCEIVER_PEER_LIST_UPDATE, },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TRANSCEIVER_SET_CHANNEL,      },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TSF,                          },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TWT_AUTO_CONFIG,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_TWT_PARAMS,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_VENDOR_IE,                    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WIFI_RSP_WPS_METHOD,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_CALIB_READ,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_CALIB_WRITE,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_CLIENT_CONNECTED,             .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_CLIENT_DISCONNECTED,          .cb = siwx91x_nwp_client_disconnected },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_DYNAMIC_POOL,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_EFUSE_READ,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_EVM_OFFSET,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_EVM_WRITE,                    },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_FREQ_OFFSET,                  },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_FULL_FW_VERSION,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_FWUP,                         },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_FW_VERSION,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_GET_DPD_DATA,                 },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_HTTP_OTAF,                    .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_MODULE_STATE,                 .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_RADIO,                        },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_SET_CERTIFICATE,              },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_SET_REGION,                   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_SET_REGION_AP,                },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_TIMEOUT,                      },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_TRANSCEIVER_TX_DATA_STATUS,   .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_TWT_ASYNC,                    .cb = siwx91x_nwp_wlan_notification   },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_WLAN_CMD,     SLI_WLAN_RSP_TX_TEST_MODE,                 },
};

static int siwx91x_nwp_feed_rx_buffer(const struct device *dev, struct net_buf **rx_buf)
{
	const struct siwx91x_nwp_config *config = dev->config;
	struct siwx91x_nwp_data *data = dev->data;
	struct siwx91x_frame_desc *frame;

	*rx_buf = data->rx_buf_in_progress;
	if (config->rx_pool) {
		data->rx_buf_in_progress = net_buf_alloc(config->rx_pool, K_NO_WAIT);
	} else {
		data->rx_buf_in_progress = net_pkt_get_reserve_rx_data(SIWX91X_MAX_PAYLOAD_SIZE, K_NO_WAIT);
	}

	__ASSERT(!*rx_buf || (data->rx_desc[0].addr == (*rx_buf)->data + SIWX91X_NWP_MEMORY_OFFSET_ADDRESS),
		 "Corrupted state");

	if (*rx_buf) {
		frame = (struct siwx91x_frame_desc *)(*rx_buf)->data;
		__ASSERT(FIELD_GET(0x0FFF, frame->length_and_queue) < SIWX91X_MAX_PAYLOAD_SIZE,
			 "Corrupted frame");
		net_buf_add(*rx_buf,
			    FIELD_GET(0x0FFF, frame->length_and_queue) + sizeof(struct siwx91x_frame_desc));
	}

	if (!data->rx_buf_in_progress) {
		return -ENOMEM;
	}

	data->rx_desc[0].addr = SIWX91X_NWP_MEMORY_OFFSET_ADDRESS + data->rx_buf_in_progress->data;
	data->rx_desc[0].length = sizeof(struct siwx91x_frame_desc);
	data->rx_desc[1].addr = data->rx_desc[0].addr + data->rx_desc[0].length;
	data->rx_desc[1].length = data->rx_buf_in_progress->len - data->rx_desc[0].length;
	config->m4_regs->m4_int_set = SIWX91X_RX_BUFFER_VALID;

	return 0;
}

/* sli_si91x_wifi_handle_rx_events */
static void siwx91x_nwp_handle_rx(const struct device *dev, struct net_buf *rx_buf)
{
	struct siwx91x_nwp_data *data = dev->data;
	struct siwx91x_frame_desc *frame = (struct siwx91x_frame_desc *)rx_buf->data;
	struct siwx91x_frame_auxdata *auxdata;
	struct net_buf *req_buf;
	k_spinlock_key_t key;
	int i;

	__ASSERT(rx_buf, "Corrupted context");
	LOG_HEXDUMP_INF(rx_buf->data, rx_buf->len, "nwp rx:");

	for (i = 0; i < ARRAY_SIZE(siwx91x_nwp_rsp_list); i++) {
		if (siwx91x_nwp_rsp_list[i].fw_queue == FIELD_GET(0xF000, frame->length_and_queue)) {
			if (siwx91x_nwp_rsp_list[i].cmd_id == frame->command) {
				break;
			}
			if (siwx91x_nwp_rsp_list[i].cmd_id == (uint16_t)-1) {
				break;
			}
		}
	}
	if (i == ARRAY_SIZE(siwx91x_nwp_rsp_list)) {
		LOG_INF("drop unhandled frame");
		net_buf_unref(rx_buf);
		return;
	}

	key = k_spin_lock(&data->cmd_queues[siwx91x_nwp_rsp_list[i].cmd_queue].tx_in_progress_lock);
	req_buf = data->cmd_queues[siwx91x_nwp_rsp_list[i].cmd_queue].tx_in_progress;
	if (((struct siwx91x_frame_desc *)req_buf->data)->command == frame->command) {
		data->cmd_queues[siwx91x_nwp_rsp_list[i].cmd_queue].tx_in_progress = NULL;
	} else {
		req_buf = NULL;
	}
	k_spin_unlock(&data->cmd_queues[siwx91x_nwp_rsp_list[i].cmd_queue].tx_in_progress_lock, key);
	if (!req_buf && !siwx91x_nwp_rsp_list[i].cb) {
		LOG_INF("Received unexpected frame");
	}
	if (req_buf) {
		auxdata = net_buf_user_data(req_buf);
		if (auxdata->flags & SIWX91X_FRAME_FLAG_GLOBAL_LOCK) {
			data->global_lock = false;
		}
		if (auxdata->flags & SIWX91X_FRAME_FLAG_NO_REPLY) {
			auxdata->reply = NULL;
		} else {
			auxdata->reply = net_buf_ref(rx_buf);
		}
		k_sem_give(&auxdata->done);
	}
	if (siwx91x_nwp_rsp_list[i].cb) {
		siwx91x_nwp_rsp_list[i].cb(dev, rx_buf);
	}
	net_buf_unref(rx_buf);
}

/* sli_si91x_wifi_handle_tx_event */
static void siwx91x_nwp_handle_tx(const struct device *dev, struct siwx91x_nwp_cmd_queues *queue)
{
	const struct siwx91x_nwp_config *config = dev->config;
	struct siwx91x_nwp_data *data = dev->data;
	struct siwx91x_frame_auxdata *auxdata;
	struct net_buf *buf;
	int len;

	buf = k_fifo_get(&queue->tx_queue, K_NO_WAIT);
	__ASSERT(buf, "Get Tx event while tx_qeue is empty");

	if (!buf->frags) {
		/* Hoora, we are using zero-copy path */
		data->tx_buf_in_progress = net_buf_ref(buf);
		len = net_buf_frags_len(data->tx_buf_in_progress);
	} else {
		/* FIXME: This copy is on the critical path. We may preload the next frame to
		 * improve the performances. That's said, this is rather complex and the user
		 * provide non-fragmented frames if he care about performances.
		 */
		data->tx_buf_in_progress = net_buf_alloc(config->tx_pool, K_NO_WAIT);
		__ASSERT(data->tx_buf_in_progress, "Corrupted state");
		len = net_buf_linearize(data->tx_buf_in_progress->data,
					sizeof(data->tx_buf_in_progress->size), buf, 0, SIZE_MAX);
		net_buf_add(data->tx_buf_in_progress, len);
	}

	k_spinlock_key_t key = k_spin_lock(&queue->tx_in_progress_lock);
	__ASSERT(!queue->tx_in_progress, "Get Tx event while tx_qeue is empty");
	queue->tx_in_progress = buf;
	k_spin_unlock(&queue->tx_in_progress_lock, key);

	__ASSERT(len >= sizeof(struct siwx91x_frame_desc), "Corrupted buffer");
	data->tx_desc[0].addr = SIWX91X_NWP_MEMORY_OFFSET_ADDRESS + data->tx_buf_in_progress->data;
	data->tx_desc[0].length = sizeof(struct siwx91x_frame_desc);
	data->tx_desc[1].addr = data->tx_desc[0].addr + sizeof(struct siwx91x_frame_desc);
	data->tx_desc[1].length = len - sizeof(struct siwx91x_frame_desc);
	auxdata = net_buf_user_data(buf);
	if (auxdata->flags & SIWX91X_FRAME_FLAG_GLOBAL_LOCK) {
		data->global_lock = true;
	}
	compiler_barrier(); /* Useful? */
	LOG_HEXDUMP_INF(data->tx_buf_in_progress->data, len, "nwp tx:");
	config->m4_regs->m4_int_set = SIWX91X_TX_PKT_PENDING;
	net_buf_unref(buf);
}

/* sli_wifi_command_engine */
void siwx91x_nwp_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct net_buf_pool *rx_buf_pool = arg2;
	struct siwx91x_nwp_data *data = dev->data;
	bool lock_tx_queues = false;
	struct net_buf *rx_buffer;
	int i, ret;
	struct k_poll_event events[] = {
		[0] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->cmd_queues[0].tx_queue, 0),
		[1] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->cmd_queues[1].tx_queue, 0),
		[2] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->cmd_queues[2].tx_queue, 0),
		[3] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->cmd_queues[3].tx_queue, 0),
		[4] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->cmd_queues[4].tx_queue, 0),
		[5] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->tx_data_complete, 0),
		[6] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->rx_data_complete, 0),
		[7] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->refresh_queues_state, 0),
		[8] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      /* lifo and fifo are binary compatible */
						      (struct k_fifo *)&rx_buf_pool->free, 0),
	};

	k_thread_name_set(NULL, "nwp");

	/* Force first rx_buffer */
	ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
	__ASSERT(rx_buffer == NULL, "Corrupted state");
	__ASSERT(!ret, "Not supported");
	LOG_INF("event: Rx buffer available (rx queue started)");
	events[8].type = K_POLL_TYPE_IGNORE;

	for (;;) {
		ret = k_poll(events, ARRAY_SIZE(events), K_FOREVER);
		/* Tx path */
		for (i = 0; i < 5; i++) {
			if (events[i].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
				/* FIXME: the two subsystem (Bluetooth and Wifi) have independent
				 * queues on the NWP. Currently if we enqueue a message on a busy
				 * subsystem, the DMA will be blocked until the subsystem consume
				 * the frame, so the other subsystem won't be fed while it could
				 * receive data. Instead, we could check the TA status register. If
				 * the targeted subsystem for the packet is full, mark the subsystem
				 * unavailable until we receive the SIWX91X_TA_BUFFER_FULL_CLEAR
				 * interrupt.
				 */
				events[i].state = K_POLL_STATE_NOT_READY;
				siwx91x_nwp_handle_tx(dev, &data->cmd_queues[i]);
				lock_tx_queues = true;
				LOG_INF("event: Tx data send (queue %d)", i);
				break;
			}
		}
		if (events[5].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[5].state = K_POLL_STATE_NOT_READY;
	                k_sem_take(&data->tx_data_complete, K_NO_WAIT);
			net_buf_unref(data->tx_buf_in_progress);
			data->tx_buf_in_progress = NULL;
			lock_tx_queues = false;
			LOG_INF("event: Tx data complete");
		}
		if (events[7].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[7].state = K_POLL_STATE_NOT_READY;
			k_sem_take(&data->refresh_queues_state, K_NO_WAIT);
			LOG_INF("event: Refresh queue state");
		}

		/* Rx path */
		if (events[6].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[6].state = K_POLL_STATE_NOT_READY;
	                k_sem_take(&data->rx_data_complete, K_NO_WAIT);
			ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
			if (ret) {
				LOG_INF("event: Rx data complete (rx queue stopped)");
				events[8].type = K_POLL_TYPE_FIFO_DATA_AVAILABLE;
			} else {
				LOG_INF("event: Rx data complete");
			}
			siwx91x_nwp_handle_rx(dev, rx_buffer);
		}
		if (events[8].state == K_POLL_TYPE_FIFO_DATA_AVAILABLE) {
			events[8].state = K_POLL_STATE_NOT_READY;
			ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
			__ASSERT(rx_buffer == NULL, "Corrupted state");
			if (ret) {
				LOG_INF("event: Rx buffer available (ignored)");
			} else {
				LOG_INF("event: Rx buffer available (rx queue restarted)");
				events[8].type = K_POLL_TYPE_IGNORE;
			}
		}
		for (i = 0; i < 5; i++) {
			/* FIXME: data->global_lock is not supported
			 * FIXME: Tx timeout is not supported
			 * FIXME: support SLI_WIFI_BUFFER_FULL and SLI_BT_BUFFER_FULL
			 * FIXME: check the BT_FULL and WIFI_FULL bit in ta_status register
			 * Use k_sem_give(&data->refresh_queues_state) for all these events
			 */
			if (lock_tx_queues || data->cmd_queues[i].tx_in_progress) {
				events[i].type = K_POLL_TYPE_IGNORE;
			} else {
				events[i].type = K_POLL_TYPE_FIFO_DATA_AVAILABLE;
			}
		}
	}
}

/* Ensure the M4 won't access to the flash until the NWP ends its processing. Keep this function in
 * RAM
 */
__ramfunc static void siwx91x_nwp_busy_poll(const struct device *dev, int reg_bit)
{
	const struct siwx91x_nwp_config *cfg = dev->config;

	__disable_irq();
	cfg->m4_regs->m4_int_set = reg_bit;
	while (cfg->m4_regs->m4_int_clr & reg_bit) {
		/* empty */;
	}
	__enable_irq();
}

void siwx91x_nwp_isr(const struct device *dev)
{
	const struct siwx91x_nwp_config *cfg = dev->config;
	struct siwx91x_nwp_data *data = dev->data;
	uint32_t interrupts = cfg->m4_regs->ta_int;

	LOG_INF("interrupt: 0x%04x", interrupts);
	/* ta_int is buggy so we have to write ta_int_clr */
	if (interrupts & SIWX91X_TX_PKT_DONE) {
		k_sem_give(&data->tx_data_complete);
		cfg->ta_regs->ta_int_clr = SIWX91X_TX_PKT_DONE;
	}
	if (interrupts & SIWX91X_RX_PKT_DONE) {
		k_sem_give(&data->rx_data_complete);
		cfg->ta_regs->ta_int_clr = SIWX91X_RX_PKT_DONE;
	}
	if (interrupts & SIWX91X_TA_BUFFER_FULL_CLEAR) {
		LOG_INF("Not supported");
		k_sem_give(&data->refresh_queues_state);
		cfg->ta_regs->ta_int_clr = SIWX91X_TA_BUFFER_FULL_CLEAR;
	}
	if (interrupts & SIWX91X_SIDE_BAND_CRYPTO_DONE) {
		__ASSERT(0, "Not supported");
		cfg->ta_regs->ta_int_clr = SIWX91X_SIDE_BAND_CRYPTO_DONE;
	}
	if (interrupts & SIWX91X_TA_WRITING_ON_FLASH) {
		siwx91x_nwp_busy_poll(dev, SIWX91X_M4_WAITING_FOR_TA_FLASH);
		cfg->ta_regs->ta_int_clr = SIWX91X_TA_WRITING_ON_FLASH;
	}
	if (interrupts & SIWX91X_NWP_DEINIT) {
		siwx91x_nwp_busy_poll(dev, SIWX91X_M4_WAITING_FOR_TA_DEINIT);
		cfg->ta_regs->ta_int_clr = SIWX91X_NWP_DEINIT;
	}
	if (interrupts & SIWX91X_M4_IMAGE_UPGRADATION) {
		siwx91x_nwp_busy_poll(dev, SIWX91X_UPGRADE_M4_IMAGE);
		cfg->ta_regs->ta_int_clr = SIWX91X_M4_IMAGE_UPGRADATION;
	}
	if (interrupts & SIWX91X_TA_USING_FLASH) {
		/* NWP alwasy report it use the flash, don't bother with this status */
	}
}
