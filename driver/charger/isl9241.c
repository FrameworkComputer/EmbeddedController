/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-9241 battery charger driver.
 */

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "isl9241.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "ISL9241 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

/* Sense resistor default values in milli Ohm */
#define ISL9241_DEFAULT_RS1 20	/* Input current sense resistor */
#define ISL9241_DEFAULT_RS2 10	/* Battery charge current sense resistor */

#define BOARD_RS1 CONFIG_CHARGER_SENSE_RESISTOR_AC
#define BOARD_RS2 CONFIG_CHARGER_SENSE_RESISTOR

#define BC_REG_TO_CURRENT(REG) (((REG) * ISL9241_DEFAULT_RS2) / BOARD_RS2)
#define BC_CURRENT_TO_REG(CUR) (((CUR) * BOARD_RS2) / ISL9241_DEFAULT_RS2)

#define AC_REG_TO_CURRENT(REG) (((REG) * ISL9241_DEFAULT_RS1) / BOARD_RS1)
#define AC_CURRENT_TO_REG(CUR) (((CUR) * BOARD_RS1) / ISL9241_DEFAULT_RS1)

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

static int learn_mode;

/* Mutex for CONTROL1 register, that can be updated from multiple tasks. */
static struct mutex control1_mutex;

/* Charger parameters */
static const struct charger_info isl9241_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = CHARGE_I_MAX,
	.current_min  = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max  = INPUT_I_MAX,
	.input_current_min  = INPUT_I_MIN,
	.input_current_step = INPUT_I_STEP,
};

static enum ec_error_list isl9241_discharge_on_ac(int chgnum, int enable);

static inline enum ec_error_list isl9241_read(int chgnum, int offset,
					      int *value)
{
	return i2c_read16(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags,
			  offset, value);
}

static inline enum ec_error_list isl9241_write(int chgnum, int offset,
					       int value)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags,
			   offset, value);
}

static inline enum ec_error_list isl9241_update(int chgnum, int offset,
						uint16_t mask,
						enum mask_update_action action)
{
	return i2c_update16(chg_chips[chgnum].i2c_port,
			    chg_chips[chgnum].i2c_addr_flags,
			    offset, mask, action);
}

/* chip specific interfaces */

/*****************************************************************************/
/* Charger interfaces */
static enum ec_error_list isl9241_set_input_current(int chgnum,
						    int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = isl9241_write(chgnum, ISL9241_REG_ADAPTER_CUR_LIMIT1, reg);
	if (rv)
		return rv;

	return isl9241_write(chgnum, ISL9241_REG_ADAPTER_CUR_LIMIT2, reg);
}

static enum ec_error_list isl9241_get_input_current(int chgnum,
						    int *input_current)
{
	int rv;

	rv = isl9241_read(chgnum, ISL9241_REG_ADAPTER_CUR_LIMIT1,
			  input_current);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(*input_current);
	return EC_SUCCESS;
}

