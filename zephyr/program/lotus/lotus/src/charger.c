/* Copyright 2022 The ChromiumOS Authors
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
#include "driver/charger/isl9241.h"
#include "driver/ina2xx.h"
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

static int last_extpower_present;

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

	gpio_enable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_ina236_alert));

	/* TODO(crosbug.com/p/29730): assume 1mA/LSB, revisit later */
	rv = ina2xx_write(0, INA2XX_REG_CALIB, 0x0831);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write calib fail");

	rv = ina2xx_write(0, INA2XX_REG_CONFIG, 0x4027);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write config fail");

	rv = ina2xx_write(0, INA2XX_REG_ALERT, 0x5DC0);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write alert fail");

	rv = ina2xx_write(0, INA2XX_REG_MASK, 0x8009);

	if (rv != EC_SUCCESS)
		CPRINTS("ina236 write mask fail");

}

static void ina236_alert_release(void)
{
	int rv;

	rv = ina2xx_read(0, INA2XX_REG_MASK);

	if (rv == 0x0bad)
		CPRINTS("ina236 read mask fail");

}
DECLARE_DEFERRED(ina236_alert_release);

void ina236_alert_interrupt(void)
{
	hook_call_deferred(&ina236_alert_release_data, 6 * MSEC);
}

static void charge_gate_onoff(bool status)
{
	if (status) {
		/* Clear Control0 register bit 12; NGATE on */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL0,
			ISL9241_CONTROL0_NGATE_OFF, MASK_CLR);

		/* Clear Control1 register bit 6; BGATE on */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL1,
			ISL9241_CONTROL1_BGATE_OFF, MASK_CLR);

	} else {
		/* Set Control0 register bit 12; NGATE off */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL0,
			ISL9241_CONTROL0_NGATE_OFF, MASK_SET);

		/* Set Control1 register bit 6; BGATE off */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL1,
			ISL9241_CONTROL1_BGATE_OFF, MASK_SET);
	}
}

static  void charger_psys_enable(bool status)
{
	if (status) {

		i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_ACOK_REFERENCE, ISL9241_MV_TO_ACOK_REFERENCE(4000));

		/* Clear Control1 register bit 5; Enable IMON */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL1,
			ISL9241_CONTROL1_IMON, MASK_CLR);

		/* Clear Control4 register bit 12; Enable all mode */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL4,
			ISL9241_CONTROL4_GP_COMPARATOR, MASK_CLR);

	} else {

		i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_ACOK_REFERENCE, ISL9241_MV_TO_ACOK_REFERENCE(0));

		/* Set Control1 register bit 5; Disable IMON */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL1,
			ISL9241_CONTROL1_IMON, MASK_SET);

		/* Set Control4 register bit 12; Disable for battery only mode */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL4,
			ISL9241_CONTROL4_GP_COMPARATOR, MASK_SET);
	}
}

void charger_input_current_limit_control(enum power_state state)
{
	int acin = gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_hw_acav_in));

	if ((state == POWER_S5 && !acin) ||
		 (acin && battery_is_present() != BP_YES)) {
		/**
		 * Set Control3 register bit 5;
		 * Condition 1: DC mode S5
		 * Condition 2: AC only
		 */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL3,
			ISL9241_CONTROL3_INPUT_CURRENT_LIMIT, MASK_SET);
	} else {
		/* Clear Control3 register bit 5; */
		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL3,
			ISL9241_CONTROL3_INPUT_CURRENT_LIMIT, MASK_CLR);
	}
}

static void board_charger_lpm_control(void)
{
	enum power_state ps = power_get_state();
	static enum power_state pre_power_state = POWER_G3;

	if (battery_cutoff_in_progress() || battery_is_cut_off())
		return;

	switch (ps) {
	case POWER_G3:
	case POWER_G3S5:
	case POWER_S5:
	case POWER_S3S5:
	case POWER_S4S5:
		if (pre_power_state != ps)
			charger_psys_enable(false);
		charger_input_current_limit_control(POWER_S5);
		break;
	case POWER_S0:
	case POWER_S3S0:
	case POWER_S5S3:
	case POWER_S3:
		if (pre_power_state != ps)
			charger_psys_enable(true);
		charger_input_current_limit_control(POWER_S0);
		break;
	default:
		break;
	}

	pre_power_state = ps;
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, board_charger_lpm_control, HOOK_PRIO_DEFAULT+1);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_charger_lpm_control, HOOK_PRIO_DEFAULT+1);

