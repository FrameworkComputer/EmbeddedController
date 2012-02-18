/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32L Discovery board-specific configuration */

#include "board.h"
#include "gpio.h"
#include "registers.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"USER_BUTTON", GPIO_A, (1<<0),  GPIO_INT_BOTH, NULL},
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
	{"BLUE_LED",    GPIO_B, (1<<6), GPIO_OUT_LOW, NULL},
	{"GREEN_LED",   GPIO_B, (1<<7), GPIO_OUT_LOW, NULL},
};

void configure_board(void)
{
	/* Enable all GPIOs clocks in normal run-mode, enable keyboard
	 * GPIO clocks in low-power (sleep) mode.
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;
	STM32L_RCC_AHBLPENR |= 0x0e;

#if CONFIG_CONSOLE_UART == 1
	/* Select Alternate function for USART1 on pins PA9/PA10 */
        gpio_set_alternate_function(GPIO_A, (1<<9) | (1<<10), GPIO_ALT_USART);
#elif CONFIG_CONSOLE_UART == 3
	/* Select Alternate function for USART3 on pins PB10/PB11 */
        gpio_set_alternate_function(GPIO_B, (1<<10) | (1<<11), GPIO_ALT_USART);
#endif
}
