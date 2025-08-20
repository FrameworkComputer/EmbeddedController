/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "console.h"
#include "cypress_pd_common.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "raa489300.h"
#include "task.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

static int pd_voltage;
static int target_mode = -1;

static K_MUTEX_DEFINE(level_buck_mutex);

struct reg_val {
	uint8_t reg;
	uint16_t val;
};

static const struct reg_val spr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1580}, /* 5.5A */
	{RAA489300_REG_CONTROL5, 0x0801},
	{RAA489300_REG_CONTROL2, 0x2B10},
	{RAA489300_REG_CONTROL1, 0x80A0},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x3410}, /* 20V */
	{RAA489300_REG_CONTROL0, 0x5003},
};

static const struct reg_val epr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1B58}, /* 7A */
	{RAA489300_REG_CONTROL5, 0x0001},
	{RAA489300_REG_CONTROL2, 0x2B10},
	{RAA489300_REG_CONTROL1, 0x80A4},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x3e80}, /* 24V */
	{RAA489300_REG_CONTROL0, 0x5001},
};

static const struct reg_val enter_epr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1B58}, /* 7A */
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x2EE0},	/* 18V */
	{RAA489300_REG_CONTROL5, 0x0801},
	{RAA489300_REG_CONTROL2, 0x2B90},
	{RAA489300_REG_CONTROL1, 0x80A4},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_CONTROL0, 0x5001},
};

static const struct reg_val exit_epr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1B58}, /* 7A */
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x2EE0}, /* 18V */
	{RAA489300_REG_CONTROL5, 0x0001},
	{RAA489300_REG_CONTROL2, 0x2B10},
	{RAA489300_REG_CONTROL1, 0x80A4},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_CONTROL0, 0x5001},
};

static const struct reg_val dc_values[] = {
	{RAA489300_REG_CONTROL3, 0x1000},
	{RAA489300_REG_CONTROL2, 0x0B00},
	{RAA489300_REG_CONTROL0, 0x0000},
};

int write_level_buck_registers(enum level_buck_mode mode)
{
	int rv;
	const struct reg_val *reg_values;
	size_t size;

	mutex_lock(&level_buck_mutex);

	switch (mode) {
	case LEVEL_BUCK_SPR:
		reg_values = spr_values;
		size = ARRAY_SIZE(spr_values);
		break;
	case LEVEL_BUCK_EPR:
		reg_values = epr_values;
		size = ARRAY_SIZE(epr_values);
		break;
	case LEVEL_BUCK_ENTER_EPR:
		reg_values = enter_epr_values;
		size = ARRAY_SIZE(enter_epr_values);
		break;
	case LEVEL_BUCK_EXIT_EPR:
		reg_values = exit_epr_values;
		size = ARRAY_SIZE(exit_epr_values);
		break;
	case LEVEL_BUCK_DC:
		reg_values = dc_values;
		size = ARRAY_SIZE(dc_values);
		break;
	default:
		mutex_unlock(&level_buck_mutex);
		return EC_ERROR_INVAL;
	}

	for (size_t i = 0; i < size; i++) {
		uint8_t reg = reg_values[i].reg;
		uint16_t val = reg_values[i].val;

		rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, reg, val);
		if (rv != EC_SUCCESS) {
			CPRINTS("3Level-Buck write failed reg 0x%02X", reg);
			mutex_unlock(&level_buck_mutex);
			return rv;
		}
	}

	mutex_unlock(&level_buck_mutex);
	return EC_SUCCESS;
}

static void level_buck_switch_spr(void);
DECLARE_DEFERRED(level_buck_switch_spr);

static void level_buck_switch_epr(void);
DECLARE_DEFERRED(level_buck_switch_epr);

int level_buck_check_expected_state(enum level_buck_mode mode)
{
	int rv;
	int val = 0x0000;
	uint16_t expected;

	rv = i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_INFORMATION1, &val);

	if (rv)
		return rv;

	if (mode == LEVEL_BUCK_DC) {
		/* DC mode [11:8]=0001 */
		expected = PSM_SLEEP_STATE;
	} else if (mode == LEVEL_BUCK_SPR) {
		/* PTM mode [13:12]=10, [11:8]=0110 */
		expected = OPER_MODE_FORWARD_PTM | PSM_FORWARD_PTM_STATE;
	} else {
		/* BUCK mode [13:12]=11, [11:8]=0101 */
		expected = OPER_MODE_FORWARD_BUCK | PSM_FORWARD_SWITCHING_STATE;
	}

	/* Check whether the state machine status is as expected */
	if ((val & RAA489300_STATE_MASK) == expected) {
		CPRINTS("3Level-Buck is %s mode", mode == LEVEL_BUCK_DC  ? "DC" :
			mode == LEVEL_BUCK_SPR ? "PTM" : "BUCK");
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;
}

