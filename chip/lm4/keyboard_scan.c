/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Keyboard scanner module for Chrome EC */

#include "console.h"
#include "keyboard.h"
#include "keyboard_scan.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "uart.h"
#include "util.h"


/* Notes:
 *
 * EVT board:
 *
 *   Columns (outputs):
 *      KSO0 - KSO7  = PP0:7
 *      KSO8 - KSO12 = PQ0:4
 *
 *   Rows (inputs):
 *      KSI0 - KSI7  = PN0:7
 *
 *   Other:
 *      PWR_BTN#     = PK7
 *
 *
 * Hacked board:
 *
 *   Columns (outputs):
 *      KSO0 - KSO7  = PQ0:7
 *      KSO8 - KSO11 = PK0:3
 *      KSO12        = PN2
 *   Rows (inputs):
 *      KSI0 - KSI7  = PH0:7
 *   Other:
 *      PWR_BTN#     = PC5
 */


/* used for select_column() */
enum COLUMN_INDEX {
	COLUMN_ASSERT_ALL = -2,
	COLUMN_TRI_STATE_ALL = -1,
	/* 0 ~ 12 for the corresponding column */
};

#define POLLING_MODE_TIMEOUT 1000000  /* 1 sec */
#define SCAN_LOOP_DELAY 10000         /* 10 ms */

#undef EVT  /* FIXME: define this for EVT board. */

#define KB_COLS 13

static uint8_t raw_state[KB_COLS];

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition */
/* TODO: (crosbug.com/p/7485) fill in real key mask with 0-bits for coords that
   aren't keys */
static const uint8_t actual_key_masks[4][KB_COLS] = {
	{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
	{0},
	{0},
	{0},
	};

/* Drives the specified column low; other columns are tri-stated */
static void select_column(int col)
{
#if defined(EVT)
	if (col == COLUMN_ASSERT_ALL) {
		LM4_GPIO_DIR(LM4_GPIO_P) = 0xff;
		LM4_GPIO_DIR(LM4_GPIO_Q) |= 0x1f;
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0xff) &= ~0x1f;
	} else if (col == COLUMN_TRI_STATE_ALL) {
		LM4_GPIO_DIR(LM4_GPIO_P) &= ~0xff;
		LM4_GPIO_DIR(LM4_GPIO_Q) &= ~0x1f;
	} else if (col < 8) {
		LM4_GPIO_DIR(LM4_GPIO_P) &= ~0xff;
		LM4_GPIO_DIR(LM4_GPIO_Q) &= ~0x1f;
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = ~(1 << col);
		LM4_GPIO_DIR(LM4_GPIO_P) = (1 << col) & 0xff;
	} else {
		LM4_GPIO_DIR(LM4_GPIO_P) &= ~0xff;
		LM4_GPIO_DIR(LM4_GPIO_Q) &= ~0x1f;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0xff) = ~(1 << (col - 8));
		LM4_GPIO_DIR(LM4_GPIO_Q) |= 1 << (col - 8);
	}
