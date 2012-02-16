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
        gpio_set_alternate_function(GPIO_A, (1<<9) | (1<<10), GPIO_ALT_USART);
#elif CONFIG_CONSOLE_UART == 3
	/* Select Alternate function for USART3 on pins PB10/PB11 */
        gpio_set_alternate_function(GPIO_B, (1<<10) | (1<<11), GPIO_ALT_USART);
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
