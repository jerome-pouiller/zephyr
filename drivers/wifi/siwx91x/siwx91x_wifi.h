/*
 * Copyright (c) 2024 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_WIFI_H
#define SIWX91X_WIFI_H

#include <zephyr/net/wifi_mgmt.h>
#include "siwx91x_nwp.h"
#include "wiseconnect/components/device/silabs/si91x/wireless/inc/sl_si91x_protocol_types.h"

struct siwx91x_wifi_config {
	const struct device *nwp_dev;
};

struct siwx91x_wifi_data {
	struct net_if *iface;
	struct siwx91x_nwp_wifi_cb nwp_ops;
	scan_result_cb_t zephyr_scan_cb;
	uint8_t operating_mode;
	int ap_idle_timeout;
	int ap_max_num_sta;
	bool ap_hide_ssid;

	bool ps_enabled;
	unsigned int ps_timeout_ms;
	unsigned short ps_listen_interval;
	enum wifi_ps_exit_strategy ps_exit_strategy;
	enum wifi_ps_wakeup_mode ps_wakeup_mode;

#ifdef CONFIG_WIFI_SILABS_SIWX91X_NET_STACK_OFFLOAD
	uint32_t sock_bitmap;
	struct k_event fds_recv_event;
	/* FIXME replace SLI_NUMBER_OF_SOCKETS by a configurable number */
	struct {
		net_context_recv_cb_t cb;
		void *user_data;
		struct net_context *context;
	} fds_cb[SLI_NUMBER_OF_SOCKETS];
#endif
};

#endif
