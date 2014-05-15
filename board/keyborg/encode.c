/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Touch data encoding/decoding */

/*
 * This removes the "whitespace" (i.e. cells below the threshold) and
 * group the remaining active cells into "segments". By only storing
 * the segments, we can fit a single frame in RAM in most cases.
 */

#include "common.h"
#include "debug.h"
#include "touch_scan.h"
#include "util.h"

#define BUF_SIZE 6000
static uint8_t encoded[BUF_SIZE];
static int encoded_size;

void encode_reset(void)
{
	/* Just clear the encoded data */
	encoded_size = 0;
}

void encode_add_column(const uint8_t *dptr)
{
	uint8_t *seg_count_ptr;
	int p, p_start;
	uint8_t *eptr = encoded + encoded_size, *e_seg_size;

	seg_count_ptr = eptr;
	eptr++;

	*seg_count_ptr = 0;
	p = 0;
	while (p < ROW_COUNT * 2) {
		if (dptr[p] < THRESHOLD) {
			++p;
			continue;
		}

		/* Give up on overflow */
		if (eptr + 2 >= encoded + BUF_SIZE)
			return;

		/* Save current position */
		*(eptr++) = p;

		/* Leave a byte for storing segment size */
		e_seg_size = eptr;
		eptr++;

		/* Record segment starting point */
		p_start = p;

		/* Save the segment */
		while (p < ROW_COUNT * 2 && dptr[p] >= THRESHOLD) {
			if (eptr >= encoded + BUF_SIZE)
				return;
			*(eptr++) = dptr[p++];
		}

		/* Fill in the segment size now that we know it */
		*e_seg_size = p - p_start;

		(*seg_count_ptr)++;
	}

	/* Update encoded data size now that we're sure it fits */
	encoded_size = eptr - encoded;
}

void encode_dump_matrix(void)
{
	uint8_t *dptr;
	int row, col;
	int seg_count;
	int seg;
	int seg_end;

	debug_printf("Encoded size = %d\n", encoded_size);

	dptr = encoded;
	for (col = 0; col < COL_COUNT * 2; ++col) {
		if (dptr >= encoded + encoded_size) {
			for (row = 0; row < ROW_COUNT * 2; ++row)
				debug_printf("  - ");
			debug_printf("\n");
			continue;
		}
		seg_count = *(dptr++);
		row = 0;
		for (seg = 0; seg < seg_count; ++seg) {
			while (row < *dptr) {
				debug_printf("  - ");
				row++;
			}
			dptr++;
			seg_end = *dptr + row;
			dptr++;
			for (; row < seg_end; ++row, ++dptr)
				debug_printf("%3d ", *dptr);
		}
		while (row < ROW_COUNT * 2) {
			debug_printf("  - ");
			row++;
		}
		debug_printf("\n");
	}
}
