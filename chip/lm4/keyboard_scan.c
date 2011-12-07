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

#define KB_COLS 13

/* Notes:
 *
 * Columns (outputs):
 *    KSO0 - KSO7  = PQ0:7
 *    KSO8 - KSO11 = PK0:3
 *    KSO12        = PN2
 * Rows (inputs):
 *    KSI0 - KSI7  = PH0:7
 * Other:
 *    PWR_BTN#     = PC5
 */

static uint8_t raw_state[KB_COLS];

/* Mask with 1 bits only for keys that actually exist */
static const uint8_t *actual_key_mask;

/* All actual key masks (todo: move to keyboard matrix definition */
/* TODO: fill in real key mask with 0-bits for coords that aren't keys */
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
	if (col < 0) {
		LM4_GPIO_DIR(P) &= ~0xff;
		LM4_GPIO_DIR(Q) &= ~0x1f;
	} else if (col < 8) {
		LM4_GPIO_DIR(P) &= ~0xff;
		LM4_GPIO_DIR(Q) &= ~0x1f;
		LM4_GPIO_DATA_BITS(P, 0xff << 2) = ~(1 << col);
		LM4_GPIO_DIR(P) = (1 << col) & 0xff;
	} else {
		LM4_GPIO_DIR(P) &= ~0xff;
		LM4_GPIO_DIR(Q) &= ~0x1f;
		LM4_GPIO_DATA_BITS(Q, 0xff << 2) = ~(1 << (col - 8));
		LM4_GPIO_DIR(Q) |= 1 << (col - 8);
	}
#else  /* BDS definition */
	/* Somehow the col 10 and 11 are swapped on bds. */
	if (col == 10) {
		col = 11;
	} else if (col == 11) {
		col = 10;
	}

	if (col < 0) {
		/* All tri-stated */
		LM4_GPIO_DIR(Q) = 0;
		LM4_GPIO_DIR(K) &= ~0x0f;
		LM4_GPIO_DIR(N) &= ~0x04;
	} else if (col < 8) {
		LM4_GPIO_DIR(Q) = 1 << col;
		LM4_GPIO_DIR(K) &= ~0x0f;
		LM4_GPIO_DIR(N) &= ~0x04;
		LM4_GPIO_DATA_BITS(Q, 0xff << 2) = ~(1 << col);
	} else if (col < 12) {
		LM4_GPIO_DIR(Q) = 0;
		LM4_GPIO_DIR(K) = (LM4_GPIO_DIR(K) & ~0x0f) | (1 << (col - 8));
		LM4_GPIO_DIR(N) &= ~0x04;
		LM4_GPIO_DATA_BITS(K, 0x0f << 2) = ~(1 << (col - 8));
	} else {  /* col == 12 */
		LM4_GPIO_DIR(Q) = 0;
		LM4_GPIO_DIR(K) &= ~0x0f;
		LM4_GPIO_DIR(N) |= 0x04;
		LM4_GPIO_DATA_BITS(N, 0x04 << 2) = ~0x04;
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
	LM4_GPIO_AFSEL(C) &= ~0x20;
	LM4_GPIO_DEN(C) |= 0x20;
#if defined(EVT)
	LM4_GPIO_AFSEL(N) &= 0xff;  /* KSI[7:0] */
	LM4_GPIO_DEN(N) |= 0xff;
	LM4_GPIO_AFSEL(P) &= 0xff;  /* KSO[7:0] */
	LM4_GPIO_DEN(P) |= 0xff;
	LM4_GPIO_AFSEL(Q) &= 0x1f;  /* KSO[12:8] */
	LM4_GPIO_DEN(Q) |= 0x1f;
#else
	LM4_GPIO_AFSEL(H) = 0;
	LM4_GPIO_DEN(H) = 0xff;
	LM4_GPIO_AFSEL(K) &= ~0x0f;
	LM4_GPIO_DEN(K) |= 0x0f;
	LM4_GPIO_AFSEL(N) &= ~0x04;
	LM4_GPIO_DEN(N) |= 0x04;
	LM4_GPIO_AFSEL(Q) = 0;
	LM4_GPIO_DEN(Q) = 0xff;
#endif

#if defined(EVT)
	/* Set PN0:7 as inputs with pull-up */
	LM4_GPIO_DIR(N) = 0;
	LM4_GPIO_PUR(N) = 0xff;
#else
	/* Set PH0:7 as inputs with pull-up */
	LM4_GPIO_DIR(H) = 0;
	LM4_GPIO_PUR(H) = 0xff;
#endif

	/* Set PC5 as input with pull-up. */
	/* TODO: no need for pull-up on real circuit, since it'll be
	 * externally pulled up. */
	LM4_GPIO_DIR(C) &= ~0x04;
	LM4_GPIO_PUR(C) |= 0x04;

	/* Tri-state the columns */
	select_column(-1);

	/* Initialize raw state */
	for (i = 0; i < KB_COLS; i++)
		raw_state[i] = 0;

	/* TODO: method to set which keyboard we have, so we set the actual
	 * key mask properly */
	actual_key_mask = actual_key_masks[0];

	return EC_SUCCESS;
}


void check_keys_down(void)
{
	int c;
	uint8_t r;
	int change = 0;

	for (c = 0; c < KB_COLS; c++) {
		/* Select column, then wait a bit for it to settle */
		select_column(c);
		usleep(20);
		/* Read the row state */
#if defined(EVT)
		r = LM4_GPIO_DATA_BITS(N, 0xff << 2);
#else
		r = LM4_GPIO_DATA_BITS(H, 0xff << 2);
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
	select_column(-1);

	if (change) {
		uart_puts("[Keyboard state:");
		for (c = 0; c < KB_COLS; c++) {
			if (raw_state[c])
				uart_printf(" %02x", raw_state[c]);
			else
				uart_puts(" --");
		}
		uart_puts("]\n");
	}
}


void keyboard_scan_task(void)
{
	keyboard_scan_init();

	while (1) {
		/* Sleep for a while */
		usleep(25000);
		/* Check for keys down */
		check_keys_down();
	}
}
