/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver for TI BQ4050 family, including BQ40Z50 (and -R1, -R2),
 * BQ40Z552, and BQ40Z60.
 */

#include "battery_smart.h"
#include "util.h"

#include <stdint.h>

int battery_bq4050_imbalance_mv(void)
{
	/*
	 * The BQ4050 family can manage up to four cells.  In testing it always
	 * returns a voltage for each cell, regardless of the number of cells
	 * actually installed in the pack.  Unpopulated cells read exactly zero.
	 */
	static const uint8_t cell_voltage_address[4] = {
		0x3c, 0x3d, 0x3e, 0x3f
	};
	int i, res, cell_voltage;
	int n_cells = 0;
	int max_voltage = 0;
	int min_voltage = 0xffff;

	for (i = 0; i != ARRAY_SIZE(cell_voltage_address); ++i) {
		res = sb_read(cell_voltage_address[i], &cell_voltage);
		if (res == EC_SUCCESS && cell_voltage != 0) {
			n_cells++;
			max_voltage = MAX(max_voltage, cell_voltage);
			min_voltage = MIN(min_voltage, cell_voltage);
		}
	}
	return (n_cells == 0) ? 0 : max_voltage - min_voltage;
}

