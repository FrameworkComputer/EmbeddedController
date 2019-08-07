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
	.current_max  = BC_REG_TO_CURRENT(CHARGE_I_MAX),
	.current_min  = BC_REG_TO_CURRENT(CHARGE_I_MIN),
	.current_step = BC_REG_TO_CURRENT(CHARGE_I_STEP),
	.input_current_max  = AC_REG_TO_CURRENT(INPUT_I_MAX),
	.input_current_min  = AC_REG_TO_CURRENT(INPUT_I_MIN),
	.input_current_step = AC_REG_TO_CURRENT(INPUT_I_STEP),
};

static inline int isl9241_read(int offset, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			  offset, value);
}

static inline int isl9241_write(int offset, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			   offset, value);
}

/* chip specific interfaces */

/*****************************************************************************/
/* Charger interfaces */
int charger_set_input_current(int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = isl9241_write(ISL9241_REG_ADAPTER_CUR_LIMIT1, reg);
	if (rv)
		return rv;

	return isl9241_write(ISL9241_REG_ADAPTER_CUR_LIMIT2, reg);
}

int charger_get_input_current(int *input_current)
{
	int rv;

	rv = isl9241_read(ISL9241_REG_ADAPTER_CUR_LIMIT1, input_current);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(*input_current);
	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return isl9241_read(ISL9241_REG_MANUFACTURER_ID, id);
}

int charger_device_id(int *id)
{
	return isl9241_read(ISL9241_REG_DEVICE_ID, id);
}

