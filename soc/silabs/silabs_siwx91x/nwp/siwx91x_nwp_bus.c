/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/net_buf.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/logging/log.h>
#include "siwx91x_nwp.h"
#include "siwx91x_nwp_bus.h"

LOG_MODULE_DECLARE(siwx91x_nwp, CONFIG_SIWX91X_NWP_LOG_LEVEL);

#ifdef CONFIG_NET_PKT_BUF_USER_DATA_SIZE
BUILD_ASSERT(CONFIG_NET_PKT_BUF_USER_DATA_SIZE >= sizeof(void *), "net_buf user data too small");
#endif

static void siwx91x_nwp_cb_rx_bt(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->bt && data->bt->on_rx)
		data->bt->on_rx(data->bt, buf);
}

static void siwx91x_nwp_cb_rx_wifi(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_rx)
		data->wifi->on_rx(data->wifi, buf);
}

static void siwx91x_nwp_cb_scan_result(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_scan_results)
		data->wifi->on_scan_results(data->wifi, buf);
}

static void siwx91x_nwp_cb_join(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_join)
		data->wifi->on_join(data->wifi, buf);
}

static void siwx91x_nwp_cb_sta_connect(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_sta_connect)
		data->wifi->on_sta_connect(data->wifi, buf);
}

static void siwx91x_nwp_cb_sta_disconnect(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	if (data->wifi && data->wifi->on_sta_disconnect)
		data->wifi->on_sta_disconnect(data->wifi, buf);
}

static void siwx91x_nwp_cb_card_ready(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_nwp_data *data = dev->data;

	k_sem_give(&data->firmware_ready);
}

static void siwx91x_nwp_cb_state(const struct device *dev, struct net_buf *buf)
{
	struct siwx91x_frame_desc *frame = (struct siwx91x_frame_desc *)buf->data;

	LOG_INF("Ignored reply: 0x%02x", frame->command);
}

// See wiseconnect/components/device/silabs/si91x/wireless/inc/sl_si91x_constants.h for a list of
// commands
static const struct {
	uint8_t  queue;
	uint16_t cmd_id;
	void (*cb)(const struct device *, struct net_buf *);
} siwx91x_nwp_rsp_list[] = {
	{ SLI_BT_Q,        -1 /* wildcard */,                         .cb = siwx91x_nwp_cb_rx_bt            },
	{ SLI_WLAN_DATA_Q, SLI_RECEIVE_RAW_DATA,                      .cb = siwx91x_nwp_cb_rx_wifi          },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_RSP_CARDREADY,                    .cb = siwx91x_nwp_cb_card_ready       },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_RSP_JOIN,                         .cb = siwx91x_nwp_cb_join             },
	{ SLI_WLAN_MGMT_Q, SLI_WIFI_RSP_SCAN_RESULTS,                 .cb = siwx91x_nwp_cb_scan_result      },
	{ SLI_WLAN_MGMT_Q, SLI_WLAN_RSP_CLIENT_CONNECTED,             .cb = siwx91x_nwp_cb_sta_connect      },
	{ SLI_WLAN_MGMT_Q, SLI_WLAN_RSP_CLIENT_DISCONNECTED,          .cb = siwx91x_nwp_cb_sta_disconnect   },
	{ SLI_WLAN_MGMT_Q, SLI_WLAN_RSP_MODULE_STATE,                 .cb = siwx91x_nwp_cb_state            },
};

struct siwx91x_nwp_cmd_queue *siwx91x_nwp_get_queue(const struct device *dev, int id)
{
	struct siwx91x_nwp_data *data = dev->data;
	int i;

