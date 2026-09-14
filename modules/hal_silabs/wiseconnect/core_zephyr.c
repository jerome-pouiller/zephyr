/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The SDK normally provides these functions in sl_core_cortexm.c. However, that
 * file relies on em_device.h, which only exists for the Silabs SoCs. So, when
 * the NWP is driven by a third party host, provide an implementation based on
 * the Zephyr API.
 *
 * WiSeConnect distinguishes the ATOMIC sections (which mask the interrupts up
 * to a configurable priority) from the CRITICAL ones (which mask all of them).
 * Zephyr only exposes irq_lock(), so both flavors are mapped on it.
 */

#include <zephyr/irq.h>

#include <sl_core.h>

CORE_irqState_t CORE_EnterAtomic(void)
{
	return irq_lock();
}

void CORE_ExitAtomic(CORE_irqState_t irq_state)
{
	irq_unlock(irq_state);
}

CORE_irqState_t CORE_EnterCritical(void)
{
	return irq_lock();
}

void CORE_ExitCritical(CORE_irqState_t irq_state)
{
	irq_unlock(irq_state);
}
