/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Renesas (Intersil) ISL-9241 battery charger driver.
 */

/* TODO(b/175881324) */
#ifndef CONFIG_ZEPHYR
#include "adc.h"
#endif
#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "charge_manager.h"
#include "charge_state.h"
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
#define ISL9241_DEFAULT_RS1 20 /* Input current sense resistor */
#define ISL9241_DEFAULT_RS2 10 /* Battery charge current sense resistor */

#define BOARD_RS1 CONFIG_CHARGER_SENSE_RESISTOR_AC
#define BOARD_RS2 CONFIG_CHARGER_SENSE_RESISTOR

#define BC_REG_TO_CURRENT(REG) (((REG)*ISL9241_DEFAULT_RS2) / BOARD_RS2)
#define BC_CURRENT_TO_REG(CUR) (((CUR)*BOARD_RS2) / ISL9241_DEFAULT_RS2)

#define AC_REG_TO_CURRENT(REG) (((REG)*ISL9241_DEFAULT_RS1) / BOARD_RS1)
#define AC_CURRENT_TO_REG(CUR) (((CUR)*BOARD_RS1) / ISL9241_DEFAULT_RS1)

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, "ISL9241 " format, ##args)

static int learn_mode;

/* Mutex for CONTROL1 register, that can be updated from multiple tasks. */
K_MUTEX_DEFINE(control1_mutex_isl9241);

/* Charger parameters */
static const struct charger_info isl9241_charger_info = {
	.name = CHARGER_NAME,
	.voltage_max = CHARGE_V_MAX,
	.voltage_min = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max = BC_REG_TO_CURRENT(CHARGE_I_MAX),
	.current_min = BC_REG_TO_CURRENT(CHARGE_I_MIN),
	.current_step = BC_REG_TO_CURRENT(CHARGE_I_STEP),
	.input_current_max = AC_REG_TO_CURRENT(INPUT_I_MAX),
	.input_current_min = AC_REG_TO_CURRENT(INPUT_I_MIN),
	.input_current_step = AC_REG_TO_CURRENT(INPUT_I_STEP),
};

static enum ec_error_list isl9241_discharge_on_ac(int chgnum, int enable);
static enum ec_error_list isl9241_discharge_on_ac_unsafe(int chgnum,
							 int enable);
static enum ec_error_list isl9241_discharge_on_ac_weak_disable(int chgnum);