	for (i = 0; i < ARRAY_SIZE(data->cmd_queues); i++) {
		if (data->cmd_queues[i].id == id) {
			return &data->cmd_queues[i];
		}
	}
	return NULL;
}


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
	struct siwx91x_frame_desc *frame = (struct siwx91x_frame_desc *)rx_buf->data;
	void (*cb)(const struct device *, struct net_buf *);
	struct siwx91x_nwp_data *data = dev->data;
	struct siwx91x_nwp_cmd_queue *queue;
	struct net_buf **reply_ptr; /* user_data out */
	uint32_t flags; /* user_data in */
	struct net_buf *req_buf;
	int i;

	__ASSERT(rx_buf, "Corrupted context");
	LOG_HEXDUMP_DBG(rx_buf->data, rx_buf->len, "nwp rx:");

	queue = siwx91x_nwp_get_queue(dev, FIELD_GET(0xF000, frame->length_and_queue));
	if (!queue) {
		LOG_INF("drop unhandled frame");
		goto end;
	}
	cb = NULL;
	for (i = 0; i < ARRAY_SIZE(siwx91x_nwp_rsp_list); i++) {
		if (siwx91x_nwp_rsp_list[i].queue == queue->id) {
			if (siwx91x_nwp_rsp_list[i].cmd_id == frame->command ||
			    siwx91x_nwp_rsp_list[i].cmd_id == (uint16_t)-1) {
				cb = siwx91x_nwp_rsp_list[i].cb;
				break;
			}
		}
	}
	req_buf = queue->tx_in_progress;
	if (req_buf && ((struct siwx91x_frame_desc *)req_buf->data)->command == frame->command) {
		queue->tx_in_progress = NULL;
		k_sem_give(&data->refresh_queues_state);
	} else {
		req_buf = NULL;
	}
	if (req_buf) {
		flags = *(uint32_t *)net_buf_user_data(req_buf);
		reply_ptr = net_buf_user_data(req_buf);
		if (flags & SIWX91X_FRAME_FLAG_ASYNC) {
			/* We receive the Rx interrupt with the reply before we received the Tx sent
			 * interrupt. This looks very strange.
			 */
			LOG_WRN("Race in interrupt mangement");
		}
		if (flags & SIWX91X_FRAME_FLAG_NO_REPLY) {
			*reply_ptr = NULL;
		} else {
			*reply_ptr = net_buf_ref(rx_buf);
		}
		k_sem_give(&queue->tx_done);
	}
	if (cb) {
		cb(dev, rx_buf);
	}
	if (!req_buf && !cb) {
		LOG_INF("drop unhandled frame: %d/0x%02x", queue->id, frame->command);
	}
end:
	net_buf_unref(rx_buf);
}

/* sli_si91x_wifi_handle_tx_event */
static void siwx91x_nwp_handle_tx(const struct device *dev, struct siwx91x_nwp_cmd_queue *queue)
{
	const struct siwx91x_nwp_config *config = dev->config;
	struct siwx91x_nwp_data *data = dev->data;
	uint32_t flags; /* user_data in */
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
					data->tx_buf_in_progress->size, buf, 0, SIZE_MAX);
		net_buf_add(data->tx_buf_in_progress, len);
	}

	flags = *(uint32_t *)net_buf_user_data(buf);
	if (!(flags & SIWX91X_FRAME_FLAG_ASYNC)) {
		__ASSERT(!queue->tx_in_progress, "Get Tx event while tx_qeue is empty");
		queue->tx_in_progress = buf;
	}

	__ASSERT(len >= sizeof(struct siwx91x_frame_desc), "Corrupted buffer");
	data->tx_desc[0].addr = SIWX91X_NWP_MEMORY_OFFSET_ADDRESS + data->tx_buf_in_progress->data;
	data->tx_desc[0].length = sizeof(struct siwx91x_frame_desc);
	data->tx_desc[1].addr = data->tx_desc[0].addr + sizeof(struct siwx91x_frame_desc);
	data->tx_desc[1].length = len - sizeof(struct siwx91x_frame_desc);
	compiler_barrier(); /* Useful? */
	LOG_HEXDUMP_DBG(data->tx_buf_in_progress->data, len, "nwp tx:");
	config->m4_regs->m4_int_set = SIWX91X_TX_PKT_PENDING;
	net_buf_unref(buf);
}

