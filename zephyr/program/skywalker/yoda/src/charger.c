/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "driver/charger/bq25710.h"
#include "hooks.h"
#include "util.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charger, LOG_LEVEL_INF);

enum battery_cells {
	BATT_UNKNOWN = 0,
	BATT_2_CELLS = 2,
	BATT_3_CELLS = 3,
};

static const char *p2s_battery_models[] = {
	"l26n2pk1", // MODEL_L26N2PK1
	"l26b2pk1", // MODEL_L26B2PK1
	"l26x2pk1", // MODEL_L26X2PK1
	"l26m2pk1", // MODEL_L26M2PK1
	"l26d2pk1", // MODEL_L26D2PK1
};

static enum battery_cells pre_battery_cells = BATT_UNKNOWN;

int pd_get_usb_pd_3a_ports(void)
{
	return (pre_battery_cells == BATT_2_CELLS) ? 0 : 1;
}

static enum battery_cells get_battery_cells(const char *model)
{
	for (int i = 0; i < ARRAY_SIZE(p2s_battery_models); i++) {
		int len = strlen(p2s_battery_models[i]);
		if (strncasecmp(model, p2s_battery_models[i], len) == 0) {
			return BATT_2_CELLS;
		}
	}
	return BATT_3_CELLS;
}

static int write_reg(int reg, int val)
{
	return bq257x0_set_option_reg(charge_get_active_chg_chip(), reg, val);
}

static int set_chg_reg_custom(enum battery_cells battery_cell)
{
	int ret = 0;
	switch (battery_cell) {
	case BATT_2_CELLS: {
		// Address: 0x37, Voltage: 6.0 V
		ret |= write_reg(BQ25720_REG_VMIN_ACTIVE_PROTECTION, 0x0070);
		// Address: 0x3E, Voltage: 6.6 V
		ret |= write_reg(BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x4200);
		break;
	}
	case BATT_3_CELLS: {
		// Address: 0x37, Voltage: 9.0 V
		ret |= write_reg(BQ25720_REG_VMIN_ACTIVE_PROTECTION, 0x00e8);
		// Address: 0x3E, Voltage: 9.2 V
		ret |= write_reg(BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x5c00);
		break;
	}
	default:
		ret |= EC_ERROR_UNKNOWN;
		LOG_ERR("Detected invalid battery cell: %d", battery_cell);
		break;
	}
	return ret;
}

void battery_policy(void)
{
	if (battery_is_present() != BP_YES) {
		pre_battery_cells = BATT_UNKNOWN;
		LOG_ERR("Battery not present, cells=UNKNOWN");
		return;
	}
	char batt_model_ext[SBS_MAX_STR_OBJ_SIZE];
	/* Do not get the device name from battery_static, because it will not
	 * be initialized yet.
	 */
	if (battery_device_name(batt_model_ext, sizeof(batt_model_ext))) {
		pre_battery_cells = BATT_UNKNOWN;
		LOG_ERR("Failed to get battery device name, cells=UNKNOWN");
		return;
	}
	pre_battery_cells = get_battery_cells(batt_model_ext);
	if (set_chg_reg_custom(pre_battery_cells)) {
		LOG_ERR("Failed to set charger registers, battery cells=%d",
			pre_battery_cells);
	}
}
DECLARE_DEFERRED(battery_policy);
DECLARE_HOOK(HOOK_INIT, battery_policy, HOOK_PRIO_POST_BATTERY_INIT + 1);

static void battery_policy_check(void)
{
	/*
	 * A new/shutdown battery requires more wake-up time than a normal
	 * battery. This may cause pre_battery_cells to remain BATT_UNKNOWN.
	 *
	 * In factory test with new battery, SBS data become available
	 * around 1.7–1.9s after power-on. We schedule a deferred retry with
	 * 2105ms as a conservative upper bound to ensure SBS data is readable
	 * before policy execution.
	 */
	if (pre_battery_cells == BATT_UNKNOWN) {
		hook_call_deferred(&battery_policy_data, 2105 * USEC_PER_MSEC);
	}
}
DECLARE_HOOK(HOOK_INIT, battery_policy_check, HOOK_PRIO_LAST);