static inline enum ec_error_list isl9241_read(int chgnum, int offset,
					      int *value)
{
	int rv = i2c_read16(chg_chips[chgnum].i2c_port,
			    chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

static inline enum ec_error_list isl9241_write(int chgnum, int offset,
					       int value)
{
	int rv = i2c_write16(chg_chips[chgnum].i2c_port,
			     chg_chips[chgnum].i2c_addr_flags, offset, value);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

static inline enum ec_error_list isl9241_update(int chgnum, int offset,
						uint16_t mask,
						enum mask_update_action action)
{
	int rv = i2c_update16(chg_chips[chgnum].i2c_port,
			      chg_chips[chgnum].i2c_addr_flags, offset, mask,
			      action);
	if (rv)
		CPRINTS("%s failed (%d)", __func__, rv);

	return rv;
}

/* chip specific interfaces */

/*****************************************************************************/
/* Charger interfaces */
static enum ec_error_list isl9241_set_input_current_limit(int chgnum,
							  int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = isl9241_write(chgnum, ISL9241_REG_ADAPTER_CUR_LIMIT1, reg);
	if (rv)
		return rv;

	return isl9241_write(chgnum, ISL9241_REG_ADAPTER_CUR_LIMIT2, reg);
}

static enum ec_error_list isl9241_get_input_current_limit(int chgnum,
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

static enum ec_error_list isl9241_bypass_mode_enabled(int chgnum, int *enabled)
{
	int reg, rv;

	rv = isl9241_read(chgnum, ISL9241_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	*enabled = !!(reg & ISL9241_CONTROL0_EN_BYPASS_GATE);

	return EC_SUCCESS;
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

	/* Bypass mode status */
	rv = isl9241_bypass_mode_enabled(chgnum, &reg);
	if (rv)
		return rv;
	if (reg)
		*status |= CHARGER_BYPASS_MODE;

	return EC_SUCCESS;
}

static enum ec_error_list isl9241_set_mode(int chgnum, int mode)
{
	int rv;

	/*
	 * See crosbug.com/p/51196.
	 * Disable learn mode if it wasn't explicitly enabled.
	 */
	rv = isl9241_discharge_on_ac_weak_disable(chgnum);
	if (rv)
		return rv;

	/*
	 * Charger inhibit
	 * MinSystemVoltage 0x00h = disables all battery charging
	 */
	rv = isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE,
			   mode & CHARGE_FLAG_INHIBIT_CHARGE ?
				   0 :
				   battery_get_info()->voltage_min);
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
		CPRINTS("Could not read VBUS ADC! Error: %d", rv);

	return rv;
}

static enum ec_error_list isl9241_get_vsys_voltage(int chgnum, int port,
						   int *voltage)
{
	int val = 0;
	int rv;

	rv = isl9241_update(chgnum, ISL9241_REG_CONTROL3,
			    ISL9241_CONTROL3_ENABLE_ADC, MASK_SET);
	if (rv) {
		CPRINTS("Could not enable ADC for Vsys. (rv=%d)", rv);
		return rv;
	}

	usleep(ISL9241_ADC_POLLING_TIME_US);

	/* Read voltage ADC value */
	rv = isl9241_read(chgnum, ISL9241_REG_VSYS_ADC_RESULTS, &val);
	if (rv) {
		CPRINTS("Could not read Vsys. (rv=%d)", rv);
		isl9241_update(chgnum, ISL9241_REG_CONTROL3,
			       ISL9241_CONTROL3_ENABLE_ADC, MASK_CLR);
		return rv;
	}

	/* Adjust adc_val. Same as Vin. */
	val >>= ISL9241_VIN_ADC_BIT_OFFSET;
	val *= ISL9241_VIN_ADC_STEP_MV;
	*voltage = val;

	return EC_SUCCESS;
}

static enum ec_error_list isl9241_post_init(int chgnum)
{
	return EC_SUCCESS;
}

/*
 * Writes to ISL9241_REG_CONTROL1, unsafe as it does not lock
 * control1_mutex_isl9241.
 */
static enum ec_error_list isl9241_discharge_on_ac_unsafe(int chgnum, int enable)
{
	int rv = isl9241_update(chgnum, ISL9241_REG_CONTROL1,
				ISL9241_CONTROL1_LEARN_MODE,
				(enable) ? MASK_SET : MASK_CLR);
	if (!rv)
		learn_mode = enable;

	return rv;
}

/* Disables discharge on ac only if it wasn't explicitly enabled. */
static enum ec_error_list isl9241_discharge_on_ac_weak_disable(int chgnum)
{
	int rv = 0;

	mutex_lock(&control1_mutex_isl9241);
	if (!learn_mode) {
		rv = isl9241_discharge_on_ac_unsafe(chgnum, 0);
	}

	mutex_unlock(&control1_mutex_isl9241);
	return rv;
}

static enum ec_error_list isl9241_discharge_on_ac(int chgnum, int enable)
{
	int rv = 0;

	mutex_lock(&control1_mutex_isl9241);
	rv = isl9241_discharge_on_ac_unsafe(chgnum, enable);
	mutex_unlock(&control1_mutex_isl9241);
	return rv;
}

int isl9241_set_ac_prochot(int chgnum, int ma)
{
	int rv;
	uint16_t reg;

	/*
	 * The register reserves bits [6:0] and bits [15:13].
	 * This routine should ensure these bits are not set
	 * before writing the register.
	 */
	if (ma > AC_REG_TO_CURRENT(ISL9241_AC_PROCHOT_CURRENT_MAX))
		reg = ISL9241_AC_PROCHOT_CURRENT_MAX;
	else if (ma < AC_REG_TO_CURRENT(ISL9241_AC_PROCHOT_CURRENT_MIN))
		reg = ISL9241_AC_PROCHOT_CURRENT_MIN;
	else
		reg = AC_CURRENT_TO_REG(ma);

	rv = isl9241_write(chgnum, ISL9241_REG_AC_PROCHOT, reg);
	if (rv)
		CPRINTS("set_ac_prochot failed (%d)", rv);

	return rv;
}

int isl9241_set_dc_prochot(int chgnum, int ma)
{
	int rv;

	/*
	 * The register reserves bits [7:0] and bits [15:14].
	 * This routine should ensure these bits are not set
	 * before writing the register.
	 */
	if (ma > ISL9241_DC_PROCHOT_CURRENT_MAX)
		ma = ISL9241_DC_PROCHOT_CURRENT_MAX;
	else if (ma < ISL9241_DC_PROCHOT_CURRENT_MIN)
		ma = ISL9241_DC_PROCHOT_CURRENT_MIN;

	rv = isl9241_write(chgnum, ISL9241_REG_DC_PROCHOT, ma);
	if (rv)
		CPRINTS("set_dc_prochot failed (%d)", rv);

	return rv;
}

static bool isl9241_is_ac_present(int chgnum)
{
	static bool ac_is_present;
	int reg;
	int rv;

	rv = isl9241_read(chgnum, ISL9241_REG_INFORMATION2, &reg);
	if (rv == EC_SUCCESS)
		ac_is_present = !!(reg & ISL9241_INFORMATION2_ACOK_PIN);

	return ac_is_present;
}

/*
 * Check whether ISL9241 is in any CHRG state, including NVDC+CHRG, Bypass+CHRG,
 * RTB+CHRG.
 */
static bool isl9241_is_in_chrg(int chgnum)
{
	static bool trickle_charge_enabled, fast_charge_enabled;
	int reg;
	int rv;

	rv = isl9241_read(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE, &reg);
	if (rv == EC_SUCCESS)
		trickle_charge_enabled = reg > 0;

	rv = isl9241_read(chgnum, ISL9241_REG_CHG_CURRENT_LIMIT, &reg);
	if (rv == EC_SUCCESS)
		fast_charge_enabled = reg > 0;

	return trickle_charge_enabled || fast_charge_enabled;
}

/*
 * Transition from Bypass to BAT.
 */
static enum ec_error_list isl9241_bypass_to_bat(int chgnum)
{
	const struct battery_info *bi = battery_get_info();

	CPRINTS("bypass -> bat");

	/* 1: Disable force forward buck/reverse boost. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL4,
		       ISL9241_CONTROL4_FORCE_BUCK_MODE, MASK_CLR);

	/*
	 * 2: Turn off BYPSG, turn on NGATE, disable charge pump 100%, disable
	 *    Vin<Vout comparator.
	 */
	isl9241_write(chgnum, ISL9241_REG_CONTROL0, 0);

	/* 3: Set MaxSysVoltage to full charge. */
	isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, bi->voltage_max);

	/* 4: Disable ADC. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL3,
		       ISL9241_CONTROL3_ENABLE_ADC, MASK_CLR);

	/* 5: Set BGATE to normal operation. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL1, ISL9241_CONTROL1_BGATE_OFF,
		       MASK_CLR);

	/* 6: Set ACOK reference to normal value. TODO: Revisit. */
	isl9241_write(chgnum, ISL9241_REG_ACOK_REFERENCE,
		      ISL9241_MV_TO_ACOK_REFERENCE(
			      ISL9241_ACOK_REF_LOW_VOLTAGE_ADAPTER_MV));

	return EC_SUCCESS;
}

/*
 * Transition from Bypass+CHRG to BAT (M).
 */
static enum ec_error_list isl9241_bypass_chrg_to_bat(int chgnum)
{
	CPRINTS("bypass_chrg -> bat");

	/* 1: Disable force forward buck/reverse boost. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL4,
		       ISL9241_CONTROL4_FORCE_BUCK_MODE, MASK_CLR);
	/* 2: Disable fast charge. */
	isl9241_write(chgnum, ISL9241_REG_CHG_CURRENT_LIMIT, 0);
	/* 3: Disable trickle charge. */
	isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE, 0);
	/*
	 * 4: Turn off BYPSG, turn on NGATE, disable charge pump 100%, disable
	 *     Vin<Vout comparator.
	 */
	isl9241_write(chgnum, ISL9241_REG_CONTROL0, 0);
	/* 5: Disable ADC. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL3,
		       ISL9241_CONTROL3_ENABLE_ADC, MASK_CLR);
	/* 6: Set BGATE to normal operation. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL1, ISL9241_CONTROL1_BGATE_OFF,
		       MASK_CLR);
	/* 7: Set ACOK reference to normal value. TODO: Revisit. */
	isl9241_write(chgnum, ISL9241_REG_ACOK_REFERENCE,
		      ISL9241_MV_TO_ACOK_REFERENCE(3600));

	return EC_SUCCESS;
}

/*
 * Transition from NVDC+CHRG to NVDC (L).
 */
static enum ec_error_list isl9241_nvdc_chrg_to_nvdc(int chgnum)
{
	enum ec_error_list rv;

	CPRINTS("nvdc_chrg -> nvdc");

	/* L: If we're in NVDC+Chg, first transition to NVDC. */
	/* 1: Disable fast charge. */
	rv = isl9241_set_current(chgnum, 0);
	if (rv)
		return rv;

	/* 2: Disable trickle charge. */
	rv = isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE, 0);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

static enum ec_error_list isl9241_enable_bypass_mode(int chgnum, bool enable);

/*
 * Transition from NVDC to Bypass (A).
 */
static enum ec_error_list isl9241_nvdc_to_bypass(int chgnum)
{
	const struct battery_info *bi = battery_get_info();
	const int charge_current = charge_manager_get_charger_current();
	const int charge_voltage = charge_manager_get_charger_voltage();
	int vsys, vsys_target;
	timestamp_t deadline;

	CPRINTS("nvdc -> bypass");

	/* 1: Set adapter current limit. */
	isl9241_set_input_current_limit(chgnum, charge_current);

	/* 2: Set charge pumps to 100%. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL0,
		       ISL9241_CONTROL0_EN_CHARGE_PUMPS, MASK_SET);

	/* 3: Enable ADC. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL3,
		       ISL9241_CONTROL3_ENABLE_ADC, MASK_SET);

	/* 4: Turn on Vin/Vout comparator. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL0,
		       ISL9241_CONTROL0_EN_VIN_VOUT_COMP, MASK_SET);

	/* 5: Set ACOK reference higher than battery full voltage. */
	isl9241_write(chgnum, ISL9241_REG_ACOK_REFERENCE,
		      ISL9241_MV_TO_ACOK_REFERENCE(bi->voltage_max + 800));

	/* 6*: Reduce system load below ACLIM. */
	/* 7: Turn off BGATE */
	isl9241_update(chgnum, ISL9241_REG_CONTROL1, ISL9241_CONTROL1_BGATE_OFF,
		       MASK_SET);

	/* 8*: Set MaxSysVoltage to VADP. */
	vsys_target = MIN(charge_voltage - 256, CHARGE_V_MAX);
	isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, vsys_target);

	/* 9*: Wait until VSYS == MaxSysVoltage. */
	deadline.val = get_time().val + ISL9241_BYPASS_VSYS_TIMEOUT_MS * MSEC;
	do {
		msleep(ISL9241_BYPASS_VSYS_TIMEOUT_MS / 10);
		if (isl9241_get_vsys_voltage(chgnum, 0, &vsys)) {
			CPRINTS("Aborting bypass mode. Vsys is unknown.");
			return EC_ERROR_UNKNOWN;
		}
		if (timestamp_expired(deadline, NULL)) {
			CPRINTS("Aborting bypass mode. Vsys too low (%d < %d)",
				vsys, vsys_target);
			return EC_ERROR_TIMEOUT;
		}
	} while (vsys < vsys_target - 256);

	/* 10*: Turn on Bypass gate */
	isl9241_update(chgnum, ISL9241_REG_CONTROL0,
		       ISL9241_CONTROL0_EN_BYPASS_GATE, MASK_SET);

	/* 11: Wait 1 ms. */
	msleep(1);

	/* 12*: Turn off NGATE. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL0, ISL9241_CONTROL0_NGATE_OFF,
		       MASK_SET);

	/* 14*: Stop switching. */
	isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, 0);

	/* 15: Set BGATE to normal operation. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL1, ISL9241_CONTROL1_BGATE_OFF,
		       MASK_CLR);

	if (!isl9241_is_ac_present(chgnum))
		/*
		 * Suggestion: If ACOK goes low before step A16, stop
		 * executing commands and complete steps for Bypass to BAT.
		 */
		return EC_ERROR_PARAM1;

	/* 16: Enable 10 mA discharge on CSOP. */
	/* 17: Read diode emulation active bit. */
	/* 18: Disable 10mA discharge on CSOP. */
	/* 19*: Force forward buck/reverse boost mode. */
	isl9241_update(chgnum, ISL9241_REG_CONTROL4,
		       ISL9241_CONTROL4_FORCE_BUCK_MODE, MASK_SET);

	if (!isl9241_is_ac_present(chgnum))
		/*
		 * Suggestion: If AC is removed on or after A16, complete all
		 * 19 steps then execute Bypass to BAT.
		 */
		return EC_ERROR_PARAM2;

	return EC_SUCCESS;
}