__override void board_hibernate(void)
{
	/* for i2c analyze, re-write again */
	board_charger_lpm_control();
	charge_gate_onoff(false);

}

int update_charger_in_cutoff_mode(void)
{
	/* Turn off the charger NGATE */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL0, (ISL9241_CONTROL0_NGATE_OFF |
			ISL9241_CONTROL0_BGATE_FORCE_ON)))
		return EC_ERROR_UNKNOWN;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
			ISL9241_REG_CONTROL3, (ISL9241_CONTROL3_ACLIM_RELOAD |
			ISL9241_CONTROL3_BATGONE)))
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static void charger_chips_init(void)
{
	uint16_t val = 0x0000; /* default ac setting */
	uint32_t data = 0;
	int value;

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

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL4, ISL9241_CONTROL4_WOCP_FUNCTION |
		ISL9241_CONTROL4_VSYS_SHORT_CHECK |
		ISL9241_CONTROL4_ACOK_BATGONE_DEBOUNCE_25US))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL3,
		(ISL9241_CONTROL3_ACLIM_RELOAD | ISL9241_CONTROL3_BATGONE)))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_VOLTAGE, 0x0000))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_OTG_CURRENT, 0x0000))
		goto init_fail;

	value = battery_is_charge_fet_disabled();

	/* reverse the flag if no error */
	if (value != -1)
		value = !value;

	/* According to Power team suggest, Set ACOK reference to 4.500V */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_ACOK_REFERENCE, ISL9241_MV_TO_ACOK_REFERENCE(4500)))
		goto init_fail;

	/*
	 * Set the MaxSystemVoltage to battery maximum,
	 * 0x00=disables switching charger states
	 */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_MAX_SYSTEM_VOLTAGE, bi->voltage_max))
		goto init_fail;

	/*
	 * Set the MinSystemVoltage to battery minimum,
	 * 0x00=disables all battery charging
	 */
	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_MIN_SYSTEM_VOLTAGE, bi->voltage_min))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL2,
		ISL9241_CONTROL2_TRICKLE_CHG_CURR(bi->precharge_current) |
		ISL9241_CONTROL2_GENERAL_PURPOSE_COMPARATOR |
		ISL9241_CONTROL2_PROCHOT_DEBOUNCE_500))
		goto init_fail;

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL0, 0x0000))
		goto init_fail;

	val = ISL9241_CONTROL1_PROCHOT_REF_6000;
	val |= ((ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7) &
			ISL9241_CONTROL1_SWITCHING_FREQ_MASK);

	if (i2c_write16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS,
		ISL9241_REG_CONTROL1, val))
		goto init_fail;

	board_charger_lpm_control();

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

	if (pre_ac_state != extpower_is_present() ||
		pre_dc_state != battery_is_present()) {
		CPRINTS("update charger!!");

		i2c_update16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL1,
				(ISL9241_CONTROL1_SWITCHING_FREQ_656KHZ << 7), MASK_SET);

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

		board_charger_lpm_control();
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

int charger_in_bypass_mode(void)
{
	int reg;
	int rv;

	rv = i2c_read16(I2C_PORT_CHARGER, ISL9241_ADDR_FLAGS, ISL9241_REG_CONTROL0, &reg);

	/* read register fail */
	if (rv)
		return 0;

	/* charer not enter bypass mode */
	if ((reg & ISL9241_CONTROL0_EN_BYPASS_GATE) != ISL9241_CONTROL0_EN_BYPASS_GATE)
		return 0;

	return 1;
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
