/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "board.h"
#include "gpio.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* Notes:
 *
 * Daisy schematic calls the outputs rows and the inputs columns. The
 * codebase uses the opposite convention.
 *
 * Outputs: Open-drain, pull-up, output '1' --> impedence state (Hi-Z)
 * Inputs: Pull-up
 * Daisy:
 *
 *  Columns (outputs):
 *    KB_ROW00 = PB5
 *    KB_ROW01 = PB8
 *    KB_ROW02:5 = PB12:15
 *    KB_ROW06:8 = PC0:2
 *    KB_ROW09:12 = PC4:7
 *  Rows (inputs):
 *    KB_COL00:04 = PC8:12
 *    KB_COL05:06 = PC14:15
 *    KB_COL07 = PD2
 *  Other:
 *
 *
 * Discovery:
 *
 *  Columns (outputs):
 *    KB_ROW00 = PB5
 *    KB_ROW01 = PB8
 *    KB_ROW02:05 = PB12:15
 *    KB_ROW06:08 = PC0:2
 *    KB_ROW09:10 = PA1:2
 *    KB_ROW11:12 = PC6:7
 *  Rows (inputs):
 *    KB_COL00:04 = PC8:12
 *    KB_COL05:06 = PC14:15
 *    KB_COL07 = PD2
 *  Other:
 *
 * TODO: clean up the nomenclature above; it's weird that KB_ROW00 is a column
 * and KB_COL00 is a row...
 */

extern struct gpio_info gpio_list[];

/* used for select_column() */
enum COL_INDEX {
	COL_ASSERT_ALL = -2,
	COL_TRI_STATE_ALL = -1,
	/* 0 ~ 12 for the corresponding column */
};

#define POLLING_MODE_TIMEOUT 1000000  /* 1 sec */
#define SCAN_LOOP_DELAY 10000         /* 10 ms */

#define KB_COLS 13

/* 15:14, 12:8, 2 */
#define IRQ_MASK 0xdf04

static uint8_t raw_state[KB_COLS];

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition) */
/* TODO: (crosbug.com/p/7485) fill in real key mask with 0-bits for coords that
   aren't keys */
