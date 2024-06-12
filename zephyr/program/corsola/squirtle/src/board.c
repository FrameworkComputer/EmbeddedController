/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "dps.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/battery.h>

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define SB_AP23A7L 0x00

bool squirtle_is_more_efficient(int curr_mv, int prev_mv, int batt_mv,
				int batt_mw, int input_mw)
{
	int batt_state;

	battery_status(&batt_state);

	/* Choose 15V PDO or higher when battery is full. */
	if ((batt_state & SB_STATUS_FULLY_CHARGED) && (curr_mv >= 15000) &&
	    (prev_mv < 15000 || curr_mv <= prev_mv)) {
		return true;
	} else {
		return ABS(curr_mv - batt_mv) < ABS(prev_mv - batt_mv);
	}
}

__override struct dps_config_t dps_config = {
	.k_less_pwr = 93,
	.k_more_pwr = 96,
	.k_sample = 1,
	.k_window = 3,
	.t_stable = 10 * SECOND,
	.t_check = 5 * SECOND,
	.is_more_efficient = &squirtle_is_more_efficient,
};

enum battery_present battery_is_present(void)
{
	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];

	if (gpio_get_level(GPIO_BATT_PRES_ODL))
		return BP_NO;

	if (!strcasecmp(bs->model_ext, "AP23A7L")) {
		uint8_t state[4];
		uint8_t page = 0x54;

		/*
		 * According to the battery manufacturer's reply:
		 * To detect a bad battery, need to write 0x54 to the
		 * 0x00 register and then read the 0x23. If the 12th
		 * bit(Permanently Failure) is 1, it means a bad battery.
		 */
		sb_write_block(SB_AP23A7L, &page, 1);

		if (sb_read_sized_block(SB_MANUFACTURER_DATA, state, 4))
			return BP_NO;

		/* Detect the 12th bit value */
		if (state[1] & BIT(4))
			return BP_NO;
	} else if (!strcasecmp(bs->model_ext, "AP23A8L")) {
		int value;

		/*
		 * According to the battery manufacturer's reply:
		 * To detect a bad battery, need to read the 0x43 register.
		 * If the second bit(Permanently Failure) is 1, it means a bad
		 * battery.
		 */
		if (sb_read(SB_PACK_STATUS, &value))
			return BP_NO;

		if (value & BIT(2))
			return BP_NO;
	} else if (!strcasecmp(bs->model_ext, ""))
		return BP_NO;

	return BP_YES;
}
