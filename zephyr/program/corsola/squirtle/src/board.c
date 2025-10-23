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
#include "math_util.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/battery.h>

#define STABLE_THRESHOLD 2

/* If the battery is bad, the battery reading will be more frequent. */
#define BAD_DELAY (500 * USEC_PER_MSEC)
#define GOOD_DELAY (30 * USEC_PER_SEC)

LOG_MODULE_REGISTER(board_init, LOG_LEVEL_ERR);

#define SB_AP23A7L 0x00
#define CONFIG_BATTERY_ACTIVATION_TIMEOUT (10 * USEC_PER_SEC)

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
	.t_stable = 10 * USEC_PER_SEC,
	.t_check = 5 * USEC_PER_SEC,
	.is_more_efficient = &squirtle_is_more_efficient,
};

static enum battery_present cached_batt_state = BP_NO;
static enum battery_present raw_state = BP_NO;
static int stable_count;

static void update_battery_state_cache(void);
DECLARE_DEFERRED(update_battery_state_cache);
/*
 * I2C read register to detect battery
 */
static void update_battery_state_cache(void)
{
	int state;
	int rv;
	enum battery_present current_state;

	/*
	 *  According to the battery manufacturer's reply:
	 *  To detect a bad battery, need to read the 0x00 register.
	 *  If the 12th bit(Permanently Failure) is 1, it means a bad battery.
	 */
	rv = sb_read(SB_MANUFACTURER_ACCESS, &state);

	/* If an i2c read exception occurs or PF Bit12 is 1, it is BP_NO. */
	if (rv || (state & BIT(12)))
		current_state = BP_NO;
	else
		current_state = BP_YES;

	/*
	 * Retry is added to avoid misjudgment.
	 */
	if (current_state == raw_state) {
		if (stable_count < STABLE_THRESHOLD)
			stable_count++;
	} else {
		raw_state = current_state;
		stable_count = 1;
	}

	if (stable_count >= STABLE_THRESHOLD)
		cached_batt_state = raw_state;

	hook_call_deferred(&update_battery_state_cache_data,
			   (cached_batt_state == BP_NO) ? BAD_DELAY :
							  GOOD_DELAY);
}
/*
 * I2C reads take 3.5ms. Battery_is_present is called continuously during
 * the boot process, which delays the DUT from loading powerd. To avoid
 * powerd delays, I2C reads are placed in update_battery_state_cache to
 * record register status.
 */
DECLARE_HOOK(HOOK_INIT, update_battery_state_cache, HOOK_PRIO_DEFAULT);

enum battery_present battery_is_present(void)
{
	if (gpio_get_level(GPIO_BATT_PRES_ODL)) {
		return BP_NO;
	}
	return cached_batt_state;
}

static const struct battery_info *batt_info;

int charger_profile_override(struct charge_state_data *curr)
{
	batt_info = battery_get_info();

	if (get_time().val < CONFIG_BATTERY_ACTIVATION_TIMEOUT &&
	    !gpio_get_level(GPIO_BATT_PRES_ODL) &&
	    curr->batt.voltage <= batt_info->voltage_min) {
		int current = 256;

		curr->requested_current = max(curr->requested_current, current);

		return -1;
	}

	return 0;
}

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	return EC_RES_INVALID_PARAM;
}
