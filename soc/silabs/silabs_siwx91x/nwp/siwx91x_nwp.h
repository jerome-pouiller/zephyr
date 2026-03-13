/*
 * Copyright (c) 2025 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef SIWX91X_NWP_H
#define SIWX91X_NWP_H

#include <zephyr/device.h>
#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/net/wifi.h>
#include "sl_wifi.h"

struct net_buf;

/* The NWP don't have same memory mapping */
#define SIWX91X_NWP_MEMORY_OFFSET_ADDRESS 0x00500000

/* ta_status */
#define SIWX91X_NWP_WIFI_BUFFER_FULL BIT(0) /* SLI_WIFI_BUFFER_FULL Wifi buffer full indication register value from NWP module */
#define SIWX91X_NWP_BUFFER_EMPTY     BIT(1) /* SLI_BUFFER_EMPTY     Wi-Fi buffer empty indication register value from NWP module */
#define SIWX91X_NWP_RX_PKT_PENDING   BIT(3) /* SLI_RX_PKT_PENDING   RX packet pending register value from NWP module */
#define SIWX91X_NWP_BLE_BUFFER_FULL  BIT(4) /* SLI_BLE_BUFFER_FULL  BLE buffer full indication register value from NWP module */
#define SIWX91X_NWP_ASSERT_INTR      BIT(7) /* SLI_ASSERT_INTR      Assertion Interrupt indication from NWP module */

/* siw91x has some cache consistency problem when one part access the memory byte per byte while the
 * other part access using a word or a double word. This is usally not a problem. However, gcc
 * suppose members of __packed structures are unaligned and acces them byte per byte.
 * Therefore always use __packed with __aligned(4).
 */
struct siwx91x_nwp_dma_desc {
	uint8_t *addr;
	uint16_t length;
} __packed __aligned(4);

struct siwx91x_nwp_ta_regs {
	volatile uint32_t reserved0;                   /* 0x00 */
	volatile uint32_t status;                      /* 0x04: Status of the TA (HOST_INTR_STATUS_REG) */
	volatile uint32_t reserved1[11];               /* 0x08-0x30 */
	volatile struct siwx91x_nwp_dma_desc *dma_desc_tx; /* 0x34: TX DMA descriptor address. Also used as Input register for the bootloader (M4_TX_DMA_DESC_REG and SI91X_INTERFACE_IN_REGISTER)  */
	volatile uint32_t reserved2;                   /* 0x38 */
	volatile uint32_t bootloader_out;              /* 0x3C Output register for bootloader (SI91X_INTERFACE_OUT_REGISTER) */
	volatile uint32_t reserved3[7];                /* 0x40-0x58 */
	volatile struct siwx91x_nwp_dma_desc *dma_desc_rx; /* 0x5C: RX DMA descriptor address (M4_RX_DMA_DESC_REG) */
	volatile uint32_t reserved4[11];               /* 0x60-0x88 */
	volatile uint32_t ta_int_set;                  /* 0x8C: Set TA interrupt (TASS_P2P_INTR_SET_REG). Reserved for the TA, never used */
	volatile uint32_t ta_int_clr;                  /* 0x90: Clear TA interrupt (TASS_P2P_INTR_CLR_REG). Identical to ta_int, but on NWP side. Used as a workaround for bug on ta_int */
} __packed __aligned(4);

/*  M4 to TA interrupt bits (m4_int_*) */
#define SIWX91X_RX_BUFFER_VALID          BIT(1)  /* RX_BUFFER_VALID */
#define SIWX91X_TX_PKT_PENDING           BIT(2)  /* TX_PKT_PENDING_INTERRUPT */
#define SIWX91X_PROGRAM_COMMON_FLASH     BIT(4)  /* PROGRAM_COMMON_FLASH*/
#define SIWX91X_UPGRADE_M4_IMAGE         BIT(5)  /* UPGRADE_M4_IMAGE */
#define SIWX91X_M4_WAITING_FOR_TA_FLASH  BIT(6)  /* M4_WAITING_FOR_TA_TO_WR_ON_FLASH */
#define SIWX91X_M4_WAITING_FOR_TA_DEINIT BIT(8)  /* M4_WAITING_FOR_TA_DEINIT */

