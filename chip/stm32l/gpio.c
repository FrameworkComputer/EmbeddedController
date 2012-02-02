/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "registers.h"
#include "task.h"

/* Signal information from board.c.  Must match order from enum gpio_signal. */
extern const struct gpio_info gpio_list[GPIO_COUNT];


int gpio_pre_init(void)
{
	const struct gpio_info *g = gpio_list;
	int i;

	/* Enable all GPIOs clocks
	 * TODO: more fine-grained enabling for power saving
	 */
	STM32L_RCC_AHBENR |= 0x3f;

	/* Set all GPIOs to defaults */
	for (i = 0; i < GPIO_COUNT; i++, g++) {
		/* bitmask for registers with 2 bits per GPIO pin */
		uint32_t mask2 = (g->mask * g->mask) | (g->mask * g->mask * 2);

		if (g->flags & GPIO_OUTPUT) {
			/* Set pin level */
			gpio_set_level(i, g->flags & GPIO_HIGH);
			/* General purpose output : MODE = 01 */
			STM32L_GPIO_MODER_OFF(g->port) |= 0x55555555 & mask2;
		} else {
			/* Input */
			STM32L_GPIO_MODER_OFF(g->port) &= ~mask2;
			if (g->flags & GPIO_PULL) {
				/* With pull up/down */
				if (g->flags & GPIO_HIGH) /* Pull Up = 01 */
					STM32L_GPIO_PUPDR_OFF(g->port) |=
							0x55555555 & mask2;
				else /* Pull Down = 10 */
					STM32L_GPIO_PUPDR_OFF(g->port) |=
							0xaaaaaaaa & mask2;
			}
		}
	}

	return EC_SUCCESS;
}


int gpio_get_level(enum gpio_signal signal)
{
	return !!(STM32L_GPIO_IDR_OFF(gpio_list[signal].port) &
		  gpio_list[signal].mask);
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	STM32L_GPIO_BSRR_OFF(gpio_list[signal].port) =
			gpio_list[signal].mask << (value ? 0 : 16);

	return EC_SUCCESS;
}