#else  /* BDS definition */
	/* Somehow the col 10 and 11 are swapped on bds. */
	if (col == 10) {
		col = 11;
	} else if (col == 11) {
		col = 10;
	}

	if (col == COLUMN_ASSERT_ALL) {
		LM4_GPIO_DIR(LM4_GPIO_Q) = 0xff;
		LM4_GPIO_DIR(LM4_GPIO_K) |= 0x0f;
		LM4_GPIO_DIR(LM4_GPIO_N) |= 0x04;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0xff) = 0;
		LM4_GPIO_DATA(LM4_GPIO_K, 0xff) &= ~0x0f;
		LM4_GPIO_DATA(LM4_GPIO_N, 0xff) &= ~0x04;
	} else if (col == COLUMN_TRI_STATE_ALL) {
		/* All tri-stated */
		LM4_GPIO_DIR(LM4_GPIO_Q) = 0;
		LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x0f;
		LM4_GPIO_DIR(LM4_GPIO_N) &= ~0x04;
	} else if (col < 8) {
		LM4_GPIO_DIR(LM4_GPIO_Q) = 1 << col;
		LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x0f;
		LM4_GPIO_DIR(LM4_GPIO_N) &= ~0x04;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0xff) = ~(1 << col);
	} else if (col < 12) {
		LM4_GPIO_DIR(LM4_GPIO_Q) = 0;
		LM4_GPIO_DIR(LM4_GPIO_K) = (LM4_GPIO_DIR(LM4_GPIO_K) & ~0x0f) |
			(1 << (col - 8));
		LM4_GPIO_DIR(LM4_GPIO_N) &= ~0x04;
		LM4_GPIO_DATA(LM4_GPIO_K, 0x0f) = ~(1 << (col - 8));
	} else {  /* col == 12 */
		LM4_GPIO_DIR(LM4_GPIO_Q) = 0;
		LM4_GPIO_DIR(LM4_GPIO_K) &= ~0x0f;
		LM4_GPIO_DIR(LM4_GPIO_N) |= 0x04;
		LM4_GPIO_DATA(LM4_GPIO_N, 0x04) = ~0x04;
	}
#endif
}

int keyboard_scan_init(void)
{
	volatile uint32_t scratch  __attribute__((unused));
	int i;

        /* Enable GPIOs */
#if defined(EVT)
	/* Enable clock to GPIO modules C,H,K,N,P,Q */
	LM4_SYSTEM_RCGCGPIO |= 0x7284;
#else
	/* Enable clock to GPIO modules C,H,K,N,Q */
	LM4_SYSTEM_RCGCGPIO |= 0x5284;
#endif
	scratch = LM4_SYSTEM_RCGCGPIO;

	/* Clear GPIOAFSEL and enable digital function for PC5, PH0:7,
         * PK0:3, PN2, PQ0:7. */
	LM4_GPIO_AFSEL(LM4_GPIO_C) &= ~0x20;
	LM4_GPIO_DEN(LM4_GPIO_C) |= 0x20;
#if defined(EVT)
	LM4_GPIO_AFSEL(LM4_GPIO_N) &= 0xff;  /* KSI[7:0] */
	LM4_GPIO_DEN(LM4_GPIO_N) |= 0xff;
	LM4_GPIO_AFSEL(LM4_GPIO_P) &= 0xff;  /* KSO[7:0] */
	LM4_GPIO_DEN(LM4_GPIO_P) |= 0xff;
	LM4_GPIO_AFSEL(LM4_GPIO_Q) &= 0x1f;  /* KSO[12:8] */
	LM4_GPIO_DEN(LM4_GPIO_Q) |= 0x1f;
#else
	LM4_GPIO_AFSEL(LM4_GPIO_H) = 0;      /* KSI[7:0] */
	LM4_GPIO_DEN(LM4_GPIO_H) = 0xff;
	LM4_GPIO_AFSEL(LM4_GPIO_K) &= ~0x0f;
	LM4_GPIO_DEN(LM4_GPIO_K) |= 0x0f;
	LM4_GPIO_AFSEL(LM4_GPIO_N) &= ~0x04;
	LM4_GPIO_DEN(LM4_GPIO_N) |= 0x04;
	LM4_GPIO_AFSEL(LM4_GPIO_Q) = 0;
	LM4_GPIO_DEN(LM4_GPIO_Q) = 0xff;
#endif

#if defined(EVT)
	/* Set PN0:7 as inputs with pull-up */
	LM4_GPIO_DIR(LM4_GPIO_N) = 0;
	LM4_GPIO_PUR(LM4_GPIO_N) = 0xff;
#else
	/* Set PH0:7 as inputs with pull-up */
	LM4_GPIO_DIR(LM4_GPIO_H) = 0;
	LM4_GPIO_PUR(LM4_GPIO_H) = 0xff;
#endif

	/* Set PC5 as input with pull-up. */
	/* TODO: no need for pull-up on real circuit, since it'll be
	 * externally pulled up. */
	LM4_GPIO_DIR(LM4_GPIO_C) &= ~0x04;
	LM4_GPIO_PUR(LM4_GPIO_C) |= 0x04;

	/* Tri-state the columns */
	select_column(COLUMN_TRI_STATE_ALL);

	/* Initialize raw state */
	for (i = 0; i < KB_COLS; i++)
		raw_state[i] = 0;

	/* TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly */
	actual_key_mask = actual_key_masks[0];

	return EC_SUCCESS;
}


