/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rsi_rom_egpio.h"
#include "sl_si91x_constants.h"
#include "sl_wifi_device.h"

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
#include "hal_ram.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(siwx91x_nwp);

#define DMA_DESC_REG_VALID (0xA0 << 8)

int siwx91x_nwp_get_firmware_version(const struct device *dev, sl_wifi_firmware_version_t *version)
{
	SIX91X_NWP_NET_BUF_DEFINE(frame, 0);
	struct net_buf *reply_buf;
	int reply_len;

	reply_buf = siwx91x_nwp_send_frame(dev, frame, SLI_WLAN_REQ_FULL_FW_VERSION,
					   SLI_WIFI_WLAN_CMD, K_FOREVER, 0);
	if (!reply_buf) {
		return -EIO;
	}

	reply_len = net_buf_linearize(version, sizeof(sl_wifi_firmware_version_t),
				      reply_buf, sizeof(struct siwx91x_frame_desc), SIZE_MAX);
	net_buf_unref(reply_buf);

	LOG_INF("Get NWP firmware version: %X.%d.%d.%d.%d.%d.%d",
		version->rom_id,
		version->major,
		version->minor,
		version->security_version,
		version->patch_num,
		version->customer_id,
		version->build_num);

	// Because sl_wifi_firmware_version_t lacks of __packed attribute, we receive less byte than
	// expected. Let's ignore that for now.
	//if (reply_len < sizeof(sl_wifi_firmware_version_t)) {
	//	return -EIO;
	//}
	return 0;
}

static int siwx91x_nwp_wait_ready(const struct device *dev)
{
	const struct siwx91x_nwp_config *config = dev->config;
	uint16_t val = (uint16_t)config->ta_regs->bootloader_out;
	/* uint16_t val = (uint16_t)SI91X_INTERFACE_OUT_REGISTER; */

	if (!val) {
		return -EBUSY;
	}
	if (FIELD_GET(0xFF00, val) != SLI_WIFI_REGISTER_VALID) {
		return -EBUSY;
	}
	if (FIELD_GET(0x00FF, val) == SLI_BOOTUP_OPTIONS_LAST_CONFIG_NOT_SAVED) {
		return -EIO;
	}
	if (FIELD_GET(0x00FF, val) == SLI_BOOTUP_OPTIONS_CHECKSUM_FAIL) {
		return -EIO;
	}
	return 0;
}

static int siwx91x_nwp_load_firmware(const struct device *dev)
{
	const struct siwx91x_nwp_config *config = dev->config;
	uint16_t val;

	/* config->dma_desc_tx is also the input register for the bootloader */
	config->ta_regs->dma_desc_tx = (void *)(FIELD_PREP(0xFF00, SLI_WIFI_REGISTER_VALID) |
						FIELD_PREP(0x00FF, LOAD_NWP_FW));
	val = (uint16_t)config->ta_regs->bootloader_out;
	if (FIELD_GET(0x00FF, val) == SLI_VALID_FIRMWARE_NOT_PRESENT) {
		return -EIO;
	}
	if (FIELD_GET(0x00FF, val) == SLI_INVALID_OPTION) {
		return -EIO;
	}
	return 0;
}

static int siwx91x_nwp_init_firmware(const struct device *dev)
{
	const struct siwx91x_nwp_config *config = dev->config;
	bool run_bootload_sequence = true;
	int ret;

	if (!(config->m4_regs->status & SIWX91X_TA_IS_ACTIVE)) {
		config->m4_regs->status |= SIWX91X_M4_WAKEUP_TA;
		run_bootload_sequence = false;
	}

	while (!(config->m4_regs->status & SIWX91X_TA_IS_ACTIVE)) {
		;
	}

	if (!run_bootload_sequence) {
		return 0;
	}

	do {
		ret = siwx91x_nwp_wait_ready(dev);
		if (ret && ret != -EBUSY) {
			return ret;
		}
	} while (ret);

	ret = siwx91x_nwp_load_firmware(dev);
	if (ret) {
		return ret;
	}

	if (!(M4_ULP_SLP_STATUS_REG & MCU_ULP_WAKEUP)) {
		/* weird... */
		while ((uint32_t)config->ta_regs->dma_desc_tx & DMA_DESC_REG_VALID) {
			;
		}
	}
	return 0;
}

