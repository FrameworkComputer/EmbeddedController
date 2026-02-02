/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "battery_smart.h"
#include "battery_fuel_gauge.h"
#include "board_charger.h"
#include "board_function.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/isl923x.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define CHARGER_75W_DC_PROCHOT_MA 7936
#define CHARGER_61W_DC_PROCHOT_MA 7680
#define CHARGER_55W_DC_PROCHOT_MA 7168
#define CHARGER_MAX_SYSTEM_VOLTAGE_16_8V 0x41A0
#define CHARGER_MIN_SYSTEM_VOLTAGE_12_2V 0x3000

#define ISL9238_C3_INPUT_CURRENT_LIMIT_LOOP BIT(5)
#define RAA489108_C6_UNDER_VOLTAGE_THRESHOLD_4_8V (3 << 0)

static void charger_chips_init(void);
static void charger_chips_init_retry(void)
{
	charger_chips_init();
}
DECLARE_DEFERRED(charger_chips_init_retry);

static inline enum ec_error_list charger_i2c_read16(int reg, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, ISL923X_ADDR_FLAGS, reg, value);
}

static inline enum ec_error_list charger_i2c_write16(int reg, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, ISL923X_ADDR_FLAGS, reg, value);
}

static inline enum ec_error_list charger_i2c_update16(int reg, int mask,
					      enum mask_update_action action)
{
	return i2c_update16(I2C_PORT_CHARGER, ISL923X_ADDR_FLAGS, reg, mask,
			    action);
}

static void charger_chips_init(void)
{
	bool batt_is_present = (battery_is_present() == BP_YES);
	uint16_t reg_val = 0x0000;
	int data;


	/*
	 * In our case the EC can boot before the charger has power so
	 * check if the charger is responsive before we try to init it
	 */
	if (charger_i2c_read16(ISL923X_REG_MANUFACTURER_ID, &data)) {
		CPRINTS("Retry Charger init");
		hook_call_deferred(&charger_chips_init_retry_data, 100 * MSEC);
		return;
	}

	/* Set CCM/CDM mode transition threshold for better power efficiency in S5 */
	reg_val = RAA489000_C0_BUCK_PHASE_THRESHOLD_MINUS4MV;
	if (charger_i2c_write16(ISL923X_REG_CONTROL0, reg_val))
		goto init_fail;

	/* Control2 Configuration */
	reg_val = ISL923X_C2_TRICKLE_256 | ISL923X_C2_ADAPTER_DEBOUNCE_150 |
		  ISL923X_C2_PROCHOT_DEBOUNCE_1000;

	if (charger_i2c_write16(ISL923X_REG_CONTROL2, reg_val))
		goto init_fail;

	/* Control3 Configuration (Check DC, also need to put into charger_update)*/
	reg_val = ISL9238_C3_NO_REREAD_PROG_PIN | ISL9238_C3_NO_RELOAD_ACLIM_ON_ACIN |
		  ISL9238_C3_DISABLE_AUTO_CHARING;

	if (!batt_is_present)
		reg_val |= ISL9238_C3_INPUT_CURRENT_LIMIT_LOOP;

	if (charger_i2c_write16(ISL9238_REG_CONTROL3, reg_val))
		goto init_fail;

	/* Control6 Configuration */
	reg_val = ISL9238C_C6_SLEW_RATE_CONTROL | RAA489108_C6_UNDER_VOLTAGE_THRESHOLD_4_8V;

	if (charger_i2c_update16(ISL9238C_REG_CONTROL6, reg_val, MASK_SET))
		goto init_fail;

	if (charger_i2c_write16(ISL923X_REG_SYS_VOLTAGE_MAX, CHARGER_MAX_SYSTEM_VOLTAGE_16_8V))
		goto init_fail;

	if (charger_i2c_write16(ISL923X_REG_SYS_VOLTAGE_MIN, CHARGER_MIN_SYSTEM_VOLTAGE_12_2V))
		goto init_fail;

	CPRINTS("Charger RAA489108 init completed!");
	return;

init_fail:
	CPRINTF("Charger RAA489108 init failed!");
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_I2C);

static void charger_enables_psys(void)
{
	CPRINTS("charger enables psys");
	charger_i2c_update16(ISL923X_REG_CONTROL1, ISL923X_C1_ENABLE_PSYS, MASK_SET);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, charger_enables_psys, HOOK_PRIO_DEFAULT);

static void charger_disables_psys(void)
{
	CPRINTS("charger disables psys");
	charger_i2c_update16(ISL923X_REG_CONTROL1, ISL923X_C1_ENABLE_PSYS, MASK_CLR);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_disables_psys, HOOK_PRIO_DEFAULT);

__override void board_hibernate(void)
{
	isl9238c_hibernate(CHARGER_SOLO);
}

static int board_select_dc_prochot_current(enum framework_battery_type type)
{
	int current;

	switch (type) {
	case FWK_BATT_NVT_55W:
		current = CHARGER_55W_DC_PROCHOT_MA;
		break;
	case FWK_BATT_NVT_61W:
		current = CHARGER_61W_DC_PROCHOT_MA;
		break;
	case FWK_BATT_ATC_75W:
		current = CHARGER_75W_DC_PROCHOT_MA;
		break;
	default:
		current = CHARGER_75W_DC_PROCHOT_MA;
	}

	return current;
}

void charger_update(void)
{
	static int pre_batt_state;
	static int pre_batt_type;
	int batt_state = battery_is_present();
	int batt_type = board_get_battery_type();

	if (pre_batt_state != batt_state || pre_batt_type != batt_type) {
		batt_type = (batt_state == BP_YES) ? batt_type : FWK_BATT_UNKNOWN;

		if (batt_type != FWK_BATT_UNKNOWN) {
			if (isl923x_set_dc_prochot(0, board_select_dc_prochot_current(batt_type)))
				CPRINTS("Charger updates DC Prochot Failed");
		}

		if (charger_i2c_update16(ISL9238_REG_CONTROL3,
		    ISL9238_C3_INPUT_CURRENT_LIMIT_LOOP,
		    (batt_state == BP_YES) ? MASK_CLR : MASK_SET))
			CPRINTS("Charger updates control6 Failed");

		pre_batt_state = batt_state;
		pre_batt_type = batt_type;
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);

void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int prochot_ma;

	if (charge_ma < CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT) {
		charge_ma = CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT;
	}
	/*
	 * ac prochot should bigger than input current
	 * And needs to be at least 128mA bigger than the adapter current
	 */
	prochot_ma = (DIV_ROUND_UP(charge_ma, 128) * 128);
	charge_ma = charge_ma * 95 / 100;

	if ((prochot_ma - charge_ma) < 128) {
		charge_ma = prochot_ma - 128;
	}

	charge_set_input_current_limit(charge_ma, charge_mv);
	/* sync-up ac prochot with current change */
	isl923x_set_ac_prochot(0, prochot_ma);
}
