/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "battery_fuel_gauge.h"
#include "board_adc.h"
#include "board_charger.h"
#include "charger.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "common_cpu_power.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "driver/charger/bq25710.h"
#include "extpower.h"
#include "gpu.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "power.h"
#include "system.h"
#include "throttle_ap.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define RAA489300_ADDR_FLAGS 0x4a

static int last_extpower_present;
static int raa489300_charge_mv;

#ifdef CONFIG_PLATFORM_EC_CHARGER_INIT_CUSTOM
static void charger_chips_init(void);
static void charger_chips_init_retry(void)
{
	charger_chips_init();
}
DECLARE_DEFERRED(charger_chips_init_retry);

__override void board_hibernate(void)
{
	/* for i2c analyze, re-write again */
}

int update_charger_in_cutoff_mode(void)
{
	return EC_SUCCESS;
}

static void charger_chips_init(void)
{
	uint32_t data = 0;
	int value;

	const struct battery_info *bi = battery_get_info();

	/*
	 * In our case the EC can boot before the charger has power so
	 * check if the charger is responsive before we try to init it
	 */
	if (i2c_read16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25770_REG_CHARGER_STATUS_0, &data) != EC_SUCCESS) {
		CPRINTS("Retry Charger init");
		hook_call_deferred(&charger_chips_init_retry_data, 100*MSEC);
		return;
	}

	/* TODO: Need to be replaced with charger api and macro */
	/* 0x14 */
	charger_set_current(CHARGER_SOLO, 4000);

	/* 0x15 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_MAX_CHARGE_VOLTAGE, bi->voltage_max))
		goto init_fail;

	/* 0x18 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25770_REG_GATEDRIVE, 0x4C4C))
		goto init_fail;

	/* 0x1A */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25770_REG_AUTO_CHARGE, 0x1DC3))
		goto init_fail;

	/* 0x33 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_PROCHOT_OPTION_0, 0x4A38))
		goto init_fail;

	/* 0x34 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_PROCHOT_OPTION_1, 0x4120))
		goto init_fail;

	/* 0x3D */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_INPUT_VOLTAGE, 0x0280))
		goto init_fail;

	/* 0x3E */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_MIN_SYSTEM_VOLTAGE, 0x0A50))
		goto init_fail;

	/* 0x3F*/
	charger_set_input_current_limit(CHARGER_SOLO, 8000);

	/* 0x61 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25770_REG_AUTOTUNE_FORCE, 0xD2D2))
		goto init_fail;

	/* 0x62 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25770_REG_GM_ADJUST_FORCE, 0xCACB))
		goto init_fail;

	value = battery_is_charge_fet_disabled();

	/* reverse the flag if no error */
	if (value != -1)
		value = !value;

	/* TODO: should we need to talk to PD chip after initial complete ? */
	CPRINTS("BQ25770 customized initial complete!");

	return;

init_fail:
	CPRINTF("BQ25770 customer init failed!");

}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_I2C + 1);
#endif

static void charger_spr(void);
DECLARE_DEFERRED(charger_spr);

static void charger_spr(void)
{
	int val = 0x0000;

	if ((!extpower_is_present()) || (raa489300_charge_mv > 36000)) {
		return;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		0x3A, &val) != EC_SUCCESS) {
		CPRINTS("3Level-Buck not ready");
		hook_call_deferred(&charger_spr_data, 500 * MSEC);
		return;
	}

	if (((val >> 8) & 0xF) == 0)
		crec_msleep(150);
	/* TODO: Need to be replaced with 3level-buck function and macro */
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x14, 0x1580);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3F, 0x157C);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4F, 0x0801);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3D, 0x0B00);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4E, 0x0140);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4C, 0x1001);
	crec_msleep(10);

	/* TODO: Need to be replaced with 3level-buck function and macro */
	if (raa489300_charge_mv <= 20000) {
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3C, 0x80A0);
		crec_msleep(10);
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x15, 0x3410);
	} else if (raa489300_charge_mv <= 36000) {
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3C, 0x80A8);
		crec_msleep(10);
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x15, 0x5DC0);
	}
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x39, 0x1003);
	crec_msleep(10);

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3A, &val)) {
		CPRINTS("read raa489300 info1 reg fail");
	}

	if (((val >> 8) & 0x3F) == 0x26) {
		CPRINTS("3-lv buck Success SPR----");
		return;
	}

	hook_call_deferred(&charger_spr_data, 100 * MSEC);
}

static void charger_epr(void);
DECLARE_DEFERRED(charger_epr);

