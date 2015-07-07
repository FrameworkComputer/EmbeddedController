/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ISL9237 battery charger driver.
 */

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "i2c.h"
#include "isl9237.h"
#include "util.h"

#define DEFAULT_R_AC 20
#define DEFAULT_R_SNS 10
#define R_AC CONFIG_CHARGER_SENSE_RESISTOR_AC
#define R_SNS CONFIG_CHARGER_SENSE_RESISTOR
#define REG_TO_CURRENT(REG) ((REG) * DEFAULT_R_SNS / R_SNS)
#define CURRENT_TO_REG(CUR) ((CUR) * R_SNS / DEFAULT_R_SNS)
#define AC_REG_TO_CURRENT(REG) ((REG) * DEFAULT_R_AC / R_AC)
#define AC_CURRENT_TO_REG(CUR) ((CUR) * R_AC / DEFAULT_R_AC)

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Charger parameters */
static const struct charger_info isl9237_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = REG_TO_CURRENT(CHARGE_I_MAX),
	.current_min  = REG_TO_CURRENT(CHARGE_I_MIN),
	.current_step = REG_TO_CURRENT(CHARGE_I_STEP),
	.input_current_max  = AC_REG_TO_CURRENT(INPUT_I_MAX),
	.input_current_min  = AC_REG_TO_CURRENT(INPUT_I_MIN),
	.input_current_step = AC_REG_TO_CURRENT(INPUT_I_STEP),
};

static inline int raw_read8(int offset, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static inline int raw_read16(int offset, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static inline int raw_write16(int offset, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static int isl9237_set_current(uint16_t current)
{
	return raw_write16(ISL9237_REG_CHG_CURRENT, CURRENT_TO_REG(current));
}

static int isl9237_set_voltage(uint16_t voltage)
{
	return raw_write16(ISL9237_REG_SYS_VOLTAGE_MAX, voltage);
}

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = raw_write16(ISL9237_REG_ADAPTER_CURRENT1, reg);
	if (rv)
		return rv;

	return raw_write16(ISL9237_REG_ADAPTER_CURRENT2, reg);
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL9237_REG_ADAPTER_CURRENT1, &reg);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL9237_REG_MANUFACTURER_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

int charger_device_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL9237_REG_DEVICE_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

int charger_get_option(int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = raw_read8(ISL9237_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = raw_read16(ISL9237_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;
	uint16_t reg;

	reg = option & 0xffff;
	rv = raw_write16(ISL9237_REG_CONTROL0, reg);

	if (rv)
		return rv;

	reg = (option >> 16) & 0xffff;
	return raw_write16(ISL9237_REG_CONTROL1, reg);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &isl9237_charger_info;
}

int charger_get_status(int *status)
{
	*status = CHARGER_LEVEL_2;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	/* ISL9237 does not support inhibit mode setting. */
	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL9237_REG_CHG_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	return isl9237_set_current(current);
}

int charger_get_voltage(int *voltage)
{
	return raw_read16(ISL9237_REG_SYS_VOLTAGE_MAX, voltage);
}

int charger_set_voltage(int voltage)
{
	/* The ISL9237 will drop voltage to as low as requested. As the
	 * charger state machine will pass in 0 voltage, protect the system
	 * voltage by capping to the minimum. The reason is that the ISL9237
	 * only can regulate the system voltage which will kill the board's
	 * power if below 0. */
	if (voltage == 0) {
		const struct battery_info *bi = battery_get_info();
		voltage = bi->voltage_min;
	}

	return isl9237_set_voltage(voltage);
}

int charger_post_init(void)
{
	return EC_SUCCESS;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int control1;

	rv = raw_read16(ISL9237_REG_CONTROL1, &control1);
	if (rv)
		return rv;

	control1 &= ~ISL9237_C1_LEARN_MODE_AUTOEXIT;
	if (enable)
		control1 |= ISL9237_C1_LEARN_MODE_ENABLE;
	else
		control1 &= ~ISL9237_C1_LEARN_MODE_ENABLE;

	return raw_write16(ISL9237_REG_CONTROL1, control1);
}

#ifdef CONFIG_CHARGER_ADC_AMON_BMON
/**
 * Get charger AMON and BMON current.
 */
static int console_command_amon_bmon(int argc, char **argv)
{
	int adc, curr, val, ret;

	ret = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
			 ISL9237_REG_CONTROL1, &val);
	if (ret)
		return ret;

	/* Enable monitor */
	val &= ~ISL9237_C1_DISABLE_MON;
	if (argc == 1 || (argc >= 2 && argv[1][0] == 'a')) {
		/* Switch to AMON */
		val &= ~ISL9237_C1_SELECT_BMON;
		ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
				  ISL9237_REG_CONTROL1, val);
		if (ret)
			return ret;

		adc = adc_read_channel(ADC_AMON_BMON);
		curr = adc / CONFIG_CHARGER_SENSE_RESISTOR_AC;
		CPRINTF("AMON: %d uV, %d mA\n", adc, curr);
	}

	if (argc == 1 || (argc >= 2 && argv[1][0] == 'b')) {
		/* Switch to BMON */
		val |= ISL9237_C1_SELECT_BMON;
		ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
				  ISL9237_REG_CONTROL1, val);
		if (ret)
			return ret;

		adc = adc_read_channel(ADC_AMON_BMON);
		curr = adc / CONFIG_CHARGER_SENSE_RESISTOR;
		CPRINTF("BMON: %d uV, %d mA\n", adc, curr);
	}

	return ret;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
			"amonbmon [a|b]",
			"Get charger AMON/BMON voltage diff, current",
			NULL);
#endif /* CONFIG_CHARGER_ADC_AMON_BMON */