/* Status register bits */
#define SIWX91X_M4_WAKEUP_TA             BIT(0)  /* M4_wakeup_TA or M4_WAKEUP_TA */
#define SIWX91X_M4_IS_ACTIVE             BIT(1)  /* M4_is_active */
#define SIWX91X_TA_WAKEUP_M4             BIT(2)  /* TA_wakeup_M4 */
#define SIWX91X_TA_IS_ACTIVE             BIT(3)  /* TA_is_active or TA_IS_ACTIVE */

/* TA to M4 interrupt bits (ta_int) */
#define SIWX91X_RX_PKT_DONE              BIT(1)  /* RX_PKT_TRANSFER_DONE_INTERRUPT */
#define SIWX91X_TX_PKT_DONE              BIT(2)  /* TX_PKT_TRANSFER_DONE_INTERRUPT */
#define SIWX91X_TA_USING_FLASH           BIT(3)  /* TA_USING_FLASH (a status rather than an interrupt) */
#define SIWX91X_M4_IMAGE_UPGRADATION     BIT(4)  /* M4_IMAGE_UPGRADATION_PENDING_INTERRUPT */
#define SIWX91X_TA_WRITING_ON_FLASH      BIT(5)  /* TA_WRITING_ON_COMM_FLASH */
#define SIWX91X_SIDE_BAND_CRYPTO_DONE    BIT(6)  /* SIDE_BAND_CRYPTO_DONE */
#define SIWX91X_NWP_DEINIT               BIT(7)  /* NWP_DEINIT_IN_COMM_FLASH */
#define SIWX91X_TA_BUFFER_FULL_CLEAR     BIT(8)  /* TA_RSI_BUFFER_FULL_CLEAR_EVENT */
#define SIWX91X_TURN_ON_XTAL_REQUEST     BIT(9)  /* TURN_ON_XTAL_REQUEST */
#define SIWX91X_TURN_OFF_XTAL_REQUEST    BIT(10) /* TURN_OFF_XTAL_REQUEST */
#define SIWX91X_M4_IS_USING_XTAL_REQUEST BIT(11) /* M4_IS_USING_XTAL_REQUEST */

struct siwx91x_nwp_m4_regs {
	volatile uint32_t reserved0[3];           /* 0x00-0x08 */
	volatile uint32_t ctrl;                   /* 0x0C: Misc config host control, unused */
	volatile uint32_t reserved1[87];          /* 0x10-0x168 */
	volatile uint32_t m4_int_set;             /* 0x16C: M4 interrupt set (M4SS_P2P_INTR_SET_REG) */
	volatile uint32_t m4_int_clr;             /* 0x170: M4 interrupt clear (M4SS_P2P_INTR_CLR_REG) */
	volatile uint32_t status;                 /* 0x174: Status register (P2P_STATUS_REG) */
	volatile uint32_t ta_int_mask_set;        /* 0x178: Mask TA interrupts (set) (TASS_P2P_INTR_MASK_SET) */
	volatile uint32_t ta_int_mask_clr;        /* 0x17C: Mask TA interrupts (clear) (TASS_P2P_INTR_MASK_CLR) */
	volatile uint32_t ta_int;                 /* 0x180: TA to M4 interrupt status (write should clear the interrupt, but it is buggy, use ta_int_clr instead) (TASS_P2P_INTR_CLEAR) */
} __packed __aligned(4);

struct siwx91x_nwp_config {
	const struct pinctrl_dev_config *pcfg;
	void (*config_irq)(const struct device *dev);
	struct siwx91x_nwp_ta_regs *ta_regs;
	struct siwx91x_nwp_m4_regs *m4_regs;

	uint8_t antenna_selection;
	bool support_1p8v;
	bool enable_xtal_correction;
	bool qspi_80mhz_clk;
	uint32_t clock_frequency;

	struct net_buf_pool *rx_pool;
	struct net_buf_pool *tx_pool;
	k_thread_stack_t *thread_stack;
	size_t thread_stack_size;
	int thread_priority;
};

