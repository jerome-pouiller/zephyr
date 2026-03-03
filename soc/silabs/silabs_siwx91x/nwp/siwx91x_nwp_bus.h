/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_NWP_BUS_H_
#define SIWX91X_NWP_BUS_H_
#include <stdint.h>
#include <stddef.h>

struct device;
struct net_buf;

/* Send the frame even if the queues are locked. */
#define SIWX91X_FRAME_FLAG_NO_LOCK     BIT(0)
/* Enqueue the frame and return immediatly. Note the reference counter for the net_buf is
 * incremented, so the caller can safely call net_buf_unref().
 */
#define SIWX91X_FRAME_FLAG_ASYNC       BIT(1)
/* Wait for teh reply, but don't return the reply. This simplify the buffer management for the
 * caller and may sligtly improve the performances.
 * If neither this flag and SIWX91X_NWP_REQ_ASYNC are set, the caller must call net_buf_unref() on
 * the reply.
 */
#define SIWX91X_FRAME_FLAG_NO_REPLY    BIT(2)

struct net_buf *siwx91x_nwp_send_frame(const struct device *dev, struct net_buf *buf,
				       uint16_t command, int queue_id, uint8_t flags);
struct net_buf *siwx91x_nwp_send_buf(const struct device *dev, const void *buf, size_t len,
				     uint16_t command, int queue_id, uint8_t flags);

void siwx91x_nwp_tx_flush_lock(const struct device *dev);
void siwx91x_nwp_tx_unlock(const struct device *dev);

void siwx91x_nwp_isr(const struct device *dev);
void siwx91x_nwp_thread(void *arg1, void *arg2, void *arg3);

#endif