static const uint8_t actual_key_masks[4][KB_COLS] = {
	{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	{0},
	{0},
	{0},
	};

struct kbc_gpio {
	int num;		/* logical row or column number */
	uint32_t port;
	int pin;
};

const struct kbc_gpio kbc_outputs[] = {
	/* Keep this in order of column number */
#if defined(BOARD_daisy)
	{  0, GPIO_B,  5 },	/* KB_ROW00: PB5 */
	{  1, GPIO_B,  8 },	/* KB_ROW01: PB8 */
	{  2, GPIO_B, 12 },	/* KB_ROW02: PB12 */
	{  3, GPIO_B, 13 },	/* KB_ROW03: PB13 */
	{  4, GPIO_B, 14 },	/* KB_ROW04: PB14 */
	{  5, GPIO_B, 15 },	/* KB_ROW05: PB15 */
	{  6, GPIO_C,  0 },	/* KB_ROW06: PC0 */
	{  7, GPIO_C,  1 },	/* KB_ROW07: PC1 */
	{  8, GPIO_C,  2 },	/* KB_ROW08: PC2 */
	{  9, GPIO_C,  4 },	/* KB_ROW09: PC4 */
	{ 10, GPIO_C,  5 },	/* KB_ROW10: PC5 */
	{ 11, GPIO_C,  6 },	/* KB_ROW11: PC6 */
	{ 12, GPIO_C,  7 },	/* KB_ROW12: PC7 */
#elif defined(BOARD_discovery)
	{  0, GPIO_B,  5 },	/* KB_ROW00: PB5 */
	{  1, GPIO_B,  8 },	/* KB_ROW01: PB8 */
	{  2, GPIO_B, 12 },	/* KB_ROW02: PB12 */
	{  3, GPIO_B, 13 },	/* KB_ROW03: PB13 */
	{  4, GPIO_B, 14 },	/* KB_ROW04: PB14 */
	{  5, GPIO_B, 15 },	/* KB_ROW05: PB15 */
	{  6, GPIO_C,  0 },	/* KB_ROW06: PC0 */
	{  7, GPIO_C,  1 },	/* KB_ROW07: PC1 */
	{  8, GPIO_C,  2 },	/* KB_ROW08: PC2 */
	{  9, GPIO_A,  1 },	/* KB_ROW09: PA1 */
	{ 10, GPIO_A,  2 },	/* KB_ROW10: PA2 */
	{ 11, GPIO_C,  6 },	/* KB_ROW11: PC6 */
	{ 12, GPIO_C,  7 },	/* KB_ROW12: PC7 */
#elif defined(BOARD_adv)
	{  0, GPIO_B,  5 },	/* KB_ROW00: PB5 */
	{  1, GPIO_B,  8 },	/* KB_ROW01: PB8 */
	{  2, GPIO_B, 12 },	/* KB_ROW02: PB12 */
	{  3, GPIO_B, 14 },	/* KB_ROW03: PB14 */
	{  4, GPIO_B, 15 },	/* KB_ROW04: PB15 */
	{  5, GPIO_C,  0 },	/* KB_ROW05: PC0 */
	{  6, GPIO_C,  2 },	/* KB_ROW06: PC2 */
	{  7, GPIO_C,  4 },	/* KB_ROW07: PC4 */
	{  8, GPIO_C,  5 },	/* KB_ROW08: PC5 */
	{  9, GPIO_C,  6 },	/* KB_ROW09: PC6 */
	{ 10, GPIO_B, 13 },	/* KB_ROW10: PB13 */
	{ 11, GPIO_C,  1 },	/* KB_ROW11: PC1 */
	{ 12, GPIO_C,  7 },	/* KB_ROW12: PC7 */
#else
#error "Need to define columns (outputs) for this board"
#endif
};

const struct kbc_gpio kbc_inputs[] = {
#if defined(BOARD_daisy) || defined(BOARD_discovery) || defined(BOARD_adv)
	{  0, GPIO_C,  8 },	/* KB_COL00: PC8 */
	{  1, GPIO_C,  9 },	/* KB_COL01: PC9 */
	{  2, GPIO_C, 10 },	/* KB_COL02: PC10 */
	{  3, GPIO_C, 11 },	/* KB_COL03: PC11 */
	{  4, GPIO_C, 12 },	/* KB_COL04: PC12 */
	{  5, GPIO_C, 14 },	/* KB_COL05: PC14 */
	{  6, GPIO_C, 15 },	/* KB_COL06: PC15 */
	{  7, GPIO_D,  2 },	/* KB_COL07: PD2 */
#else
#error "Need to define rows (inputs) for this board"
#endif
};

#if defined(BOARD_daisy) || defined(BOARD_adv)
static const uint32_t ports[] = { GPIO_B, GPIO_C, GPIO_D };
#elif defined(BOARD_discovery)
static const uint32_t ports[] = { GPIO_A, GPIO_B, GPIO_C, GPIO_D };
#else
#error "Need to specify GPIO ports used by keyboard"
#endif

static void select_column(int col)
{
	int i;
	int done = 0;

	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		uint32_t bsrr = 0;
		int j;

		for (j = 0; j < ARRAY_SIZE(kbc_outputs); j++) {
			if (kbc_outputs[j].port != ports[i])
				continue;

			if (col == COL_ASSERT_ALL) {
				/* drive low (clear output data) */
				bsrr |= (1 << (kbc_outputs[j].pin)) << 16;
			} else if (col == COL_TRI_STATE_ALL) {
				/* put column in hi-Z state (set output data) */
				bsrr |= 1 << (kbc_outputs[j].pin);
			} else {
				/* drive specified column low, others => hi-Z */
				if (kbc_outputs[j].num == col) {
					/* to avoid conflict, tri-state all
					 * columns first, then assert column */
					select_column(COL_TRI_STATE_ALL);
					bsrr |= (1 << kbc_outputs[j].pin) << 16;
					done = 1;
					break;
				}
			}
		}

		if (bsrr)
			STM32L_GPIO_BSRR_OFF(ports[i]) = bsrr;

		if (done)
			break;
	}
}


