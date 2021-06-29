/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "driver/bc12/mt6360.h"

/* SD Card */
int board_regulator_get_info(uint32_t index, char *name,
			     uint16_t *num_voltages, uint16_t *voltages_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_get_info(id, name, num_voltages,
					 voltages_mv);
}

int board_regulator_enable(uint32_t index, uint8_t enable)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_enable(id, enable);
}

int board_regulator_is_enabled(uint32_t index, uint8_t *enabled)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_is_enabled(id, enabled);
}

int board_regulator_set_voltage(uint32_t index, uint32_t min_mv,
				uint32_t max_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_set_voltage(id, min_mv, max_mv);
}

int board_regulator_get_voltage(uint32_t index, uint32_t *voltage_mv)
{
	enum mt6360_regulator_id id = index;

	return mt6360_regulator_get_voltage(id, voltage_mv);
}
