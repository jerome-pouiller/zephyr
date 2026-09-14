/**
 * @file
 * @brief SPI host interface for the SiWx91x Network Processor.
 *
 * On SiWN917 parts, the NWP is a standalone chip driven by an arbitrary host.
 * This file implements the host interface expected by WiseConnect
 * (sl_si91x_host_*()) on top of the Zephyr SPI and GPIO APIs.
 *
 * WiseConnect drives the chip select itself: it keeps it asserted across
 * several transfers to delimit a bus transaction. This maps to
 * SPI_HOLD_ON_CS combined with spi_release().
 *
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT silabs_siwx91x_nwp_spi

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <sl_si91x_host_interface.h>
#include <sl_si91x_status.h>

#include "mfd_silabs_siwx91x_nwp.h"

LOG_MODULE_DECLARE(siwx91x_nwp, CONFIG_MFD_LOG_LEVEL);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "WiseConnect only supports one NWP instance");

struct siwx91x_nwp_spi_config {
	struct siwx91x_nwp_config common;
	struct spi_dt_spec bus;
	struct gpio_dt_spec irq_gpio;
	struct gpio_dt_spec reset_gpio;
};

struct siwx91x_nwp_spi_data {
	struct siwx91x_nwp_data common;
	struct gpio_callback irq_cb;
	sl_status_t (*rx_irq)(void);
	void (*rx_done)(void);
};

/* WiseConnect exposes its host interface as global functions without any
 * device parameter, so the instance has to be reachable from a global.
 */
static const struct device *siwx91x_nwp_spi_dev = DEVICE_DT_INST_GET(0);

static void siwx91x_nwp_spi_irq_handler(const struct device *port, struct gpio_callback *cb,
					gpio_port_pins_t pins)
{
	struct siwx91x_nwp_spi_data *data =
		CONTAINER_OF(cb, struct siwx91x_nwp_spi_data, irq_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	if (data->rx_irq) {
		data->rx_irq();
	}
}

sl_status_t sl_si91x_host_init(const sl_si91x_host_init_configuration_t *config)
{
	struct siwx91x_nwp_spi_data *data = siwx91x_nwp_spi_dev->data;

	data->rx_irq = config->rx_irq;
	data->rx_done = config->rx_done;

	return SL_STATUS_OK;
}

sl_status_t sl_si91x_host_deinit(void)
{
	struct siwx91x_nwp_spi_data *data = siwx91x_nwp_spi_dev->data;

	data->rx_irq = NULL;
	data->rx_done = NULL;

	return SL_STATUS_OK;
}

sl_status_t sl_si91x_host_spi_transfer(const void *tx_buffer, void *rx_buffer,
				       uint16_t buffer_length)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;
	/* A NULL buffer means "send zeros" or "discard the received bytes" for
	 * both WiseConnect and the Zephyr SPI API.
	 */
	const struct spi_buf tx_buf = { .buf = (void *)tx_buffer, .len = buffer_length };
	const struct spi_buf rx_buf = { .buf = rx_buffer, .len = buffer_length };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
	int ret;

	ret = spi_transceive_dt(&config->bus, &tx_set, &rx_set);
	if (ret < 0) {
		LOG_ERR("SPI transfer of %u bytes failed (%d)", buffer_length, ret);
		return SL_STATUS_FAIL;
	}

	return SL_STATUS_OK;
}

void sl_si91x_host_spi_cs_assert(void)
{
	/* The chip select is asserted by the first transfer of the transaction
	 * and kept asserted thanks to SPI_HOLD_ON_CS.
	 */
}

void sl_si91x_host_spi_cs_deassert(void)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;

	spi_release_dt(&config->bus);
}

void sl_si91x_host_hold_in_reset(void)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;

	if (config->reset_gpio.port) {
		gpio_pin_set_dt(&config->reset_gpio, 1);
	}
}

void sl_si91x_host_release_from_reset(void)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;

	if (config->reset_gpio.port) {
		gpio_pin_set_dt(&config->reset_gpio, 0);
	}
}

void sl_si91x_host_enable_bus_interrupt(void)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;

	gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_EDGE_TO_ACTIVE);
}

void sl_si91x_host_disable_bus_interrupt(void)
{
	const struct siwx91x_nwp_spi_config *config = siwx91x_nwp_spi_dev->config;

	gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_DISABLE);
}

void sl_si91x_host_enable_high_speed_bus(void)
{
	/* The bus frequency is fixed by the device tree */
}

void sl_si91x_host_set_sleep_indicator(void)
{
	/* The ULP GPIO handshake is not supported yet */
}

void sl_si91x_host_clear_sleep_indicator(void)
{
	/* The ULP GPIO handshake is not supported yet */
}

uint32_t sl_si91x_host_get_wake_indicator(void)
{
	/* The ULP GPIO handshake is not supported yet, so the NWP is assumed to
	 * be always awake.
	 */
	return 1;
}

bool sl_si91x_host_is_in_irq_context(void)
{
	return k_is_in_isr();
}

static int siwx91x_nwp_spi_init(const struct device *dev)
{
	const struct siwx91x_nwp_spi_config *config = dev->config;
	struct siwx91x_nwp_spi_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&config->bus)) {
		return -ENODEV;
	}

	if (config->reset_gpio.port) {
		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	} else {
		LOG_WRN("No 'reset-gpios', unable to reset the NWP");
	}

	ret = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(&data->irq_cb, siwx91x_nwp_spi_irq_handler,
			   BIT(config->irq_gpio.pin));
	ret = gpio_add_callback_dt(&config->irq_gpio, &data->irq_cb);
	if (ret < 0) {
		return ret;
	}

	return siwx91x_nwp_common_init(dev);
}

#define SIWX91X_NWP_SPI_DEFINE(inst)                                                               \
                                                                                                   \
	static struct siwx91x_nwp_spi_data siwx91x_nwp_spi_data_##inst;                            \
                                                                                                   \
	static const struct siwx91x_nwp_spi_config siwx91x_nwp_spi_config_##inst = {               \
		.common = {                                                                        \
			/* The host owns none of the NWP memory and provides the RTC */            \
			.ext_custom_feature_bit_map = SL_SI91X_EXT_FEAT_672K_M4SS_0K,              \
			.custom_feature_bit_map = SL_SI91X_CUSTOM_FEAT_RTC_FROM_HOST,              \
			.support_1p8v = DT_INST_PROP(inst, support_1p8v),                          \
			.enable_xtal_correction = DT_INST_PROP(inst, enable_xtal_correction),      \
			.qspi_80mhz_clk = DT_INST_PROP(inst, qspi_80mhz_clk),                      \
			.antenna_selection = DT_INST_ENUM_IDX(inst, antenna_selection),            \
			.clock_frequency = DT_INST_PROP(inst, clock_frequency),                    \
		},                                                                                 \
		.bus = SPI_DT_SPEC_INST_GET(inst, SPI_WORD_SET(8) | SPI_TRANSFER_MSB |             \
						  SPI_OP_MODE_MASTER | SPI_HOLD_ON_CS),            \
		.irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                                \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &siwx91x_nwp_spi_init, NULL, &siwx91x_nwp_spi_data_##inst,     \
			      &siwx91x_nwp_spi_config_##inst, POST_KERNEL,                        \
			      CONFIG_MFD_SILABS_SIWX91X_NWP_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SIWX91X_NWP_SPI_DEFINE)
