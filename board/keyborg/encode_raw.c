/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Raw touch data recording */

#include "common.h"
#include "debug.h"
#include "touch_scan.h"
#include "util.h"

#define ENC_COL_COUNT 70
static uint8_t encoded[ENC_COL_COUNT][ROW_COUNT * 2];
static int encoded_col;

void encode_reset(void)
{
	encoded_col = 0;
}

void encode_add_column(const uint8_t *dptr)
{
	if (encoded_col >= ENC_COL_COUNT)
		return;
	memcpy(encoded[encoded_col], dptr, ROW_COUNT * 2);
	encoded_col++;
}

void encode_dump_matrix(void)
{
	int row, col;

#ifdef CONFIG_ENCODE_DUMP_PYTHON
	debug_printf("heat_map = [");
	for (row = 0; row < ROW_COUNT * 2; ++row) {
		debug_printf("[");
		for (col = 0; col < encoded_col; ++col) {
			if (encoded[col][row] < THRESHOLD)
				debug_printf("0,");
			else
				debug_printf("%d,", encoded[col][row]);
		}
		debug_printf("],\n");
	}
	debug_printf("]\n");
#else
	for (row = 0; row < ROW_COUNT * 2; ++row) {
		for (col = 0; col < encoded_col; ++col) {
			if (encoded[col][row] < THRESHOLD)
				debug_printf("  - ");
			else
				debug_printf("%3d ", encoded[col][row]);
		}
		debug_printf("\n");
	}
#endif
}
