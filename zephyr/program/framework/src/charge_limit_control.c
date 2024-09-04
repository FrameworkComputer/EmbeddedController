/*
 * Copyright 2024 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/gpio.h>

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "board_adc.h"
#include "board_host_command.h"
#include "board_function.h"
#include "charger.h"
#include "charge_state.h"
#include "console.h"
#include "customized_shared_memory.h"
#include "hooks.h"
#include "host_command.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

static uint8_t charging_maximum_level = EC_CHARGE_LIMIT_RESTORE;

static void battery_percentage_control(void)
{
	enum ec_charge_control_mode new_mode;
	static int in_percentage_control;
	uint32_t batt_os_percentage = get_system_percentage();
	int rv;

	/**
	 * If the host command EC_CMD_CHARGE_CONTROL set control mode to CHARGE_CONTROL_DISCHARGE
	 * or CHARGE_CONTROL_IDLE, ignore the battery_percentage_control();
	 */
	if (!in_percentage_control && get_chg_ctrl_mode() != CHARGE_CONTROL_NORMAL)
		return;

	if (charging_maximum_level == EC_CHARGE_LIMIT_RESTORE)
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);

	if (charging_maximum_level & CHG_LIMIT_OVERRIDE) {
		new_mode = CHARGE_CONTROL_NORMAL;
		if (batt_os_percentage == 1000)
			charging_maximum_level = charging_maximum_level | 0x64;
	} else if (charging_maximum_level < 20)
		new_mode = CHARGE_CONTROL_NORMAL;
	else if (batt_os_percentage > charging_maximum_level * 10) {
		new_mode = CHARGE_CONTROL_DISCHARGE;
		in_percentage_control = 1;
	} else if (batt_os_percentage == charging_maximum_level * 10) {
		new_mode = CHARGE_CONTROL_IDLE;
		in_percentage_control = 1;
	} else {
		new_mode = CHARGE_CONTROL_NORMAL;
		in_percentage_control = 0;
	}

	set_chg_ctrl_mode(new_mode);
#ifdef CONFIG_PLATFORM_EC_CHARGER_DISCHARGE_ON_AC
	rv = charger_discharge_on_ac(new_mode == CHARGE_CONTROL_DISCHARGE);
#endif
	if (rv != EC_SUCCESS)
		CPRINTS("Failed to discharge.");
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
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, 0);
	}

	if (p->modes & CHG_LIMIT_SET_LIMIT) {
		if (p->max_percentage < 20)
			return EC_RES_ERROR;

		charging_maximum_level = p->max_percentage;
		system_set_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, charging_maximum_level);
	}

	if (p->modes & CHG_LIMIT_OVERRIDE)
		charging_maximum_level = charging_maximum_level | CHG_LIMIT_OVERRIDE;

	if (p->modes & CHG_LIMIT_GET_LIMIT) {
		system_get_bbram(SYSTEM_BBRAM_IDX_CHARGE_LIMIT_MAX, &charging_maximum_level);
		r->max_percentage = charging_maximum_level;
		args->response_size = sizeof(*r);
	}

	battery_percentage_control();

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_CHARGE_LIMIT_CONTROL, cmd_charging_limit_control,
			EC_VER_MASK(0));