static void charger_epr(void)
{
	int val = 0x0000;

	if ((!extpower_is_present()) || (raa489300_charge_mv != 48000)) {
		return;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		0x3A, &val) != EC_SUCCESS) {
		CPRINTS("3Level-Buck not ready");
		hook_call_deferred(&charger_epr_data, 500 * MSEC);
		return;
	}

	if (((val >> 8) & 0xF) == 0)
		crec_msleep(150);
	/* TODO: Need to be replaced with 3level-buck function and macro */
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x14, 0x1B58);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3F, 0x157C);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x15, 0x2EE0);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4F, 0x0001);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3D, 0x0B00);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3C, 0x80A4);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4E, 0x0140);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4C, 0x1001);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x39, 0x1001);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3F, 0x1B58);
	crec_msleep(10);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x15, 0x5358);
	crec_msleep(10);

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x3A, &val)) {
		CPRINTS("read raa489300 info1 reg fail");
	}

	if (((val >> 8) & 0x3F) == 0x35) {
		CPRINTS("3-lv buck Success EPR----");
		return;
	}

	hook_call_deferred(&charger_epr_data, 100 * MSEC);
}

void charger_update(void)
{
	static int pre_power_uw;
	int power_uw = cypd_get_ac_power();

	raa489300_charge_mv = cypd_get_active_port_voltage();

	if (extpower_is_present()) {
		if (pre_power_uw != power_uw) {
			CPRINTS("3lv-buck update ! V:%dmV,W:%dmW", raa489300_charge_mv, power_uw);
			if (raa489300_charge_mv <= 36000) {
				charger_spr();
			} else if (raa489300_charge_mv == 48000) {
				charger_epr();
			}
			/* TODO: Need to be replaced with 3level-buck function and macro */
			if (power_uw == 65000 || power_uw == 100000 ||
				power_uw == 130000 || power_uw == 210000) {
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x40, 0x3800);
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4B, 0x3800);
			} else if (power_uw == 165000) {
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x40, 0x4F00);
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4B, 0x4F00);
			} else if (power_uw >= 180000) {
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x40, 0x6500);
				i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, 0x4B, 0x6500);
			}

			pre_power_uw = power_uw;
		}
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_AC_CHANGE, charger_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, charger_update, HOOK_PRIO_POST_I2C + 1);

int board_discharge_on_ac(int enable)
{
	int chgnum;
	int rv = EC_SUCCESS;

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

	charge_set_input_current_limit((int)calculate_ma, charge_mv);
}

__overridable int extpower_is_present(void)
{
	return last_extpower_present;
}

__override void board_check_extpower(void)
{
	static int pre_active_port = -1;
	int pd_active_port = get_active_charge_pd_port();
	int hw_extpower_status = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hw_acav_in));
	int extpower_present = hw_extpower_status;
	uint8_t c_fet_status = cypd_get_cfet_status();

	/*
	 * AC status
	 *
	 * +--------------+---------+---------------+------------+--------------------+
	 * |   extpower   |  C fet  |  active port  |   result   |       status       |
	 * +--------------+---------+---------------+------------+--------------------+
	 * |     High     |  Close  |   non-active  |   HW pin   | Leakage            |
	 * |     High     |  Close  |     active    |   HW pin   | Leakage            |
	 * |     High     |  Open   |   non-active  |   HW pin   | Leakage            |
	 * |     High     |  Open   |     active    |  PD state  | Normal             |
	 * |     Low      |  Close  |   non-active  |  PD state  | Normal             |
	 * |     Low      |  Close  |     active    |   HW pin   | VBUS control fail  |
	 * |     Low      |  Open   |   non-active  |   HW pin   | VBUS control fail  |
	 * |     Low      |  Open   |     active    |   HW pin   | Multi-ports switch |
	 * |     Low      |  Open   |     active    |  PD state  | EPR mode switch    |
	 * +--------------+---------+---------------+------------+--------------------+
	 */

	if ((pre_active_port == pd_active_port) &&
		(((pd_active_port != -1) && c_fet_status) ||
		((pd_active_port == -1) && !c_fet_status)))
		extpower_present = (pd_active_port == -1) ? 0 : 1;

	if (last_extpower_present != extpower_present) {
		/**
		 * last extpower present is a return value in function "extpower_is_present()",
		 * must update the value before extpower_handle_update();
		 */
		last_extpower_present = extpower_present;
		extpower_handle_update(extpower_present);
	} else
		last_extpower_present = extpower_present;

	/* we should update the PMF as soon as possible after the typec port state is changed */
	update_soc_power_limit(false, false);

	/**
	 * set the GPU to ac mode if the adapter power = 100w,
	 * more than 100w will be controlled by enter EPR mode.
	 */
	if (pd_active_port && cypd_get_ac_power() == 100000)
		set_gpu_gpio(GPIO_FUNC_ACDC, 1);

	pre_active_port = pd_active_port;
}

static int raa489300_cmd(int argc, const char **argv)
{
	if (argc >= 2) {
		if (!strncmp(argv[1], "get", 3)) {
			int i;
			int val;

			/* Dump all readable registers*/
			static const uint8_t regs[] = {
				0x14, 0x15, 0x39, 0x3a, 0x3c, 0x3d, 0x3f, 0x40, 0x43,
				0x49, 0x4b, 0x4c, 0x4e, 0x4f,
			};

			for (i = 0; i < ARRAY_SIZE(regs); ++i) {
				if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
					regs[i], &val))
					continue;
				ccprintf("raa489300 REG 0x%02x:  0x%04x\n", regs[i], val);
			}
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(raa489300, raa489300_cmd, "[get]",
			"Get raa489300 register");
