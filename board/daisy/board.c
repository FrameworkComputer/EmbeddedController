/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Daisy board-specific configuration */

#include "board.h"
#include "common.h"
#include "gpio.h"
#include "registers.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"EC_PWRON",    GPIO_A, (1<<0),  GPIO_INT_BOTH, NULL},
	{"PP1800_LDO2", GPIO_A, (1<<1),  GPIO_INT_BOTH, NULL},
	{"XPSHOLD",     GPIO_A, (1<<11), GPIO_INT_RISING, NULL},
	{"CHARGER_INT", GPIO_B, (1<<0),  GPIO_INT_RISING, NULL},
	{"EC_INT",      GPIO_B, (1<<9),  GPIO_INT_RISING, NULL},
	{"LID_OPEN",    GPIO_C, (1<<13), GPIO_INT_BOTH, NULL},
	/* Other inputs */
	/* Outputs */
	{"EN_PP1350",   GPIO_A, (1<<2),  GPIO_OUT_LOW, NULL},
	{"EN_PP5000",   GPIO_A, (1<<3),  GPIO_OUT_LOW, NULL},
	{"EN_PP3300",   GPIO_A, (1<<8),  GPIO_OUT_LOW, NULL},
	{"PMIC_ACOK",   GPIO_A, (1<<12), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW", GPIO_B, (1<<1),  GPIO_OUT_LOW, NULL},
	{"CHARGER_EN",  GPIO_B, (1<<2),  GPIO_OUT_LOW, NULL},
};

void configure_board(void)
{
	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

	/* Select Alternate function for USART1 on pins PA9/PA10 */
	STM32L_GPIO_AFRH(A) = (STM32L_GPIO_AFRH(A) & ~0x00000FF0) |
				(0x7 << 8) | (0x7 << 4);
	STM32L_GPIO_MODER(A) = (STM32L_GPIO_MODER(A) & ~0x003C0000) |
				0x00280000;
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
