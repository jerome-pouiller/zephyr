/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Work queue shared by the transports of the "silabs,ncp" devices.
 *
 * The transports must not use the system work queue: the users of the
 * co-processor may block on it while waiting for a reply, e.g. bt_enable()
 * with a callback sends HCI commands synchronously from the system work queue.
 */

#include <zephyr/drivers/mfd/silabs_ncp.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

static K_THREAD_STACK_DEFINE(mfd_silabs_ncp_work_q_stack, CONFIG_MFD_SILABS_NCP_WORKQ_STACK_SIZE);
static struct k_work_q mfd_silabs_ncp_wq;

struct k_work_q *mfd_silabs_ncp_work_q(void)
{
	return &mfd_silabs_ncp_wq;
}

static int mfd_silabs_ncp_work_q_init(void)
{
	const struct k_work_queue_config cfg = {
		.name = "silabs_ncp_wq",
	};

	k_work_queue_init(&mfd_silabs_ncp_wq);
	k_work_queue_start(&mfd_silabs_ncp_wq, mfd_silabs_ncp_work_q_stack,
			   K_THREAD_STACK_SIZEOF(mfd_silabs_ncp_work_q_stack),
			   CONFIG_MFD_SILABS_NCP_WORKQ_PRIORITY, &cfg);

	return 0;
}

/* Must run before the transports, which schedule work during their initialization */
SYS_INIT(mfd_silabs_ncp_work_q_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
