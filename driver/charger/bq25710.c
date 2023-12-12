/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25710 battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "bq257x0_regs.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#include <stdbool.h>

#if !defined(CONFIG_CHARGER_BQ25710) && !defined(CONFIG_CHARGER_BQ25720)
#error Only the BQ25720 and BQ25710 are supported by bq25710 driver.
#endif

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "BQ25710 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

#ifndef CONFIG_CHARGER_BQ25720_VSYS_TH2_CUSTOM
#define CONFIG_CHARGER_BQ25720_VSYS_TH2_DV \
	GET_BQ_FIELD(BQ25720, VMIN_AP, VSYS_TH2, UINT16_MAX)
#endif

#ifndef CONFIG_CHARGER_BQ25710_VSYS_MIN_VOLTAGE_CUSTOM
#define CONFIG_CHARGER_BQ25710_VSYS_MIN_VOLTAGE_MV 0
#endif

#ifndef CONFIG_CHARGER_BQ25720_VSYS_UVP_CUSTOM
#define CONFIG_CHARGER_BQ25720_VSYS_UVP 0
#endif

#ifndef CONFIG_CHARGER_BQ25720_IDCHG_DEG2_CUSTOM
#define CONFIG_CHARGER_BQ25720_IDCHG_DEG2 1
#endif

#ifndef CONFIG_CHARGER_BQ25720_IDCHG_TH2_CUSTOM
#define CONFIG_CHARGER_BQ25720_IDCHG_TH2 1
#endif

#if !defined(CONFIG_ZEPHYR) && \
	!defined(CONFIG_CHARGER_BQ25710_PKPWR_TOVLD_DEG_CUSTOM)
#define CONFIG_CHARGER_BQ25710_PKPWR_TOVLD_DEG 0
#endif

#ifndef CONFIG_CHARGER_BQ257X0_ILIM2_VTH_CUSTOM
/* Reduce ILIM from default of 150% to 110% */
#define CONFIG_CHARGER_BQ257X0_ILIM2_VTH \
	BQ257X0_PROCHOT_OPTION_0_ILIM2_VTH__1P10
#endif

/*
 * Helper macros
 */

#define SET_CO1_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ257X0, CHARGE_OPTION_1, _field, _c, (_x))

#define SET_CO2(_field, _v, _x) \
	SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_2, _field, _v, (_x))

#define SET_CO2_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ257X0, CHARGE_OPTION_2, _field, _c, (_x))

#define SET_CO3(_field, _v, _x) \
	SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_3, _field, _v, (_x))

#define SET_CO3_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ257X0, CHARGE_OPTION_3, _field, _c, (_x))

#define SET_CO4(_field, _v, _x) \
	SET_BQ_FIELD(BQ25720, CHARGE_OPTION_4, _field, _v, (_x))

#define SET_CO4_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ25720, CHARGE_OPTION_4, _field, _c, (_x))

#define SET_PO0(_field, _v, _x) \
	SET_BQ_FIELD(BQ257X0, PROCHOT_OPTION_0, _field, _v, (_x))

#define SET_PO0_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ257X0, PROCHOT_OPTION_0, _field, _c, (_x))

#define SET_PO1(_field, _v, _x) \
	SET_BQ_FIELD(BQ257X0, PROCHOT_OPTION_1, _field, _v, (_x))

#define SET_PO1_BY_NAME(_field, _c, _x) \
	SET_BQ_FIELD_BY_NAME(BQ257X0, PROCHOT_OPTION_1, _field, _c, (_x))

/*
 * Delay required from taking the bq25710 out of low power mode and having the
 * correct value in register 0x3E for VSYS_MIN voltage. The length of the delay
 * was determined by experiment. Less than 12 msec was not enough of delay, so
 * the value here is set to 20 msec to have plenty of margin.
 */
#define BQ25710_VDDA_STARTUP_DELAY_MSEC 20

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10

#define REG_TO_CHARGING_CURRENT(REG) \
	((REG)*DEFAULT_SENSE_RESISTOR / CONFIG_CHARGER_BQ25710_SENSE_RESISTOR)
#define REG_TO_CHARGING_CURRENT_AC(REG) \
	((REG)*DEFAULT_SENSE_RESISTOR / \
	 CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC)
#define CHARGING_CURRENT_TO_REG(CUR) \
	((CUR)*CONFIG_CHARGER_BQ25710_SENSE_RESISTOR / DEFAULT_SENSE_RESISTOR)
