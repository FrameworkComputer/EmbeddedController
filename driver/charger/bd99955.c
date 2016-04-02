/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ROHM BD99955 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bd99955.h"
#include "charger.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Charger parameters */
static const struct charger_info bd99955_charger_info = {
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

/* Charge command code map */
static enum bd99955_command charger_map_cmd = BD99955_INVALID_COMMAND;

static struct mutex bd99955_map_mutex;

static inline int ch_raw_read16(int cmd, int *param,
				enum bd99955_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd99955_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
				 BD99955_CMD_MAP_SET, map_cmd);
		if (rv)
			goto bd99955_read_cleanup;

		charger_map_cmd = map_cmd;
	}

	rv = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, cmd, param);

bd99955_read_cleanup:
	mutex_unlock(&bd99955_map_mutex);

	return rv;
}

static inline int ch_raw_write16(int cmd, int param,
					enum bd99955_command map_cmd)
{
	int rv;

	/* Map the Charge command code to appropriate region */
	mutex_lock(&bd99955_map_mutex);
	if (charger_map_cmd != map_cmd) {
		rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
					BD99955_CMD_MAP_SET, map_cmd);
		if (rv)
			goto bd99955_write_cleanup;

		charger_map_cmd = map_cmd;
	}

	rv = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, cmd, param);

bd99955_write_cleanup:
	mutex_unlock(&bd99955_map_mutex);

	return rv;
}

/* BD99955 local interfaces */

static int bd99955_charger_enable(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable)
		reg |= BD99955_CMD_CHGOP_SET2_CHG_EN;
	else
		reg &= ~BD99955_CMD_CHGOP_SET2_CHG_EN;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
				BD99955_EXTENDED_COMMAND);
}

static int bd99955_por_reset(void)
{
	return ch_raw_write16(BD99955_CMD_SYSTEM_CTRL_SET,
				BD99955_CMD_SYSTEM_CTRL_SET_OTPLD |
				BD99955_CMD_SYSTEM_CTRL_SET_ALLRST,
				BD99955_EXTENDED_COMMAND);
}

static int bd99955_reset_to_zero(void)
{
	int rv;

	rv = charger_set_current(0);
	if (rv)
		return rv;

	return charger_set_voltage(0);
}

static int bd99955_get_charger_op_status(int *status)
{
	return ch_raw_read16(BD99955_CMD_CHGOP_STATUS, status,
				BD99955_EXTENDED_COMMAND);
}


