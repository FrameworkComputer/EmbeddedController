/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "cypress_pd_common.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "raa489300.h"
#include "task.h"

#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

static int pd_voltage;

static K_MUTEX_DEFINE(level_buck_mutex);

struct reg_val {
	uint8_t reg;
	uint16_t val;
};

static const struct reg_val spr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1580},
	{RAA489300_REG_CONTROL5, 0x0801},
	{RAA489300_REG_CONTROL2, 0x0B00},
	{RAA489300_REG_CONTROL1, 0x80A0},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x3410},
	{RAA489300_REG_CONTROL0, 0x1003},
};

static const struct reg_val epr_values[] = {
	{RAA489300_REG_OUTPUT_CURRENT_LIMIT, 0x1B58},
	{RAA489300_REG_OUTPUT_VOLTAGE, 0x2EE0},
	{RAA489300_REG_CONTROL5, 0x0001},
	{RAA489300_REG_CONTROL2, 0x0B00},
	{RAA489300_REG_CONTROL1, 0x80A4},
	{RAA489300_REG_CONTROL4, 0x0140},
	{RAA489300_REG_CONTROL3, 0x1001},
	{RAA489300_REG_CONTROL0, 0x1001},
};

static int level_buck_set_output_voltage(int mv)
{
	uint16_t reg_value = 0;

	if (mv > AVS_VOLTAGE_MAX) {
		mv = AVS_VOLTAGE_MAX;
	}

	if (mv > PPS_VOLTAGE_MAX) {
		/* AVS mode */
		reg_value = ((mv / AVS_VOLTAGE_STEP_MV) << 4);
	} else {
		/* PPS mode */
		reg_value = ((mv / PPS_VOLTAGE_STEP_MV) << 3);
	}

	return i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_OUTPUT_VOLTAGE, reg_value);
}

int write_level_buck_registers(bool is_epr)
{
	int rv;
	const struct reg_val *reg_values;
	size_t size;

	mutex_lock(&level_buck_mutex);

	if (is_epr) {
		reg_values = epr_values;
		size = ARRAY_SIZE(epr_values);
	} else {
		reg_values = spr_values;
		size = ARRAY_SIZE(spr_values);
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
		crec_msleep(2);
	}

	mutex_unlock(&level_buck_mutex);
	return EC_SUCCESS;
}

static void level_buck_switch_spr(void);
DECLARE_DEFERRED(level_buck_switch_spr);

static void level_buck_switch_epr(void);
DECLARE_DEFERRED(level_buck_switch_epr);

static void configure_buck_mode(bool is_epr)
{
	int val = 0x0000;

	if (is_epr) {
		if ((!extpower_is_present()) || (pd_voltage <= 20000))
			return;
	} else {
		if (pd_voltage > 20000)
			return;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_INFORMATION1, &val)) {
		CPRINTS("3Level-Buck not ready");
		hook_call_deferred(is_epr ? &level_buck_switch_epr_data :
				   &level_buck_switch_spr_data, 500 * MSEC);
		return;
	}

	/* check the regulator has gone to the reset state */
	if (((val >> 8) & 0xF) == 0)
		crec_msleep(150);

	if (write_level_buck_registers(is_epr)) {
		return;
	}

	mutex_lock(&level_buck_mutex);

	if (is_epr) {
		if (level_buck_set_output_voltage(24000))
			goto unlock;
	}

	if (i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
		RAA489300_REG_INFORMATION1, &val)) {
		CPRINTS("read raa489300 info1 reg fail");
		goto unlock;
	}

	if (((val >> 8) & 0x3F) == (is_epr ? 0x35 : 0x26)) {
		CPRINTS("3Level-Buck Success %s", is_epr ? "EPR" : "SPR");
		goto unlock;
	}

	if (is_epr) {
		hook_call_deferred(&level_buck_switch_epr_data, 200 * MSEC);
	}

unlock:
	mutex_unlock(&level_buck_mutex);
}

static void level_buck_switch_spr(void)
{
	configure_buck_mode(false);
}
static void level_buck_switch_epr(void)
{
	configure_buck_mode(true);
}

void board_level_buck_update(void)
{
	static int pre_pd_voltage;
	int power_uw = cypd_get_ac_power();

	pd_voltage = cypd_get_active_port_voltage();

	if (pre_pd_voltage != pd_voltage) {
		CPRINTS("3lv-buck update! V:%dmV,W:%dmW", pd_voltage, power_uw);
		if (pd_voltage <= 20000) {
			level_buck_switch_spr();
		} else if (pd_voltage > 20000) {
			level_buck_switch_epr();
		}
		/* TODO: Need to be replaced with 3level-buck function and macro */
		if (pd_voltage <= 20000) {
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_VINOK_REFERENCE, 0x3800);
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_MIN_INPUT_VOLTAGE, 0x3800);
		} else if (pd_voltage <= 28000) {
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_VINOK_REFERENCE, 0x4F00);
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_MIN_INPUT_VOLTAGE, 0x4F00);
		} else {
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_VINOK_REFERENCE, 0x6500);
			i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_MIN_INPUT_VOLTAGE, 0x6500);
		}

		pre_pd_voltage = pd_voltage;
	}
}
DECLARE_HOOK(HOOK_POWER_SUPPLY_CHANGE, board_level_buck_update, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, board_level_buck_update, HOOK_PRIO_POST_I2C + 1);

void level_buck_set_input_current_limit(int ma)
{
	int rv;

	if (!extpower_is_present())
		return;

	mutex_lock(&level_buck_mutex);
	rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
					 RAA489300_REG_INPUT_CURRENT_LIMIT, ma);
	mutex_unlock(&level_buck_mutex);

	if (rv != EC_SUCCESS)
		CPRINTS("raa489300 write current limit fail");
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
