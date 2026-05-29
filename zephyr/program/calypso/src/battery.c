/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mensa battery-specific configuration */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "hooks.h"

LOG_MODULE_REGISTER(mensa_battery, LOG_LEVEL_INF);

void poll_battery_info(void);
DECLARE_DEFERRED(poll_battery_info);

/*
 * Deferred task that periodically polls the battery for dynamic information
 * (like SOC, voltage, current) while the chipset is in the HARD_OFF state.
 * It re-arms itself every CHARGE_POLL_PERIOD_CHARGE until the chipset leaves
 * this state.
 */
void poll_battery_info(void)
{
	/* Exit polling loop if chipset is no longer off */
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
		return;

	/* Keep polling as long as the chipset is off */
	hook_call_deferred(&poll_battery_info_data, CHARGE_POLL_PERIOD_CHARGE);

	/* Update dynamic battery information if battery is present */
	if (battery_is_present() == BP_YES)
		battery_poll_dynamic_info();
}

/*
 * Hook registered to run when the chipset enters the HARD_OFF state.
 * It ensures an immediate battery SOC check and kicks off the periodic
 * polling task.
 */
void board_chipset_hard_off(void)
{
	/* Force a check for battery SOC change upon entering HARD_OFF */
	hook_notify(HOOK_BATTERY_SOC_CHANGE);
	/* Start the periodic polling loop */
	hook_call_deferred(&poll_battery_info_data, CHARGE_POLL_PERIOD_CHARGE);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, board_chipset_hard_off, HOOK_PRIO_DEFAULT);

static void board_battery_init(void)
{
	/* Cache the battery static information */
	if (battery_is_present() == BP_YES) {
		update_static_battery_info();
		LOG_INF("battery static information cached");
	}
}
DECLARE_HOOK(HOOK_INIT, board_battery_init, HOOK_PRIO_POST_BATTERY_INIT);

void board_chipset_pre_init(void)
{
	/* Cache the battery dynamic information before AP power on */
	if (battery_is_present() == BP_YES) {
		battery_poll_dynamic_info();
		LOG_INF("battery dynamic information cached");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_PRE_INIT, board_chipset_pre_init, HOOK_PRIO_DEFAULT);

#if defined(CONFIG_PLATFORM_EC_BATTERY_ACCESS_LIMIT)
enum battery_access_type battery_check_access_limit(void)
{
	if (!chipset_in_state(CHIPSET_STATE_HARD_OFF)) {
		LOG_INF("battery access not allowed when chipset on");
		return BATTERY_ACCESS_NOT_ALLOWED;
	}

	return BATTERY_ACCESS_ALLOWED;
}
#endif

void board_battery_compensate_params(struct batt_params *batt)
{
	/* Update display SOC based on current state_of_charge (multiply by 10)
	 */
	if (!(batt->flags & BATT_FLAG_BAD_STATE_OF_CHARGE)) {
		batt->display_charge = batt->state_of_charge * 10;
		if (batt->display_charge < 0)
			batt->display_charge = 0;
		if (batt->display_charge > 1000)
			batt->display_charge = 1000;
	}
}
