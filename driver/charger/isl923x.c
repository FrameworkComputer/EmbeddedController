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
#include "compile_time_macros.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "isl923x.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "usb_pd_tcpm.h"
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

static enum ec_error_list isl923x_discharge_on_ac(int chgnum, int enable);

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

static inline enum ec_error_list raw_read8(int chgnum, int offset, int *value)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags,
			 offset, value);
}

static inline enum ec_error_list raw_read16(int chgnum, int offset, int *value)
{
	return i2c_read16(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags,
			  offset, value);
}

static inline enum ec_error_list raw_write16(int chgnum, int offset, int value)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags,
			   offset, value);
}

static enum ec_error_list isl9237_set_current(int chgnum, uint16_t current)
{
	return raw_write16(chgnum, ISL923X_REG_CHG_CURRENT,
			   CURRENT_TO_REG(current));
}

static enum ec_error_list isl9237_set_voltage(int chgnum, uint16_t voltage)
{
	return raw_write16(chgnum, ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

/* chip specific interfaces */

static enum ec_error_list isl923x_set_input_current(int chgnum,
						    int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = raw_write16(chgnum, ISL923X_REG_ADAPTER_CURRENT1, reg);
	if (rv)
		return rv;

	return raw_write16(chgnum, ISL923X_REG_ADAPTER_CURRENT2, reg);
}

static enum ec_error_list isl923x_get_input_current(int chgnum,
						    int *input_current)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, ISL923X_REG_ADAPTER_CURRENT1, &reg);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

#if defined(CONFIG_CHARGER_OTG) && defined(CONFIG_CHARGER_ISL9238)
static enum ec_error_list isl923x_enable_otg_power(int chgnum, int enabled)
{
	int rv, control1;

	mutex_lock(&control1_mutex);

	rv = raw_read16(chgnum, ISL923X_REG_CONTROL1, &control1);
	if (rv)
		goto out;

	if (enabled)
		control1 |= ISL923X_C1_OTG;
	else
		control1 &= ~ISL923X_C1_OTG;

	rv = raw_write16(chgnum, ISL923X_REG_CONTROL1, control1);

out:
	mutex_unlock(&control1_mutex);

	return rv;
}

/*
 * TODO(b:67920792): OTG is not implemented for ISL9237 that has different
 * register scale and range.
 */
static enum ec_error_list isl923x_set_otg_current_voltage(int chgnum,
							  int output_current,
							  int output_voltage)
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
	rv = raw_write16(chgnum, ISL923X_REG_OTG_VOLTAGE, volt_reg);
	if (rv)
		return rv;

	/* Set current. */
	return raw_write16(chgnum, ISL923X_REG_OTG_CURRENT, current_reg);
}
#endif /* CONFIG_CHARGER_OTG && CONFIG_CHARGER_ISL9238 */

static enum ec_error_list isl923x_manufacturer_id(int chgnum, int *id)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, ISL923X_REG_MANUFACTURER_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

static enum ec_error_list isl923x_device_id(int chgnum, int *id)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, ISL923X_REG_DEVICE_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