static void configure_buck_mode(enum level_buck_mode mode)
{
	int rv;
	int val = 0x0000;

	/*
	 * Early exit if the system state does not match the requested mode.
	 *
	 * - SPR mode: only valid if AC is present and PD voltage <= 20V
	 * - EPR mode: only valid if PD voltage > 20V
	 * - DC mode:  only valid if AC is not present
	 */
	switch (mode) {
	case LEVEL_BUCK_SPR:
		if ((!extpower_is_present()) || pd_voltage > 20000)
			return;
		break;
	case LEVEL_BUCK_EPR:
		if (pd_voltage <= 20000)
			return;
		break;
	case LEVEL_BUCK_DC:
		if (extpower_is_present())
			return;
		break;
	default:
		break;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_INFORMATION1, &val)) {
		CPRINTS("3Level-Buck read register fail");
		return;
	}

	/* check the regulator has gone to the reset state */
	if ((val & PSM_MASK) == PSM_RESET_STATE) {
		crec_msleep(150);
	}

	/* attempt to set mode */
	if (write_level_buck_registers(mode)) {
		return;
	}

	mutex_lock(&level_buck_mutex);

	/* check the regulator is ready */
	rv = level_buck_check_expected_state(mode);

	if ((rv != EC_SUCCESS) && (mode == LEVEL_BUCK_EPR)) {
		hook_call_deferred(&level_buck_switch_epr_data, 200 * MSEC);
	}

	mutex_unlock(&level_buck_mutex);
}

static void level_buck_switch_spr(void)
{
	configure_buck_mode(LEVEL_BUCK_SPR);
}
static void level_buck_switch_epr(void)
{
	configure_buck_mode(LEVEL_BUCK_EPR);
}
static void level_buck_switch_dc(void)
{
	configure_buck_mode(LEVEL_BUCK_DC);
}

void board_level_buck_update(void)
{
	static int pre_pd_voltage = -1;
	int power_uw = cypd_get_ac_power();

	pd_voltage = cypd_get_active_port_voltage();

	/* AC is unplugged and battery is present, switch to DC mode */
	if (!extpower_is_present() && battery_is_present() == BP_YES) {
		target_mode = LEVEL_BUCK_DC;
	} else if (pd_voltage <= 20000) {
		target_mode = LEVEL_BUCK_SPR;
	} else {
		target_mode = LEVEL_BUCK_EPR;
	}

	if (pre_pd_voltage != pd_voltage) {
		CPRINTS("3lv-buck update! V:%dmV,W:%dmW", pd_voltage, power_uw);
		switch (target_mode) {
		case LEVEL_BUCK_SPR:
			level_buck_switch_spr();
			break;
		case LEVEL_BUCK_EPR:
			level_buck_switch_epr();
			break;
		case LEVEL_BUCK_DC:
			level_buck_switch_dc();
			break;
		default:
			break;
		}

		if (pd_voltage < 9000) {
			level_buck_set_acok_reference(3900); /* 0x0F00 */
		} else if (pd_voltage < 20000) {
			level_buck_set_acok_reference(7000); /* 0x1B00 */
		} else if (pd_voltage < 28000) {
			level_buck_set_acok_reference(14500); /* 0x3800 */
		} else if (pd_voltage < 36000) {
			level_buck_set_acok_reference(20400); /* 0x4F00 */
		} else {
			level_buck_set_acok_reference(26000); /* 0x6500 */
		}

		pre_pd_voltage = pd_voltage;
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, board_level_buck_update, HOOK_PRIO_DEFAULT);

void level_buck_set_acok_reference(int mv)
{
	uint16_t reg;

	reg = RAA489300_MV_TO_VIN(mv);

	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_VINOK_REFERENCE, reg);
	i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_MIN_INPUT_VOLTAGE, reg);
}

void level_buck_set_input_current_limit(int ma)
{
	int rv;

	mutex_lock(&level_buck_mutex);
	rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
					 RAA489300_REG_INPUT_CURRENT_LIMIT, ma);
	mutex_unlock(&level_buck_mutex);

	if (rv != EC_SUCCESS)
		CPRINTS("raa489300 write current limit fail");
}

static int raa489300_cmd(int argc, const char **argv)
{
	int i, val;
	uint8_t reg;
	uint16_t value;
	char *e;

	if (argc == 2 && !strncmp(argv[1], "get", 3)) {
		/* Print last target mode */
		ccprintf("Last Buck Mode: ");
		switch (target_mode) {
		case LEVEL_BUCK_SPR:
			ccprintf("SPR\n");
			break;
		case LEVEL_BUCK_EPR:
			ccprintf("EPR\n");
			break;
		case LEVEL_BUCK_ENTER_EPR:
			ccprintf("ENTER_EPR\n");
			break;
		case LEVEL_BUCK_EXIT_EPR:
			ccprintf("EXIT_EPR\n");
			break;
		case LEVEL_BUCK_DC:
			ccprintf("DC\n");
			break;
		default:
			ccprintf("UNKNOWN\n");
			break;
		}
		/* Dump all readable registers*/
		static const uint8_t regs[] = {
			0x14, 0x15, 0x39, 0x3a, 0x3c, 0x3d, 0x3f, 0x40, 0x43,
			0x49, 0x4b, 0x4c, 0x4e, 0x4f, 0x90, 0x91,
		};

		for (i = 0; i < ARRAY_SIZE(regs); ++i) {
			if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				regs[i], &val))
				continue;
			ccprintf("raa489300 REG 0x%02x:  0x%04x\n", regs[i], val);
		}
	}

	if (argc >= 4 && !strncmp(argv[1], "set", 3)) {
		reg = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;
		value = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			reg, value)) {
			ccprintf("Failed to write 0x%04x to REG 0x%02x\n", value, reg);
			return EC_ERROR_UNKNOWN;
		}

		ccprintf("raa489300 REG 0x%02x set to 0x%04x\n", reg, value);
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(raa489300, raa489300_cmd,
			"raa489300 set/get reg value",
			"Set/Get raa489300 register");
