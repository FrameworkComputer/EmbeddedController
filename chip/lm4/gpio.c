/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "gpio.h"
#include "registers.h"


int gpio_pre_init(void)
{
	/* Enable clock to GPIO block A */
	LM4_SYSTEM_RCGCGPIO |= 0x0001;

	/* Turn off the LED before we make it an output */
	gpio_set(EC_GPIO_DEBUG_LED, 0);

	/* Clear GPIOAFSEL bits for block A pin 7 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~(0x80);

	/* Set GPIO to digital enable, output */
	LM4_GPIO_DEN(LM4_GPIO_A) |= 0x80;
	LM4_GPIO_DIR(LM4_GPIO_A) |= 0x80;

	return EC_SUCCESS;
}


int gpio_init(void)
{
	return EC_SUCCESS;
}


int gpio_get(enum gpio_signal signal, int *value_ptr)
{
	switch (signal) {
	case EC_GPIO_DEBUG_LED:
		*value_ptr = (LM4_GPIO_DATA(LM4_GPIO_A, 0x80) & 0x80 ? 1 : 0);
		return EC_SUCCESS;
	default:
		return EC_ERROR_UNKNOWN;
	}
}


int gpio_set(enum gpio_signal signal, int value)
{
	switch (signal) {
	case EC_GPIO_DEBUG_LED:
		LM4_GPIO_DATA(LM4_GPIO_A, 0x80) = (value ? 0x80 : 0);
		return EC_SUCCESS;
	default:
		return EC_ERROR_UNKNOWN;
	}
}
