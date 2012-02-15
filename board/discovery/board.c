/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32L Discovery board-specific configuration */

#include "board.h"
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"USER_BUTTON", GPIO_A, (1<<0), GPIO_INT_BOTH, NULL},
	/* Other inputs */
	/* Outputs */
	{"BLUE_LED",    GPIO_B, (1<<6), GPIO_OUT_LOW, NULL},
	{"GREEN_LED",   GPIO_B, (1<<7), GPIO_OUT_LOW, NULL},
};

void configure_board(void)
{
	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

#if CONFIG_CONSOLE_UART == 1
	/* Select Alternate function for USART1 on pins PA9/PA10 */
	STM32L_GPIO_AFRH(A) = (STM32L_GPIO_AFRH(A) & ~0x00000FF0) |
				(0x7 << 8) | (0x7 << 4);
	STM32L_GPIO_MODER(A) = (STM32L_GPIO_MODER(A) & ~0x003C0000) |
				0x00280000;
#elif CONFIG_CONSOLE_UART == 3
	/* Select Alternate function for USART3 on pins PB10/PB11 */
	STM32L_GPIO_AFRH(B) = (STM32L_GPIO_AFRH(B) & ~0x0000FF00) |
				(0x7 << 12) | (0x7 << 8);
	STM32L_GPIO_MODER(B) = (STM32L_GPIO_MODER(B) & ~0x00F00000) |
				0x00A00000;
#endif
}

/**
 * Stubs for non implemented drivers
 * TODO: implement
 */
int jtag_pre_init(void)
{
	/* stop TIM2, TIM3 and watchdogs when the JTAG stops the CPU */
	STM32L_DBGMCU_APB1FZ |= 0x00001803;

	return EC_SUCCESS;
}

int eeprom_init(void)
{
	return EC_SUCCESS;
}

int i2c_init(void)
{
	return EC_SUCCESS;
}

int power_button_init(void)
{
	return EC_SUCCESS;
}

int adc_init(void)
{
	return EC_SUCCESS;
}
