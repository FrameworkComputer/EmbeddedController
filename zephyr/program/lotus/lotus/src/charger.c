/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "board_adc.h"
#include "board_charger.h"
#include "charger.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "driver/charger/isl9241.h"
#include "driver/ina2xx.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "system.h"
#include "throttle_ap.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#ifdef CONFIG_PLATFORM_EC_CHARGER_INIT_CUSTOM
static void charger_chips_init(void);
static void charger_chips_init_retry(void)
{
	charger_chips_init();
}
DECLARE_DEFERRED(charger_chips_init_retry);

static void board_check_current(void);
DECLARE_DEFERRED(board_check_current);

static void board_ina236_init(void)
{
	int rv;

	/* TODO(crosbug.com/p/29730): assume 1mA/LSB, revisit later */
	rv = ina2xx_write(0, INA2XX_REG_CALIB, 0x0831);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write calib fail");

	rv = ina2xx_write(0, INA2XX_REG_CONFIG, 0x4007);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write config fail");

	rv = ina2xx_write(0, INA2XX_REG_ALERT, 0x5DC0);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write alert fail");

	rv = ina2xx_write(0, INA2XX_REG_MASK, 0x8008);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write mask fail");
}


static void charger_chips_init(void)
{
	int chip;
	uint16_t val = 0x0000; /*default ac setting */
	uint32_t data = 0;

	const struct battery_info *bi = battery_get_info();

	/*
	 * In our case the EC can boot before the charger has power so
	 * check if the charger is responsive before we try to init it
	 */
	if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, &data) != EC_SUCCESS) {
		CPRINTS("Retry Charger init");
		hook_call_deferred(&charger_chips_init_retry_data, 100*MSEC);
		return;
	}

	for (chip = 0; chip < board_get_charger_chip_count(); chip++) {
		if (chg_chips[chip].drv->init)
			chg_chips[chip].drv->init(chip);
	}

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL2, 
		ISL9241_CONTROL2_TRICKLE_CHG_CURR(bi->precharge_current) |
		ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR |
		ISL9241_CONTROL2_PROCHOT_DEBOUNCE_500))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3, ISL9241_CONTROL3_ACLIM_RELOAD |
		ISL9241_CONTROL3_BATGONE))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6000;
	val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

	/* make sure battery FET is enabled on EC on */
	val &= ~ISL9241_CONTROL1_BGATE_OFF;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, val))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, ISL9241_CONTROL4_WOCP_FUNCTION |
		ISL9241_CONTROL4_VSYS_SHORT_CHECK |
		ISL9241_CONTROL4_ACOK_BATGONE_DEBOUNCE_25US))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_VOLTAGE, 0x0000))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_CURRENT, 0x0000))
		goto init_fail;

	/* According to Power team suggest, Set ACOK reference to 4.704V */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, ISL9241_MV_TO_ACOK_REFERENCE(4704)))
		goto init_fail;

	/* TODO: should we need to talk to PD chip after initial complete ? */
	hook_call_deferred(&board_check_current_data, 10*MSEC);
	CPRINTS("ISL9241 customized initial complete!");

	/* Initial the INA236 */
	board_ina236_init();

	return;

init_fail:
	CPRINTF("ISL9241 customized initial failed!");
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_I2C + 1);
#endif

void charger_update(void)
{
	static int pre_ac_state;
	static int pre_dc_state;
	int val = 0x0000;

	if (pre_ac_state != extpower_is_present() ||
		pre_dc_state != battery_is_present()) {
		CPRINTS("update charger!!");

		if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, &val))
			CPRINTS("read charger control1 fail");

		val &= ~ISL9241_CONTROL1_SWITCHING_FREQ_MASK;
		val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

		if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL1, val))
			CPRINTS("Update charger control1 fail");

		/**
		 * Update the isl9241 control3
		 * AC only need to disable the input current limit loop
		 */
		if (i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL3, &val))
			CPRINTS("read charger control3 fail");

		if (extpower_is_present() && !battery_is_present())
			i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL3,
				ISL9241_CONTROL3_INPUT_CURRENT_LIMIT_LOOP, MASK_SET);
		else
			i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL3,
				ISL9241_CONTROL3_INPUT_CURRENT_LIMIT_LOOP, MASK_CLR);

		/**
		 * Update the DC prochot current limit
		 * EVT: DC prochot value = 6820 mA / (10 / 3) = 2130 mA (0x800)
		 * DVT: DC prochot value = 13000 mA / (10 / 5) = 6500 mA (0x1d00)
		 */
		if (isl9241_set_dc_prochot(0,
			(board_get_version() < BOARD_VERSION_7) ? 0x800 : 0x1d00))
			CPRINTS("Update DC prochot fail");

		pre_ac_state = extpower_is_present();
		pre_dc_state = battery_is_present();
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);

static bool bypass_force_en;
static bool bypass_force_disable;
__override int board_should_charger_bypass(void)
{
	int power_uw = charge_manager_get_power_limit_uw();
	int voltage_mv = charge_manager_get_charger_voltage();
	int curr_batt = battery_is_present();

	if (bypass_force_en)
		return true;

	if (bypass_force_disable)
		return false;

	if (curr_batt == BP_YES) {
		if (power_uw > 100000000)
			return true;
		else
			return false;
	} else {
		if (voltage_mv > 20000)
			return true;
		else
			return false;
	}
}

