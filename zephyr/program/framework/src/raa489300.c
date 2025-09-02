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
static bool enable_low_power_mode;

static K_MUTEX_DEFINE(level_buck_mutex);

struct reg_val {
	uint8_t reg;
	uint16_t val;
};

static const struct reg_val spr_values[] = {
	{RAA489300_REG_CONTROL5, RAA489300_C5_VALUE},
	{RAA489300_REG_CONTROL2, RAA489300_C2_VALUE_SPR},
	{RAA489300_REG_CONTROL1, RAA489300_C1_VALUE},
	{RAA489300_REG_CONTROL4, RAA489300_C4_VALUE},
	{RAA489300_REG_CONTROL3, RAA489300_C3_VALUE
							| RAA489300_C3_SWITCHING_FREQ_400K},
	{RAA489300_REG_OUTPUT_VOLTAGE, RAA489300_REG_OUTPUT_VOLTAGE_18V},
	{RAA489300_REG_CONTROL0, RAA489300_C0_VALUE
							| RAA489300_C0_ENABLE_PTM_MODE},
};

static const struct reg_val epr_values[] = {
	{RAA489300_REG_OUTPUT_VOLTAGE, RAA489300_REG_OUTPUT_VOLTAGE_24V},
	{RAA489300_REG_CONTROL5, RAA489300_C5_VALUE},
	{RAA489300_REG_CONTROL2, RAA489300_C2_VALUE_SPR
							| RAA489300_C2_ENABLE_CFLY_PRECHARGE},
	{RAA489300_REG_CONTROL1, RAA489300_C1_VALUE
							| RAA489300_C1_VOUT_OVP_33V},
	{RAA489300_REG_CONTROL4, RAA489300_C4_VALUE},
	{RAA489300_REG_CONTROL3, RAA489300_C3_VALUE
							| RAA489300_C3_SWITCHING_FREQ_400K},
	{RAA489300_REG_CONTROL0, RAA489300_C0_VALUE},
};

static const struct reg_val enter_epr_values[] = {
	{RAA489300_REG_OUTPUT_VOLTAGE, RAA489300_REG_OUTPUT_VOLTAGE_18V},
	{RAA489300_REG_CONTROL5, RAA489300_C5_VALUE},
	{RAA489300_REG_CONTROL2, RAA489300_C2_VALUE_SPR
							| RAA489300_C2_ENABLE_CFLY_PRECHARGE},
	{RAA489300_REG_CONTROL1, RAA489300_C1_VALUE
							| RAA489300_C1_VOUT_OVP_33V},
	{RAA489300_REG_CONTROL4, RAA489300_C4_VALUE},
	{RAA489300_REG_CONTROL3, RAA489300_C3_VALUE
							| RAA489300_C3_SWITCHING_FREQ_400K},
	{RAA489300_REG_CONTROL0, RAA489300_C0_VALUE},
};

static const struct reg_val exit_epr_values[] = {
	{RAA489300_REG_OUTPUT_VOLTAGE, RAA489300_REG_OUTPUT_VOLTAGE_18V},
	{RAA489300_REG_CONTROL5, RAA489300_C5_DISABLE_GP_COMPARATOR},
	{RAA489300_REG_CONTROL2, RAA489300_C2_VALUE_SPR},
	{RAA489300_REG_CONTROL1, RAA489300_C1_VALUE
							| RAA489300_C1_VOUT_OVP_33V},
	{RAA489300_REG_CONTROL4, RAA489300_C4_VALUE},
	{RAA489300_REG_CONTROL3, RAA489300_C3_VALUE
							| RAA489300_C3_SWITCHING_FREQ_400K},
	{RAA489300_REG_CONTROL0, RAA489300_C0_VALUE},
};

static const struct reg_val dc_values[] = {
	{RAA489300_REG_CONTROL3, RAA489300_C3_VALUE
							| RAA489300_C3_SWITCHING_FREQ_400K},
	{RAA489300_REG_CONTROL2, RAA489300_C2_ENABLE_AUTO_DISCHARGE
							| RAA489300_C2_PGOOD_WINDOW_20},
	{RAA489300_REG_CONTROL0, 0x0000},
};

