/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charger.h"
#include "charge_state.h"
#include <zephyr/devicetree.h>
#include "charger/chg_bq25710.h"
#include "charger/chg_isl923x.h"
#include "charger/chg_isl9241.h"
#include "charger/chg_rt9490.h"
#include "charger/chg_sm5803.h"
#include "hooks.h"

LOG_MODULE_DECLARE(ap_pwrseq, LOG_LEVEL_INF);

#define CHG_CHIP(id, fn) [DT_REG_ADDR(DT_PARENT(id))] = fn(id)

/* Charger chips */
#ifndef CONFIG_PLATFORM_EC_CHARGER_RUNTIME_CONFIG
const struct charger_config_t chg_chips[] = {
#else
struct charger_config_t chg_chips[] = {
#endif
	DT_FOREACH_STATUS_OKAY_VARGS(BQ25710_CHG_COMPAT, CHG_CHIP,
				     CHG_CONFIG_BQ25710)
		DT_FOREACH_STATUS_OKAY_VARGS(ISL923X_CHG_COMPAT, CHG_CHIP,
					     CHG_CONFIG_ISL923X)
			DT_FOREACH_STATUS_OKAY_VARGS(ISL9241_CHG_COMPAT,
						     CHG_CHIP,
						     CHG_CONFIG_ISL9241)
				DT_FOREACH_STATUS_OKAY_VARGS(RT9490_CHG_COMPAT,
							     CHG_CHIP,
							     CHG_CONFIG_RT9490)
					DT_FOREACH_STATUS_OKAY_VARGS(
						SM5803_CHG_COMPAT, CHG_CHIP,
						CHG_CONFIG_SM5803)
};

#ifdef CONFIG_PLATFORM_EC_CHARGER_SINGLE_CHIP
BUILD_ASSERT(ARRAY_SIZE(chg_chips) == 1,
	     "For the CHARGER_SINGLE_CHIP config, the number of defined charger "
	     "chips must equal 1.");
#else
BUILD_ASSERT(
	ARRAY_SIZE(chg_chips) == CONFIG_USB_PD_PORT_MAX_COUNT,
	"For the OCPC config, the number of defined charger chips must equal "
	"the number of USB-C ports.");
#endif

#if defined(CONFIG_AP_PWRSEQ)

bool ap_power_is_ok_to_power_up(void)
{
	return !charge_prevent_power_on(false) && !charge_want_shutdown();
}

static void hook_battery_soc_change(void)
{
	static bool charger_ok_to_power_up;
	bool cur_charger_ok_to_power_up;

	cur_charger_ok_to_power_up = ap_power_is_ok_to_power_up();
	if (charger_ok_to_power_up != cur_charger_ok_to_power_up) {
		LOG_INF("Battery is %s to boot AP!",
			cur_charger_ok_to_power_up ? "READY" : "NOT READY");
		charger_ok_to_power_up = cur_charger_ok_to_power_up;
		if (cur_charger_ok_to_power_up) {
			/* Charger is Ready, power up the AP */
			chipset_exit_hard_off();
		}
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, hook_battery_soc_change,
	     HOOK_PRIO_DEFAULT);
#endif
