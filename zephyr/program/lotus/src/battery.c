/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "board_host_command.h"
#include "charger.h"
#include "charge_state.h"
#include "console.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

static uint8_t charging_maximum_level = EC_CHARGE_LIMIT_RESTORE;

enum battery_present battery_is_present(void)
{
	enum battery_present batt_pres = BP_NO;
	char text[32];
	static int retry;

	/*
	 * EC does not connect to the battery present pin,
	 * add the workaround to read the battery device name (register 0x21).
	 */

	if (battery_device_name(text, sizeof(text))) {
		if (retry++ > 3) {
			batt_pres = BP_NO;
			retry = 0;
		}
	} else {
		batt_pres = BP_YES;
		retry = 0;
	}

	return batt_pres;
}

static void battery_percentage_control(void)
{
	enum ec_charge_control_mode new_mode;
	int rv;

	/**
	 * TODO: After BBRAM function is enabled, restore the charging maximum level
	 * from BBRAM.
	 * if (charging_maximum_level == EC_CHARGE_LIMIT_RESTORE)
	 *	system_get_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, &charging_maximum_level);
	 */

	if (charging_maximum_level & CHG_LIMIT_OVERRIDE) {
		new_mode = CHARGE_CONTROL_NORMAL;
		if (charge_get_percent() == 100)
			charging_maximum_level = charging_maximum_level | 0x64;
	} else if (charging_maximum_level < 20)
		new_mode = CHARGE_CONTROL_NORMAL;
	else if (charge_get_percent() > charging_maximum_level)
		new_mode = CHARGE_CONTROL_DISCHARGE;
	else if (charge_get_percent() == charging_maximum_level)
		new_mode = CHARGE_CONTROL_IDLE;
	else
		new_mode = CHARGE_CONTROL_NORMAL;

	ccprints("Charge Limit mode = %d", new_mode);

	set_chg_ctrl_mode(new_mode);
#ifdef CONFIG_PLATFORM_EC_CHARGER_DISCHARGE_ON_AC
	rv = charger_discharge_on_ac(new_mode == CHARGE_CONTROL_DISCHARGE);
#endif
	if (rv != EC_SUCCESS)
		ccprintf("fail to discharge.");
}
DECLARE_HOOK(HOOK_AC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, battery_percentage_control, HOOK_PRIO_DEFAULT);

/*****************************************************************************/
/* Host command */

static enum ec_status cmd_charging_limit_control(struct host_cmd_handler_args *args)
{

	const struct ec_params_ec_chg_limit_control *p = args->params;
	struct ec_response_chg_limit_control *r = args->response;

	if (p->modes & CHG_LIMIT_DISABLE) {
		charging_maximum_level = 0;
		/**
		 * TODO: After BBRAM function is enabled, save the charging maximum level
		 * into BBRAM.
		 * system_set_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, 0);
		 */
	}

	if (p->modes & CHG_LIMIT_SET_LIMIT) {
		if( p->max_percentage < 20 )
			return EC_RES_ERROR;

		charging_maximum_level = p->max_percentage;
		/**
		 * TODO: After BBRAM function is enabled, save the charging maximum level
		 * into BBRAM.
		 * system_set_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, charging_maximum_level);
		 */
	}

	if (p->modes & CHG_LIMIT_OVERRIDE)
		charging_maximum_level = charging_maximum_level | CHG_LIMIT_OVERRIDE;

	if (p->modes & CHG_LIMIT_GET_LIMIT) {
		/**
		 * TODO: After BBRAM function is enabled, restore the charging maximum level
		 * from BBRAM.
		 * system_get_bbram(SYSTEM_BBRAM_IDX_CHG_MAX, &charging_maximum_level);
		 */
		r->max_percentage = charging_maximum_level;
		args->response_size = sizeof(*r);
	}

	battery_percentage_control();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_LIMIT_CONTROL, cmd_charging_limit_control,
			EC_VER_MASK(0));