int write_level_buck_registers(enum level_buck_mode mode)
{
	int rv;
	const struct reg_val *reg_values;
	size_t size;

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
		return EC_ERROR_INVAL;
	}

	for (size_t i = 0; i < size; i++) {
		uint8_t reg = reg_values[i].reg;
		uint16_t val = reg_values[i].val;

		if (mode == LEVEL_BUCK_SPR && reg == RAA489300_REG_CONTROL2 &&
		    enable_low_power_mode) {
			val |= RAA489300_C2_LOW_POWER_PTM_MODE;
		}

		rv = i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS, reg, val);
		if (rv != EC_SUCCESS) {
			CPRINTS("3Level-Buck write failed reg 0x%02X", reg);
			return rv;
		}
		crec_msleep(1);
	}
	return EC_SUCCESS;
}

static void level_buck_switch_mode(void);
DECLARE_DEFERRED(level_buck_switch_mode);

int level_buck_check_expected_state(enum level_buck_mode mode, int *data)
{
	int rv;
	int val = 0x0000;
	uint16_t expected;

	rv = i2c_read16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_INFORMATION1, &val);

	if (rv)
		return rv;

	if (data)
		*data = val;

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

	mutex_lock(&level_buck_mutex);

	/* check state machine status and read Info1 register */
	rv = level_buck_check_expected_state(mode, &val);

	if (rv != EC_SUCCESS) {
		/* check the regulator has gone to the reset state */
		if ((val & PSM_MASK) == PSM_RESET_STATE) {
			crec_msleep(150);
		/* Do nothing in auto discharge state */
		} else if ((val & PSM_MASK) == PSM_AUTO_DISCHARGE_STATE) {
			if (mode == LEVEL_BUCK_SPR || mode == LEVEL_BUCK_EPR) {
				hook_call_deferred(&level_buck_switch_mode_data, 200 * MSEC);
				mutex_unlock(&level_buck_mutex);
				return;
			}
		}

		/* attempt to set mode */
		if (write_level_buck_registers(mode)) {
			mutex_unlock(&level_buck_mutex);
			return;
		}
		/* check the regulator is ready */
		rv = level_buck_check_expected_state(mode, NULL);
	}

	/* if the state machine status is as expected */
	if (rv == EC_SUCCESS) {
		if (mode == LEVEL_BUCK_SPR) {
			level_buck_set_output_current_limit(5504);
		} else if (mode == LEVEL_BUCK_EPR) {
			level_buck_set_output_current_limit(7000);
			crec_msleep(1);
			level_buck_set_output_voltage(24000);
		}
	} else if (mode == LEVEL_BUCK_EPR) {
		hook_call_deferred(&level_buck_switch_mode_data, 200 * MSEC);
	}

	mutex_unlock(&level_buck_mutex);
}

static void level_buck_switch_mode(void)
{
	configure_buck_mode(target_mode);
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
		level_buck_switch_mode();

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
	static int pre_reg;

	reg = RAA489300_MV_TO_VIN(mv);

	if (pre_reg != reg) {
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_VINOK_REFERENCE, reg);
		i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
			RAA489300_REG_MIN_INPUT_VOLTAGE, reg);
		pre_reg = reg;
	}
}

int level_buck_set_input_current_limit(int ma)
{
	return i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_INPUT_CURRENT_LIMIT, ma);
}

int level_buck_set_output_current_limit(int ma)
{
	return i2c_write16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_OUTPUT_CURRENT_LIMIT, ma);
}

int level_buck_set_output_voltage(int mv)
{
	uint16_t reg_value;

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

void raa489300_enter_low_power_ptm_mode(bool enabled)
{
	int rv = EC_ERROR_INVAL;

	mutex_lock(&level_buck_mutex);

	if (level_buck_check_expected_state(LEVEL_BUCK_SPR, NULL) == EC_SUCCESS) {
		rv = i2c_update16(I2C_PORT_CHARGER, RAA489300_ADDR_FLAGS,
				RAA489300_REG_CONTROL2, RAA489300_C2_LOW_POWER_PTM_MODE,
				enabled ? MASK_SET : MASK_CLR);
	}
	mutex_unlock(&level_buck_mutex);

	enable_low_power_mode = enabled;

	CPRINTS("raa489300 %sables low power PTM mode %s",
		enabled ? "en" : "dis",
		(rv == EC_SUCCESS) ? "" : "failed");
}

static int raa489300_cmd(int argc, const char **argv)
{
	int i, val, mode;
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
	} else if (argc >= 4 && !strncmp(argv[1], "set", 3)) {
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
	} else if (argc == 3 && !strncmp(argv[1], "lpm", 3)) {
		mode = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		raa489300_enter_low_power_ptm_mode(mode);
	} else {
		return EC_ERROR_PARAM_COUNT;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(raa489300, raa489300_cmd,
			"[get | set <reg> <value>]",
			"Get/set raa489300 register");