/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;

	/* Input current step 32 mA */
	input_current &= ~0x1F;

	rv = ch_raw_write16(BD99955_CMD_IBUS_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD99955_CMD_ICC_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_get_input_current(int *input_current)
{
	return ch_raw_read16(BD99955_CMD_CUR_ILIM_VAL, input_current,
			     BD99955_EXTENDED_COMMAND);
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	return ch_raw_read16(BD99955_CMD_CHIP_ID, id, BD99955_EXTENDED_COMMAND);
}

int charger_get_option(int *option)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET1, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	*option = reg;
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	*option |= reg << 16;

	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;

	rv = ch_raw_write16(BD99955_CMD_CHGOP_SET1, option & 0xFFFF,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, (option >> 16) & 0xFFFF,
				BD99955_EXTENDED_COMMAND);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bd99955_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int reg;
	int ch_status;

	/* charger level */
	*status = CHARGER_LEVEL_2;

	/* charger enable/inhibit */
	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & BD99955_CMD_CHGOP_SET2_CHG_EN))
		*status |= CHARGER_CHARGE_INHIBITED;

	/* charger alarm enable/inhibit */
	rv = ch_raw_read16(BD99955_CMD_PROCHOT_CTRL_SET, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (!(reg & (BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN4 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN3 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN2 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN1 |
			BD99955_CMD_PROCHOT_CTRL_SET_PROCHOT_EN0)))
		*status |= CHARGER_ALARM_INHIBITED;

	rv = bd99955_get_charger_op_status(&reg);
	if (rv)
		return rv;

	/* power fail */
	if (!(reg & BD99955_CMD_CHGOP_STATUS_RBOOST_UV))
		*status |= CHARGER_POWER_FAIL;

	/* Safety signal ranges & battery presence */
	ch_status = (reg & BD99955_CMD_CHGOP_STATUS_BATTEMP0) |
			((reg & BD99955_CMD_CHGOP_STATUS_BATTEMP1) << 1) |
			((reg & BD99955_CMD_CHGOP_STATUS_BATTEMP2) << 2);

	*status |= CHARGER_BATTERY_PRESENT;

	switch (ch_status) {
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_COLD1:
		*status |= CHARGER_RES_COLD;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_COLD2:
		*status |= CHARGER_RES_COLD;
		*status |= CHARGER_RES_UR;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT1:
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT2:
		*status |= CHARGER_RES_HOT;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_HOT3:
		*status |= CHARGER_RES_HOT;
		*status |= CHARGER_RES_OR;
		break;
	case BD99955_CMD_CHGOP_STATUS_BATTEMP_BATOPEN:
		*status &= ~CHARGER_BATTERY_PRESENT;
	default:
		break;
	}

	/* source of power */
	if (bd99955_extpower_is_present())
		*status |= CHARGER_AC_PRESENT;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;
	int enable;

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		enable = 0;
	else
		enable = 1;

	rv = bd99955_charger_enable(enable);
	if (rv)
		return rv;

	if (mode & CHARGE_FLAG_POR_RESET) {
		rv = bd99955_por_reset();
		if (rv)
			return rv;
	}

	if (mode & CHARGE_FLAG_RESET_TO_ZERO) {
		rv = bd99955_reset_to_zero();
		if (rv)
			return rv;
	}

	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	return ch_raw_read16(BD99955_CMD_CHG_CURRENT, current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_set_current(int current)
{
	/* Charge current step 64 mA */
	current &= ~0x3F;

	return ch_raw_write16(BD99955_CMD_CHG_CURRENT, current,
				BD99955_BAT_CHG_COMMAND);
}

int charger_get_voltage(int *voltage)
{
	return ch_raw_read16(BD99955_CMD_CHG_VOLTAGE, voltage,
				BD99955_BAT_CHG_COMMAND);
}

int charger_set_voltage(int voltage)
{
	/*
	 * The BD99955 will drop voltage to as low as requested. As the
	 * charger state machine will pass in 0 voltage, protect the system
	 * voltage by capping to the minimum. The reason is that the BD99955
	 * only can regulate the system voltage which will kill the board's
	 * power if below 0.
	 */
	if (voltage == 0) {
		const struct battery_info *bi = battery_get_info();

		voltage = bi->voltage_min;
	}

	/* Charge voltage step 16 mV */
	voltage &= ~0x0F;
	return ch_raw_write16(BD99955_CMD_CHG_VOLTAGE, voltage,
				BD99955_BAT_CHG_COMMAND);
}

static void bd99995_init(void)
{
	int reg;
	const struct battery_info *bi = battery_get_info();

	/* Disable BC1.2 detection on VCC */
	if (ch_raw_read16(BD99955_CMD_VCC_UCD_SET, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg &= ~BD99955_CMD_UCD_SET_USBDETEN;
	ch_raw_write16(BD99955_CMD_VCC_UCD_SET, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Disable BC1.2 detection on VBUS */
	if (ch_raw_read16(BD99955_CMD_VBUS_UCD_SET, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg &= ~BD99955_CMD_UCD_SET_USBDETEN;
	ch_raw_write16(BD99955_CMD_VBUS_UCD_SET, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Disable BC1.2 charge enable trigger */
	if (ch_raw_read16(BD99955_CMD_CHGOP_SET1, &reg,
			  BD99955_EXTENDED_COMMAND))
		return;
	reg |= (BD99955_CMD_CHGOP_SET1_VCC_BC_DISEN |
		BD99955_CMD_CHGOP_SET1_VBUS_BC_DISEN);
	ch_raw_write16(BD99955_CMD_CHGOP_SET1, reg,
		       BD99955_EXTENDED_COMMAND);

	/* Set battery OVP to 500 + maximum battery voltage */
	ch_raw_write16(BD99955_CMD_VBATOVP_SET,
		       (bi->voltage_max + 500) & 0x7ff0,
		       BD99955_EXTENDED_COMMAND);
}
DECLARE_HOOK(HOOK_INIT, bd99995_init, HOOK_PRIO_DEFAULT);

int charger_post_init(void)
{
	return EC_SUCCESS;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable)
		reg |= BD99955_CMD_CHGOP_SET2_BATT_LEARN;
	else
		reg &= ~BD99955_CMD_CHGOP_SET2_BATT_LEARN;

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
				BD99955_EXTENDED_COMMAND);
}

/*** Non-standard interface functions ***/

int bd99955_extpower_is_present(void)
{
	int reg;

	if (ch_raw_read16(BD99955_CMD_VBUS_VCC_STATUS, &reg,
			  BD99955_EXTENDED_COMMAND))
		return 0;

	reg &= (BD99955_CMD_VBUS_VCC_STATUS_VCC_DETECT |
		BD99955_CMD_VBUS_VCC_STATUS_VBUS_DETECT);
	return !!reg;
}

int bd99955_select_input_port(enum bd99955_charge_port port)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_VIN_CTRL_SET, &reg,
			   BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (port == BD99955_CHARGE_PORT_NONE) {
		reg &= ~(BD99955_CMD_VIN_CTRL_SET_VBUS_EN |
			 BD99955_CMD_VIN_CTRL_SET_VBUS_EN);
	} else if (port == BD99955_CHARGE_PORT_VBUS) {
		reg |= BD99955_CMD_VIN_CTRL_SET_VBUS_EN;
		reg &= ~BD99955_CMD_VIN_CTRL_SET_VCC_EN;
	} else if (port == BD99955_CHARGE_PORT_VCC) {
		reg |= BD99955_CMD_VIN_CTRL_SET_VCC_EN;
		reg &= ~BD99955_CMD_VIN_CTRL_SET_VBUS_EN;
	} else {
		/* Invalid charge port */
		panic("Invalid charge port");
	}

	return ch_raw_write16(BD99955_CMD_VIN_CTRL_SET, reg,
			      BD99955_EXTENDED_COMMAND);
}

#ifdef CONFIG_CMD_CHARGER
static int read_bat(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD99955_BAT_CHG_COMMAND);
	return read;
}

static int read_ext(uint8_t cmd)
{
	int read = 0;

	ch_raw_read16(cmd, &read, BD99955_EXTENDED_COMMAND);
	return read;
}

/* Dump all readable registers on bd99955 */
static int console_bd99955_dump_regs(int argc, char **argv)
{
	int i;
	uint8_t regs[] = { 0x14, 0x15, 0x3c, 0x3d, 0x3e, 0x3f };

	/* Battery group registers */
	for (i = 0; i < ARRAY_SIZE(regs); ++i)
		ccprintf("BAT REG %4x:  %4x\n", regs[i], read_bat(regs[i]));

	/* Extended group registers */
	for (i = 0; i < 0x7f; ++i)
		ccprintf("EXT REG %4x:  %4x\n", i, read_ext(i));

	return 0;
}
DECLARE_CONSOLE_COMMAND(bd99955_dump, console_bd99955_dump_regs,
			NULL,
			"Dump all charger registers",
			NULL);

static int console_command_bd99955(int argc, char **argv)
{
	int rv, reg, data, val;
	char rw, *e;
	enum bd99955_command cmd;

	if (argc < 4)
		return EC_ERROR_PARAM_COUNT;

	rw = argv[1][0];
	if (rw == 'w' && argc < 5)
			return EC_ERROR_PARAM_COUNT;
	else if (rw != 'w' && rw != 'r')
		return EC_ERROR_PARAM1;

	reg = strtoi(argv[2], &e, 16);
	if (*e || reg < 0)
		return EC_ERROR_PARAM2;

	cmd = strtoi(argv[3], &e, 0);
	if (*e || cmd < 0)
		return EC_ERROR_PARAM3;

	if (argc == 5) {
		val = strtoi(argv[4], &e, 16);
		if (*e || val < 0)
			return EC_ERROR_PARAM4;
	}

	if (rw == 'r')
		rv = ch_raw_read16(reg, &data, cmd);
	else {
		rv = ch_raw_write16(reg, val, cmd);
		if (rv == EC_SUCCESS)
			rv = ch_raw_read16(reg, &data, cmd);
	}

	CPRINTS("register 0x%x [%d] = 0x%x [%d]", reg, reg, data, data);

	return rv;
}
DECLARE_CONSOLE_COMMAND(bd99955, console_command_bd99955,
			"bd99955 <r/w> <reg_hex> <cmd_type> | <val_hex>",
			"Read or write a charger register",
			NULL);
#endif /* CONFIG_CMD_CHARGER */