#define VMIN_AP_VSYS_TH2_TO_REG(DV) ((DV)-32)

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

#ifdef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA
/*
 * If this config option is defined, then the bq25710 needs to remain in
 * performance mode when the AP is in S0. Performance mode is active whenever AC
 * power is connected or when the EN_LWPWR bit in ChargeOption0 is clear.
 */
static uint32_t bq25710_perf_mode_req;
static struct mutex bq25710_perf_mode_mutex;
#endif

/*
 * 10mOhm sense resistor, there is 50mA offset at code 0.
 * 5mOhm sense resistor, there is 100mA offset at code 0.
 */
#define BQ25710_IIN_DPM_CODE0_OFFSET REG_TO_CHARGING_CURRENT(50)

/* Charger parameters */
static const struct charger_info bq25710_charger_info = {
	.name = "bq25710",
	.voltage_max = 19200,
	.voltage_min = 1024,
	.voltage_step = 8,
	.current_max = REG_TO_CHARGING_CURRENT(8128),
	.current_min = REG_TO_CHARGING_CURRENT(64),
	.current_step = REG_TO_CHARGING_CURRENT(64),
	.input_current_max = REG_TO_CHARGING_CURRENT_AC(6400),
	.input_current_min = REG_TO_CHARGING_CURRENT_AC(50),
	.input_current_step = REG_TO_CHARGING_CURRENT_AC(50),
};

static enum ec_error_list bq25710_get_option(int chgnum, int *option);
static enum ec_error_list bq25710_set_option(int chgnum, int option);

static inline int iin_dpm_reg_to_current(int reg)
{
	/*
	 * When set 00 at 3F register, read 22h back,
	 * you will see 00, but actually it’s 50mA@10mOhm right now.
	 * TI don’t have exactly 0A setting for input current limit,
	 * it set the 50mA@10mOhm offset so that the converter can
	 * work normally.
	 */
	if (reg == 0)
		return BQ25710_IIN_DPM_CODE0_OFFSET;
	else
		return REG_TO_CHARGING_CURRENT_AC(
			reg * BQ257X0_IIN_DPM_CURRENT_STEP_MA);
}

static inline int iin_host_current_to_reg(int current)
{
	return (REG_TO_CHARGING_CURRENT_AC(current) /
		BQ257X0_IIN_HOST_CURRENT_STEP_MA);
}

static inline enum ec_error_list raw_read16(int chgnum, int offset, int *value)
{
	return i2c_read16(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags, offset, value);
}

static inline int min_system_voltage_to_reg(int voltage_mv)
{
	int steps;
	int reg;

	if (IS_ENABLED(CONFIG_CHARGER_BQ25720)) {
		steps = voltage_mv / BQ25720_VSYS_MIN_VOLTAGE_STEP_MV;
		reg = SET_BQ_FIELD(BQ25720, VSYS_MIN, VOLTAGE, steps, 0);
	} else {
		steps = voltage_mv / BQ25710_MIN_SYSTEM_VOLTAGE_STEP_MV;
		reg = SET_BQ_FIELD(BQ25710, MIN_SYSTEM, VOLTAGE, steps, 0);
	}
	return reg;
}

static inline enum ec_error_list raw_write16(int chgnum, int offset, int value)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags, offset, value);
}

static int bq25710_set_low_power_mode(int chgnum, int enable)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA
	mutex_lock(&bq25710_perf_mode_mutex);
	/*
	 * Performance mode means not in low power mode. The bit that controls
	 * this is EN_LWPWR in ChargeOption0. The 'enable' param in this
	 * function is refeerring to low power mode, so enabling low power mode
	 * means disabling performance mode and vice versa.
	 */
	if (enable)
		bq25710_perf_mode_req &= ~(1 << task_get_current());
	else
		bq25710_perf_mode_req |= (1 << task_get_current());
	enable = !bq25710_perf_mode_req;
#endif

	if (enable)
		reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_LWPWR, true,
				   reg);
	else
		reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_LWPWR, false,
				   reg);

	rv = raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_0, reg);
#ifdef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA
	mutex_unlock(&bq25710_perf_mode_mutex);
#endif
	if (rv)
		return rv;

	return EC_SUCCESS;
}

#if defined(CONFIG_CHARGE_RAMP_HW) || \
	defined(CONFIG_USB_PD_VBUS_MEASURE_CHARGER)
