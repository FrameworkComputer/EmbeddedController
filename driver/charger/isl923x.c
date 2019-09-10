/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ISL-9237/8 battery charger driver.
 */

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "isl923x.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "ISL9237/8 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

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
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

static int learn_mode;

/* Mutex for CONTROL1 register, that can be updated from multiple tasks. */
static struct mutex control1_mutex;

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
	return i2c_read8(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			 offset, value);
}

static inline int raw_read16(int offset, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			  offset, value);
}

static inline int raw_write16(int offset, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			   offset, value);
}

static int isl9237_set_current(uint16_t current)
{
	return raw_write16(ISL923X_REG_CHG_CURRENT, CURRENT_TO_REG(current));
}

static int isl9237_set_voltage(uint16_t voltage)
{
	return raw_write16(ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = raw_write16(ISL923X_REG_ADAPTER_CURRENT1, reg);
	if (rv)
		return rv;

	return raw_write16(ISL923X_REG_ADAPTER_CURRENT2, reg);
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_ADAPTER_CURRENT1, &reg);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

#if defined(CONFIG_CHARGER_OTG) && defined(CONFIG_CHARGER_ISL9238)
int charger_enable_otg_power(int enabled)
{
	int rv, control1;

	mutex_lock(&control1_mutex);

	rv = raw_read16(ISL923X_REG_CONTROL1, &control1);
	if (rv)
		goto out;

	if (enabled)
		control1 |= ISL923X_C1_OTG;
	else
		control1 &= ~ISL923X_C1_OTG;

	rv = raw_write16(ISL923X_REG_CONTROL1, control1);

out:
	mutex_unlock(&control1_mutex);

	return rv;
}

/*
 * TODO(b:67920792): OTG is not implemented for ISL9237 that has different
 * register scale and range.
 */
int charger_set_otg_current_voltage(int output_current, int output_voltage)
{
	int rv;
	uint16_t volt_reg = (output_voltage / ISL9238_OTG_VOLTAGE_STEP)
			<< ISL9238_OTG_VOLTAGE_SHIFT;
	uint16_t current_reg =
		DIV_ROUND_UP(output_current, ISL923X_OTG_CURRENT_STEP)
			<< ISL923X_OTG_CURRENT_SHIFT;

	if (output_current < 0 || output_current > ISL923X_OTG_CURRENT_MAX ||
			output_voltage > ISL9238_OTG_VOLTAGE_MAX)
		return EC_ERROR_INVAL;

	/* Set voltage. */
	rv = raw_write16(ISL923X_REG_OTG_VOLTAGE, volt_reg);
	if (rv)
		return rv;

	/* Set current. */
	return raw_write16(ISL923X_REG_OTG_CURRENT, current_reg);
}
#endif /* CONFIG_CHARGER_OTG && CONFIG_CHARGER_ISL9238 */

int charger_manufacturer_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_MANUFACTURER_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

int charger_device_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_DEVICE_ID, &reg);
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

	rv = raw_read16(ISL923X_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = raw_read16(ISL923X_REG_CONTROL1, &reg);
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
	rv = raw_write16(ISL923X_REG_CONTROL0, reg);

	if (rv)
		return rv;

	reg = (option >> 16) & 0xffff;
	return raw_write16(ISL923X_REG_CONTROL1, reg);
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
	int rv = EC_SUCCESS;

	/*
	 * See crosbug.com/p/51196.  Always disable learn mode unless it was set
	 * explicitly.
	 */
	if (!learn_mode)
		rv = charger_discharge_on_ac(0);

	/* ISL923X does not support inhibit mode setting. */
	return rv;
}

int charger_get_current(int *current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_CHG_CURRENT, &reg);
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
	return raw_read16(ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

int charger_set_voltage(int voltage)
{
	/* The ISL923X will drop voltage to as low as requested. As the
	 * charger state machine will pass in 0 voltage, protect the system
	 * voltage by capping to the minimum. The reason is that the ISL923X
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
	/*
	 * charger_post_init() is called every time AC becomes present in the
	 * system.  It's called this frequently because there are some charger
	 * ICs which become unpowered when AC is not present.  Therefore, upon
	 * AC becoming present again, the chargers need to be reinitialized.
	 * The ISL9237/8 can be powered from VSYS and therefore do not need to
	 * be reinitialized everytime.  This is why isl923x_init() is called
	 * once at HOOK_INIT time.
	 */
	return EC_SUCCESS;
}

int isl923x_set_ac_prochot(uint16_t ma)
{
	int rv;

	if (ma > ISL923X_AC_PROCHOT_CURRENT_MAX) {
		CPRINTS("%s: invalid current (%d mA)\n", __func__, ma);
		return EC_ERROR_INVAL;
	}

	rv = raw_write16(ISL923X_REG_PROCHOT_AC, ma);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);
	return rv;
}

