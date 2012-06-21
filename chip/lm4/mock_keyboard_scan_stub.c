/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock functions for keyboard scanner module for Chrome EC */

#include "board.h"
#include "console.h"
#include "keyboard_scan.h"
#include "keyboard_scan_stub.h"
#include "task.h"
#include "uart.h"
#include "util.h"

#define MOCK_COLUMN_COUNT 13

static int enable_scanning = 1;
static int selected_column = -1;
static int interrupt_enabled = 0;
static uint8_t matrix_status[MOCK_COLUMN_COUNT];


void lm4_set_scanning_enabled(int enabled)
{
	enable_scanning = enabled;
	uart_printf("%s keyboard scanning\n", enabled ? "Enable" : "Disable");
}


int lm4_get_scanning_enabled(void)
{
	return enable_scanning;
}


void lm4_select_column(int col)
{
	selected_column = col;
}


uint32_t lm4_clear_matrix_interrupt_status(void)
{
	/* Not implemented */
	return 0;
}


void lm4_enable_matrix_interrupt(void)
{
	interrupt_enabled = 1;
}


void lm4_disable_matrix_interrupt(void)
{
	interrupt_enabled = 0;
}


int lm4_read_raw_row_state(void)
{
	if (selected_column >= 0)
		return matrix_status[selected_column];
	else
		return 0;
}


void lm4_configure_keyboard_gpio(void)
{
	/* Init matrix status to release all */
	int i;
	for (i = 0; i < MOCK_COLUMN_COUNT; ++i)
		matrix_status[i] = 0xff;
}


static int command_mock_matrix(int argc, char **argv)
{
	int r, c, p;
	char *e;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	c = strtoi(argv[1], &e, 0);
	if (*e || c < 0 || c >= MOCK_COLUMN_COUNT)
		return EC_ERROR_PARAM1;

	r = strtoi(argv[2], &e, 0);
	if (*e || r < 0 || r >= 8)
		return EC_ERROR_PARAM2;

	p = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	if (p)
		matrix_status[c] &= ~(1 << r);
	else
		matrix_status[c] |= (1 << r);

	if (interrupt_enabled)
		task_wake(TASK_ID_KEYSCAN);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(mockmatrix, command_mock_matrix,
			"<Col> <Row> <0 | 1>",
			"Mock keyboard matrix",
			NULL);
