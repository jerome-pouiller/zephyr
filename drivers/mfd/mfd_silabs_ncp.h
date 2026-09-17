/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Helpers shared by the transports of the "silabs,ncp" device. The including
 * file must have registered its log module.
 */

#ifndef ZEPHYR_DRIVERS_MFD_MFD_SILABS_NCP_H_
#define ZEPHYR_DRIVERS_MFD_MFD_SILABS_NCP_H_

#include <zephyr/device.h>
#include <zephyr/drivers/mfd/silabs_ncp.h>
#include <zephyr/logging/log.h>

#include "sl_cpc.h"
#include "sl_cpc_bus.h"
#include "sl_cpc_config.h"
#include "sli_cpc_bus.h"
#include "sli_cpc_drv.h"
#include "sli_cpc_frame.h"
#include "sli_cpc_frame_list.h"
#include "sli_cpc_hdr.h"
#include "sli_cpc_memory.h"

/*
 * Initialize and start the CPC bus of a "silabs,ncp" device. Called by the
 * transports at the end of their device initialization.
 */
static inline int mfd_silabs_ncp_bus_start(const struct device *dev)
{
	sl_cpc_bus_t *bus = mfd_silabs_ncp_cpc_bus(dev);
	sl_status_t status;

	status = sl_cpc_bus_init(bus);
	if (status != SL_STATUS_OK) {
		LOG_ERR("%s: CPC bus init failed: 0x%lx", dev->name, (unsigned long)status);
		return -EIO;
	}

	status = sl_cpc_bus_start(bus);
	if (status != SL_STATUS_OK) {
		LOG_ERR("%s: CPC bus start failed: 0x%lx", dev->name, (unsigned long)status);
		sl_cpc_bus_deinit(bus);
		return -EIO;
	}

	return 0;
}

/*
 * Drop a RX frame after sli_cpc_alloc_rx_payload() was attempted on it: give
 * the payload chain back to the endpoint, release the endpoint reference and
 * free the frame. The core then calls on_rx_frame_free().
 */
static inline void mfd_silabs_ncp_drop_rx_frame(sl_cpc_frame_t *frame)
{
	if (frame->ep != NULL && frame->payload != NULL) {
		sli_cpc_buffer_chain_release(&frame->ep->rx_buffer_queue, frame->payload,
					     frame->ep->mtu);
		frame->payload = NULL;
	}
	sli_cpc_frame_put_ref(&frame);
}

#endif /* ZEPHYR_DRIVERS_MFD_MFD_SILABS_NCP_H_ */