static int bq25710_get_low_power_mode(int chgnum, int *mode)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		return rv;

	*mode = !!(reg & BQ_FIELD_MASK(BQ257X0, CHARGE_OPTION_0, EN_LWPWR));

	return EC_SUCCESS;
}

static int bq25710_adc_start(int chgnum, int adc_en_mask)
{
	int reg;
	int mode;
	int tries_left = BQ25710_ADC_OPTION_ADC_CONV_MS;

	/* Save current mode to restore same state after ADC read */
	if (bq25710_get_low_power_mode(chgnum, &mode))
		return EC_ERROR_UNKNOWN;

	/* Exit low power mode so ADC conversion takes typical time */
	if (bq25710_set_low_power_mode(chgnum, 0))
		return EC_ERROR_UNKNOWN;

	/*
	 * Turn on the ADC for one reading. Note that adc_en_mask
	 * maps to bit[7:0] in ADCOption register.
	 */
	reg = (adc_en_mask & BQ257X0_ADC_OPTION_EN_ADC_ALL) |
	      BQ_FIELD_MASK(BQ257X0, ADC_OPTION, ADC_START);
	if (raw_write16(chgnum, BQ25710_REG_ADC_OPTION, reg))
		return EC_ERROR_UNKNOWN;

	/*
	 * Wait until the ADC operation completes. The spec says typical
	 * conversion time is 10 msec (25 msec on bq25720). If low power
	 * mode isn't exited first, then the conversion time jumps to
	 * ~60 msec.
	 */
	do {
		/* sleep 2 ms so we time out after 2x the expected time */
		msleep(2);
		raw_read16(chgnum, BQ25710_REG_ADC_OPTION, &reg);
	} while (--tries_left &&
		 (reg & BQ_FIELD_MASK(BQ257X0, ADC_OPTION, ADC_START)));

	/* ADC reading attempt complete, go back to low power mode */
	if (bq25710_set_low_power_mode(chgnum, mode))
		return EC_ERROR_UNKNOWN;

	/* Could not complete read */
	if (reg & BQ_FIELD_MASK(BQ257X0, ADC_OPTION, ADC_START))
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}
#endif

static int co1_set_psys_sensing(int reg, bool enable)
{
	if (IS_ENABLED(CONFIG_CHARGER_BQ25720)) {
		if (enable)
			reg = SET_BQ_FIELD_BY_NAME(BQ25720, CHARGE_OPTION_1,
						   PSYS_CONFIG, PBUS_PBAT, reg);
		else
			reg = SET_BQ_FIELD_BY_NAME(BQ25720, CHARGE_OPTION_1,
						   PSYS_CONFIG, OFF, reg);
	} else if (IS_ENABLED(CONFIG_CHARGER_BQ25710)) {
		reg = SET_BQ_FIELD(BQ25710, CHARGE_OPTION_1, EN_PSYS, enable,
				   reg);
	}

	return reg;
}

static int bq257x0_init_charge_option_1(int chgnum)
{
	int rv;
	int reg;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25710_PSYS_SENSING) &&
	    !IS_ENABLED(CONFIG_CHARGER_BQ25710_CMP_REF_1P2))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_1, &reg);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PSYS_SENSING))
		reg = co1_set_psys_sensing(reg, true);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_CMP_REF_1P2))
		reg = SET_CO1_BY_NAME(CMP_REF, 1P2, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_CMP_POL_EXTERNAL))
		reg = SET_CO1_BY_NAME(CMP_POL, EXTERNAL, reg);

	return raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_1, reg);
}

static int bq257x0_init_prochot_option_0(int chgnum)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, BQ25710_REG_PROCHOT_OPTION_0, &reg);
	if (rv)
		return rv;

	reg = SET_PO0(ILIM2_VTH, CONFIG_CHARGER_BQ257X0_ILIM2_VTH, reg);

	return raw_write16(chgnum, BQ25710_REG_PROCHOT_OPTION_0, reg);
}

static int bq257x0_init_prochot_option_1(int chgnum)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, BQ25710_REG_PROCHOT_OPTION_1, &reg);
	if (rv)
		return rv;

	/* Disable VDPM prochot profile at initialization */
	reg = SET_PO1_BY_NAME(PP_VDPM, DISABLE, reg);

	/*
	 * Enable PROCHOT to be asserted with VSYS min detection. Note
	 * that when no battery is present, then VSYS will be set to the
	 * value in register 0x3E (MinSysVoltage) which means that when
	 * no battery is present prochot will continuosly be asserted.
	 */
	reg = SET_PO1_BY_NAME(PP_VSYS, ENABLE, reg);