int board_want_change_mode(void)
{
	static int pre_batt = BP_YES;
	int curr_batt = battery_is_present();

	if (pre_batt != curr_batt) {
		pre_batt = curr_batt;
		return true;
	} else
		return false;
}

int board_discharge_on_ac(int enable)
{
	int chgnum;
	int rv = EC_SUCCESS;

	bypass_force_disable = enable;
	/*
	 * When discharge on AC is selected, cycle through all chargers to
	 * enable or disable this feature.
	 */
	for (chgnum = 0; chgnum < board_get_charger_chip_count(); chgnum++)
		if (chg_chips[chgnum].drv->discharge_on_ac)
			rv = chg_chips[chgnum].drv->discharge_on_ac(chgnum, enable);
	return rv;
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int prochot_ma;
	int64_t calculate_ma;

	if (charge_ma < CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT) {
		charge_ma = CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT;
	}


	/* Handle EPR converstion through the buck switcher */
	if (charge_mv > 20000) {
		/**
		 * (charge_ma * charge_mv / 20000 ) * 0.9 * 0.94
		 */
		calculate_ma = (int64_t)charge_ma * (int64_t)charge_mv * 90 * 95 / 200000000;
	} else {
		calculate_ma = (int64_t)charge_ma * 88 / 100;
	}

	CPRINTS("Updating charger with EPR correction: ma %d", (int16_t)calculate_ma);

	prochot_ma = (DIV_ROUND_UP(((int)calculate_ma * 200 / 100), 855) * 855);

	if ((prochot_ma - (int)calculate_ma) < 853) {
		/* We need prochot to be at least 1 LSB above
		 * the input current limit. This is not ideal
		 * due to the low accuracy on prochot.
		 */
		prochot_ma += 853;
	}

	charge_set_input_current_limit((int)calculate_ma, charge_mv);
	/* sync-up ac prochot with current change */
	isl9241_set_ac_prochot(0, prochot_ma);
}

bool log_ina236;
void board_check_current(void)
{
	int16_t sv = ina2xx_read(0, INA2XX_REG_SHUNT_VOLT);
	static int curr_status = EC_DEASSERTED_PROCHOT;
	static int pre_status = EC_DEASSERTED_PROCHOT;
	static int active_port;
	static int active_current;
	static int pre_active_port;
	static int shunt_register;

	active_port = charge_manager_get_active_charge_port();
	active_current = pd_get_active_current(active_port);

	if (active_port == CHARGE_PORT_NONE || !extpower_is_present()) {
		if (pre_active_port != active_port) {
			curr_status = EC_DEASSERTED_PROCHOT;
			throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_AC);

			pre_status = curr_status;
			pre_active_port = active_port;
		}
		hook_call_deferred(&board_check_current_data, 100 * MSEC);
		return;
	}

	if (board_get_version() >= BOARD_VERSION_7)
		shunt_register = 10;
	else
		shunt_register = 5;

	if (log_ina236) {
		CPRINTS("INA236 %d mA %d mV", INA2XX_SHUNT_UV(sv) / shunt_register,
				INA2XX_BUS_MV((int)ina2xx_read(0, INA2XX_REG_BUS_VOLT)));
	}

	if (ABS(INA2XX_SHUNT_UV(sv) / shunt_register) > (active_current * 120 / 100) &&
		(INA2XX_SHUNT_UV(sv) > 0) && (active_current != 0)) {
		curr_status = EC_ASSERTED_PROCHOT;
		hook_call_deferred(&board_check_current_data, 10 * MSEC);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/* Don't need to de-assert the prochot when system in S5/G3 */
		curr_status = EC_DEASSERTED_PROCHOT;
		hook_call_deferred(&board_check_current_data, 100 * MSEC);
	} else {
		curr_status = EC_DEASSERTED_PROCHOT;
		hook_call_deferred(&board_check_current_data, 10 * MSEC);
	}

	if ((curr_status != pre_status) && !chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("EC %sassert prochot!! INA236 current=%d mA",
			curr_status == EC_DEASSERTED_PROCHOT ? "de-" : "",
			(INA2XX_SHUNT_UV(sv) / shunt_register));

		throttle_ap((curr_status == EC_ASSERTED_PROCHOT) ? THROTTLE_ON : THROTTLE_OFF,
				THROTTLE_HARD, THROTTLE_SRC_AC);
	}

	pre_status = curr_status;
	pre_active_port = active_port;
}

/* EC console command */
static int ina236_cmd(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2)) {
			log_ina236 = true;
		} else if (!strncmp(argv[1], "dis", 3)) {
			log_ina236 = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(ina236, ina236_cmd, "[en/dis]",
			"Enable or disable ina236 logging");

/* EC console command */
static int chgbypass_cmd(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strncmp(argv[1], "en", 2)) {
			bypass_force_en = true;
		} else if (!strncmp(argv[1], "dis", 3)) {
			bypass_force_en = false;
		} else {
			return EC_ERROR_PARAM1;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(chargerbypass, chgbypass_cmd, "[en/dis]",
			"Force charger bypass enabled");