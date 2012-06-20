/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Functions needed by keyboard scanner module for Chrome EC */

#include "board.h"
#include "keyboard_scan.h"
#include "keyboard_scan_stub.h"
#include "registers.h"


/* Notes:
 *
 * Link proto0 board:
 *
 *   Columns (outputs):
 *      KSO0 - KSO7  = PP0:7
 *      KSO8 - KSO12 = PQ0:4
 *
 *   Rows (inputs):
 *      KSI0 - KSI7  = PN0:7
 *
 *   Other:
 *      PWR_BTN#     = PK7 (handled by gpio module)
 */


static int enable_scanning = 1;  /* Must init to 1 for scanning at boot */


void lm4_set_scanning_enabled(int enabled)
{
	enable_scanning = enabled;
}


int lm4_get_scanning_enabled(void)
{
	return enable_scanning;
}


void lm4_select_column(int col)
{
	if (col == COLUMN_TRI_STATE_ALL || !enable_scanning) {
		/* Tri-state all outputs */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0xff;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0x1f;
	} else if (col == COLUMN_ASSERT_ALL) {
		/* Assert all outputs */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0;
	} else {
		/* Assert a single output */
		LM4_GPIO_DATA(LM4_GPIO_P, 0xff) = 0xff;
		LM4_GPIO_DATA(LM4_GPIO_Q, 0x1f) = 0x1f;
		if (col < 8)
			LM4_GPIO_DATA(LM4_GPIO_P, 1 << col) = 0;
		else
			LM4_GPIO_DATA(LM4_GPIO_Q, 1 << (col - 8)) = 0;
	}
}


uint32_t lm4_clear_matrix_interrupt_status(void)
{
	uint32_t ris = LM4_GPIO_RIS(KB_SCAN_ROW_GPIO);
	LM4_GPIO_ICR(KB_SCAN_ROW_GPIO) = ris;

	return ris;
}


void lm4_enable_matrix_interrupt(void)
{
	LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0xff;   /* 1: enable interrupt */
}


void lm4_disable_matrix_interrupt(void)
{
	LM4_GPIO_IM(KB_SCAN_ROW_GPIO) = 0;      /* 0: disable interrupt */
}


int lm4_read_raw_row_state(void)
{
	return LM4_GPIO_DATA(KB_SCAN_ROW_GPIO, 0xff);
}


void lm4_configure_keyboard_gpio(void)
{
	/* Set column outputs as open-drain; we either pull them low or let
	 * them float high. */
	LM4_GPIO_AFSEL(LM4_GPIO_P) = 0;  /* KSO[7:0] */
	LM4_GPIO_AFSEL(LM4_GPIO_Q) &= ~0x1f;  /* KSO[12:8] */
	LM4_GPIO_DEN(LM4_GPIO_P) = 0xff;
	LM4_GPIO_DEN(LM4_GPIO_Q) |= 0x1f;
	LM4_GPIO_DIR(LM4_GPIO_P) = 0xff;
	LM4_GPIO_DIR(LM4_GPIO_Q) |= 0x1f;
	LM4_GPIO_ODR(LM4_GPIO_P) = 0xff;
	LM4_GPIO_ODR(LM4_GPIO_Q) |= 0x1f;

	/* Set row inputs with pull-up */
	LM4_GPIO_AFSEL(KB_SCAN_ROW_GPIO) &= 0xff;
	LM4_GPIO_DEN(KB_SCAN_ROW_GPIO) |= 0xff;
	LM4_GPIO_DIR(KB_SCAN_ROW_GPIO) = 0;
	LM4_GPIO_PUR(KB_SCAN_ROW_GPIO) = 0xff;
	/* Edge-sensitive on both edges.  Don't enable interrupts yet. */
	LM4_GPIO_IS(KB_SCAN_ROW_GPIO) = 0;
	LM4_GPIO_IBE(KB_SCAN_ROW_GPIO) = 0xff;
}