#ifdef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA
	/*
	 * Set the IDCHG limit who's value is defined in the config
	 * option in mA.
	 *
	 * IDCHG limit is in 512 mA steps. Note there is a 128 mA offset
	 * so the actual IDCHG limit will be the value stored in
	 * IDCHG_VTH + 128 mA.
	 */
	reg = SET_PO1(IDCHG_VTH, CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA >> 9,
		      reg);

	/*  Enable IDCHG trigger for prochot. */
	reg = SET_PO1_BY_NAME(PP_IDCHG, ENABLE, reg);
#endif
	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PP_COMP))
		reg = SET_PO1_BY_NAME(PP_COMP, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PP_INOM))
		reg = SET_PO1_BY_NAME(PP_INOM, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PP_BATPRES))
		reg = SET_PO1_BY_NAME(PP_BATPRES, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PP_ACOK))
		reg = SET_PO1_BY_NAME(PP_ACOK, ENABLE, reg);

	return raw_write16(chgnum, BQ25710_REG_PROCHOT_OPTION_1, reg);
}

static int bq257x0_init_charge_option_2(int chgnum)
{
	int reg;
	int rv;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_2, &reg);
	if (rv)
		return rv;

	/*
	 * Reduce peak power mode overload and relax cycle time from
	 * default 20 msec to the minimum of 5 msec on the bq25710. The
	 * minimum is 20 msec on the bq25720.
	 */
	reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_2, PKPWR_TMAX, 0, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_PKPWR_TOVLD_DEG_CUSTOM)) {
		/* Set input overload time in peak power mode. */
		reg = SET_CO2(PKPWR_TOVLD_DEG,
			      CONFIG_CHARGER_BQ25710_PKPWR_TOVLD_DEG, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_EN_ACOC)) {
		/* Enable AC input over-current protection. */
		reg = SET_CO2_BY_NAME(EN_ACOC, ENABLE, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_ACOC_VTH_1P33)) {
		/* Set ACOC threshold to 133% of ILIM2 */
		reg = SET_CO2_BY_NAME(ACOC_VTH, 1P33, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_BQ25710_BATOC_VTH_MINIMUM)) {
		/* Set battery over-current threshold to minimum. */
		reg = SET_CO2_BY_NAME(BATOC_VTH, 1P33, reg);
	}

	return raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_2, reg);
}

static int bq257x0_init_charge_option_3(int chgnum)
{
	int reg;
	int rv;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25720))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_3, &reg);
	if (rv)
		return rv;

	reg = SET_CO3_BY_NAME(IL_AVG, 10A, reg);

	return raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_3, reg);
}

static int bq257x0_init_charge_option_4(int chgnum)
{
	int reg;
	int rv;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25720))
		return EC_SUCCESS;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25720_VSYS_UVP_CUSTOM) &&
	    !IS_ENABLED(CONFIG_CHARGER_BQ25720_IDCHG_DEG2_CUSTOM) &&
	    !IS_ENABLED(CONFIG_CHARGER_BQ25720_IDCHG_TH2_CUSTOM))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, BQ25720_REG_CHARGE_OPTION_4, &reg);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_CHARGER_BQ25720_VSYS_UVP_CUSTOM))
		reg = SET_CO4(VSYS_UVP, CONFIG_CHARGER_BQ25720_VSYS_UVP, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25720_IDCHG_DEG2_CUSTOM))
		reg = SET_CO4(IDCHG_DEG2, CONFIG_CHARGER_BQ25720_IDCHG_DEG2,
			      reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25720_IDCHG_TH2_CUSTOM))
		reg = SET_CO4(IDCHG_TH2, CONFIG_CHARGER_BQ25720_IDCHG_TH2, reg);

	if (IS_ENABLED(CONFIG_CHARGER_BQ25720_PP_IDCHG2))
		reg = SET_CO4_BY_NAME(PP_IDCHG2, ENABLE, reg);

	return raw_write16(chgnum, BQ25720_REG_CHARGE_OPTION_4, reg);
}