/* The wifi/bt drivers are expected to define the list of callback in a wider structrure and retirve
 * it using CONTAINER_OF():
 *
 *  struct siwx91x_wifi_context {
 *     struct device *dev;
 *     struct siwx91x_nwp_wifi_cb cb;
 *  } = {
 *     .dev = dev,
 *     .cb.on_rx = siwx91x_wifi_rx,
 *  };
 *
 *  void siwx91x_wifi_rx(struct siwx91x_nwp_wifi_cb *arg, struct net_buf *buf) {
 *     struct siwx91x_wifi_context *ctxt = CONTAINER_OF(arg, struct siwx91x_wifi_context, cb);
 *     ...
 *  }
 *
 */
struct siwx91x_nwp_wifi_cb {
	void (*on_rx)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_scan_results)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_join)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_sta_connect)(const struct siwx91x_nwp_wifi_cb *, uint8_t remote_addr[WIFI_MAC_ADDR_LEN]);
	void (*on_sta_disconnect)(const struct siwx91x_nwp_wifi_cb *, uint8_t remote_addr[WIFI_MAC_ADDR_LEN]);

	void (*on_sock_select)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_sock_terminate)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_sock_recv)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
	void (*on_sock_tcp_ack)(const struct siwx91x_nwp_wifi_cb *, struct net_buf *);
};

struct siwx91x_nwp_bt_cb {
	void (*on_rx)(const struct siwx91x_nwp_bt_cb *, uint8_t type, void *payload, size_t len);
};

struct siwx91x_nwp_cmd_queue {
	int id;
	struct k_fifo tx_queue;
	/* tx_queue can have several asynchronous frames queued, but only one synchronous frame.
	 * On Wifi, this is not a big deal since data (async) frames are sent on another queue. In
	 * the case multiple threads want to send commands, the thread priorities will impact the
	 * transmission order.
	 */
	struct k_sem sync_frame_in_queue;
	/* Keep a pointer to the current sync frame. So we can match the reply */
	struct net_buf *tx_in_progress;
	/* Unlock the emiter of the current sync frame */
	struct k_sem tx_done;
};

struct siwx91x_nwp_data {
	uint8_t power_profile;
	char current_country_code[2];
	const struct siwx91x_nwp_bt_cb *bt;
	const struct siwx91x_nwp_wifi_cb *wifi;

	struct k_sem global_lock;
	struct k_sem firmware_ready;

	struct k_thread thread_id;
	struct k_sem refresh_queues_state;
	struct k_sem tx_data_complete;
	struct k_sem rx_data_complete;
	struct siwx91x_nwp_cmd_queue cmd_queues[3];
	struct siwx91x_nwp_dma_desc tx_desc[2];
	struct siwx91x_nwp_dma_desc rx_desc[2];
	struct net_buf *rx_buf_in_progress;
	struct net_buf *tx_buf_in_progress;
};

struct siwx91x_frame_desc {
	uint16_t length_and_queue;
	uint16_t command;
	/* Varies with frames:
	 *  [5]  = RSSI
	 *  [6]  = channel (1-14)
	 *  [8]  = frame status (MSB) (on replies)
	 *  [9]  = frame status (LSB) (on replies)
	 *  [8] = channel 32bits ?
	 *  [9] = channel 32bits ?
	 *  [10] = channel 32bits ?
	 *  [11] = channel 32bits ?
	 */
	uint8_t  reserved[12];
	uint8_t  data[];
} __packed __aligned(4);
BUILD_ASSERT(sizeof(struct siwx91x_frame_desc) == 16, "wrong frame description definition");

#define SIWX91X_MAX_PAYLOAD_SIZE (1600 + 16)

void siwx91x_nwp_register_wifi(const struct device *dev, const struct siwx91x_nwp_wifi_cb *val);
void siwx91x_nwp_register_bt(const struct device *dev, const struct siwx91x_nwp_bt_cb *val);
int siwx91x_nwp_reset(const struct device *dev, uint8_t oper_mode, bool hidden_ssid,
		      uint8_t max_num_sta);
int siwx91x_nwp_apply_power_profile(const struct device *dev);

#endif