/*
 * Transition from Bypass + CHRG to Bypass (J).
 */
static enum ec_error_list isl9241_bypass_chrg_to_bypass(int chgnum)
{
	int rv;

	CPRINTS("bypass_chrg -> bypass");

	/* 1: Stop switching. */
	rv = isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE, 0);
	if (rv)
		return rv;

	/* 2: Disable fast charge. */
	rv = isl9241_write(chgnum, ISL9241_REG_CHG_CURRENT_LIMIT, 0);
	if (rv)
		return rv;

	/* 3: Disable trickle charge. */
	rv = isl9241_write(chgnum, ISL9241_REG_MIN_SYSTEM_VOLTAGE, 0);
	if (rv)
		return rv;

	return EC_SUCCESS;
}

/*
 * Transition from Bypass to NVDC (B).
 */
static enum ec_error_list isl9241_bypass_to_nvdc(int chgnum)
{
	const struct battery_info *bi = battery_get_info();
	int voltage;
	int rv;

	CPRINTS("bypass -> nvdc");

	/* 1*: Reduce system load below ACLIM. */
	/* 3*: Disable force forward buck/reverse boost. */
	rv = isl9241_update(chgnum, ISL9241_REG_CONTROL4,
			    ISL9241_CONTROL4_FORCE_BUCK_MODE, MASK_CLR);
	if (rv)
		return rv;