static int bq25720_init_vmin_active_protection(int chgnum)
{
	int reg;
	int rv;
	int th2_dv;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25720))
		return EC_SUCCESS;

	if (!IS_ENABLED(CONFIG_CHARGER_BQ25720_VSYS_TH2_CUSTOM))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, BQ25720_REG_VMIN_ACTIVE_PROTECTION, &reg);
	if (rv)
		return rv;

	/*
	 * The default VSYS_TH2 is 5.9v for a 2S config. Boards may need
	 * to increase this for stability. PROCHOT is asserted when the
	 * threshold is reached.
	 */
	th2_dv = VMIN_AP_VSYS_TH2_TO_REG(CONFIG_CHARGER_BQ25720_VSYS_TH2_DV);
	reg = SET_BQ_FIELD(BQ25720, VMIN_AP, VSYS_TH2, th2_dv, reg);

	return raw_write16(chgnum, BQ25720_REG_VMIN_ACTIVE_PROTECTION, reg);
}

static void bq25710_init(int chgnum)
{
	int reg;
	int vsys;
	int rv;

	/*
	 * Reset registers to their default settings. There is no reset
	 * pin for this chip so without a full power cycle, some
	 * registers may not be at their default values. Note, need to
	 * save the POR value of MIN_SYSTEM_VOLTAGE/VSYS_MIN register
	 * prior to setting the reset so that the correct value is
	 * preserved. In order to have the correct value read, the
	 * bq25710 must not be in low power mode, otherwise the VDDA
	 * rail may not be powered if AC is not connected. Note, this
	 * reset is only required when running out of RO and not
	 * following sysjump to RW.
	 */
	if (!system_jumped_late()) {
		rv = bq25710_set_low_power_mode(chgnum, 0);
		/* Allow enough time for VDDA to be powered */
		msleep(BQ25710_VDDA_STARTUP_DELAY_MSEC);

		if (IS_ENABLED(
			    CONFIG_CHARGER_BQ25710_VSYS_MIN_VOLTAGE_CUSTOM)) {
			vsys = min_system_voltage_to_reg(
				CONFIG_CHARGER_BQ25710_VSYS_MIN_VOLTAGE_MV);
		} else {
			rv |= raw_read16(chgnum, BQ25710_REG_MIN_SYSTEM_VOLTAGE,
					 &vsys);
		}

		rv |= raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_3, &reg);
		if (!rv) {
			reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_3, RESET_REG,
					   1, reg);
			/* Set all registers to default values */
			raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_3, reg);
			/* Restore VSYS_MIN voltage to POR reset value */
			raw_write16(chgnum, BQ25710_REG_MIN_SYSTEM_VOLTAGE,
				    vsys);
		}
		/* Reenable low power mode */
		bq25710_set_low_power_mode(chgnum, 1);
	}

	bq257x0_init_charge_option_1(chgnum);

	bq257x0_init_prochot_option_0(chgnum);

	bq257x0_init_prochot_option_1(chgnum);

	bq257x0_init_charge_option_2(chgnum);

	bq257x0_init_charge_option_3(chgnum);

	bq257x0_init_charge_option_4(chgnum);

	bq25720_init_vmin_active_protection(chgnum);
}

/* Charger interfaces */
static const struct charger_info *bq25710_get_info(int chgnum)
{
	return &bq25710_charger_info;
}

static enum ec_error_list bq25710_post_init(int chgnum)
{
	/*
	 * Note: bq25710 power on reset state is:
	 *	watch dog timer     = 175 sec
	 *	input current limit = ~1/2 maximum setting
	 *	charging voltage    = 0 mV
	 *	charging current    = 0 mA
	 *	discharge on AC     = disabled
	 */

	return EC_SUCCESS;
}

static enum ec_error_list bq25710_get_status(int chgnum, int *status)
{
	int rv;
	int option;

	rv = bq25710_get_option(chgnum, &option);
	if (rv)
		return rv;

	/* Default status */
	*status = CHARGER_LEVEL_2;

	if (option & BQ_FIELD_MASK(BQ257X0, CHARGE_OPTION_0, CHRG_INHIBIT))
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list bq25710_set_mode(int chgnum, int mode)
{
	int rv;
	int option;

	rv = bq25710_get_option(chgnum, &option);
	if (rv)
		return rv;

	if (mode & CHARGER_CHARGE_INHIBITED)
		option = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, CHRG_INHIBIT, 1,
				      option);
	else
		option = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, CHRG_INHIBIT, 0,
				      option);

	return bq25710_set_option(chgnum, option);
}

static enum ec_error_list bq25710_enable_otg_power(int chgnum, int enabled)
{
	/* This is controlled with the EN_OTG pin. Support not added yet. */
	return EC_ERROR_UNIMPLEMENTED;
}

static enum ec_error_list bq25710_set_otg_current_voltage(int chgum,
							  int output_current,
							  int output_voltage)
{
	/* Add when needed. */
	return EC_ERROR_UNIMPLEMENTED;
}

static enum ec_error_list bq25710_get_current(int chgnum, int *current)
{
	int rv, reg;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_CURRENT, &reg);
	if (!rv)
		*current = REG_TO_CHARGING_CURRENT(reg);

	return rv;
}

static enum ec_error_list bq25710_set_current(int chgnum, int current)
{
	return raw_write16(chgnum, BQ25710_REG_CHARGE_CURRENT,
			   CHARGING_CURRENT_TO_REG(current));
}

/* Get/set charge voltage limit in mV */
static enum ec_error_list bq25710_get_voltage(int chgnum, int *voltage)
{
	return raw_read16(chgnum, BQ25710_REG_MAX_CHARGE_VOLTAGE, voltage);
}

static enum ec_error_list bq25710_set_voltage(int chgnum, int voltage)
{
	return raw_write16(chgnum, BQ25710_REG_MAX_CHARGE_VOLTAGE, voltage);
}

/* Discharge battery when on AC power. */
static enum ec_error_list bq25710_discharge_on_ac(int chgnum, int enable)
{
	int rv, option;

	rv = bq25710_get_option(chgnum, &option);
	if (rv)
		return rv;

	if (enable)
		option = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_LEARN, 1,
				      option);
	else
		option = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_LEARN, 0,
				      option);

	return bq25710_set_option(chgnum, option);
}

static enum ec_error_list bq25710_set_input_current_limit(int chgnum,
							  int input_current)
{
	int num_steps = iin_host_current_to_reg(input_current);

	return raw_write16(chgnum, BQ25710_REG_IIN_HOST,
			   num_steps << BQ257X0_IIN_HOST_CURRENT_SHIFT);
}

static enum ec_error_list bq25710_get_input_current_limit(int chgnum,
							  int *input_current)
{
	int rv, reg;

	/*
	 * IIN_DPM register reflects the actual input current limit programmed
	 * in the register, either from host or from ICO. After ICO, the
	 * current limit used by DPM regulation may differ from the IIN_HOST
	 * register settings.
	 */
	rv = raw_read16(chgnum, BQ25710_REG_IIN_DPM, &reg);
	if (!rv)
		*input_current = iin_dpm_reg_to_current(
			reg >> BQ257X0_IIN_DPM_CURRENT_SHIFT);

	return rv;
}

static enum ec_error_list bq25710_manufacturer_id(int chgnum, int *id)
{
	return raw_read16(chgnum, BQ25710_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list bq25710_device_id(int chgnum, int *id)
{
	return raw_read16(chgnum, BQ25710_REG_DEVICE_ADDRESS, id);
}

#ifdef CONFIG_USB_PD_VBUS_MEASURE_CHARGER

#if defined(CONFIG_CHARGER_BQ25720)

static int reg_adc_vbus_to_mv(int reg)
{
	/*
	 * LSB => 96mV, no DC offset.
	 */
	return reg * BQ25720_ADC_VBUS_STEP_MV;
}

#elif defined(CONFIG_CHARGER_BQ25710)

static int reg_adc_vbus_to_mv(int reg)
{
	/*
	 * LSB => 64mV.
	 * Return 0 when VBUS <= 3.2V as ADC can't measure it.
	 */
	return reg ? (reg * BQ25710_ADC_VBUS_STEP_MV +
		      BQ25710_ADC_VBUS_BASE_MV) :
		     0;
}

#else
#error Only the BQ25720 and BQ25710 are supported by bq25710 driver.
#endif

static enum ec_error_list bq25710_get_vbus_voltage(int chgnum, int port,
						   int *voltage)
{
	int reg, rv;

	rv = bq25710_adc_start(chgnum,
			       BQ_FIELD_MASK(BQ257X0, ADC_OPTION, EN_ADC_VBUS));
	if (rv)
		goto error;

	/* Read ADC value */
	rv = raw_read16(chgnum, BQ25710_REG_ADC_VBUS_PSYS, &reg);
	if (rv)
		goto error;

	reg >>= BQ257X0_ADC_VBUS_PSYS_VBUS_SHIFT;
	*voltage = reg_adc_vbus_to_mv(reg);

error:
	if (rv)
		CPRINTF("Could not read VBUS ADC! Error: %d\n", rv);
	return rv;
}
#endif

static enum ec_error_list bq25710_get_option(int chgnum, int *option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_0, option);
}

static enum ec_error_list bq25710_set_option(int chgnum, int option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_0, option);
}