int keyboard_scan_init(void)
{
	int i, j;
	uint32_t tmp32;
	uint16_t tmp16;

	uart_printf("[kbscan %s()] initializing keyboard...\n", __func__);

	/* initialize outputs (pull-up, open-drain)
	 * TODO: this should be done via GPIO declaration in board.c */
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		uint32_t mask32 = 0, mode = 0, pupd = 0;
		uint16_t mask16 = 0, otype = 0;

		for (j = 0; j < ARRAY_SIZE(kbc_outputs); j++) {
			if (kbc_outputs[j].port != ports[i])
				continue;

			mask32 |= 3 << (kbc_outputs[j].pin * 2);
			mask16 |= 1 << kbc_outputs[j].pin;

			/* output mode */
			mode |= 1 << (kbc_outputs[j].pin * 2);

			/* pull-up */
			pupd |= 1 << (kbc_outputs[j].pin * 2);

			/* open-drain */
			otype |= 1 << kbc_outputs[j].pin;
		}

		if (!mask32)	/* nothing to do on this port */
			continue;

		tmp32 = STM32L_GPIO_MODER_OFF(ports[i]);
		tmp32 = (tmp32 & ~mask32) | mode;
		STM32L_GPIO_MODER_OFF(ports[i]) = tmp32;

		tmp32 = STM32L_GPIO_PUPDR_OFF(ports[i]);
		tmp32 = (tmp32 & ~mask32) | pupd;
		STM32L_GPIO_PUPDR_OFF(ports[i]) = tmp32;

		tmp16 = STM32L_GPIO_OTYPER_OFF(ports[i]);
		tmp16 = (tmp16 & ~mask16) | otype;
		STM32L_GPIO_OTYPER_OFF(ports[i]) = tmp16;
	}

	/* Tri-state (put into Hi-Z) the outputs */
	select_column(COL_TRI_STATE_ALL);

	/* initialize inputs
	 * TODO: this should be done via GPIO declaration in board.c */
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		uint32_t mask32 = 0, pupd = 0;

		for (j = 0; j < ARRAY_SIZE(kbc_inputs); j++) {
			if (kbc_inputs[j].port != ports[i])
				continue;

			mask32 |= 3 << (kbc_inputs[j].pin * 2);

			/* pull-up */
			pupd |= 1 << (kbc_inputs[j].pin * 2);
		}

		if (!mask32)
			continue;	/* nothing to do on this port */

		STM32L_GPIO_MODER_OFF(ports[i]) &= ~mask32;

		tmp32 = STM32L_GPIO_PUPDR_OFF(ports[i]);
		tmp32 = (tmp32 & ~mask32) | pupd;
		STM32L_GPIO_PUPDR_OFF(ports[i]) = tmp32;
	}

	/* Initialize raw state */
	for (i = 0; i < KB_COLS; i++)
		raw_state[i] = 0;

	/* TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly */
	actual_key_mask = actual_key_masks[0];

	return EC_SUCCESS;
}


void wait_for_interrupt(void)
{
	uint32_t pr_before, pr_after;

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	pr_before = STM32L_EXTI_PR;
	select_column(COL_ASSERT_ALL);
	pr_after = STM32L_EXTI_PR;
	STM32L_EXTI_PR |= ((pr_after & ~pr_before) & IRQ_MASK);

	STM32L_EXTI_IMR |= IRQ_MASK;	/* 1: unmask interrupt */
}


void enter_polling_mode(void)
{
	STM32L_EXTI_IMR &= ~IRQ_MASK;	/* 0: mask interrupts */
	select_column(COL_TRI_STATE_ALL);
}