	/* 6*: Set MaxSysVoltage to VADP. */
	rv = isl9241_get_vbus_voltage(chgnum, 0, &voltage);
	if (rv)
		return rv;
	rv = isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE,
			   voltage - 256);
	if (rv)
		return rv;

	/* 7*: Wait until VSYS == MaxSysVoltage. */
	msleep(1);

	/* 8*: Turn on NGATE. */
	rv = isl9241_update(chgnum, ISL9241_REG_CONTROL0,
			    ISL9241_CONTROL0_NGATE_OFF, MASK_CLR);
	if (rv)
		return rv;

	/* 10*: Turn off Bypass gate */
	rv = isl9241_update(chgnum, ISL9241_REG_CONTROL0,
			    ISL9241_CONTROL0_EN_BYPASS_GATE, MASK_CLR);
	if (rv)
		return rv;

	/* 12*: Set MaxSysVoltage to full charge. */
	return isl9241_write(chgnum, ISL9241_REG_MAX_SYSTEM_VOLTAGE,
			     bi->voltage_max);
}

static enum ec_error_list isl9241_enable_bypass_mode(int chgnum, bool enable)
{
	enum ec_error_list rv = EC_ERROR_UNKNOWN;

	if (enable) {
		/* We should be already in NVDC. */
		if (isl9241_is_in_chrg(chgnum)) {
			/* (Optional) L (then A) */
			rv = isl9241_nvdc_chrg_to_nvdc(chgnum);
			if (rv)
				CPRINTS("nvdc_chrg -> nvdc failed(%d)", rv);
		}
		/* A */
		rv = isl9241_nvdc_to_bypass(chgnum);
		if (rv == EC_ERROR_PARAM1 || rv == EC_ERROR_PARAM2) {
			CPRINTS("AC removed (%d) in nvdc -> bypass mode", rv);
			return isl9241_bypass_to_bat(chgnum);
		} else if (rv) {
			CPRINTS("Failed to enable bypass mode(%d)", rv);
			return isl9241_bypass_to_nvdc(chgnum);
		}
		return rv;
	}

