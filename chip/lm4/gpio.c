/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* GPIO module for Chrome EC */

#include "gpio.h"
#include "power_button.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


struct gpio_info {
	int port;   /* Port (LM4_GPIO_*) */
	int mask;   /* Bitmask on that port (0x01 - 0x80) */
	void (*irq_handler)(enum gpio_signal signal);
};


const struct gpio_info signal_info[EC_GPIO_COUNT] = {
	{LM4_GPIO_A, 0x80, NULL},                     /* DEBUG_LED */
	{LM4_GPIO_C, 0x20, power_button_interrupt},   /* POWER_BUTTON */
	{LM4_GPIO_C, 0x00, NULL},                     /* POWER_BUTTON_OUT */
	{LM4_GPIO_D, 0x01, power_button_interrupt},   /* LID_SWITCH */
	{LM4_GPIO_D, 0x00, NULL},                     /* LID_SWITCH_OUT */
};


int gpio_pre_init(void)
{
	/* Enable clock to GPIO block A */
	LM4_SYSTEM_RCGCGPIO |= 0x0001;

	/* Turn off the LED before we make it an output */
	gpio_set_level(EC_GPIO_DEBUG_LED, 0);

	/* Clear GPIOAFSEL bits for block A pin 7 */
	LM4_GPIO_AFSEL(LM4_GPIO_A) &= ~(0x80);

	/* Set GPIO to digital enable, output */
	LM4_GPIO_DEN(LM4_GPIO_A) |= 0x80;
	LM4_GPIO_DIR(LM4_GPIO_A) |= 0x80;

#ifdef BOARD_link
	/* Set up LID switch input (block K pin 5) */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~(0xf00000);
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~(0x20);
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x20;

	/* Block F pin 0 is NMI pin, so we have to unlock GPIO Lock register and
	   set the bit in GPIOCR register first. */
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x4c4f434b;
	LM4_GPIO_CR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_LOCK(LM4_GPIO_F) = 0x0;

	/* Set up LID switch output (block F pin 0) */
	LM4_GPIO_PCTL(LM4_GPIO_F) &= ~(0xf);
	LM4_GPIO_DATA(LM4_GPIO_F, 0x1) =
		(LM4_GPIO_DATA(LM4_GPIO_K, 0x20) ? 1 : 0);
	LM4_GPIO_DIR(LM4_GPIO_F) |= 0x1;
	LM4_GPIO_DEN(LM4_GPIO_F) |= 0x1;
#endif

	/* Setup power button input and output pins */
#ifdef BOARD_link
	/* input: PK7 */
	LM4_GPIO_PCTL(LM4_GPIO_K) &= ~0xf0000000;
	LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x80;
	LM4_GPIO_PUR(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IM(LM4_GPIO_K) |= 0x80;
	LM4_GPIO_IBE(LM4_GPIO_K) |= 0x80;
	/* output: PG7 */
	LM4_GPIO_PCTL(LM4_GPIO_G) &= ~0xf0000000;
	LM4_GPIO_DATA(LM4_GPIO_G, 0x80) = 0x80;
	LM4_GPIO_DIR(LM4_GPIO_G) |= 0x80;
	LM4_GPIO_DEN(LM4_GPIO_G) |= 0x80;
#else
	/* input: PC5 */
	LM4_GPIO_PCTL(LM4_GPIO_C) &= ~0x00f00000;
	LM4_GPIO_DIR(LM4_GPIO_C) &= ~0x20;
	LM4_GPIO_PUR(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_DEN(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IM(LM4_GPIO_C) |= 0x20;
	LM4_GPIO_IBE(LM4_GPIO_C) |= 0x20;
#endif

	return EC_SUCCESS;
}


int gpio_get_level(enum gpio_signal signal)
{
	return LM4_GPIO_DATA(signal_info[signal].port,
			     signal_info[signal].mask) ? 1 : 0;
}


int gpio_set_level(enum gpio_signal signal, int value)
{
	/* Ok to write 0xff becuase LM4_GPIO_DATA bit-masks only the bit
	 * we care about. */
	LM4_GPIO_DATA(signal_info[signal].port,
		      signal_info[signal].mask) = (value ? 0xff : 0);
	return EC_SUCCESS;
}


static void gpio_interrupt(int port, uint32_t mis)
{
	int i = 0;
	const struct gpio_info *g = signal_info;

	for (i = 0; i < EC_GPIO_COUNT; i++, g++) {
		if (port == g->port && (mis & g->mask) && g->irq_handler)
			g->irq_handler(i);
	}
}


static void __gpio_c_interrupt(void)
{
	uint32_t mis = LM4_GPIO_MIS(LM4_GPIO_C);

	/* Clear the interrupt bits we received */
	LM4_GPIO_ICR(LM4_GPIO_C) = mis;

	gpio_interrupt(LM4_GPIO_C, mis);
}
DECLARE_IRQ(LM4_IRQ_GPIOC, __gpio_c_interrupt, 1);


int gpio_init(void)
{
	return EC_SUCCESS;
}