static enum ec_error_list isl9241_manufacturer_id(int chgnum, int *id)
{
	return isl9241_read(chgnum, ISL9241_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list isl9241_device_id(int chgnum, int *id)
{
	return isl9241_read(chgnum, ISL9241_REG_DEVICE_ID, id);
}

static enum ec_error_list isl9241_get_option(int chgnum, int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = isl9241_read(chgnum, ISL9241_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = isl9241_read(chgnum, ISL9241_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

static enum ec_error_list isl9241_set_option(int chgnum, int option)
{
	int rv;

	rv = isl9241_write(chgnum, ISL9241_REG_CONTROL0, option & 0xFFFF);
	if (rv)
		return rv;

	return isl9241_write(chgnum, ISL9241_REG_CONTROL1,
			     (option >> 16) & 0xFFFF);
}

static const struct charger_info *isl9241_get_info(int chgnum)
{
	return &isl9241_charger_info;
}

static enum ec_error_list isl9241_get_status(int chgnum, int *status)
{
	int rv;
	int reg;

	/* Level 2 charger */
	*status = CHARGER_LEVEL_2;

	/* Charge inhibit status */
	rv = isl9241_read(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE, &reg);
	if (rv)
		return rv;
	if (!reg)
		*status |= CHARGER_CHARGE_INHIBITED;

	/* Battery present & AC present status */
	rv = isl9241_read(chgnum, ISL9241_REG_INFORMATION2, &reg);
	if (rv)
		return rv;
	if (!(reg & ISL9241_INFORMATION2_BATGONE_PIN))
		*status |= CHARGER_BATTERY_PRESENT;
	if (reg & ISL9241_INFORMATION2_ACOK_PIN)
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

static enum ec_error_list isl9241_set_mode(int chgnum, int mode)
{
	int rv;

	/*
	 * See crosbug.com/p/51196. Always disable learn mode unless it was set
	 * explicitly.
	 */
	if (!learn_mode) {
		rv = isl9241_discharge_on_ac(chgnum, 0);
		if (rv)
			return rv;
	}

	/*
	 * Charger inhibit
	 * MinSystemVoltage 0x00h = disables all battery charging
	 */
	rv = isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE,
		mode & CHARGE_FLAG_INHIBIT_CHARGE ?
		0 : battery_get_info()->voltage_min);
	if (rv)
		return rv;

	/* POR reset */
	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = isl9241_write(chgnum, ISL9241_REG_CONTROL3,
			ISL9241_CONTROL3_DIGITAL_RESET);
	}

	return rv;
}

static enum ec_error_list isl9241_get_current(int chgnum, int *current)
{
	int rv;

	rv = isl9241_read(chgnum, ISL9241_REG_CHG_CURRENT_LIMIT, current);
	if (rv)
		return rv;

	*current = BC_REG_TO_CURRENT(*current);
	return EC_SUCCESS;
}

static enum ec_error_list isl9241_set_current(int chgnum, int current)
{
	return isl9241_write(chgnum, ISL9241_REG_CHG_CURRENT_LIMIT,
				BC_CURRENT_TO_REG(current));
}

static enum ec_error_list isl9241_get_voltage(int chgnum, int *voltage)
{
	return isl9241_read(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

static enum ec_error_list isl9241_set_voltage(int chgnum, int voltage)
{
	return isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

static enum ec_error_list isl9241_get_vbus_voltage(int chgnum, int port,
						   int *voltage)
{
	int adc_val = 0;
	int ctl3_val;
	int rv;

	/* Get current Control3 value */
	rv = isl9241_read(chgnum, ISL9241_REG_CONTROL3, &ctl3_val);
	if (rv)
		goto error;

	/* Enable ADC */
	if (!(ctl3_val & ISL9241_CONTROL3_ENABLE_ADC)) {
		rv = isl9241_write(chgnum, ISL9241_REG_CONTROL3,
				   ctl3_val | ISL9241_CONTROL3_ENABLE_ADC);
		if (rv)
			goto error;
	}

	/* Read voltage ADC value */
	rv = isl9241_read(chgnum, ISL9241_REG_VIN_ADC_RESULTS, &adc_val);
	if (rv)
		goto error_restore_ctl3;

	/*
	 * Adjust adc_val
	 *
	 * raw adc_val has VIN_ADC in bits [13:6], so shift this down
	 * this puts adc_val in the range of 0..255, which maps to 0..24.48V
	 * each step in adc_val is 96mv
	 */
	adc_val >>= ISL9241_VIN_ADC_BIT_OFFSET;
	adc_val *= ISL9241_VIN_ADC_STEP_MV;
	*voltage = adc_val;

error_restore_ctl3:
	/* Restore Control3 value */
	if (!(ctl3_val & ISL9241_CONTROL3_ENABLE_ADC))
		(void)isl9241_write(chgnum, ISL9241_REG_CONTROL3, ctl3_val);

error:
	if (rv)
		CPRINTF("Could not read VBUS ADC! Error: %d\n", rv);

	return rv;
}

static enum ec_error_list isl9241_post_init(int chgnum)
{
	return EC_SUCCESS;
}

static enum ec_error_list isl9241_discharge_on_ac(int chgnum, int enable)
{
	int rv;

	mutex_lock(&control1_mutex);

	rv = isl9241_update(chgnum, ISL9241_REG_CONTROL1,
			    ISL9241_CONTROL1_LEARN_MODE,
			    (enable) ? MASK_SET : MASK_CLR);
	if (!rv)
		learn_mode = enable;

	mutex_unlock(&control1_mutex);
	return rv;
}

int isl9241_set_ac_prochot(int chgnum, int ma)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(ma);

	rv = isl9241_write(chgnum, ISL9241_REG_AC_PROCHOT, reg);
	if (rv)
		CPRINTF("set_ac_prochot failed (%d)", rv);

	return rv;
}

/*****************************************************************************/
/* ISL-9241 initialization */
static void isl9241_init(int chgnum)
{
	const struct battery_info *bi = battery_get_info();

	/*
	 * Set the MaxSystemVoltage to battery maximum,
	 * 0x00=disables switching charger states
	 */
	if (isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE,
			  bi->voltage_max))
		goto init_fail;

	/*
	 * Set the MinSystemVoltage to battery minimum,
	 * 0x00=disables all battery charging
	 */
	if (isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE,
			  bi->voltage_min))
		goto init_fail;

	/*
	 * Set control2 register to
	 * [15:13]: Trickle Charging Current (battery pre-charge current)
	 * [10:9] : Prochot# Debounce time (1000us)
	 */
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL2,
			   (ISL9241_CONTROL2_TRICKLE_CHG_CURR(
				bi->precharge_current) |
			    ISL9241_CONTROL2_PROCHOT_DEBOUNCE_1000),
			   MASK_SET))
		goto init_fail;

	/*
	 * Set control3 register to
	 * [14]: ACLIM Reload (Do not reload)
	 */
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL3,
			   ISL9241_CONTROL3_ACLIM_RELOAD,
			   MASK_SET))
		goto init_fail;

