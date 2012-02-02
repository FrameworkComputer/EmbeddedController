/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* STM32L Discovery board-specific configuration */

#include "board.h"
#include "common.h"
#include "registers.h"

void configure_board(void)
{
	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

	/* Select Alternate function for USART3 on pins PB10/PB11 */
	STM32L_GPIO_AFRH(B) = (STM32L_GPIO_AFRH(B) & ~0x0000FF00) |
				(0x7 << 12) | (0x7 << 8);
	STM32L_GPIO_MODER(B) = (STM32L_GPIO_MODER(B) & ~0x00F00000) |
				0x00A00000;

	/* Green and blue LEDs : configure port 6 and 7 as output */
	STM32L_GPIO_MODER(B) |= (1 << (7 * 2)) | (1 << (6 * 2));
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

int gpio_pre_init(void)
{
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
