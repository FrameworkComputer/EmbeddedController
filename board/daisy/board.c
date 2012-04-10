/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Daisy board-specific configuration */

#include "board.h"
#include "common.h"
#include "dma.h"
#include "gpio.h"
#include "registers.h"
#include "util.h"

/* GPIO interrupt handlers prototypes */
void gaia_power_event(enum gpio_signal signal);

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"EC_PWRON",    GPIO_A, (1<<0),  GPIO_INT_BOTH, gaia_power_event},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, gaia_power_event},
	{"XPSHOLD",     GPIO_A, (1<<11), GPIO_INT_RISING, gaia_power_event},
	{"CHARGER_INT", GPIO_B, (1<<0),  GPIO_INT_RISING, NULL},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, NULL},
	{"KB_COL00",    GPIO_C, (1<<8),  GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL01",    GPIO_C, (1<<9),  GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL02",    GPIO_C, (1<<10), GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL03",    GPIO_C, (1<<11), GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL04",    GPIO_C, (1<<12), GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL05",    GPIO_C, (1<<14), GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL06",    GPIO_C, (1<<15), GPIO_INT_BOTH, matrix_interrupt},
	{"KB_COL07",    GPIO_D, (1<<2),  GPIO_INT_BOTH, matrix_interrupt},
	/* Other inputs */
	/* Outputs */
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<3),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_ACOK",   GPIO_A, (1<<12), GPIO_OUT_HIGH, NULL},
	{"ENTERING_RW", GPIO_B, (1<<1),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_OUT_LOW, NULL},
};

void configure_board(void)
{
	dma_init();

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

	/* Select Alternate function for USART1 on pins PA9/PA10 */
        gpio_set_alternate_function(GPIO_A, (1<<9) | (1<<10), GPIO_ALT_USART);
}
