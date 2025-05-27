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
#include "driver/charger/bq257x0_regs.h"
#include "extpower.h"
#include "gpu.h"
#include "gpio.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "power.h"
#include "raa489300.h"
#include "system.h"
#include "throttle_ap.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

#define VINDPM_TO_REG(mv) (((mv) < 3200) ? 160 : ((mv) / 20))
#define IDCHG_TH1_CURRENT_TO_REG(CUR) (((CUR) <= 1500) ? 0 : (((CUR) - 1500) / 500))

static int last_extpower_present;
static int prev_charge_ma;

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
	uint16_t option0 = BQ25770_CHARGE_OPTION_0_RESET_VALUE;
	uint16_t option1 = 0x0000;
	uint16_t option4 = 0x0000;
	int value;
	int idchg;

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

	/* 0x31 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_CHARGE_OPTION_2, 0x0037))
		goto init_fail;

	/* 0x33 */
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_PROCHOT_OPTION_0, 0x4A38))
		goto init_fail;

	/* 0x34 */
	idchg = IDCHG_TH1_CURRENT_TO_REG(7500);
	option1 |= (idchg << BQ257X0_PROCHOT_OPTION_1_IDCHG_VTH_SHIFT) |
				BQ25770_PROCHOT_OPTION_1_IDCHG_DEGLITCH_5S |
				BQ25770_PROCHOT_OPTION_1_PP_ICRIT |
				BQ25770_PROCHOT_OPTION_1_PP_INOM |
				BQ25770_PROCHOT_OPTION_1_PP_IDCHG1;
	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_PROCHOT_OPTION_1, option1))
		goto init_fail;

	/* 0x36 */
	option4 = BQ25770_CHARGE_OPTION_4_IDCHG_DEG2_5P2MS |
			  BQ25770_CHARGE_OPTION_4_PP_IDCHG2;

	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25720_REG_CHARGE_OPTION_4, option4))
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

	/* 0x12 */
	option0 &= ~(1 << BQ257X0_CHARGE_OPTION_0_EN_LWPWR_SHIFT);

	if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		BQ25710_REG_CHARGE_OPTION_0, option0))
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

static void board_dynamic_acok_control(void)
{
	static int pre_acok_mv = -1;
	int acok_mv;
	int voltage = cypd_get_active_port_voltage();

	if (voltage <= 5000)
		acok_mv = 3200;		/*set ACOK 3.2V*/
	else if (voltage < 20000)
		acok_mv = 7000;		/*set ACOK 7V*/
	else
		acok_mv = 15000;	/*set ACOK 15V*/

	if (acok_mv != pre_acok_mv) {
		if (i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
			BQ25710_REG_INPUT_VOLTAGE, VINDPM_TO_REG(acok_mv) << 2)) {
			CPRINTS("BQ25770 update ACOK reference fail");
		}

		pre_acok_mv = acok_mv;
	}
}

__override void board_set_charge_limit(int port, int supplier, int charge_ma,
			    int max_ma, int charge_mv)
{
	int level_buck_ma;
	int64_t calculate_ma;

	if (charge_ma < CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT) {
		charge_ma = CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT;
	}

	/* Handle EPR converstion through the buck switcher */
	if (charge_mv > 20000) {
		/**
		 * (charge_ma * charge_mv / 24000 ) * 0.95 * 0.95
		 */
		calculate_ma = (int64_t)charge_ma * (int64_t)charge_mv * 95 * 95 / 240000000;
	} else {
		calculate_ma = (int64_t)charge_ma * 98 / 100;
	}

	level_buck_ma = charge_ma * 98 / 100;

	CPRINTS("Updating charger with EPR correction: ma %d", (int16_t)calculate_ma);

	if (charge_ma < prev_charge_ma) {
		/* adjusting the limit down */
		charge_set_input_current_limit((int)calculate_ma, charge_mv);
		level_buck_set_input_current_limit(level_buck_ma);
	} else {
		/* adjusting the limit up */
		level_buck_set_input_current_limit(level_buck_ma);
		charge_set_input_current_limit((int)calculate_ma, charge_mv);
	}

	prev_charge_ma = charge_ma;

	board_dynamic_acok_control();
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

__override int board_confirm_buck_transition_ready(bool is_epr)
{
	int rv;
	int val = 0x0000;

	if (!extpower_is_present()) {
		return EC_ERROR_NOT_POWERED;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_INFORMATION1, &val) != EC_SUCCESS) {
		CPRINTS("3Level-Buck not ready");
		return EC_ERROR_NOT_POWERED;
	}

	/* check the regulator has gone to the reset state */
	if (((val >> 8) & 0xF) == 0)
		crec_msleep(150);

	rv = write_level_buck_registers(is_epr);
	if (rv)
		return rv;


	rv = i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
					RAA489300_REG_INFORMATION1, &val);
	if (rv)
		return rv;

	if (((val >> 8) & 0x3F) == (is_epr ? 0x35 : 0x26)) {
		rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_VINOK_REFERENCE, 0x3800);
		rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_MIN_INPUT_VOLTAGE, 0x3800);
		return EC_SUCCESS;
	} else {
		return EC_ERROR_UNKNOWN;
	}
}
