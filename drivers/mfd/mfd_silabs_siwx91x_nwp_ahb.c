/**
 * @file
 * @brief AHB host interface for the SiWx91x Network Processor.
 *
 * On SiWG917 parts, the NWP shares the die with the Cortex-M4 host. Both are
 * connected through an AHB bus and share the same SRAM. This file describes
 * that topology to the bus agnostic part of the driver and wires the NWP
 * interrupt to the host.
 *
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT silabs_siwx91x_nwp

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <sl_si91x_power_manager.h>

#include "mfd_silabs_siwx91x_nwp.h"

LOG_MODULE_DECLARE(siwx91x_nwp, CONFIG_MFD_LOG_LEVEL);

/* The NWP provides the crypto services used by mbed TLS, which is initialized
 * at CONFIG_KERNEL_INIT_PRIORITY_DEFAULT.
 */
#if defined(CONFIG_MBEDTLS_INIT)
BUILD_ASSERT(CONFIG_MFD_SILABS_SIWX91X_NWP_INIT_PRIORITY < CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
	     "mbed TLS must be initialized after the NWP.");
#endif

/* The host and the NWP share the same SRAM. The size does not match exactly
 * because 1 KB is reserved at the start of the RAM.
 */
#if DT_REG_SIZE(DT_CHOSEN(zephyr_sram)) == (195 * 1024)
#define SIWX91X_NWP_AHB_MEMORY_CONFIG SL_SI91X_EXT_FEAT_480K_M4SS_192K
#elif DT_REG_SIZE(DT_CHOSEN(zephyr_sram)) == (255 * 1024)
#define SIWX91X_NWP_AHB_MEMORY_CONFIG SL_SI91X_EXT_FEAT_416K_M4SS_256K
#elif DT_REG_SIZE(DT_CHOSEN(zephyr_sram)) == (319 * 1024)
#define SIWX91X_NWP_AHB_MEMORY_CONFIG SL_SI91X_EXT_FEAT_352K_M4SS_320K
#else
#error "Unsupported SRAM size for the SiWx91x NWP/host memory split"
#endif

struct siwx91x_nwp_ahb_config {
	struct siwx91x_nwp_config common;
	const struct pinctrl_dev_config *pcfg;
	void (*config_irq)(const struct device *dev);
	bool antenna_ext_gpios;
};

static int siwx91x_nwp_ahb_init(const struct device *dev)
{
	const struct siwx91x_nwp_ahb_config *config = dev->config;
	int ret;

	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0 && ret != -ENOENT) {
		return ret;
	}
	if (config->antenna_ext_gpios && ret == -ENOENT) {
		LOG_WRN("'ext-gpios' expects some pinctrl configuration");
	}

	ret = siwx91x_nwp_common_init(dev);
	if (ret < 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_PM)) {
		/* Remove the requirement added by siwx91x_power_init() */
		sl_si91x_power_manager_remove_ps_requirement(SL_SI91X_POWER_MANAGER_PS4);
	}

	config->config_irq(dev);

	return 0;
}

#define SIWX91X_NWP_AHB_DEFINE(inst)                                                               \
                                                                                                   \
	static void silabs_siwx91x_nwp_irq_configure_##inst(const struct device *dev)              \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(inst, nwp_irq, irq),                               \
			    DT_INST_IRQ_BY_NAME(inst, nwp_irq, priority), IRQ074_Handler, NULL,    \
			    0);                                                                    \
		irq_enable(DT_INST_IRQ_BY_NAME(inst, nwp_irq, irq));                               \
	};                                                                                         \
                                                                                                   \
	static struct siwx91x_nwp_data siwx91x_nwp_data_##inst;                                    \
                                                                                                   \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static const struct siwx91x_nwp_ahb_config siwx91x_nwp_config_##inst = {                   \
		.common = {                                                                        \
			.ext_custom_feature_bit_map = SIWX91X_NWP_AHB_MEMORY_CONFIG,               \
			.support_1p8v = DT_INST_PROP(inst, support_1p8v),                          \
			.enable_xtal_correction = DT_INST_PROP(inst, enable_xtal_correction),      \
			.qspi_80mhz_clk = DT_INST_PROP(inst, qspi_80mhz_clk),                      \
			.antenna_selection = DT_INST_ENUM_IDX(inst, antenna_selection),            \
			.clock_frequency = DT_INST_PROP(inst, clock_frequency),                    \
		},                                                                                 \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.config_irq = silabs_siwx91x_nwp_irq_configure_##inst,                             \
		.antenna_ext_gpios = DT_INST_ENUM_HAS_VALUE(inst, antenna_selection, ext_gpios),   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &siwx91x_nwp_ahb_init, NULL, &siwx91x_nwp_data_##inst,         \
			      &siwx91x_nwp_config_##inst, POST_KERNEL,                             \
			      CONFIG_MFD_SILABS_SIWX91X_NWP_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SIWX91X_NWP_AHB_DEFINE)
