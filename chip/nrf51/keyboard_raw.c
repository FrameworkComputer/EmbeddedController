/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Raw keyboard I/O layer for STM32
 *
 * To make this code portable, we rely heavily on looping over the keyboard
 * input and output entries in the board's gpio_list[]. Each set of inputs or
 * outputs must be listed in consecutive, increasing order so that scan loops
 * can iterate beginning at KB_IN00 or KB_OUT00 for however many GPIOs are
 * utilized (KEYBOARD_ROWS or KEYBOARD_COLS).
 */

#include "gpio.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"
#include "util.h"

/* Mask of external interrupts on input lines */
static unsigned int irq_mask;

static const uint32_t kb_out_ports[] = { KB_OUT_PORT_LIST };

static void set_irq_mask(void)
{
	int i;

	for (i = GPIO_KB_IN00; i < GPIO_KB_IN00 + KEYBOARD_ROWS; i++)
		irq_mask |= gpio_list[i].mask;
}

void keyboard_raw_init(void)
{
	/* Determine EXTI_PR mask to use for the board */
	set_irq_mask();

	/* Ensure interrupts are disabled in EXTI_PR */
	keyboard_raw_enable_interrupt(0);
}

void keyboard_raw_task_start(void)
{
	/* Enable interrupts for keyboard matrix inputs */
	gpio_enable_interrupt(GPIO_KB_IN00);
	gpio_enable_interrupt(GPIO_KB_IN01);
	gpio_enable_interrupt(GPIO_KB_IN02);
	gpio_enable_interrupt(GPIO_KB_IN03);
	gpio_enable_interrupt(GPIO_KB_IN04);
	gpio_enable_interrupt(GPIO_KB_IN05);
	gpio_enable_interrupt(GPIO_KB_IN06);
	gpio_enable_interrupt(GPIO_KB_IN07);
}

test_mockable void keyboard_raw_drive_column(int out)
{
	int i, done = 0;

	for (i = 0; i < ARRAY_SIZE(kb_out_ports); i++) {
		uint32_t bsrr = 0;
		int j;

		for (j = GPIO_KB_OUT00; j <= GPIO_KB_OUT12; j++) {
			if (gpio_list[j].port != kb_out_ports[i])
				continue;

			if (out == KEYBOARD_COLUMN_ALL) {
				/* drive low (clear bit) */
				bsrr |= gpio_list[j].mask << 16;
			} else if (out == KEYBOARD_COLUMN_NONE) {
				/* put output in hi-Z state (set bit) */
				bsrr |= gpio_list[j].mask;
			} else if (j - GPIO_KB_OUT00 == out) {
				/*
				 * Drive specified output low, others => hi-Z.
				 *
				 * To avoid conflict, tri-state all outputs
				 * first, then assert specified output.
				 */
				keyboard_raw_drive_column(KEYBOARD_COLUMN_NONE);
				bsrr |= gpio_list[j].mask << 16;
				done = 1;
				break;
			}
		}

	#ifdef CONFIG_KEYBOARD_COL2_INVERTED
		if (bsrr & (gpio_list[GPIO_KB_OUT02].mask << 16 |
				 gpio_list[GPIO_KB_OUT02].mask))
			bsrr ^= (gpio_list[GPIO_KB_OUT02].mask << 16 |
				 gpio_list[GPIO_KB_OUT02].mask);
	#endif

		if (bsrr)
			STM32_GPIO_BSRR(kb_out_ports[i]) = bsrr;

		if (done)
			break;
	}
}

test_mockable int keyboard_raw_read_rows(void)
{
	int i;
	unsigned int port, prev_port = 0;
	int state = 0;
	uint16_t port_val = 0;

	for (i = 0; i < KEYBOARD_ROWS; i++) {
		port = gpio_list[GPIO_KB_IN00 + i].port;
		if (port != prev_port) {
			port_val = STM32_GPIO_IDR(port);
			prev_port = port;
		}

		if (port_val & gpio_list[GPIO_KB_IN00 + i].mask)
			state |= 1 << i;
	}

	/* Invert it so 0=not pressed, 1=pressed */
	return state ^ 0xff;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		/*
		 * Assert all outputs would trigger un-wanted interrupts.
		 * Clear them before enable interrupt.
		 */
		STM32_EXTI_PR |= irq_mask;
		STM32_EXTI_IMR |= irq_mask;	/* 1: unmask interrupt */
	} else {
		STM32_EXTI_IMR &= ~irq_mask;	/* 0: mask interrupts */
	}
}

void keyboard_raw_gpio_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_KEYSCAN);
}