static uint32_t clear_matrix_interrupt_status(void) {
#if defined(EVT)
	uint32_t port = LM4_GPIO_N;
#else
	uint32_t port = LM4_GPIO_H;
#endif
	uint32_t ris = LM4_GPIO_RIS(port);
	LM4_GPIO_ICR(port) = ris;

	return ris;
}


void wait_for_interrupt(void)
{
	uart_printf("Enter %s() ...\n", __func__);

	/* Assert all outputs would trigger un-wanted interrupts.
	 * Clear them before enable interrupt. */
	select_column(COLUMN_ASSERT_ALL);
	clear_matrix_interrupt_status();

#if defined(EVT)
	LM4_GPIO_IS(LM4_GPIO_N) = 0;      /* 0: edge-sensitive */
	LM4_GPIO_IBE(LM4_GPIO_N) = 0xff;  /* 1: both edge */
	LM4_GPIO_IM(LM4_GPIO_N) = 0xff;   /* 1: enable interrupt */
#else
	LM4_GPIO_IS(LM4_GPIO_H) = 0;      /* 0: edge-sensitive */
	LM4_GPIO_IBE(LM4_GPIO_H) = 0xff;  /* 1: both edge */
	LM4_GPIO_IM(LM4_GPIO_H) = 0xff;   /* 1: enable interrupt */
#endif
}


void enter_polling_mode(void)
{
	uart_printf("Enter %s() ...\n", __func__);
#if defined(EVT)
	LM4_GPIO_IM(LM4_GPIO_N) = 0;  /* 0: disable interrupt */
#else
	LM4_GPIO_IM(LM4_GPIO_H) = 0;  /* 0: disable interrupt */
#endif
	select_column(COLUMN_TRI_STATE_ALL);
}


/* Returns 1 if any key is still pressed. 0 if no key is pressed. */
static int check_keys_changed(void)
{
	int c;
	uint8_t r;
	int change = 0;
	int num_press = 0;

	for (c = 0; c < KB_COLS; c++) {
		/* Select column, then wait a bit for it to settle */
		select_column(c);
		usleep(20);
		/* Read the row state */
#if defined(EVT)
		r = LM4_GPIO_DATA(LM4_GPIO_N, 0xff);
#else
		r = LM4_GPIO_DATA(LM4_GPIO_H, 0xff);
#endif
		/* Invert it so 0=not pressed, 1=pressed */
		r ^= 0xff;
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
				if (prev != now) {
					keyboard_state_changed(i, c, now);
				}
			}
			raw_state[c] = r;
			change = 1;
		}
	}
	select_column(COLUMN_TRI_STATE_ALL);

	if (change) {
		uart_puts("[Keyboard state:");
		for (c = 0; c < KB_COLS; c++) {
			if (raw_state[c]) {
				uart_printf(" %02x", raw_state[c]);
			} else {
				uart_puts(" --");
			}
		}
		uart_puts("]\n");
	}

	/* Count number of key pressed */
	for (c = 0; c < KB_COLS; c++) {
		if (raw_state[c]) ++num_press;
	}

	return num_press ? 1 : 0;
}


void keyboard_scan_task(void)
{
	int key_press_timer = 0;

	keyboard_scan_init();

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



static void matrix_interrupt(void)
{
	uint32_t ris = clear_matrix_interrupt_status();

	if (ris) {
		task_send_msg(TASK_ID_KEYSCAN, TASK_ID_KEYSCAN, 0);
	}
}

#if defined(EVT)
DECLARE_IRQ(LM4_IRQ_GPION, matrix_interrupt, 3);
#else
DECLARE_IRQ(LM4_IRQ_GPIOH, matrix_interrupt, 3);
#endif
