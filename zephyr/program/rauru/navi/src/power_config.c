/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "dps.h"
#include "hooks.h"
#include "math_util.h"
#include "power.h"
#include "usb_pd.h"
#include "util.h"

#include <dt-bindings/battery.h>

#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)

bool navi_is_more_efficient(int curr_mv, int prev_mv, int batt_mv, int batt_mw,
			    int input_mw)
{
	int batt_state;

	battery_status(&batt_state);

	/* Choose 15V PDO or higher when battery is full. */
	if ((batt_state & SB_STATUS_FULLY_CHARGED) && (curr_mv >= 15000) &&
	    (prev_mv < 15000 || curr_mv <= prev_mv)) {
		return true;
	} else {
		return false;
	}
}

__override struct dps_config_t dps_config = {
	.k_less_pwr = 26,
	.k_more_pwr = 50,
	.k_sample = 1,
	.k_window = 3,
	.t_stable = 10 * USEC_PER_SEC,
	.t_check = 5 * USEC_PER_SEC,
	.is_more_efficient = &navi_is_more_efficient,
};