int isl923x_set_dc_prochot(uint16_t ma)
{
	int rv;

	if (ma > ISL923X_DC_PROCHOT_CURRENT_MAX) {
		CPRINTS("%s: invalid current (%d mA)\n", __func__, ma);
		return EC_ERROR_INVAL;
	}

	rv = raw_write16(ISL923X_REG_PROCHOT_DC, ma);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);
	return rv;
}

static void isl923x_init(void)
{
	int reg;

#ifdef CONFIG_TRICKLE_CHARGING
	const struct battery_info *bi = battery_get_info();
	int precharge_voltage = bi->precharge_voltage ?
		bi->precharge_voltage : bi->voltage_min;

	if (raw_write16(ISL923X_REG_SYS_VOLTAGE_MIN, precharge_voltage))
		goto init_fail;
#endif

	/*
	 * [10:9]: Prochot# Debounce time
	 *         11b: 1ms
	 */
	if (raw_read16(ISL923X_REG_CONTROL2, &reg))
		goto init_fail;

	if (raw_write16(ISL923X_REG_CONTROL2,
			reg |
			ISL923X_C2_OTG_DEBOUNCE_150 |
			ISL923X_C2_PROCHOT_DEBOUNCE_1000 |
			ISL923X_C2_ADAPTER_DEBOUNCE_150))
		goto init_fail;

#ifdef CONFIG_CHARGE_RAMP_HW
#ifdef CONFIG_CHARGER_ISL9237
	if (raw_read16(ISL923X_REG_CONTROL0, &reg))
		goto init_fail;

	/* Set input voltage regulation reference voltage for charge ramp */
	reg &= ~ISL9237_C0_VREG_REF_MASK;
	reg |= ISL9237_C0_VREG_REF_4200;

	if (raw_write16(ISL923X_REG_CONTROL0, reg))
		goto init_fail;
#else /* !defined(CONFIG_CHARGER_ISL9237) */
	/*
	 * For the ISL9238, set the input voltage regulation to 4.439V.  Note,
	 * the voltage is set in 341.3 mV steps.
	 */
	reg = (4439 / ISL9238_INPUT_VOLTAGE_REF_STEP)
		<< ISL9238_INPUT_VOLTAGE_REF_SHIFT;

	if (raw_write16(ISL9238_REG_INPUT_VOLTAGE, reg))
		goto init_fail;
#endif /* defined(CONFIG_CHARGER_ISL9237) */
#else /* !defined(CONFIG_CHARGE_RAMP_HW) */
	if (raw_read16(ISL923X_REG_CONTROL0, &reg))
		goto init_fail;

	/* Disable voltage regulation loop to disable charge ramp */
	reg |= ISL923X_C0_DISABLE_VREG;

	if (raw_write16(ISL923X_REG_CONTROL0, reg))
		goto init_fail;
#endif /* defined(CONFIG_CHARGE_RAMP_HW) */

#ifdef CONFIG_CHARGER_ISL9238
	/*
	 * Don't reread the prog pin and don't reload the ILIM on ACIN.
	 */
	if (raw_read16(ISL9238_REG_CONTROL3, &reg))
		goto init_fail;
	reg |= ISL9238_C3_NO_RELOAD_ACLIM_ON_ACIN |
		ISL9238_C3_NO_REREAD_PROG_PIN;
	/*
	 * Disable autonomous charging initially since 1) it causes boot loop
	 * issues with 2S batteries, and 2) it will automatically get disabled
	 * as soon as we manually set the current limit anyway.
	 */
	reg |= ISL9238_C3_DISABLE_AUTO_CHARING;
	if (raw_write16(ISL9238_REG_CONTROL3, reg))
		goto init_fail;

	/*
	 * No need to proceed with the rest of init if we sysjump'd to this
	 * image as the input current limit has already been set.
	 */
	if (system_jumped_to_this_image())
		return;

	/*
	 * Initialize the input current limit to the board's default.
	 */
	if (charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT))
		goto init_fail;