int charger_get_option(int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = isl9241_read(ISL9241_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = isl9241_read(ISL9241_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;

	rv = isl9241_write(ISL9241_REG_CONTROL0, option & 0xFFFF);
	if (rv)
		return rv;

	return isl9241_write(ISL9241_REG_CONTROL1, (option >> 16) & 0xFFFF);
}

const struct charger_info *charger_get_info(void)
{
	return &isl9241_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int reg;

	/* Level 2 charger */
	*status = CHARGER_LEVEL_2;

	/* Charge inhibit status */
	rv = isl9241_read(ISL9241_REG_MIN_SYSTEM_VOLTAGE, &reg);
	if (rv)
		return rv;
	if (!reg)
		*status |= CHARGER_CHARGE_INHIBITED;

	/* Battery present & AC present status */
	rv = isl9241_read(ISL9241_REG_INFORMATION2, &reg);
	if (rv)
		return rv;
	if (!(reg & ISL9241_INFORMATION2_BATGONE_PIN))
		*status |= CHARGER_BATTERY_PRESENT;
	if (reg & ISL9241_INFORMATION2_ACOK_PIN)
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;

	/*
	 * See crosbug.com/p/51196. Always disable learn mode unless it was set
	 * explicitly.
	 */
	if (!learn_mode) {
		rv = charger_discharge_on_ac(0);
		if (rv)
			return rv;
	}

	/*
	 * Charger inhibit
	 * MinSystemVoltage 0x00h = disables all battery charging
	 */
	rv = isl9241_write(ISL9241_REG_MIN_SYSTEM_VOLTAGE,
		mode & CHARGE_FLAG_INHIBIT_CHARGE ?
		0 : battery_get_info()->voltage_min);
	if (rv)
		return rv;

	/* POR reset */
	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = isl9241_write(ISL9241_REG_CONTROL3,
			ISL9241_CONTROL3_DIGITAL_RESET);
	}

	return rv;
}

int charger_get_current(int *current)
{
	int rv;

	rv = isl9241_read(ISL9241_REG_CHG_CURRENT_LIMIT, current);
	if (rv)
		return rv;

	*current = BC_REG_TO_CURRENT(*current);
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	return isl9241_write(ISL9241_REG_CHG_CURRENT_LIMIT,
				BC_CURRENT_TO_REG(current));
}

int charger_get_voltage(int *voltage)
{
	return isl9241_read(ISL9241_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

int charger_set_voltage(int voltage)
{
	return isl9241_write(ISL9241_REG_MAX_SYSTEM_VOLTAGE, voltage);
}

int charger_get_vbus_voltage(int port)
{
	int adc_val = 0;
	int ctl3_val;
	int rv;

	/* Get current Control3 value */
	rv = isl9241_read(ISL9241_REG_CONTROL3, &ctl3_val);
	if (rv)
		goto error;

	/* Enable ADC */
	if (!(ctl3_val & ISL9241_CONTROL3_ENABLE_ADC)) {
		rv = isl9241_write(ISL9241_REG_CONTROL3,
				   ctl3_val | ISL9241_CONTROL3_ENABLE_ADC);
		if (rv)
			goto error;
	}

	/* Read voltage ADC value */
	rv = isl9241_read(ISL9241_REG_VIN_ADC_RESULTS, &adc_val);
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

error_restore_ctl3:
	/* Restore Control3 value */
	if (!(ctl3_val & ISL9241_CONTROL3_ENABLE_ADC))
		(void)isl9241_write(ISL9241_REG_CONTROL3, ctl3_val);

error:
	if (rv)
		CPRINTF("Could not read VBUS ADC! Error: %d\n", rv);

	return adc_val;
}

int charger_post_init(void)
{
	return EC_SUCCESS;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int control1;

	mutex_lock(&control1_mutex);

	rv = isl9241_read(ISL9241_REG_CONTROL1, &control1);
	if (rv)
		goto out;

	if (enable)
		control1 |= ISL9241_CONTROL1_LEARN_MODE;
	else
		control1 &= ~ISL9241_CONTROL1_LEARN_MODE;

	rv = isl9241_write(ISL9241_REG_CONTROL1, control1);
	if (!rv)
		learn_mode = enable;

out:
	mutex_unlock(&control1_mutex);
	return rv;
}

/*****************************************************************************/
/* ISL-9241 initialization */
static void isl9241_init(void)
{
	int reg;

	const struct battery_info *bi = battery_get_info();

	/*
	 * Set the MaxSystemVoltage to battery maximum,
	 * 0x00=disables switching charger states
	 */
	if (isl9241_write(ISL9241_REG_MAX_SYSTEM_VOLTAGE, bi->voltage_max))
		goto init_fail;

	/*
	 * Set the MinSystemVoltage to battery minimum,
	 * 0x00=disables all battery charging
	 */
	if (isl9241_write(ISL9241_REG_MIN_SYSTEM_VOLTAGE, bi->voltage_min))
		goto init_fail;

	/*
	 * Set control2 register to
	 * [15:13]: Trickle Charging Current (battery pre-charge current)
	 * [12]   : Two-Level Adapter Current Limit (enable)
	 * [10:9] : Prochot# Debounce time (1000us)
	 */
	if (isl9241_read(ISL9241_REG_CONTROL2, &reg))
		goto init_fail;

	if (isl9241_write(ISL9241_REG_CONTROL2,
		reg | ISL9241_CONTROL2_TRICKLE_CHG_CURR(bi->precharge_current) |
		ISL9241_CONTROL2_TWO_LEVEL_ADP_CURR |
		ISL9241_CONTROL2_PROCHOT_DEBOUNCE_1000))
		goto init_fail;

	/*
	 * Set control3 register to
	 * [14]: ACLIM Reload (Do not reload)
	 */
	if (isl9241_read(ISL9241_REG_CONTROL3, &reg))
		goto init_fail;

	if (isl9241_write(ISL9241_REG_CONTROL3,
		reg | ISL9241_CONTROL3_ACLIM_RELOAD))
		goto init_fail;

#ifndef CONFIG_CHARGE_RAMP_HW
	if (isl9241_read(ISL9241_REG_CONTROL0, &reg))
		goto init_fail;
	if (isl9241_write(ISL9241_REG_CONTROL0,
		reg | ISL9241_CONTROL0_INPUT_VTG_REGULATION))
		goto init_fail;
#endif

	/*
	 * No need to proceed with the rest of init if we sysjump'd to this
	 * image as the input current limit has already been set.
	 */
	if (system_jumped_to_this_image())
		return;

	/* Initialize the input current limit to the board's default. */
	if (charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT))
		goto init_fail;

	return;

init_fail:
	CPRINTF("ISL9241_init failed!");
}
DECLARE_HOOK(HOOK_INIT, isl9241_init, HOOK_PRIO_INIT_I2C + 1);

/*****************************************************************************/
/* Hardware current ramping */

#ifdef CONFIG_CHARGE_RAMP_HW
int charger_set_hw_ramp(int enable)
{
	int rv, reg;

	rv = isl9241_read(ISL9241_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	/* HW ramp is controlled by input voltage regulation reference bits */
	if (enable)
		reg &= ~ISL9241_CONTROL0_INPUT_VTG_REGULATION;
	else
		reg |= ISL9241_CONTROL0_INPUT_VTG_REGULATION;

	return isl9241_write(ISL9241_REG_CONTROL0, reg);
}

int chg_ramp_is_stable(void)
{
	/*
	 * Since ISL cannot read the current limit that the ramp has settled
	 * on, then we can never consider the ramp stable, because we never
	 * know what the stable limit is.
	 */
	return 0;
}

int chg_ramp_is_detected(void)
{
	return 1;
}

int chg_ramp_get_current_limit(void)
{
	int reg;

	if (isl9241_read(ISL9241_REG_IADP_ADC_RESULTS, &reg))
		return 0;

	/* LSB value of register = 22.2mA */
	return (reg * 222) / 10;
}
#endif /* CONFIG_CHARGE_RAMP_HW */

/*****************************************************************************/
#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int low, int high)
{
	int reg;
	int regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		CPRINTF("[%Xh] = ", reg);
		rv = isl9241_read(reg, &regval);
		if (!rv)
			CPRINTF("0x%04x\n", regval);
		else
			CPRINTF("ERR (%d)\n", rv);
		cflush();
	}
}

static int command_isl9241_dump(int argc, char **argv)
{
	dump_reg_range(0x14, 0x15);
	dump_reg_range(0x38, 0x40);
	dump_reg_range(0x43, 0x43);
	dump_reg_range(0x47, 0x4F);
	dump_reg_range(0x80, 0x87);
	dump_reg_range(0x90, 0x91);
	dump_reg_range(0xFE, 0xFF);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger_dump, command_isl9241_dump, "",
			"Dumps ISL9241 registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */
