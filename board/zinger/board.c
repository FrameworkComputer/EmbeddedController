/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Tiny charger configuration */

#include "common.h"
#include "debug.h"
#include "irq_handler.h"
#include "registers.h"
#include "usb_pd.h"
#include "util.h"

extern void pd_rx_handler(void);

/* External interrupt EXTINT7 for external comparator on PA7 */
void IRQ_HANDLER(STM32_IRQ_EXTI4_15)(void)
{
	/* clear the interrupt */
	STM32_EXTI_PR = STM32_EXTI_PR;
	/* trigger reception handling */
	pd_rx_handler();
}

extern void pd_task(void);

int main(void)
{
	hardware_init();
	debug_printf("Power supply started ...\n");

	/* background loop for PD events */
	pd_task();

	while (1)
		;
}