#endif /* defined(CONFIG_CHARGER_ISL9238) */

	return;
init_fail:
	CPRINTS("%s failed!", __func__);
}
DECLARE_HOOK(HOOK_INIT, isl923x_init, HOOK_PRIO_INIT_I2C + 1);

int charger_discharge_on_ac(int enable)
{
	int rv;
	int control1;

	mutex_lock(&control1_mutex);

	rv = raw_read16(ISL923X_REG_CONTROL1, &control1);
	if (rv)
		goto out;

	control1 &= ~ISL923X_C1_LEARN_MODE_AUTOEXIT;
	if (enable)
		control1 |= ISL923X_C1_LEARN_MODE_ENABLE;
	else
		control1 &= ~ISL923X_C1_LEARN_MODE_ENABLE;

	rv = raw_write16(ISL923X_REG_CONTROL1, control1);

	learn_mode = !rv && enable;

out:
	mutex_unlock(&control1_mutex);
	return rv;
}

/*****************************************************************************/
/* Hardware current ramping */

#ifdef CONFIG_CHARGE_RAMP_HW
int charger_set_hw_ramp(int enable)
{
	int rv, reg;

	rv = raw_read16(ISL923X_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	/* HW ramp is controlled by input voltage regulation reference bits */
	if (enable)
		reg &= ~ISL923X_C0_DISABLE_VREG;
	else
		reg |= ISL923X_C0_DISABLE_VREG;

	return raw_write16(ISL923X_REG_CONTROL0, reg);
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
	/*
	 * ISL doesn't have a way to get this info, so return the nominal
	 * current limit as an estimate.
	 */
	int input_current;

	if (charger_get_input_current(&input_current) != EC_SUCCESS)
		return 0;
	return input_current;
}
#endif /* CONFIG_CHARGE_RAMP_HW */


#ifdef CONFIG_CHARGER_PSYS
static int psys_enabled;

static void charger_enable_psys(void)
{
	int val;

	mutex_lock(&control1_mutex);

	/*
	 * enable system power monitor PSYS function
	 */
	if (raw_read16(ISL923X_REG_CONTROL1, &val))
		goto out;

	val |= ISL923X_C1_ENABLE_PSYS;

	if (raw_write16(ISL923X_REG_CONTROL1, val))
		goto out;

	psys_enabled = 1;

out:
	mutex_unlock(&control1_mutex);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, charger_enable_psys, HOOK_PRIO_DEFAULT);

static void charger_disable_psys(void)
{
	int val;

	mutex_lock(&control1_mutex);

	/*
	 * disable system power monitor PSYS function
	 */
	if (raw_read16(ISL923X_REG_CONTROL1, &val))
		goto out;

	val &= ~ISL923X_C1_ENABLE_PSYS;

	if (raw_write16(ISL923X_REG_CONTROL1, val))
		goto out;

	psys_enabled = 0;

out:
	mutex_unlock(&control1_mutex);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_disable_psys, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CHARGER_PSYS_READ
int charger_get_system_power(void)
{
	int adc;

	/*
	 * If PSYS is not enabled, AP is probably off, and the value is usually
	 * too small to be measured acurately anyway.
	 */
	if (!psys_enabled)
		return -1;

	/*
	 * We assume that the output gain is always left to the default
	 * 1.44 uA/W, and that the ADC scaling values are setup accordingly in
	 * board file, so that the value is indicated in uW.
	 */
	adc = adc_read_channel(ADC_PSYS);

	return adc;
}

static int console_command_psys(int argc, char **argv)
{
	ccprintf("PSYS = %d uW\n", charger_get_system_power());
	return 0;
}
DECLARE_CONSOLE_COMMAND(psys, console_command_psys,
			NULL,
			"Get the system power in mW");
#endif /* CONFIG_CHARGER_PSYS_READ */
#endif /* CONFIG_CHARGER_PSYS */

#ifdef CONFIG_CMD_CHARGER_ADC_AMON_BMON
enum amon_bmon { AMON, BMON };

static int print_amon_bmon(enum amon_bmon amon, int direction,
			   int resistor)
{
	int adc, curr, reg, ret;

#ifdef CONFIG_CHARGER_ISL9238
	ret = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			 ISL9238_REG_CONTROL3, &reg);
	if (ret)
		return ret;

	/* Switch direction */
	if (direction)
		reg |= ISL9238_C3_AMON_BMON_DIRECTION;
	else
		reg &= ~ISL9238_C3_AMON_BMON_DIRECTION;
	ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			  ISL9238_REG_CONTROL3, reg);
	if (ret)
		return ret;
#endif

	mutex_lock(&control1_mutex);

	ret = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
			 ISL923X_REG_CONTROL1, &reg);
	if (!ret) {
		/* Switch between AMON/BMON */
		if (amon == AMON)
			reg &= ~ISL923X_C1_SELECT_BMON;
		else
			reg |= ISL923X_C1_SELECT_BMON;

		/* Enable monitor */
		reg &= ~ISL923X_C1_DISABLE_MON;
		ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				  ISL923X_REG_CONTROL1, reg);
	}

	mutex_unlock(&control1_mutex);

	if (ret)
		return ret;

	adc = adc_read_channel(ADC_AMON_BMON);
	curr = adc / resistor;
	ccprintf("%cMON(%sharging): %d uV, %d mA\n", amon == AMON ? 'A' : 'B',
		direction ? "Disc" : "C", adc, curr);

	return ret;
}