/* sli_wifi_command_engine */
void siwx91x_nwp_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	struct net_buf_pool *rx_buf_pool = arg2;
	struct siwx91x_nwp_data *data = dev->data;
	const struct siwx91x_nwp_config *cfg = dev->config;
	uint32_t nwp_status;
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
		[3] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->tx_data_complete, 0),
		[4] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->rx_data_complete, 0),
		[5] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      &data->refresh_queues_state, 0),
		[6] = K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
						      K_POLL_MODE_NOTIFY_ONLY,
						      /* lifo and fifo are binary compatible */
						      (struct k_fifo *)&rx_buf_pool->free, 0),
	};

	k_thread_name_set(NULL, "nwp");

	/* Force first rx_buffer */
	ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
	__ASSERT(rx_buffer == NULL, "Corrupted state");
	__ASSERT(!ret, "Not supported");
	LOG_DBG("event: Rx buffer available (rx queue started)");
	events[6].type = K_POLL_TYPE_IGNORE;

	for (;;) {
		ret = k_poll(events, ARRAY_SIZE(events), K_FOREVER);
		nwp_status = cfg->ta_regs->status;
		/* Tx path */
		for (i = 0; i < ARRAY_SIZE(data->cmd_queues); i++) {
			if (events[i].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
				events[i].state = K_POLL_STATE_NOT_READY;
				if (data->cmd_queues[i].id == SLI_BT_Q &&
				    (nwp_status & SIWX91X_NWP_BLE_BUFFER_FULL)) {
					LOG_DBG("event: Tx data delayed (queue %d)", i);
					break;
				}
				if (data->cmd_queues[i].id == SLI_WLAN_DATA_Q &&
				    (nwp_status & SIWX91X_NWP_WIFI_BUFFER_FULL)) {
					LOG_DBG("event: Tx data delayed (queue %d)", i);
					break;
				}
				siwx91x_nwp_handle_tx(dev, &data->cmd_queues[i]);
				lock_tx_queues = true;
				LOG_DBG("event: Tx data send (queue %d)", i);
				break;
			}
		}
		if (events[3].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[3].state = K_POLL_STATE_NOT_READY;
	                k_sem_take(&data->tx_data_complete, K_NO_WAIT);
			net_buf_unref(data->tx_buf_in_progress);
			data->tx_buf_in_progress = NULL;
			lock_tx_queues = false;
			LOG_DBG("event: Tx data complete");
		}
		if (events[5].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[5].state = K_POLL_STATE_NOT_READY;
			k_sem_take(&data->refresh_queues_state, K_NO_WAIT);
			LOG_DBG("event: Refresh queue state");
		}

		/* Rx path */
		if (events[4].state == K_POLL_STATE_SEM_AVAILABLE) {
			events[4].state = K_POLL_STATE_NOT_READY;
	                k_sem_take(&data->rx_data_complete, K_NO_WAIT);
			ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
			if (ret) {
				LOG_DBG("event: Rx data complete (rx queue stopped)");
				events[6].type = K_POLL_TYPE_FIFO_DATA_AVAILABLE;
			} else {
				LOG_DBG("event: Rx data complete");
			}
			siwx91x_nwp_handle_rx(dev, rx_buffer);
		}
		if (events[6].state == K_POLL_TYPE_FIFO_DATA_AVAILABLE) {
			events[6].state = K_POLL_STATE_NOT_READY;
			ret = siwx91x_nwp_feed_rx_buffer(dev, &rx_buffer);
			__ASSERT(rx_buffer == NULL, "Corrupted state");
			if (ret) {
				LOG_DBG("event: Rx buffer available (ignored)");
			} else {
				LOG_DBG("event: Rx buffer available (rx queue restarted)");
				events[6].type = K_POLL_TYPE_IGNORE;
			}
		}
		for (i = 0; i < ARRAY_SIZE(data->cmd_queues); i++) {
			if (lock_tx_queues || data->cmd_queues[i].tx_in_progress ||
			    (data->cmd_queues[i].id == SLI_BT_Q && (nwp_status & SIWX91X_NWP_BLE_BUFFER_FULL)) ||
			    (data->cmd_queues[i].id == SLI_WLAN_DATA_Q && (nwp_status & SIWX91X_NWP_WIFI_BUFFER_FULL))) {
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

	LOG_DBG("interrupt: 0x%04x", interrupts);
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

struct net_buf *siwx91x_nwp_send_frame(const struct device *dev, struct net_buf *buf,
				       uint16_t command, int queue_id, uint8_t flags)
{
	struct siwx91x_nwp_cmd_queue *queue = siwx91x_nwp_get_queue(dev, queue_id);
	struct siwx91x_frame_desc *desc = (void *)buf->data;
	struct siwx91x_nwp_data *data = dev->data;
	size_t len = net_buf_frags_len(buf);

	__ASSERT(queue != NULL, "Corrupted command");
	__ASSERT(buf->len >= sizeof(struct siwx91x_frame_desc), "Invalid buffer");
	__ASSERT(buf->user_data_size >= sizeof(uint32_t), "Not supported");
	if (buf->frags) {
		LOG_DBG("Fragmented frame");
	}

	/* FIXME: Should we leave that operation to the caller? */
	memset(desc, 0, sizeof(struct siwx91x_frame_desc));
	desc->command = command;
	desc->length_and_queue = FIELD_PREP(0x0FFF, len - sizeof(struct siwx91x_frame_desc));
	desc->length_and_queue |= FIELD_PREP(0xF000, queue_id);

	__ASSERT(buf->user_data_size >= sizeof(uint32_t), "Not supported");
	*(uint32_t *)net_buf_user_data(buf) = flags;

	if (!(flags & SIWX91X_FRAME_FLAG_NO_LOCK)) {
		k_sem_take(&data->global_lock, K_FOREVER);
		k_sem_give(&data->global_lock);
	}
	if (!(flags & SIWX91X_FRAME_FLAG_ASYNC)) {
		/* FIXME: We could easily implement timeout here */
		k_sem_take(&queue->sync_frame_in_queue, K_FOREVER);
	}
	k_fifo_put(&queue->tx_queue, net_buf_ref(buf));
	if (flags & SIWX91X_FRAME_FLAG_ASYNC) {
		return NULL;
	}
	/* FIXME: We could also implement timeout here (more complex) */
	k_sem_take(&queue->tx_done, K_FOREVER);
	k_sem_give(&queue->sync_frame_in_queue);
	return *(struct net_buf **)net_buf_user_data(buf);
}

/* sli_si91x_driver_send_command */
struct net_buf *siwx91x_nwp_send_buf(const struct device *dev, const void *buf, size_t len,
                                    uint16_t command, int queue_id, uint8_t flags)
{
	/* Originally, we allocate this buffer on the stack. However, since this this buffer is
	 * quite large and several thread call it, it is cheaper to allocate a static buffer.
	 * This prevents to have several commands in parallel. If you consider command for wifi
	 * should not lock commands for bluetooth, you may increase size of this pool (or use
	 * another pool, or allocate on the stack).
	 */
	NET_BUF_POOL_FIXED_DEFINE(siwx91x_nwp_cmd_pool, 1, 160, 4, NULL);
	struct net_buf *cmd, *reply;

	cmd = net_buf_alloc(&siwx91x_nwp_cmd_pool, K_FOREVER);
	__ASSERT(!(flags & SIWX91X_FRAME_FLAG_ASYNC), "Invalid call");
	__ASSERT(sizeof(struct siwx91x_frame_desc) + len <= 160, "Invalid call: %d > 160", len);
	net_buf_add(cmd, sizeof(struct siwx91x_frame_desc));
	net_buf_add_mem(cmd, buf, len);
	reply = siwx91x_nwp_send_frame(dev, cmd, command, queue_id, flags);
	net_buf_unref(cmd);
	return reply;
}

void siwx91x_nwp_tx_flush_lock(const struct device *dev)
{
	struct siwx91x_nwp_data *data = dev->data;
	bool wait;
	int i;

	k_sem_take(&data->global_lock, K_FOREVER);
	do {
		wait = false;
		for (i = 0; i < ARRAY_SIZE(data->cmd_queues); i++) {
			if (!k_fifo_is_empty(&data->cmd_queues[i].tx_queue) ||
			    data->cmd_queues[i].tx_in_progress) {
				wait = true;
				break;
			}
		}
		if (wait)
			k_msleep(2);
	} while (wait);
}

void siwx91x_nwp_tx_unlock(const struct device *dev)
{
	struct siwx91x_nwp_data *data = dev->data;

	k_sem_give(&data->global_lock);
}