	/* Disable */
	if (isl9241_is_ac_present(chgnum)) {
		/* Switch to another AC (e.g. BJ -> Type-C) */
		if (isl9241_is_in_chrg(chgnum)) {
			/* J (then B) */
			rv = isl9241_bypass_chrg_to_bypass(chgnum);
			if (rv)
				CPRINTS("bypass_chrg -> bypass failed(%d)", rv);
		}
		/* B */
		rv = isl9241_bypass_to_nvdc(chgnum);
		if (rv)
			CPRINTS("bypass -> nvdc failed(%d)", rv);
		return rv;
	} else {
		/* AC removal */
		if (isl9241_is_in_chrg(chgnum)) {
			/* M */
			rv = isl9241_bypass_chrg_to_bat(chgnum);
			if (rv)
				CPRINTS("bypass_chrg -> bat failed(%d)", rv);
		} else {
			/* M' */
			rv = isl9241_bypass_to_bat(chgnum);
			if (rv)
				CPRINTS("bypass -> bat failed(%d)", rv);
		}
		return rv;
	}

	return rv;
}

/*****************************************************************************/
/* ISL-9241 initialization */
static void isl9241_init(int chgnum)
{
#ifdef CONFIG_ISL9241_SWITCHING_FREQ
	int ctl_val;
#endif

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
	if (isl9241_update(
		    chgnum, ISL9241_REG_CONTROL2,
		    (ISL9241_CONTROL2_TRICKLE_CHG_CURR(bi->precharge_current) |
		     ISL9241_CONTROL2_PROCHOT_DEBOUNCE_1000),
		    MASK_SET))
		goto init_fail;

	/*
	 * Set control3 register to
	 * [14]: ACLIM Reload (Do not reload)
	 */
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL3,
			   ISL9241_CONTROL3_ACLIM_RELOAD, MASK_SET))
		goto init_fail;

	/*
	 * Set control4 register to
	 * [13]: Slew rate control enable (sets VSYS ramp to 8mV/us)
	 */
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL4,
			   ISL9241_CONTROL4_SLEW_RATE_CTRL, MASK_SET))
		goto init_fail;