static void siwx91x_nwp_config_common(const struct device *dev,
				      const sl_wifi_system_boot_configuration_t *boot_config,
				      const sl_wifi_system_dynamic_pool_t *ta_pool)
{
	SIX91X_NWP_NET_BUF_DEFINE(frame_opermode, sizeof(sl_wifi_system_boot_configuration_t));
	SIX91X_NWP_NET_BUF_DEFINE(frame_mempool, sizeof(sl_wifi_system_boot_configuration_t));
	SIX91X_NWP_NET_BUF_DEFINE(frame_features, sizeof(sli_si91x_feature_frame_request));
	sli_si91x_feature_frame_request *features = net_buf_add(frame_features,
								sizeof(sli_si91x_feature_frame_request));

	net_buf_add_mem(frame_opermode, boot_config, sizeof(sl_wifi_system_boot_configuration_t));
	siwx91x_nwp_send_frame(dev, frame_opermode, SLI_WIFI_REQ_OPERMODE,
			       SLI_WIFI_COMMON_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	net_buf_add_mem(frame_mempool, ta_pool, sizeof(sl_wifi_system_dynamic_pool_t));
	siwx91x_nwp_send_frame(dev, frame_mempool, SLI_WLAN_REQ_DYNAMIC_POOL,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	memset(features, 0, sizeof(sli_si91x_feature_frame_request));
	features->rf_type = RF_TYPE;
	features->wireless_mode = SL_WIFI_HP_CHAIN;
	features->enable_ppp = ENABLE_PPP;
	features->afe_type = AFE_TYPE;
	features->feature_enables = SLI_FEAT_FRAME_PREAMBLE_DUTY_CYCLE |
				    SLI_FEAT_FRAME_LP_CHAIN |
				    SLI_FEAT_FRAME_IN_PACKET_DUTY_CYCLE;
	/* FIXME: manage that in the caller */
	if (boot_config->coex_mode) {
		features->pll_mode = 0;
	} else if (boot_config->custom_feature_bit_map & SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_160MHZ) {
		features->pll_mode = 1;
	} else if (boot_config->custom_feature_bit_map & SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_120MHZ) {
		features->pll_mode = 1;
	} else {
		features->pll_mode = 0;
	}
	siwx91x_nwp_send_frame(dev, frame_features, SLI_COMMON_REQ_FEATURE_FRAME,
			       SLI_WIFI_COMMON_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);
}

/* FIXME: Relocate it in Wifi driver */
static void siwx91x_nwp_config_wifi(const struct device *dev,
				    sl_wifi_band_mode_t band,
				    sl_wifi_region_code_t region_code)
{
	SIX91X_NWP_NET_BUF_DEFINE(frame_band, sizeof(uint8_t));
	SIX91X_NWP_NET_BUF_DEFINE(frame_init, 0);
	SIX91X_NWP_NET_BUF_DEFINE(frame_config, sizeof(sli_wifi_config_request_t));
	SIX91X_NWP_NET_BUF_DEFINE(frame_region, sizeof(sli_wifi_set_region_request_t));
	sli_wifi_config_request_t *config_req = net_buf_add(frame_config,
							    sizeof(sli_wifi_config_request_t));
	sli_wifi_set_region_request_t *region_req = net_buf_add(frame_region,
								sizeof(sli_wifi_set_region_request_t));

	net_buf_add_u8(frame_band, band);
	siwx91x_nwp_send_frame(dev, frame_band, SLI_WIFI_REQ_BAND,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	siwx91x_nwp_send_frame(dev, frame_init, SLI_WIFI_REQ_INIT,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	siwx91x_nwp_send_frame(dev, frame_init, SLI_WIFI_REQ_INIT,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	memset(region_req, 0, sizeof(sli_wifi_set_region_request_t));
	region_req->set_region_code_from_user_cmd = SET_REGION_CODE_FROM_USER;
	region_req->region_code = (uint8_t)region_code;
	siwx91x_nwp_send_frame(dev, frame_region, SLI_WIFI_REQ_SET_REGION,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);

	memset(config_req, 0, sizeof(sli_wifi_config_request_t));
	config_req->config_type = SLI_WIFI_CONFIG_RTS_THRESHOLD;
	config_req->value = SLI_WIFI_RTS_THRESHOLD;
	siwx91x_nwp_send_frame(dev, frame_config, SLI_WIFI_REQ_CONFIG,
			       SLI_WIFI_WLAN_CMD, K_FOREVER, SIWX91X_FRAME_FLAG_NO_REPLY);
}

static void siwx91x_nwp_set_pinmux(uint32_t switch_sel)
{
	if (switch_sel != 1) {
		/* Nothing to do */
		return;
	}
	/* FIXME: provide the pinmux macros */
	LOG_INF("Not supported on Zephyr, use pinmux");
	//!Program GPIO mode6 in ULP for ULP4,ULP5,ULP0 GPIOS
	RSI_EGPIO_SetPinMux(EGPIO1, 0, GPIO4, 6);
	RSI_EGPIO_SetPinMux(EGPIO1, 0, GPIO5, 6);
	RSI_EGPIO_SetPinMux(EGPIO1, 0, GPIO0, 6);
}

int siwx91x_nwp_hal_init(const struct device *dev,
		     const sl_wifi_device_configuration_t *dev_config,
		     bool first_init)
{
	const struct siwx91x_nwp_config *config = dev->config;
	struct siwx91x_nwp_data *data = dev->data;
	struct net_buf_pool *rx_buf_pool;
	int ret, i;

	__ASSERT(dev_config->boot_config.coex_mode != SL_SI91X_WLAN_MODE, "You mean SL_SI91X_WLAN_ONLY_MODE");
	__ASSERT(dev_config->boot_config.oper_mode != SL_SI91X_CONCURRENT_MODE, "Not supported");

	if (!config->rx_pool) {
		LOG_INF("Use network Rx net_buf pool");
	}

	if (first_init) {
		ret = siwx91x_nwp_init_firmware(dev);
		if (ret) {
			return ret;
		}
	}

	// FIXME: if !first_init flush tx_queues

	config->ta_regs->dma_desc_tx = data->tx_desc;
	config->ta_regs->dma_desc_rx = data->rx_desc;
	config->m4_regs->ta_int_mask_set = SIWX91X_TA_BUFFER_FULL_CLEAR;
	config->m4_regs->ta_int_mask_clr =
		SIWX91X_RX_PKT_DONE |
		SIWX91X_TX_PKT_DONE |
		SIWX91X_TA_WRITING_ON_FLASH |
		SIWX91X_NWP_DEINIT |
		SIWX91X_TA_BUFFER_FULL_CLEAR;
	config->m4_regs->status |= SIWX91X_M4_IS_ACTIVE;
	config->config_irq(dev);

	if (first_init) {
		k_sem_init(&data->firmware_ready, 0, 1);
		k_sem_init(&data->tx_data_complete, 0, 1);
		k_sem_init(&data->rx_data_complete, 0, 1);
		k_sem_init(&data->refresh_queues_state, 0, 1);
		for (i = 0; i < ARRAY_SIZE(data->cmd_queues); i++) {
			k_fifo_init(&data->cmd_queues[i].tx_queue);
		}

		net_pkt_get_info(NULL, NULL, &rx_buf_pool, NULL);
		k_thread_create(&data->thread_id, config->thread_stack, config->thread_stack_size,
				siwx91x_nwp_thread, (void *)dev, rx_buf_pool, NULL,
				config->thread_priority, 0, K_NO_WAIT);
		ret = k_sem_take(&data->firmware_ready, K_SECONDS(5));
		if (ret) {
			return -EIO;
		}
	}

	siwx91x_nwp_config_common(dev, &dev_config->boot_config, &dev_config->ta_pool);

	/* FXME: relocate these functions in wifi driver */
	if (dev_config->boot_config.coex_mode != SL_SI91X_BLE_MODE) {
		siwx91x_nwp_config_wifi(dev, dev_config->band, dev_config->region_code);
		/* 0x60000000 == FRONT_END_SWITCH_MASK */
		siwx91x_nwp_set_pinmux(FIELD_GET(0x60000000,
						 dev_config->boot_config.ext_custom_feature_bit_map));
	}

	return 0;
}

void siwx91x_nwp_register_wifi(const struct device *dev, struct siwx91x_nwp_wifi_cb *val)
{
	struct siwx91x_nwp_data *data = dev->data;

	data->wifi = val;
}

void siwx91x_nwp_register_bt(const struct device *dev, struct siwx91x_nwp_bt_cb *val)
{
	struct siwx91x_nwp_data *data = dev->data;

	data->bt = val;
}
