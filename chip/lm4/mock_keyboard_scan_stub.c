/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock functions for keyboard scanner module for Chrome EC */

#include "common.h"
#include "console.h"
#include "keyboard_config.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "task.h"
#include "uart.h"
#include "util.h"

static int enable_scanning = 1;
static int selected_column = -1;
static int interrupt_enabled = 0;
static uint8_t matrix_status[KEYBOARD_COLS];

void keyboard_raw_init(void)
{
	/* Init matrix status to release all */
	int i;
	for (i = 0; i < KEYBOARD_COLS; ++i)
		matrix_status[i] = 0xff;
}

void keyboard_raw_task_start(void)
{
}

void keyboard_raw_drive_column(int col)
{
	selected_column = col;
}

int keyboard_raw_read_rows(void)
{
	if (selected_column >= 0)
		return matrix_status[selected_column] ^ 0xff;
	else
		return 0;
}

void keyboard_raw_enable_interrupt(int enable)
{
	interrupt_enabled = enable;
}

static int command_mock_matrix(int argc, char **argv)
{
	int r, c, p;
	char *e;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	c = strtoi(argv[1], &e, 0);
	if (*e || c < 0 || c >= KEYBOARD_COLS)
		return EC_ERROR_PARAM1;

	r = strtoi(argv[2], &e, 0);
	if (*e || r < 0 || r >= KEYBOARD_ROWS)
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
