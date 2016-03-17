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
static enum BD99955_COMMANDS charger_map_cmd = BD99955_INVALID_COMMAND;

static struct mutex bd99955_map_mutex;

static inline int ch_raw_read16(int cmd, int *param,
				enum BD99955_COMMANDS map_cmd)
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
					enum BD99955_COMMANDS map_cmd)
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
	int rv;

	rv = ch_raw_read16(BD99955_CMD_IBUS_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
	if (rv)
		return rv;

	return ch_raw_read16(BD99955_CMD_ICC_LIM_SET, input_current,
				BD99955_BAT_CHG_COMMAND);
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
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_set_option(int option)
{
	return EC_ERROR_UNIMPLEMENTED;
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bd99955_charger_info;
}

int charger_get_status(int *status)
{
	*status = CHARGER_LEVEL_2;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	/* BD99955 does not support inhibit mode setting. */
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

int charger_post_init(void)
{
	int rv;

	/*
	 * TODO: Disable charger & re-enable to initialize it.
	 */
	rv = charger_discharge_on_ac(1);
	if (rv)
		return rv;

	return charger_discharge_on_ac(0);
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int reg;

	rv = ch_raw_read16(BD99955_CMD_CHGOP_SET2, &reg,
				BD99955_EXTENDED_COMMAND);
	if (rv)
		return rv;

	if (enable) {
		reg |= BD99955_CHGOP_SET2_BATT_LEARN;
		reg &= ~BD99955_CHGOP_SET2_CHG_EN;
	} else {
		reg &= ~BD99955_CHGOP_SET2_BATT_LEARN;
		reg |= BD99955_CHGOP_SET2_CHG_EN;
	}

	return ch_raw_write16(BD99955_CMD_CHGOP_SET2, reg,
				BD99955_EXTENDED_COMMAND);
}
