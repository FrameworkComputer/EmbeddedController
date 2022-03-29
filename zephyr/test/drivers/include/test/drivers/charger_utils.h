/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_TEST_DRIVERS_INCLUDE_CHARGER_UTILS_H_
#define ZEPHYR_TEST_DRIVERS_INCLUDE_CHARGER_UTILS_H_

#include "charger.h"

/**
 * @brief Get the index of the charger in chg_chips
 *
 * @param charger Pointer to the charger driver.
 * @return The index of the charger if found
 * @return board_get_charger_chip_count() if not found
 */
static inline uint8_t get_charger_num(const struct charger_drv *charger)
{
	const uint8_t chip_count = board_get_charger_chip_count();
	uint8_t chip;

	for (chip = 0; chip < chip_count; ++chip) {
		if (chg_chips[chip].drv == charger)
			return chip;
	}
	return chip;
}

#endif /* ZEPHYR_TEST_DRIVERS_INCLUDE_CHARGER_UTILS_H_ */
