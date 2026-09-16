/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bluetooth HCI driver for a controller running on a Silicon Labs network
 * co-processor. HCI packets, in H4 framing, are exchanged on a CPC endpoint
 * of the "silabs,ncp" parent device.
 */

#define DT_DRV_COMPAT silabs_series3_hci

#include <string.h>

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/drivers/mfd/silabs_ncp.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "sl_cpc.h"

LOG_MODULE_REGISTER(hci_silabs_series3, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/* Largest H4 packet in each direction, as a CPC payload */
#define HCI_RX_MTU                                                                                 \
	ROUND_UP(MAX(BT_BUF_RX_SIZE, BT_BUF_EVT_SIZE(CONFIG_BT_BUF_EVT_DISCARDABLE_SIZE)),         \
		 SL_CPC_BUF_MIN_ALIGNMENT)
#if defined(CONFIG_BT_CONN)
#define HCI_TX_MTU                                                                                 \
	ROUND_UP(MAX(BT_BUF_CMD_TX_SIZE, BT_BUF_ACL_SIZE(CONFIG_BT_BUF_ACL_TX_SIZE)),              \
		 SL_CPC_BUF_MIN_ALIGNMENT)
#else
#define HCI_TX_MTU ROUND_UP(BT_BUF_CMD_TX_SIZE, SL_CPC_BUF_MIN_ALIGNMENT)
#endif

#define HCI_CONNECT_TIMEOUT K_SECONDS(5)

/* Receive buffer handed to CPC, queued to the RX thread once filled */
struct hci_series3_rx_slot {
	void *fifo_reserved;
	sl_cpc_buf_t buf;
	uint8_t data[HCI_RX_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

/* Transmit context, alive until the CPC send-done event */
struct hci_series3_tx_slot {
	sl_cpc_buf_t buf;
	sl_cpc_frame_t frame;
	uint8_t data[HCI_TX_MTU] __aligned(SL_CPC_BUF_MIN_ALIGNMENT);
};

struct hci_series3_config {
	struct bt_hci_driver_config common;
	const struct device *parent;
	uint8_t endpoint_id;
	struct k_mem_slab *tx_slab;
	k_thread_stack_t *rx_stack;
	size_t rx_stack_size;
};

struct hci_series3_data {
	struct bt_hci_driver_data common;
	const struct device *dev;
	sl_cpc_ep_t ep;
	struct k_sem connected;
	struct k_sem closed;
	struct k_fifo rx_fifo;
	struct k_thread rx_thread;
	bool rx_started;
	bool opened;
	struct hci_series3_rx_slot rx_slots[CONFIG_BT_SILABS_SERIES3_HCI_RX_COUNT];
};

static bool hci_series3_evt_discardable(const uint8_t *evt_data, size_t remaining)
{
	if (remaining <= sizeof(struct bt_hci_evt_hdr)) {
		return false;
	}
	if (evt_data[0] != BT_HCI_EVT_LE_META_EVENT) {
		return false;
	}

	switch (evt_data[sizeof(struct bt_hci_evt_hdr)]) {
	case BT_HCI_EVT_LE_ADVERTISING_REPORT:
		return true;
	default:
		return false;
	}
}

static struct net_buf *hci_series3_evt_recv(const uint8_t *data, size_t remaining)
{
	struct bt_hci_evt_hdr hdr;
	struct net_buf *buf;
	bool discardable;

	if (remaining < sizeof(hdr)) {
		LOG_ERR("Not enough data for event header");
		return NULL;
	}

	discardable = hci_series3_evt_discardable(data, remaining);
	memcpy(&hdr, data, sizeof(hdr));
	data += sizeof(hdr);
	remaining -= sizeof(hdr);

	if (remaining != hdr.len) {
		LOG_ERR("Event payload length mismatch (%u != %u)", remaining, hdr.len);
		return NULL;
	}

	buf = bt_buf_get_evt(hdr.evt, discardable, discardable ? K_NO_WAIT : K_SECONDS(10));
	if (buf == NULL) {
		LOG_DBG("No event buffer available");
		return NULL;
	}

	net_buf_add_mem(buf, &hdr, sizeof(hdr));
	if (net_buf_tailroom(buf) < remaining) {
		LOG_ERR("Not enough room in event buffer");
		net_buf_unref(buf);
		return NULL;
	}
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

static struct net_buf *hci_series3_acl_recv(const uint8_t *data, size_t remaining)
{
	struct bt_hci_acl_hdr hdr;
	struct net_buf *buf;

	if (remaining < sizeof(hdr)) {
		LOG_ERR("Not enough data for ACL header");
		return NULL;
	}

	memcpy(&hdr, data, sizeof(hdr));
	data += sizeof(hdr);
	remaining -= sizeof(hdr);

	if (remaining != sys_le16_to_cpu(hdr.len)) {
		LOG_ERR("ACL payload length mismatch");
		return NULL;
	}

	buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_SECONDS(10));
	if (buf == NULL) {
		LOG_ERR("No ACL buffer available");
		return NULL;
	}

	net_buf_add_mem(buf, &hdr, sizeof(hdr));
	if (net_buf_tailroom(buf) < remaining) {
		LOG_ERR("Not enough room in ACL buffer");
		net_buf_unref(buf);
		return NULL;
	}
	net_buf_add_mem(buf, data, remaining);

	return buf;
}

/* Deliver one H4 packet to the host. Runs in the RX thread. */
static void hci_series3_deliver(const struct device *dev, const uint8_t *payload, uint16_t len)
{
	struct net_buf *buf;
	uint8_t h4_type;

	if (len < 1) {
		LOG_WRN("Empty packet, dropping");
		return;
	}

	h4_type = payload[0];
	payload++;
	len--;

	switch (h4_type) {
	case BT_HCI_H4_EVT:
		buf = hci_series3_evt_recv(payload, len);
		break;
	case BT_HCI_H4_ACL:
		buf = hci_series3_acl_recv(payload, len);
		break;
	default:
		LOG_ERR("Unknown HCI packet type %u", h4_type);
		return;
	}

	if (buf != NULL) {
		bt_hci_recv(dev, buf);
	}
}

static void hci_series3_rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;
	struct hci_series3_data *data = dev->data;
	struct hci_series3_rx_slot *slot;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		slot = k_fifo_get(&data->rx_fifo, K_FOREVER);
		hci_series3_deliver(dev, slot->buf.ptr, slot->buf.len);
		/* Give the buffer back to CPC */
		sl_cpc_ep_push_recv_buf(&data->ep, &slot->buf);
	}
}

/* Runs in the CPC context: keep it short */
static void hci_series3_ep_event(sl_cpc_ep_t *ep, sl_cpc_ep_event_type_t type,
				 const sl_cpc_ep_event_t *event, void *arg)
{
	const struct device *dev = arg;
	const struct hci_series3_config *cfg = dev->config;
	struct hci_series3_data *data = dev->data;
	struct hci_series3_rx_slot *rx;
	struct hci_series3_tx_slot *tx;

	ARG_UNUSED(ep);

	switch (type) {
	case SL_CPC_EP_EVENT_RECV:
		rx = CONTAINER_OF(event->recv.buf, struct hci_series3_rx_slot, buf);
		k_fifo_put(&data->rx_fifo, rx);
		break;
	case SL_CPC_EP_EVENT_SEND_DONE:
		tx = CONTAINER_OF(event->send_done.frame, struct hci_series3_tx_slot, frame);
		if (event->send_done.status != SL_STATUS_OK) {
			LOG_ERR("%s: send failed: 0x%lx", dev->name,
				(unsigned long)event->send_done.status);
		}
		k_mem_slab_free(cfg->tx_slab, tx);
		break;
	case SL_CPC_EP_EVENT_CONNECTED:
		k_sem_give(&data->connected);
		break;
	case SL_CPC_EP_EVENT_CLOSED:
		k_sem_give(&data->closed);
		break;
	case SL_CPC_EP_EVENT_ERROR:
		LOG_ERR("%s: endpoint error: 0x%lx", dev->name,
			(unsigned long)event->error.status);
		break;
	default:
		break;
	}
}

static int hci_series3_send(const struct device *dev, struct net_buf *buf)
{
	const struct hci_series3_config *cfg = dev->config;
	struct hci_series3_data *data = dev->data;
	struct hci_series3_tx_slot *slot;
	sl_status_t status;
	int ret;

	if (!data->opened) {
		return -ENOTCONN;
	}

	/* The H4 packet type is the first byte of the buffer */
	if (buf->len < 1 || buf->len > HCI_TX_MTU) {
		return -EINVAL;
	}

	ret = k_mem_slab_alloc(cfg->tx_slab, (void **)&slot, K_SECONDS(1));
	if (ret < 0) {
		LOG_ERR("%s: no TX slot", dev->name);
		return -ENOMEM;
	}

	memcpy(slot->data, buf->data, buf->len);
	sl_cpc_buf_init(&slot->buf, slot->data, buf->len);
	memset(&slot->frame, 0, sizeof(slot->frame));

	status = sl_cpc_ep_send(&data->ep, &slot->buf, &slot->frame, NULL);
	if (status != SL_STATUS_OK) {
		LOG_ERR("%s: send failed: 0x%lx", dev->name, (unsigned long)status);
		k_mem_slab_free(cfg->tx_slab, slot);
		return -EIO;
	}

	net_buf_unref(buf);

	return 0;
}

static int hci_series3_open(const struct device *dev)
{
	const struct hci_series3_config *cfg = dev->config;
	struct hci_series3_data *data = dev->data;
	sl_status_t status;

	if (!device_is_ready(cfg->parent)) {
		LOG_ERR("%s: co-processor not ready", dev->name);
		return -ENODEV;
	}

	status = sl_cpc_ep_init(&data->ep, cfg->endpoint_id, HCI_RX_MTU, hci_series3_ep_event,
				(void *)dev);
	if (status != SL_STATUS_OK) {
		LOG_ERR("%s: endpoint init failed: 0x%lx", dev->name, (unsigned long)status);
		return -EIO;
	}

	for (size_t i = 0; i < ARRAY_SIZE(data->rx_slots); i++) {
		struct hci_series3_rx_slot *slot = &data->rx_slots[i];

		sl_cpc_buf_init(&slot->buf, slot->data, sizeof(slot->data));
		sl_cpc_ep_push_recv_buf(&data->ep, &slot->buf);
	}

	if (!data->rx_started) {
		k_thread_create(&data->rx_thread, cfg->rx_stack, cfg->rx_stack_size,
				hci_series3_rx_thread, (void *)dev, NULL, NULL,
				K_PRIO_COOP(CONFIG_BT_SILABS_SERIES3_HCI_RX_PRIO), 0, K_NO_WAIT);
		k_thread_name_set(&data->rx_thread, "hci_series3_rx");
		data->rx_started = true;
	}

	k_sem_reset(&data->connected);
	status = sl_cpc_ep_connect(&data->ep, mfd_silabs_ncp_cpc_bus(cfg->parent));
	if (status != SL_STATUS_OK) {
		LOG_ERR("%s: endpoint connect failed: 0x%lx", dev->name, (unsigned long)status);
		sl_cpc_ep_deinit(&data->ep);
		return -EIO;
	}

	if (k_sem_take(&data->connected, HCI_CONNECT_TIMEOUT) != 0) {
		LOG_ERR("%s: endpoint connection timed out", dev->name);
		k_sem_reset(&data->closed);
		sl_cpc_ep_close(&data->ep);
		if (k_sem_take(&data->closed, HCI_CONNECT_TIMEOUT) == 0) {
			sl_cpc_ep_deinit(&data->ep);
		}
		return -ETIMEDOUT;
	}

	data->opened = true;
	LOG_INF("%s: HCI endpoint %u connected", dev->name, cfg->endpoint_id);

	return 0;
}

static int hci_series3_close(const struct device *dev)
{
	struct hci_series3_data *data = dev->data;

	if (!data->opened) {
		return 0;
	}

	data->opened = false;
	k_sem_reset(&data->closed);
	sl_cpc_ep_close(&data->ep);
	if (k_sem_take(&data->closed, HCI_CONNECT_TIMEOUT) != 0) {
		LOG_WRN("%s: endpoint close timed out", dev->name);
		return -ETIMEDOUT;
	}
	sl_cpc_ep_deinit(&data->ep);

	return 0;
}

static int hci_series3_init(const struct device *dev)
{
	struct hci_series3_data *data = dev->data;

	data->dev = dev;
	k_sem_init(&data->connected, 0, 1);
	k_sem_init(&data->closed, 0, 1);
	k_fifo_init(&data->rx_fifo);

	return 0;
}

static DEVICE_API(bt_hci, hci_series3_api) = {
	.open = hci_series3_open,
	.close = hci_series3_close,
	.send = hci_series3_send,
};

#define HCI_SERIES3_DEFINE(inst)                                                                   \
	K_MEM_SLAB_DEFINE_STATIC(hci_series3_tx_slab_##inst, sizeof(struct hci_series3_tx_slot),   \
				 CONFIG_BT_SILABS_SERIES3_HCI_TX_COUNT, SL_CPC_BUF_MIN_ALIGNMENT); \
	static K_KERNEL_STACK_DEFINE(hci_series3_rx_stack_##inst,                                  \
				     CONFIG_BT_SILABS_SERIES3_HCI_RX_STACK_SIZE);                  \
	static struct hci_series3_data hci_series3_data_##inst;                                    \
	static const struct hci_series3_config hci_series3_config_##inst = {                       \
		.common = BT_DT_HCI_DRIVER_CONFIG_INST_GET(inst),                                  \
		.parent = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                     \
		.endpoint_id = DT_INST_PROP(inst, endpoint_id),                                    \
		.tx_slab = &hci_series3_tx_slab_##inst,                                            \
		.rx_stack = hci_series3_rx_stack_##inst,                                           \
		.rx_stack_size = K_KERNEL_STACK_SIZEOF(hci_series3_rx_stack_##inst),               \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, hci_series3_init, NULL, &hci_series3_data_##inst,             \
			      &hci_series3_config_##inst, POST_KERNEL,                             \
			      CONFIG_BT_SILABS_SERIES3_HCI_INIT_PRIORITY, &hci_series3_api);

DT_INST_FOREACH_STATUS_OKAY(HCI_SERIES3_DEFINE)