/* Returns 1 if any key is still pressed. 0 if no key is pressed. */
static int check_keys_changed(void)
{
	int c;
	uint8_t r;
	int change = 0;
	int num_press = 0;

	for (c = 0; c < KB_COLS; c++) {
		uint16_t tmp;

		/* Select column, then wait a bit for it to settle */
		select_column(c);
		udelay(100);

		r = 0;
#if defined(BOARD_daisy) || defined(BOARD_discovery) || defined(BOARD_adv)
		tmp = STM32L_GPIO_IDR(C);
		/* KB_COL00:04 = PC8:12 */
		if (tmp & (1 << 8))
			r |= 1 << 0;
		if (tmp & (1 << 9))
			r |= 1 << 1;
		if (tmp & (1 << 10))
			r |= 1 << 2;
		if (tmp & (1 << 11))
			r |= 1 << 3;
		if (tmp & (1 << 12))
			r |= 1 << 4;
		/* KB_COL05:06 = PC14:15 */
		if (tmp & (1 << 14))
			r |= 1 << 5;
		if (tmp & (1 << 15))
			r |= 1 << 6;

		tmp = STM32L_GPIO_IDR(D);
		/* KB_COL07 = PD2 */
		if (tmp & (1 << 2))
			r |= 1 << 7;

		/* Invert it so 0=not pressed, 1=pressed */
		r ^= 0xff;
#else
#error "Key scanning unsupported on this board"
#endif
		/* Mask off keys that don't exist so they never show
		 * as pressed */
		r &= actual_key_mask[c];

#ifdef OR_WITH_CURRENT_STATE_FOR_TESTING
		/* KLUDGE - or current state in, so we can make sure
		 * all the lines are hooked up */
		r |= raw_state[c];
#endif

		/* Check for changes */
		if (r != raw_state[c]) {
			int i;
			for (i = 0; i < 8; ++i) {
				uint8_t prev = (raw_state[c] >> i) & 1;
				uint8_t now = (r >> i) & 1;
				if (prev != now)
					/* TODO: implement this */
					; //keyboard_state_changed(i, c, now);
			}
			raw_state[c] = r;
			change = 1;
		}
	}
	select_column(COL_TRI_STATE_ALL);

	/* Count number of key pressed */
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state[c])
			++num_press;
	}

	if (change) {
		uart_printf("[%d keys pressed: ", num_press);
		for (c = 0; c < KB_COLS; c++) {
			if (raw_state[c])
				uart_printf(" %02x", raw_state[c]);
			else
				uart_puts(" --");
		}
		uart_puts("]\n");
	}

	return num_press ? 1 : 0;
}


void keyboard_scan_task(void)
{
	int key_press_timer = 0;

	/* Enable interrupts for keyboard rows */
	gpio_enable_interrupt(KB_COL00);
	gpio_enable_interrupt(KB_COL01);
	gpio_enable_interrupt(KB_COL02);
	gpio_enable_interrupt(KB_COL03);
	gpio_enable_interrupt(KB_COL04);
	gpio_enable_interrupt(KB_COL05);
	gpio_enable_interrupt(KB_COL06);
	gpio_enable_interrupt(KB_COL07);

	while (1) {
		wait_for_interrupt();
		task_wait_msg(-1);

		enter_polling_mode();
		/* Busy polling keyboard state. */
		while (1) {
			/* sleep for debounce. */
			usleep(SCAN_LOOP_DELAY);
			/* Check for keys down */
			if (check_keys_changed()) {
				key_press_timer = 0;
			} else {
				if (++key_press_timer >=
				    (POLLING_MODE_TIMEOUT / SCAN_LOOP_DELAY)) {
					key_press_timer = 0;
					break;  /* exit the while loop */
				}
			}
		}
		/* TODO: (crosbug.com/p/7484) A race condition here.
		 *       If a key state is changed here (before interrupt is
		 *       enabled), it will be lost.
		 */
	}
}


void matrix_interrupt(enum gpio_signal signal)
{
	task_send_msg(TASK_ID_KEYSCAN, TASK_ID_KEYSCAN, 0);
}

int keyboard_has_char()
{
	/* TODO: needs to be implemented */
	return 0;
}

void keyboard_put_char(uint8_t chr, int send_irq)
{
	/* TODO: needs to be implemented */
}