int bq25710_set_min_system_voltage(int chgnum, int mv)
{
	int reg;

	reg = min_system_voltage_to_reg(mv);
	return raw_write16(chgnum, BQ25710_REG_MIN_SYSTEM_VOLTAGE, reg);
}

#ifdef CONFIG_CHARGE_RAMP_HW

static void bq25710_chg_ramp_handle(void)
{
	int ramp_curr;
	int chgnum = 0;

	if (IS_ENABLED(CONFIG_OCPC))
		chgnum = charge_get_active_chg_chip();

	/*
	 * Once the charge ramp is stable write back the stable ramp
	 * current to the host input current limit register
	 */
	ramp_curr = chg_ramp_get_current_limit();
	if (chg_ramp_is_stable()) {
		if (ramp_curr &&
		    !charger_set_input_current_limit(chgnum, ramp_curr))
			CPRINTF("bq25710: stable ramp current=%d\n", ramp_curr);
	} else {
		CPRINTF("bq25710: ICO stall, ramp current=%d\n", ramp_curr);
	}
	/*
	 * Disable ICO mode. When ICO mode is active the input current limit is
	 * given by the value in register IIN_DPM (0x22)
	 */
	charger_set_hw_ramp(0);
}
DECLARE_DEFERRED(bq25710_chg_ramp_handle);

static enum ec_error_list bq25710_set_hw_ramp(int chgnum, int enable)
{
	int option3_reg, option2_reg, rv;

	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_3, &option3_reg);
	if (rv)
		return rv;
	rv = raw_read16(chgnum, BQ25710_REG_CHARGE_OPTION_2, &option2_reg);
	if (rv)
		return rv;

	if (enable) {
		/*
		 * ICO mode can only be used when a battery is present. If there
		 * is no battery, or if the battery has not recovered yet from
		 * cutoff, then enabling ICO mode will lead to VSYS
		 * dropping out.
		 */
		if (!battery_is_present() || (battery_get_disconnect_state() !=
					      BATTERY_NOT_DISCONNECTED)) {
			CPRINTF("bq25710: no battery, skip ICO enable\n");
			return EC_ERROR_UNKNOWN;
		}

		/* Set InputVoltage register to BC1.2 minimum ramp voltage */
		rv = raw_write16(chgnum, BQ25710_REG_INPUT_VOLTAGE,
				 BQ25710_BC12_MIN_VOLTAGE_MV);
		if (rv)
			return rv;

		/*  Enable ICO algorithm */
		option3_reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_3,
					   EN_ICO_MODE, 1, option3_reg);

		/* 0b: Input current limit is set by BQ25710_REG_IIN_HOST */
		option2_reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_2, EN_EXTILIM,
					   0, option2_reg);

		/* Charge ramp may take up to 2s to settle down */
		hook_call_deferred(&bq25710_chg_ramp_handle_data, (4 * SECOND));
	} else {
		/*  Disable ICO algorithm */
		option3_reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_3,
					   EN_ICO_MODE, 0, option3_reg);

		/*
		 * 1b: Input current limit is set by the lower value of
		 * ILIM_HIZ pin and BQ25710_REG_IIN_HOST
		 */
		option2_reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_2, EN_EXTILIM,
					   1, option2_reg);
	}

	rv = raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_2, option2_reg);
	if (rv)
		return rv;
	return raw_write16(chgnum, BQ25710_REG_CHARGE_OPTION_3, option3_reg);
}

static int bq25710_ramp_is_stable(int chgnum)
{
	int reg;

	if (raw_read16(chgnum, BQ25710_REG_CHARGER_STATUS, &reg))
		return 0;

	return reg & BQ_FIELD_MASK(BQ257X0, CHARGER_STATUS, ICO_DONE);
}

static int bq25710_ramp_get_current_limit(int chgnum)
{
	int reg, rv;

	rv = raw_read16(chgnum, BQ25710_REG_IIN_DPM, &reg);
	if (rv) {
		CPRINTF("Could not read iin_dpm current limit! Error: %d\n",
			rv);
		return 0;
	}

	return iin_dpm_reg_to_current(reg >> BQ257X0_IIN_DPM_CURRENT_SHIFT);
}
#endif /* CONFIG_CHARGE_RAMP_HW */