	/*
	 * Set control4 register to
	 * [13]: Slew rate control enable (sets VSYS ramp to 8mV/us)
	 */
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL4,
			   ISL9241_CONTROL4_SLEW_RATE_CTRL,
			   MASK_SET))
		goto init_fail;

#ifndef CONFIG_CHARGE_RAMP_HW
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL0,
			   ISL9241_CONTROL0_INPUT_VTG_REGULATION,
			   MASK_SET))
		goto init_fail;
#endif

	/*
	 * No need to proceed with the rest of init if we sysjump'd to this
	 * image as the input current limit has already been set.
	 */
	if (system_jumped_late())
		return;

	/* Initialize the input current limit to the board's default. */
	if (isl9241_set_input_current(chgnum, CONFIG_CHARGER_INPUT_CURRENT))
		goto init_fail;

	return;

init_fail:
	CPRINTF("ISL9241_init failed!");
}

/*****************************************************************************/
/* Hardware current ramping */

#ifdef CONFIG_CHARGE_RAMP_HW
static enum ec_error_list isl9241_set_hw_ramp(int chgnum, int enable)
{
	/* HW ramp is controlled by input voltage regulation reference bits */
	return isl9241_update(chgnum, ISL9241_REG_CONTROL0,
			      ISL9241_CONTROL0_INPUT_VTG_REGULATION,
			      (enable) ? MASK_CLR : MASK_SET);
}

static int isl9241_ramp_is_stable(int chgnum)
{
	/*
	 * Since ISL cannot read the current limit that the ramp has settled
	 * on, then we can never consider the ramp stable, because we never
	 * know what the stable limit is.
	 */
	return 0;
}

static int isl9241_ramp_is_detected(int chgnum)
{
	return 1;
}

static int isl9241_ramp_get_current_limit(int chgnum)
{
	int reg;

	if (isl9241_read(chgnum, ISL9241_REG_IADP_ADC_RESULTS, &reg))
		return 0;

	/* LSB value of register = 22.2mA */
	return (reg * 222) / 10;
}
#endif /* CONFIG_CHARGE_RAMP_HW */

/*****************************************************************************/
#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int chgnum, int low, int high)
{
	int reg;
	int regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		CPRINTF("[%Xh] = ", reg);
		rv = isl9241_read(chgnum, reg, &regval);
		if (!rv)
			CPRINTF("0x%04x\n", regval);
		else
			CPRINTF("ERR (%d)\n", rv);
		cflush();
	}
}

static int command_isl9241_dump(int argc, char **argv)
{
	char *e;
	int chgnum = 0;

	if (argc >= 2) {
		chgnum = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	dump_reg_range(chgnum, 0x14, 0x15);
	dump_reg_range(chgnum, 0x38, 0x40);
	dump_reg_range(chgnum, 0x43, 0x43);
	dump_reg_range(chgnum, 0x47, 0x4F);
	dump_reg_range(chgnum, 0x80, 0x87);
	dump_reg_range(chgnum, 0x90, 0x91);
	dump_reg_range(chgnum, 0xFE, 0xFF);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger_dump, command_isl9241_dump,
			"charger_dump <chgnum>",
			"Dumps ISL9241 registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv isl9241_drv = {
	.init = &isl9241_init,
	.post_init = &isl9241_post_init,
	.get_info = &isl9241_get_info,
	.get_status = &isl9241_get_status,
	.set_mode = &isl9241_set_mode,
	.get_current = &isl9241_get_current,
	.set_current = &isl9241_set_current,
	.get_voltage = &isl9241_get_voltage,
	.set_voltage = &isl9241_set_voltage,
	.discharge_on_ac = &isl9241_discharge_on_ac,
	.get_vbus_voltage = &isl9241_get_vbus_voltage,
	.set_input_current = &isl9241_set_input_current,
	.get_input_current = &isl9241_get_input_current,
	.manufacturer_id = &isl9241_manufacturer_id,
	.device_id = &isl9241_device_id,
	.get_option = &isl9241_get_option,
	.set_option = &isl9241_set_option,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &isl9241_set_hw_ramp,
	.ramp_is_stable = &isl9241_ramp_is_stable,
	.ramp_is_detected = &isl9241_ramp_is_detected,
	.ramp_get_current_limit = &isl9241_ramp_get_current_limit,
#endif
};
