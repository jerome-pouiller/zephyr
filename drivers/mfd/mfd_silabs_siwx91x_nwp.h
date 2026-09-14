/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_DRIVERS_MFD_MFD_SILABS_SIWX91X_NWP_H_
#define ZEPHYR_DRIVERS_MFD_MFD_SILABS_SIWX91X_NWP_H_

/*
 * Internal interface between the bus agnostic part of the NWP driver
 * (mfd_silabs_siwx91x_nwp.c) and the host interface backends
 * (mfd_silabs_siwx91x_nwp_ahb.c, mfd_silabs_siwx91x_nwp_spi.c, ...).
 *
 * Only one backend can be built at a time: WiseConnect exposes its host
 * interface (sl_si91x_host_*()) as global functions without any device
 * parameter.
 */

#include <zephyr/device.h>
#include <zephyr/net/wifi.h>

#include <sl_wifi.h>

/* Runtime state shared by all the backends. A backend that needs its own state
 * must embed this structure as its first member.
 */
struct siwx91x_nwp_data {
	char current_country_code[WIFI_COUNTRY_CODE_LEN];
};

/* Configuration shared by all the backends. A backend must embed this
 * structure as its first member.
 */
struct siwx91x_nwp_config {
	/* Bits unconditionally ORed into the boot configuration by the
	 * backend. They mostly describe the NWP/host memory split and the
	 * host handshake, which both depend on the host interface.
	 */
	uint32_t feature_bit_map;
	uint32_t custom_feature_bit_map;
	uint32_t ext_custom_feature_bit_map;
	uint32_t config_feature_bit_map;

	uint32_t clock_frequency;
	uint8_t antenna_selection;
	bool support_1p8v;
	bool enable_xtal_correction;
	bool qspi_80mhz_clk;
};

/**
 * @brief Boot the NWP and check its firmware version.
 *
 * Called by the backends once the host interface is usable. The backend must
 * have completed everything WiseConnect needs to talk to the NWP (bus setup,
 * reset handling, ...) before calling this function.
 *
 * @param[in] dev NWP device.
 *
 * @return 0 on success, negative error code on failure.
 */
int siwx91x_nwp_common_init(const struct device *dev);

#endif /* ZEPHYR_DRIVERS_MFD_MFD_SILABS_SIWX91X_NWP_H_ */