/**
 * Get charger AMON and BMON current.
 */
static int console_command_amon_bmon(int argc, char **argv)
{
	int ret = EC_SUCCESS;
	int print_ac = 1;
	int print_battery = 1;
	int print_charge = 1;
	int print_discharge = 1;

	if (argc >= 2) {
		print_ac = (argv[1][0] == 'a');
		print_battery = (argv[1][0] == 'b');
#ifdef CONFIG_CHARGER_ISL9238
		if (argv[1][1] != '\0') {
			print_charge = (argv[1][1] == 'c');
			print_discharge = (argv[1][1] == 'd');
		}
#endif
	}

	if (print_ac) {
		if (print_charge)
			ret |= print_amon_bmon(AMON, 0,
					CONFIG_CHARGER_SENSE_RESISTOR_AC);
#ifdef CONFIG_CHARGER_ISL9238
		if (print_discharge)
			ret |= print_amon_bmon(AMON, 1,
					CONFIG_CHARGER_SENSE_RESISTOR_AC);
#endif
	}

	if (print_battery) {
#ifdef CONFIG_CHARGER_ISL9238
		if (print_charge)
			ret |= print_amon_bmon(BMON, 0,
					/*
					 * charging current monitor has
					 * 2x amplification factor
					 */
					2*CONFIG_CHARGER_SENSE_RESISTOR);
#endif
		if (print_discharge)
			ret |= print_amon_bmon(BMON, 1,
					CONFIG_CHARGER_SENSE_RESISTOR);
	}

	return ret;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
#ifdef CONFIG_CHARGER_ISL9237
			"amonbmon [a|b]",
#else
			"amonbmon [a[c|d]|b[c|d]]",
#endif
			"Get charger AMON/BMON voltage diff, current");
#endif /* CONFIG_CMD_CHARGER_ADC_AMON_BMON */

#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int low, int high)
{
	int reg;
	int regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		CPRINTF("[%Xh] = ", reg);
		rv = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER_FLAGS,
				reg, &regval);
		if (!rv)
			CPRINTF("0x%04x\n", regval);
		else
			CPRINTF("ERR (%d)\n", rv);
		cflush();
	}
}

static int command_isl923x_dump(int argc, char **argv)
{
	dump_reg_range(0x14, 0x15);
	dump_reg_range(0x38, 0x3F);
	dump_reg_range(0x47, 0x4A);
#ifdef CONFIG_CHARGER_ISL9238
	dump_reg_range(0x4B, 0x4E);
#endif /* CONFIG_CHARGER_ISL9238 */
	dump_reg_range(0xFE, 0xFF);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger_dump, command_isl923x_dump, "",
			"Dumps ISL923x registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */
