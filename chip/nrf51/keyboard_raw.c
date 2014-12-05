/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Raw keyboard I/O layer for nRF51
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

/* Mask of output pins for driving. */
static unsigned int col_mask;

void keyboard_raw_init(void)
{
	int i;

	/* Initialize col_mask */
	col_mask = 0;
	for (i = 0; i < KEYBOARD_COLS; i++)
		col_mask |= gpio_list[GPIO_KB_OUT00 + i].mask;

	/* Ensure interrupts are disabled */
	keyboard_raw_enable_interrupt(0);
}

void keyboard_raw_task_start(void)
{
	/*
	 * Enable the interrupt for keyboard matrix inputs.
	 * One is enough, since they are shared.
	 */
	gpio_enable_interrupt(GPIO_KB_IN00);
}

test_mockable void keyboard_raw_drive_column(int out)
{
	/* tri-state all first */
	NRF51_GPIO0_OUTSET = col_mask;

	/* drive low for specified pin(s) */
	if (out == KEYBOARD_COLUMN_ALL)
		NRF51_GPIO0_OUTCLR = col_mask;
	else if (out != KEYBOARD_COLUMN_NONE)
		NRF51_GPIO0_OUTCLR = gpio_list[GPIO_KB_OUT00 + out].mask;
}

test_mockable int keyboard_raw_read_rows(void)
{
	int i;
	int state = 0;

	for (i = 0; i < KEYBOARD_ROWS; i++) {
		if (NRF51_GPIO0_IN & gpio_list[GPIO_KB_IN00 + i].mask)
			state |= 1 << i;
	}

	/* Invert it so 0=not pressed, 1=pressed */
	return state ^ 0xff;
}

void keyboard_raw_enable_interrupt(int enable)
{
	if (enable) {
		/*
		 * Clear the PORT event before enabling the interrupt.
		 */
		NRF51_GPIOTE_PORT = 0;
		NRF51_GPIOTE_INTENSET = 1 << NRF51_GPIOTE_PORT_BIT;
	} else {
		NRF51_GPIOTE_INTENCLR = 1 << NRF51_GPIOTE_PORT_BIT;
	}
}

void keyboard_raw_gpio_interrupt(enum gpio_signal signal)
{
	task_wake(TASK_ID_KEYSCAN);
}