#ifdef CONFIG_CHARGER_BQ25710_IDCHG_LIMIT_MA
/* Called on AP S5 -> S3  and S3/S0iX -> S0 transition */
static void bq25710_chipset_startup(void)
{
	bq25710_set_low_power_mode(CHARGER_SOLO, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, bq25710_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, bq25710_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S0iX/S3 or S3 -> S5 transition */
static void bq25710_chipset_suspend(void)
{
	bq25710_set_low_power_mode(CHARGER_SOLO, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, bq25710_chipset_suspend, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, bq25710_chipset_suspend, HOOK_PRIO_DEFAULT);
#endif

#ifdef CONFIG_CMD_CHARGER_DUMP
static void console_bq25710_dump_regs(int chgnum)
{
	int i;
	int val;

	/* Dump all readable registers on bq25710. */
	static const uint8_t regs[] = {
		BQ25710_REG_CHARGE_OPTION_0,
		BQ25710_REG_CHARGE_CURRENT,
		BQ25710_REG_MAX_CHARGE_VOLTAGE,
		BQ25710_REG_CHARGER_STATUS,
		BQ25710_REG_PROCHOT_STATUS,
		BQ25710_REG_IIN_DPM,
		BQ25710_REG_ADC_VBUS_PSYS,
		BQ25710_REG_ADC_IBAT,
		BQ25710_REG_ADC_CMPIN_IIN,
		BQ25710_REG_ADC_VSYS_VBAT,
		BQ25710_REG_CHARGE_OPTION_1,
		BQ25710_REG_CHARGE_OPTION_2,
		BQ25710_REG_CHARGE_OPTION_3,
		BQ25710_REG_PROCHOT_OPTION_0,
		BQ25710_REG_PROCHOT_OPTION_1,
		BQ25710_REG_ADC_OPTION,
#ifdef CONFIG_CHARGER_BQ25720
		BQ25720_REG_CHARGE_OPTION_4,
		BQ25720_REG_VMIN_ACTIVE_PROTECTION,
#endif
		BQ25710_REG_OTG_VOLTAGE,
		BQ25710_REG_OTG_CURRENT,
		BQ25710_REG_INPUT_VOLTAGE,
		BQ25710_REG_MIN_SYSTEM_VOLTAGE,
		BQ25710_REG_IIN_HOST,
		BQ25710_REG_MANUFACTURER_ID,
		BQ25710_REG_DEVICE_ADDRESS,
	};

	for (i = 0; i < ARRAY_SIZE(regs); ++i) {
		if (raw_read16(chgnum, regs[i], &val))
			continue;
		ccprintf("BQ25710 REG 0x%02x:  0x%04x\n", regs[i], val);
	}
}
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv bq25710_drv = {
	.init = &bq25710_init,
	.post_init = &bq25710_post_init,
	.get_info = &bq25710_get_info,
	.get_status = &bq25710_get_status,
	.set_mode = &bq25710_set_mode,
	.enable_otg_power = &bq25710_enable_otg_power,
	.set_otg_current_voltage = &bq25710_set_otg_current_voltage,
	.get_current = &bq25710_get_current,
	.set_current = &bq25710_set_current,
	.get_voltage = &bq25710_get_voltage,
	.set_voltage = &bq25710_set_voltage,
	.discharge_on_ac = &bq25710_discharge_on_ac,
#ifdef CONFIG_USB_PD_VBUS_MEASURE_CHARGER
	.get_vbus_voltage = &bq25710_get_vbus_voltage,
#endif
	.set_input_current_limit = &bq25710_set_input_current_limit,
	.get_input_current_limit = &bq25710_get_input_current_limit,
	.manufacturer_id = &bq25710_manufacturer_id,
	.device_id = &bq25710_device_id,
	.get_option = &bq25710_get_option,
	.set_option = &bq25710_set_option,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &bq25710_set_hw_ramp,
	.ramp_is_stable = &bq25710_ramp_is_stable,
	.ramp_get_current_limit = &bq25710_ramp_get_current_limit,
#endif /* CONFIG_CHARGE_RAMP_HW */
#ifdef CONFIG_CMD_CHARGER_DUMP
	.dump_registers = &console_bq25710_dump_regs,
#endif
};