#ifndef CONFIG_CHARGE_RAMP_HW
	if (isl9241_update(chgnum, ISL9241_REG_CONTROL0,
			   ISL9241_CONTROL0_INPUT_VTG_REGULATION, MASK_SET))
		goto init_fail;
#endif

#ifdef CONFIG_ISL9241_SWITCHING_FREQ
	if (isl9241_read(chgnum, ISL9241_REG_CONTROL1, &ctl_val))
		goto init_fail;
	ctl_val &= ~ISL9241_CONTROL1_SWITCHING_FREQ_MASK;
	ctl_val |= ((CONFIG_ISL9241_SWITCHING_FREQ << 7) &
		    ISL9241_CONTROL1_SWITCHING_FREQ_MASK);
	if (isl9241_write(chgnum, ISL9241_REG_CONTROL1, ctl_val))
		goto init_fail;
#endif

	/*
	 * No need to proceed with the rest of init if we sysjump'd to this
	 * image as the input current limit has already been set.
	 */
	if (system_jumped_late())
		return;

	/* Initialize the input current limit to the board's default. */
	if (isl9241_set_input_current_limit(chgnum,
					    CONFIG_CHARGER_INPUT_CURRENT))
		goto init_fail;

	return;

init_fail:
	CPRINTS("Init failed!");
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

/*
 * When fully charged in a low-power state, the ISL9241 may get stuck
 * in CCM. Toggle learning mode for 50 ms to enter DCM and save power.
 * This is a workaround provided by Renesas. See b/183771327.
 * Note: the charger_get_state() returns the last known charge value,
 * so need to check the battery is not disconnected when the system
 * comes from the battery cutoff.
 */
static void isl9241_restart_charge_voltage_when_full(void)
{
	if (!chipset_in_or_transitioning_to_state(CHIPSET_STATE_ON) &&
	    charge_get_state() == PWR_STATE_CHARGE_NEAR_FULL &&
	    battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED) {
		charger_discharge_on_ac(1);
		msleep(50);
		charger_discharge_on_ac(0);
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, isl9241_restart_charge_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, isl9241_restart_charge_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, isl9241_restart_charge_voltage_when_full,
	     HOOK_PRIO_DEFAULT);

/*****************************************************************************/
#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_reg_range(int chgnum, int low, int high)
{
	int reg;
	int regval;
	int rv;

	for (reg = low; reg <= high; reg++) {
		ccprintf("[%Xh] = ", reg);
		rv = isl9241_read(chgnum, reg, &regval);
		if (!rv)
			ccprintf("0x%04x\n", regval);
		else
			ccprintf("ERR (%d)\n", rv);
		cflush();
	}
}

static void command_isl9241_dump(int chgnum)
{
	dump_reg_range(chgnum, 0x14, 0x15);
	dump_reg_range(chgnum, 0x38, 0x40);
	dump_reg_range(chgnum, 0x43, 0x43);
	dump_reg_range(chgnum, 0x47, 0x4F);
	dump_reg_range(chgnum, 0x80, 0x87);
	dump_reg_range(chgnum, 0x90, 0x91);
	dump_reg_range(chgnum, 0xFE, 0xFF);
}
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
	.get_vsys_voltage = &isl9241_get_vsys_voltage,
	.set_input_current_limit = &isl9241_set_input_current_limit,
	.get_input_current_limit = &isl9241_get_input_current_limit,
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
	.enable_bypass_mode = isl9241_enable_bypass_mode,
#ifdef CONFIG_CMD_CHARGER_DUMP
	.dump_registers = &command_isl9241_dump,
#endif
};