static enum ec_error_list isl923x_get_option(int chgnum, int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = raw_read16(chgnum, ISL923X_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = raw_read16(chgnum, ISL923X_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

static enum ec_error_list isl923x_set_option(int chgnum, int option)
{
	int rv;
	uint16_t reg;

	reg = option & 0xffff;
	rv = raw_write16(chgnum, ISL923X_REG_CONTROL0, reg);

	if (rv)
		return rv;

	reg = (option >> 16) & 0xffff;
	return raw_write16(chgnum, ISL923X_REG_CONTROL1, reg);
}

/* Charger interfaces */

static const struct charger_info *isl923x_get_info(int chgnum)
{
	return &isl9237_charger_info;
}

static enum ec_error_list isl923x_get_status(int chgnum, int *status)
{
	*status = CHARGER_LEVEL_2;

	return EC_SUCCESS;
}

static enum ec_error_list isl923x_set_mode(int chgnum, int mode)
{
	int rv = EC_SUCCESS;

	/*
	 * See crosbug.com/p/51196.  Always disable learn mode unless it was set
	 * explicitly.
	 */
	if (!learn_mode)
		rv = isl923x_discharge_on_ac(chgnum, 0);

	/* ISL923X does not support inhibit mode setting. */
	return rv;
}

static enum ec_error_list isl923x_get_current(int chgnum, int *current)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, ISL923X_REG_CHG_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

static enum ec_error_list isl923x_set_current(int chgnum, int current)
{
	return isl9237_set_current(chgnum, current);
}

static enum ec_error_list isl923x_get_voltage(int chgnum, int *voltage)
{
	return raw_read16(chgnum, ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

static enum ec_error_list isl923x_set_voltage(int chgnum, int voltage)
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

	return isl9237_set_voltage(chgnum, voltage);
}

static enum ec_error_list isl923x_post_init(int chgnum)
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
		CPRINTS("%s: invalid current (%d mA)", CHARGER_NAME, ma);
		return EC_ERROR_INVAL;
	}

	rv = raw_write16(CHARGER_SOLO, ISL923X_REG_PROCHOT_AC, ma);
	if (rv)
		CPRINTS("%s set_ac_prochot failed (%d)", CHARGER_NAME, rv);
	return rv;
}

int isl923x_set_dc_prochot(uint16_t ma)
{
	int rv;

	if (ma > ISL923X_DC_PROCHOT_CURRENT_MAX) {
		CPRINTS("%s: invalid current (%d mA)", CHARGER_NAME, ma);
		return EC_ERROR_INVAL;
	}

	rv = raw_write16(CHARGER_SOLO, ISL923X_REG_PROCHOT_DC, ma);
	if (rv)
		CPRINTS("%s set_dc_prochot failed (%d)", CHARGER_NAME, rv);
	return rv;
}

static void isl923x_init(int chgnum)
{
	int reg;
	const struct battery_info *bi = battery_get_info();
	int precharge_voltage = bi->precharge_voltage ?
		bi->precharge_voltage : bi->voltage_min;

	if (IS_ENABLED(CONFIG_CHARGER_RAA489000)) {
		if (CONFIG_CHARGER_SENSE_RESISTOR ==
		    CONFIG_CHARGER_SENSE_RESISTOR_AC) {
			/*
			 * A 1:1 ratio for Rs1:Rs2 is allowed, but Control4
			 * register Bit<11> must be set.
			 */
			if (raw_read16(chgnum, ISL9238_REG_CONTROL4, &reg))
				goto init_fail;

			if (raw_write16(chgnum, ISL9238_REG_CONTROL4,
					reg |
					RAA489000_C4_PSYS_RSNS_RATIO_1_TO_1))
				goto init_fail;
		}
	}

	if (IS_ENABLED(CONFIG_TRICKLE_CHARGING))
		if (raw_write16(chgnum, ISL923X_REG_SYS_VOLTAGE_MIN,
				precharge_voltage))
			goto init_fail;

	/*
	 * [10:9]: Prochot# Debounce time
	 *         11b: 1ms
	 */
	if (raw_read16(chgnum, ISL923X_REG_CONTROL2, &reg))
		goto init_fail;

	if (!IS_ENABLED(CONFIG_CHARGER_RAA489000))
		reg |= ISL923X_C2_OTG_DEBOUNCE_150;
	if (raw_write16(chgnum, ISL923X_REG_CONTROL2,
			reg |
			ISL923X_C2_PROCHOT_DEBOUNCE_1000 |
			ISL923X_C2_ADAPTER_DEBOUNCE_150))
		goto init_fail;

	if (IS_ENABLED(CONFIG_CHARGE_RAMP_HW)) {
		if (IS_ENABLED(CONFIG_CHARGER_ISL9237)) {
			if (raw_read16(chgnum, ISL923X_REG_CONTROL0, &reg))
				goto init_fail;

			/*
			 * Set input voltage regulation reference voltage for
			 * charge ramp.
			 */
			reg &= ~ISL9237_C0_VREG_REF_MASK;
			reg |= ISL9237_C0_VREG_REF_4200;

			if (raw_write16(chgnum, ISL923X_REG_CONTROL0, reg))
				goto init_fail;
		} else {
			/*
			 * For the ISL9238, set the input voltage regulation to
			 * 4.439V.  Note, the voltage is set in 341.3 mV steps.
			 *
			 * For the RAA489000, set the input voltage regulation
			 * to 4.437V.  Note, that the voltage is set in 85.33 mV
			 * steps.
			 */
			if (IS_ENABLED(CONFIG_CHARGER_RAA489000))
				reg = (4437 / RAA489000_INPUT_VOLTAGE_REF_STEP)
					<< RAA489000_INPUT_VOLTAGE_REF_SHIFT;
			else
				reg = (4439 / ISL9238_INPUT_VOLTAGE_REF_STEP)
					<< ISL9238_INPUT_VOLTAGE_REF_SHIFT;

			if (raw_write16(chgnum, ISL9238_REG_INPUT_VOLTAGE, reg))
				goto init_fail;
		}
	} else {
		if (raw_read16(chgnum, ISL923X_REG_CONTROL0, &reg))
			goto init_fail;

		/* Disable voltage regulation loop to disable charge ramp */
		reg |= ISL923X_C0_DISABLE_VREG;

		if (raw_write16(chgnum, ISL923X_REG_CONTROL0, reg))
			goto init_fail;
	}

	if (IS_ENABLED(CONFIG_CHARGER_ISL9238) ||
	    IS_ENABLED(CONFIG_CHARGER_RAA489000)) {
		/*
		 * Don't reread the prog pin and don't reload the ILIM on ACIN.
		 * For the RAA489000, just don't reload ACLIM.
		 */
		if (raw_read16(chgnum, ISL9238_REG_CONTROL3, &reg))
			goto init_fail;
		reg |= ISL9238_C3_NO_RELOAD_ACLIM_ON_ACIN;
		if (!IS_ENABLED(CONFIG_CHARGER_RAA489000))
			reg |= ISL9238_C3_NO_REREAD_PROG_PIN;

		/*
		 * Disable autonomous charging initially since 1) it causes boot
		 * loop issues with 2S batteries, and 2) it will automatically
		 * get disabled as soon as we manually set the current limit
		 * anyway.
		 */
		reg |= ISL9238_C3_DISABLE_AUTO_CHARING;
		if (raw_write16(chgnum, ISL9238_REG_CONTROL3, reg))
			goto init_fail;

		/*
		 * No need to proceed with the rest of init if we sysjump'd to
		 * this image as the input current limit has already been set.
		 */
		if (system_jumped_to_this_image())
			return;

		/*
		 * Initialize the input current limit to the board's default.
		 */
		if (isl923x_set_input_current(chgnum,
					      CONFIG_CHARGER_INPUT_CURRENT))
			goto init_fail;
	}

	return;
init_fail:
	CPRINTS("%s init failed!", CHARGER_NAME);
}

static enum ec_error_list isl923x_discharge_on_ac(int chgnum, int enable)
{
	int rv;
	int control1;

	mutex_lock(&control1_mutex);

	rv = raw_read16(chgnum, ISL923X_REG_CONTROL1, &control1);
	if (rv)
		goto out;

	control1 &= ~ISL923X_C1_LEARN_MODE_AUTOEXIT;
	if (enable)
		control1 |= ISL923X_C1_LEARN_MODE_ENABLE;
	else
		control1 &= ~ISL923X_C1_LEARN_MODE_ENABLE;

	rv = raw_write16(chgnum, ISL923X_REG_CONTROL1, control1);

	learn_mode = !rv && enable;

out:
	mutex_unlock(&control1_mutex);
	return rv;
}

/*****************************************************************************/
/* Hardware current ramping */

#ifdef CONFIG_CHARGE_RAMP_HW
static enum ec_error_list isl923x_set_hw_ramp(int chgnum, int enable)
{
	int rv, reg;

	rv = raw_read16(chgnum, ISL923X_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	/* HW ramp is controlled by input voltage regulation reference bits */
	if (enable)
		reg &= ~ISL923X_C0_DISABLE_VREG;
	else
		reg |= ISL923X_C0_DISABLE_VREG;

	return raw_write16(chgnum, ISL923X_REG_CONTROL0, reg);
}

static int isl923x_ramp_is_stable(int chgnum)
{
	/*
	 * Since ISL cannot read the current limit that the ramp has settled
	 * on, then we can never consider the ramp stable, because we never
	 * know what the stable limit is.
	 */
	return 0;
}

static int isl923x_ramp_is_detected(int chgnum)
{
	return 1;
}

static int isl923x_ramp_get_current_limit(int chgnum)
{
	/*
	 * ISL doesn't have a way to get this info, so return the nominal
	 * current limit as an estimate.
	 */
	int input_current;

	if (isl923x_get_input_current(chgnum, &input_current) != EC_SUCCESS)
		return 0;
	return input_current;
}
#endif /* CONFIG_CHARGE_RAMP_HW */


#ifdef CONFIG_CHARGER_PSYS
static int psys_enabled;
/*
 * TODO(b/147440290): Set to appropriate charger with multiple charger support,
 * hardcode to 0 for now
 */
static void charger_enable_psys(void)
{
	int val;

	mutex_lock(&control1_mutex);

	/*
	 * enable system power monitor PSYS function
	 */
	if (raw_read16(CHARGER_SOLO, ISL923X_REG_CONTROL1, &val))
		goto out;

	val |= ISL923X_C1_ENABLE_PSYS;

	if (raw_write16(CHARGER_SOLO, ISL923X_REG_CONTROL1, val))
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
	if (raw_read16(CHARGER_SOLO, ISL923X_REG_CONTROL1, &val))
		goto out;

	val &= ~ISL923X_C1_ENABLE_PSYS;

	if (raw_write16(CHARGER_SOLO, ISL923X_REG_CONTROL1, val))
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

static int print_amon_bmon(int chgnum, enum amon_bmon amon,
					  int direction, int resistor)
{
	int adc, curr, reg, ret;

#ifdef CONFIG_CHARGER_ISL9238
	ret = raw_read16(chgnum, ISL9238_REG_CONTROL3, &reg);
	if (ret)
		return ret;

	/* Switch direction */
	if (direction)
		reg |= ISL9238_C3_AMON_BMON_DIRECTION;
	else
		reg &= ~ISL9238_C3_AMON_BMON_DIRECTION;
	ret = raw_write16(chgnum, ISL9238_REG_CONTROL3, reg);
	if (ret)
		return ret;
#endif

	mutex_lock(&control1_mutex);

	ret = raw_read16(chgnum, ISL923X_REG_CONTROL1, &reg);
	if (!ret) {
		/* Switch between AMON/BMON */
		if (amon == AMON)
			reg &= ~ISL923X_C1_SELECT_BMON;
		else
			reg |= ISL923X_C1_SELECT_BMON;

		/* Enable monitor */
		reg &= ~ISL923X_C1_DISABLE_MON;
		ret = raw_write16(chgnum, ISL923X_REG_CONTROL1, reg);
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
	int chgnum = 0;
	char *e;

	if (argc >= 2) {
		print_ac = (argv[1][0] == 'a');
		print_battery = (argv[1][0] == 'b');
#ifdef CONFIG_CHARGER_ISL9238
		if (argv[1][1] != '\0') {
			print_charge = (argv[1][1] == 'c');
			print_discharge = (argv[1][1] == 'd');
		}
#endif
		if (argc >= 3) {
			chgnum = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
		}
	}

	if (print_ac) {
		if (print_charge)
			ret |= print_amon_bmon(chgnum, AMON, 0,
					CONFIG_CHARGER_SENSE_RESISTOR_AC);
#ifdef CONFIG_CHARGER_ISL9238
		if (print_discharge)
			ret |= print_amon_bmon(chgnum, AMON, 1,
					CONFIG_CHARGER_SENSE_RESISTOR_AC);
#endif
	}

	if (print_battery) {
#ifdef CONFIG_CHARGER_ISL9238
		if (print_charge)
			ret |= print_amon_bmon(chgnum, BMON, 0,
					/*
					 * charging current monitor has
					 * 2x amplification factor
					 */
					2*CONFIG_CHARGER_SENSE_RESISTOR);
#endif
		if (print_discharge)
			ret |= print_amon_bmon(chgnum, BMON, 1,
					CONFIG_CHARGER_SENSE_RESISTOR);
	}

	return ret;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
#ifdef CONFIG_CHARGER_ISL9237
			"amonbmon [a|b] <chgnum>",
#else
			"amonbmon [a[c|d]|b[c|d]] <chgnum>",
#endif
			"Get charger AMON/BMON voltage diff, current");
#endif /* CONFIG_CMD_CHARGER_ADC_AMON_BMON */

#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int chgnum, int low, int high)
{
	int reg;
	int regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		CPRINTF("[%Xh] = ", reg);
		rv = i2c_read16(chg_chips[chgnum].i2c_port,
				chg_chips[chgnum].i2c_addr_flags,
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
	int chgnum = 0;
	char *e;

	if (argc >= 2) {
		chgnum = strtoi(argv[1], &e, 10);
		if (*e)
			return EC_ERROR_PARAM1;
	}

	dump_reg_range(chgnum, 0x14, 0x15);
	dump_reg_range(chgnum, 0x38, 0x3F);
	dump_reg_range(chgnum, 0x47, 0x4A);
#if defined(CONFIG_CHARGER_ISL9238) || defined(CONFIG_CHARGER_RAA489000)
	dump_reg_range(chgnum, 0x4B, 0x4E);
#endif /* CONFIG_CHARGER_ISL9238 || CONFIG_CHARGER_RAA489000 */
	dump_reg_range(chgnum, 0xFE, 0xFF);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(charger_dump, command_isl923x_dump,
			"charger_dump <chgnum>", "Dumps ISL923x registers");
#endif /* CONFIG_CMD_CHARGER_DUMP */

static enum ec_error_list isl923x_get_vbus_voltage(int chgnum, int port,
						   int *voltage)
{
	int val;
	int rv;

	rv = raw_read16(chgnum, RAA489000_REG_ADC_VBUS, &val);
	if (rv)
		return rv;

	/* The VBUS voltage is returned in bits 13:6. The LSB is 96mV. */
	val &= GENMASK(13, 6);
	val = val >> 6;
	val *= 96;
	*voltage = val;

	return EC_SUCCESS;
}

const struct charger_drv isl923x_drv = {
	.init = &isl923x_init,
	.post_init = &isl923x_post_init,
	.get_info = &isl923x_get_info,
	.get_status = &isl923x_get_status,
	.set_mode = &isl923x_set_mode,
#if defined(CONFIG_CHARGER_OTG) && defined(CONFIG_CHARGER_ISL9238)
	.enable_otg_power = &isl923x_enable_otg_power,
	.set_otg_current_voltage = &isl923x_set_otg_current_voltage,
#endif
	.get_current = &isl923x_get_current,
	.set_current = &isl923x_set_current,
	.get_voltage = &isl923x_get_voltage,
	.set_voltage = &isl923x_set_voltage,
	.discharge_on_ac = &isl923x_discharge_on_ac,
	.get_vbus_voltage = &isl923x_get_vbus_voltage,
	.set_input_current = &isl923x_set_input_current,
	.get_input_current = &isl923x_get_input_current,
	.manufacturer_id = &isl923x_manufacturer_id,
	.device_id = &isl923x_device_id,
	.get_option = &isl923x_get_option,
	.set_option = &isl923x_set_option,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &isl923x_set_hw_ramp,
	.ramp_is_stable = &isl923x_ramp_is_stable,
	.ramp_is_detected = &isl923x_ramp_is_detected,
	.ramp_get_current_limit = &isl923x_ramp_get_current_limit,
#endif
};
