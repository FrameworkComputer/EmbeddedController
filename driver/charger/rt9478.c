/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek 2~4 Cell NVDC Buck-Boost Battery Charge Controller Driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "charge_ramp.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "rt9478.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#ifndef CONFIG_CHARGER_RT9478
#error Only the RT9478 is supported by rt9478 driver.
#endif

#ifndef CONFIG_CHARGER_NARROW_VDC
#error "RT9478 is a NVDC charger, please enable CONFIG_CHARGER_NARROW_VDC."
#endif

#ifndef CONFIG_CHARGER_RT9478_VSYS_TH2_CUSTOM
#define CONFIG_CHARGER_RT9478_VSYS_TH2_DV \
	GET_RT_FIELD(RT9478, VMIN_AP, VSYS_TH2, UINT16_MAX)
#endif

#ifndef CONFIG_CHARGER_RT9478_VSYS_MIN_VOLTAGE_CUSTOM
#define CONFIG_CHARGER_RT9478_VSYS_MIN_VOLTAGE_MV 0
#endif

#ifndef CONFIG_CHARGER_RT9478_VSYS_UVP_CUSTOM
#define CONFIG_CHARGER_RT9478_VSYS_UVP 1
#endif

#ifndef CONFIG_CHARGER_RT9478_IDCHG_DEG2_CUSTOM
#define CONFIG_CHARGER_RT9478_IDCHG_DEG2 1
#endif

#ifndef CONFIG_CHARGER_RT9478_IDCHG_TH2_CUSTOM
#define CONFIG_CHARGER_RT9478_IDCHG_TH2 1
#endif

#if !defined(CONFIG_ZEPHYR) && \
	!defined(CONFIG_CHARGER_RT9478_PKPWR_TOVLD_DEG_CUSTOM)
#define CONFIG_CHARGER_RT9478_PKPWR_TOVLD_DEG 0
#endif

#ifndef CONFIG_CHARGER_RT9478_IAICR2_CUSTOM
/* Reduce IAICR2 from default of 150% to 110% */
#define CONFIG_CHARGER_RT9478_IAICR2 RT9478_PROCHOT_OPTION_0_IAICR2__1P10
#endif

/*
 * Helper macros
 */

#define SET_CO0(_field, _v, _x) \
	SET_RT_FIELD(RT9478, CHARGE_OPTION_0, _field, _v, (_x))

#define SET_CO1_BY_NAME(_field, _c, _x) \
	SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_1, _field, _c, (_x))

#define SET_CO2(_field, _v, _x) \
	SET_RT_FIELD(RT9478, CHARGE_OPTION_2, _field, _v, (_x))

#define SET_CO2_BY_NAME(_field, _c, _x) \
	SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_2, _field, _c, (_x))

#define SET_CO3_BY_NAME(_field, _c, _x) \
	SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_3, _field, _c, (_x))

#define SET_CO4(_field, _v, _x) \
	SET_RT_FIELD(RT9478, CHARGE_OPTION_4, _field, _v, (_x))

#define SET_CO4_BY_NAME(_field, _c, _x) \
	SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_4, _field, _c, (_x))

#define SET_PO0(_field, _v, _x) \
	SET_RT_FIELD(RT9478, PROCHOT_OPTION_0, _field, _v, (_x))

#define SET_PO1(_field, _v, _x) \
	SET_RT_FIELD(RT9478, PROCHOT_OPTION_1, _field, _v, (_x))

#define SET_PO1_BY_NAME(_field, _c, _x) \
	SET_RT_FIELD_BY_NAME(RT9478, PROCHOT_OPTION_1, _field, _c, (_x))

/*
 * Delay required from taking the rt9478 out of low power mode and having the
 * correct value in register 0x3E for VSYS_MIN voltage. The length of the delay
 * was determined by experiment. Less than 12 msec was not enough of delay, so
 * the value here is set to 20 msec to have plenty of margin.
 */
#define RT9478_VDDA_STARTUP_DELAY_MSEC 20

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10

#define REG_TO_CHARGING_CURRENT(REG) \
	((REG) * DEFAULT_SENSE_RESISTOR / CONFIG_CHARGER_RT9478_SENSE_RESISTOR)
#define REG_TO_CHARGING_CURRENT_IN(REG)   \
	((REG) * DEFAULT_SENSE_RESISTOR / \
	 CONFIG_CHARGER_RT9478_SENSE_RESISTOR_IN)
#define CHARGING_CURRENT_TO_REG(CUR) \
	((CUR) * CONFIG_CHARGER_RT9478_SENSE_RESISTOR / DEFAULT_SENSE_RESISTOR)
#define CHARGING_CURRENT_TO_REG_IN(CUR)                    \
	((CUR) * CONFIG_CHARGER_RT9478_SENSE_RESISTOR_IN / \
	 DEFAULT_SENSE_RESISTOR)
#define VMIN_AP_VSYS_TH2_TO_REG(DV) ((DV) - 32)

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)

/*
 * If this config option is defined, then the rt9478 needs to remain in
 * performance mode when the AP is in S0. Performance mode is active whenever AC
 * power is connected or when the EN_LPWR bit in ChargeOption0 is clear.
 */
static uint32_t rt9478_perf_mode_req;
#ifdef CONFIG_ZEPHYR
static mutex_t rt9478_perf_mode_mutex;
#else
static K_MUTEX_DEFINE(rt9478_perf_mode_mutex);
#endif /* CONFIG_ZEPHYR */

/*
 * 10mOhm sense resistor, there is 100mA offset at code 0.
 * 5mOhm sense resistor, there is 200mA offset at code 0.
 */
#define RT9478_IAICR_CODE0_OFFSET REG_TO_CHARGING_CURRENT_IN(100)

/* Charger parameters */
static const struct charger_info rt9478_charger_info = {
	.name = "rt9478",
	.voltage_max = 19200,
	.voltage_min = 5000,
	.voltage_step = 8,
	.current_max = REG_TO_CHARGING_CURRENT(8128), /* mA */
	.current_min = REG_TO_CHARGING_CURRENT(0),
	.current_step = REG_TO_CHARGING_CURRENT(64),
	.input_current_max = REG_TO_CHARGING_CURRENT_IN(6350),
	.input_current_min = REG_TO_CHARGING_CURRENT_IN(100),
	.input_current_step = REG_TO_CHARGING_CURRENT_IN(50),
};

static enum ec_error_list rt9478_get_option(int chgnum, int *option);
static enum ec_error_list rt9478_set_option(int chgnum, int option);

static inline int iaicr_reg_to_current(int reg)
{
	if (reg == 0 || reg == 1)
		return RT9478_IAICR_CODE0_OFFSET;
	return REG_TO_CHARGING_CURRENT_IN(reg * RT9478_AICR_CURRENT_STEP_MA);
}

static inline int aicr_host_current_to_reg(int current)
{
	return CHARGING_CURRENT_TO_REG_IN(current) /
	       RT9478_AICR_HOST_CURRENT_STEP_MA;
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

	steps = voltage_mv / RT9478_VSYS_MIN_VOLTAGE_STEP_MV;
	reg = SET_RT_FIELD(RT9478, VSYS_MIN, VOLTAGE, steps, 0);
	return reg;
}

static inline enum ec_error_list raw_write16(int chgnum, int offset, int value)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags, offset, value);
}

static int rt9478_set_low_power_mode(int chgnum, bool enable)
{
	int rv;
	int reg;

	mutex_lock(&rt9478_perf_mode_mutex);
	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		goto unlock_and_return;

	/*
	 * Performance mode means not in low power mode. The bit that controls
	 * this is EN_LPWR in ChargeOption0. The 'enable' param in this
	 * function is referring to low power mode, so enabling low power mode
	 * means disabling performance mode and vice versa.
	 */
	if (enable)
		rt9478_perf_mode_req &= ~BIT(task_get_current());
	else
		rt9478_perf_mode_req |= BIT(task_get_current());
	enable = !rt9478_perf_mode_req;

	reg = SET_CO0(EN_LPWR, enable, reg);

	rv = raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_0, reg);
unlock_and_return:
	mutex_unlock(&rt9478_perf_mode_mutex);
	return rv;
}

static int rt9478_get_low_power_mode(int chgnum, bool *mode)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		return rv;

	*mode = reg & RT_FIELD_MASK(RT9478, CHARGE_OPTION_0, EN_LPWR);

	return EC_SUCCESS;
}

static int rt9478_adc_start(int chgnum, int adc_en_mask)
{
	bool mode;
	int rv;
	int reg;
	int tries_left = RT9478_ADC_OPTION_ADC_CONV_MS;

	/* Save current mode to restore same state after ADC read */
	if (rt9478_get_low_power_mode(chgnum, &mode))
		return EC_ERROR_UNKNOWN;

	/* Exit low power mode so ADC conversion takes typical time */
	if (rt9478_set_low_power_mode(chgnum, false))
		return EC_ERROR_UNKNOWN;

	/*
	 * Turn on the ADC for one reading. Note that adc_en_mask
	 * maps to bit[7:0] in ADCOption register.
	 */
	reg = (adc_en_mask & RT9478_ADC_OPTION_EN_ADC_ALL) |
	      RT_FIELD_MASK(RT9478, ADC_OPTION, ADC_START);
	if (raw_write16(chgnum, RT9478_REG_ADC_OPTION, reg))
		return EC_ERROR_UNKNOWN;

	/*
	 * Wait until the ADC operation completes. The spec says typical
	 * conversion time is 25 msec. If low power
	 * mode isn't exited first, then the conversion time jumps to
	 * ~60 msec.
	 */
	do {
		/* sleep 2 ms so we time out after 2x the expected time */
		crec_msleep(2);
		rv = raw_read16(chgnum, RT9478_REG_ADC_OPTION, &reg);
	} while (--tries_left && !rv &&
		 (reg & RT_FIELD_MASK(RT9478, ADC_OPTION, ADC_START)));

	/* ADC reading attempt complete, go back to low power mode */
	if (rt9478_set_low_power_mode(chgnum, mode))
		return EC_ERROR_UNKNOWN;

	if (rv)
		return rv;

	/* Could not complete read */
	if (reg & RT_FIELD_MASK(RT9478, ADC_OPTION, ADC_START))
		return EC_ERROR_TIMEOUT;

	return EC_SUCCESS;
}

static int co1_set_psys_sensing(int reg, bool enable)
{
	if (enable)
		reg = SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_1, PSYS_CONFIG,
					   PBUS_PBAT, reg);
	else
		reg = SET_RT_FIELD_BY_NAME(RT9478, CHARGE_OPTION_1, PSYS_CONFIG,
					   OFF, reg);

	return reg;
}

static void rt9478_init_input_charge_current_sensing(int *reg)
{
	if (CONFIG_CHARGER_RT9478_SENSE_RESISTOR_IN == 10)
		*reg = SET_CO1_BY_NAME(RSNS_IN, 10, *reg);
	else if (CONFIG_CHARGER_RT9478_SENSE_RESISTOR_IN == 5)
		*reg = SET_CO1_BY_NAME(RSNS_IN, 5, *reg);

	if (CONFIG_CHARGER_RT9478_SENSE_RESISTOR == 10)
		*reg = SET_CO1_BY_NAME(RSNS_BAT, 10, *reg);
	else if (CONFIG_CHARGER_RT9478_SENSE_RESISTOR == 5)
		*reg = SET_CO1_BY_NAME(RSNS_BAT, 5, *reg);
}

static int rt9478_init_charge_option_1(int chgnum)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_1, &reg);
	if (rv)
		return rv;

	rt9478_init_input_charge_current_sensing(&reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PSYS_SENSING))
		reg = co1_set_psys_sensing(reg, true);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_CMP_REF_1P2))
		reg = SET_CO1_BY_NAME(CMP_REF, 1P2, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_CMP_POL_POSITIVE))
		reg = SET_CO1_BY_NAME(CMP_POL, POSITIVE, reg);

	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_1, reg);
}

static int rt9478_init_prochot_option_0(int chgnum)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, RT9478_REG_PROCHOT_OPTION_0, &reg);
	if (rv)
		return rv;

	reg = SET_PO0(IAICR2, CONFIG_CHARGER_RT9478_IAICR2, reg);

	return raw_write16(chgnum, RT9478_REG_PROCHOT_OPTION_0, reg);
}

static int rt9478_init_prochot_option_1(int chgnum)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, RT9478_REG_PROCHOT_OPTION_1, &reg);
	if (rv)
		return rv;

	/* Disable MIVR prochot profile at initialization */
	reg = SET_PO1_BY_NAME(PP_MIVR, DISABLE, reg);

	/*
	 * Enable PROCHOT to be asserted with VSYS min detection. Note
	 * that when no battery is present, then VSYS will be set to the
	 * value in register 0x3E (MinSysVoltage) which means that when
	 * no battery is present prochot will continuously be asserted.
	 */
	reg = SET_PO1_BY_NAME(PP_VSYS, ENABLE, reg);

#ifdef CONFIG_CHARGER_RT9478_IDCHG_LIMIT_MA
	/*
	 * Set the IDCHG limit who's value is defined in the config
	 * option in mA.
	 *
	 * IDCHG limit is in 512 mA steps.
	 */
	reg = SET_PO1(IDCHG_TH1, CONFIG_CHARGER_RT9478_IDCHG_LIMIT_MA >> 9,
		      reg);

	/*  Enable IDCHG trigger for prochot. */
	reg = SET_PO1_BY_NAME(PP_IDCHG1, ENABLE, reg);
#endif
	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PP_COMP))
		reg = SET_PO1_BY_NAME(PP_COMP, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PP_INOM))
		reg = SET_PO1_BY_NAME(PP_INOM, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PP_BATGONE))
		reg = SET_PO1_BY_NAME(PP_BATGONE, ENABLE, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PP_VBUSOK))
		reg = SET_PO1_BY_NAME(PP_VBUSOK, ENABLE, reg);

	return raw_write16(chgnum, RT9478_REG_PROCHOT_OPTION_1, reg);
}

static int rt9478_init_charge_option_2(int chgnum)
{
	int reg;
	int rv;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_2, &reg);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PKPWR_TOVLD_DEG_CUSTOM)) {
		/* Set input overload time in peak power mode. */
		reg = SET_CO2(PKPWR_TOVLD_DEG,
			      CONFIG_CHARGER_RT9478_PKPWR_TOVLD_DEG, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_EN_IBUS_OCP1)) {
		/* Enable the VBUS sense resistor over-current protection. */
		reg = SET_CO2_BY_NAME(EN_IBUS_OCP1, ENABLE, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_IBUS_OCP1_TH_1P33)) {
		/* Set the VBUS sense resistor over-current protection value
		 * to 133% of IAICR2 */
		reg = SET_CO2_BY_NAME(IBUS_OCP1_TH, 1P33, reg);
	}

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_BATOC_VTH_MINIMUM)) {
		/* Set battery over-current threshold to minimum. */
		reg = SET_CO2_BY_NAME(BATOC_VTH, 1P33, reg);
	}

	/* Set ILIM pin disabled if it is currently enabled. */
	if (IS_ENABLED(CONFIG_CHARGER_ILIM_PIN_DISABLED)) {
		reg = SET_CO2_BY_NAME(EN_EXTILIM, DISABLE, reg);
	}

	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_2, reg);
}

static int rt9478_init_charge_option_3(int chgnum)
{
	int reg;
	int rv;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_3, &reg);
	if (rv)
		return rv;

	reg = SET_CO3_BY_NAME(IL_AVG, 10A, reg);

	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_3, reg);
}

static int rt9478_init_charge_option_4(int chgnum)
{
	int reg;
	int rv;

	if (!IS_ENABLED(CONFIG_CHARGER_RT9478_VSYS_UVP_CUSTOM) &&
	    !IS_ENABLED(CONFIG_CHARGER_RT9478_IDCHG_DEG2_CUSTOM) &&
	    !IS_ENABLED(CONFIG_CHARGER_RT9478_IDCHG_TH2_CUSTOM))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_4, &reg);
	if (rv)
		return rv;

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_VSYS_UVP_CUSTOM))
		reg = SET_CO4(VSYS_UVP, CONFIG_CHARGER_RT9478_VSYS_UVP, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_IDCHG_DEG2_CUSTOM))
		reg = SET_CO4(IDCHG_DEG2, CONFIG_CHARGER_RT9478_IDCHG_DEG2,
			      reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_IDCHG_TH2_CUSTOM))
		reg = SET_CO4(IDCHG_TH2, CONFIG_CHARGER_RT9478_IDCHG_TH2, reg);

	if (IS_ENABLED(CONFIG_CHARGER_RT9478_PP_IDCHG2))
		reg = SET_CO4_BY_NAME(PP_IDCHG2, ENABLE, reg);

	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_4, reg);
}

static int rt9478_init_vmin_active_protection(int chgnum)
{
	int reg;
	int rv;
	int th2_dv;

	if (!IS_ENABLED(CONFIG_CHARGER_RT9478_VSYS_TH2_CUSTOM))
		return EC_SUCCESS;

	rv = raw_read16(chgnum, RT9478_REG_VMIN_ACTIVE_PROTECTION, &reg);
	if (rv)
		return rv;

	/*
	 * The default VSYS_TH2 is 5.9v. Boards may need
	 * to increase this for stability. PROCHOT is asserted when the
	 * threshold is reached.
	 */
	th2_dv = VMIN_AP_VSYS_TH2_TO_REG(CONFIG_CHARGER_RT9478_VSYS_TH2_DV);
	reg = SET_RT_FIELD(RT9478, VMIN_AP, VSYS_TH2, th2_dv, reg);

	return raw_write16(chgnum, RT9478_REG_VMIN_ACTIVE_PROTECTION, reg);
}

static void rt9478_init(int chgnum)
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
	 * rt9478 must not be in low power mode, otherwise the VDDA
	 * rail may not be powered if AC is not connected. Note, this
	 * reset is only required when running out of RO and not
	 * following sysjump to RW.
	 */
	if (!system_jumped_late() && battery_is_present() == BP_YES &&
	    (battery_get_disconnect_state() == BATTERY_NOT_DISCONNECTED)) {
		rv = rt9478_set_low_power_mode(chgnum, false);
		/* Allow enough time for VDDA to be powered */
		crec_msleep(RT9478_VDDA_STARTUP_DELAY_MSEC);

		if (IS_ENABLED(CONFIG_CHARGER_RT9478_VSYS_MIN_VOLTAGE_CUSTOM)) {
			vsys = min_system_voltage_to_reg(
				CONFIG_CHARGER_RT9478_VSYS_MIN_VOLTAGE_MV);
		} else {
			rv |= raw_read16(chgnum, RT9478_REG_MIN_SYSTEM_VOLTAGE,
					 &vsys);
		}

		rv |= raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_3, &reg);
		if (!rv) {
			reg = SET_RT_FIELD(RT9478, CHARGE_OPTION_3, RESET_REG,
					   1, reg);
			/* Set all registers to default values */
			raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_3, reg);
			/* Restore VSYS_MIN voltage to POR reset value */
			raw_write16(chgnum, RT9478_REG_MIN_SYSTEM_VOLTAGE,
				    vsys);
		}
		/* Reenable low power mode */
		rt9478_set_low_power_mode(chgnum, true);
	}

	rt9478_init_charge_option_1(chgnum);

	rt9478_init_prochot_option_0(chgnum);

	rt9478_init_prochot_option_1(chgnum);

	rt9478_init_charge_option_2(chgnum);

	rt9478_init_charge_option_3(chgnum);

	rt9478_init_charge_option_4(chgnum);

	rt9478_init_vmin_active_protection(chgnum);
}

/* Charger interfaces */
static const struct charger_info *rt9478_get_info(int chgnum)
{
	return &rt9478_charger_info;
}

static enum ec_error_list rt9478_post_init(int chgnum)
{
	/*
	 * Note: rt9478 power on reset state is:
	 *	watch dog timer     = 175 sec
	 *	input current limit = ~1/2 maximum setting
	 *	charging voltage    = 0 mV
	 *	charging current    = 0 mA
	 *	discharge on AC     = disabled
	 */

	return EC_SUCCESS;
}

static enum ec_error_list rt9478_get_status(int chgnum, int *status)
{
	int rv;
	int option;

	rv = rt9478_get_option(chgnum, &option);
	if (rv)
		return rv;

	/* Default status */
	*status = CHARGER_LEVEL_2;

	if (option & RT_FIELD_MASK(RT9478, CHARGE_OPTION_0, CHG_INHIBIT))
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list rt9478_set_mode(int chgnum, int mode)
{
	int rv;
	int option;

	rv = rt9478_get_option(chgnum, &option);
	if (rv)
		return rv;

	option = SET_CO0(CHG_INHIBIT, mode & CHARGE_FLAG_INHIBIT_CHARGE ? 1 : 0,
			 option);

	return rt9478_set_option(chgnum, option);
}

static enum ec_error_list rt9478_enable_otg_power(int chgnum, int enabled)
{
	/* This is controlled with the EN_OTG pin. Support not added yet. */
	return EC_ERROR_UNIMPLEMENTED;
}

static enum ec_error_list rt9478_set_otg_current_voltage(int chgum,
							 int output_current,
							 int output_voltage)
{
	/* Add when needed. */
	return EC_ERROR_UNIMPLEMENTED;
}

static enum ec_error_list rt9478_get_current(int chgnum, int *current)
{
	int rv, reg;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_CURRENT, &reg);
	if (!rv)
		*current = REG_TO_CHARGING_CURRENT(reg);

	return rv;
}

static enum ec_error_list rt9478_set_current(int chgnum, int current)
{
	return raw_write16(chgnum, RT9478_REG_CHARGE_CURRENT,
			   CHARGING_CURRENT_TO_REG(current));
}

/* Get/set charge voltage limit in mV */
static enum ec_error_list rt9478_get_voltage(int chgnum, int *voltage)
{
	return raw_read16(chgnum, RT9478_REG_CHARGE_VOLTAGE, voltage);
}

static enum ec_error_list rt9478_set_voltage(int chgnum, int voltage)
{
	return raw_write16(chgnum, RT9478_REG_CHARGE_VOLTAGE, voltage);
}

/* Discharge battery when on AC power. */
static enum ec_error_list rt9478_discharge_on_ac(int chgnum, int enable)
{
	int rv, option;

	rv = rt9478_get_option(chgnum, &option);
	if (rv)
		return rv;

	option = SET_CO0(EN_LEARN, enable ? 1 : 0, option);

	return rt9478_set_option(chgnum, option);
}

static enum ec_error_list rt9478_set_input_current_limit(int chgnum,
							 int input_current)
{
	int num_steps = aicr_host_current_to_reg(input_current);

	return raw_write16(chgnum, RT9478_REG_AICR_HOST,
			   num_steps << RT9478_AICR_HOST_CURRENT_SHIFT);
}

static enum ec_error_list rt9478_get_input_current_limit(int chgnum,
							 int *input_current)
{
	int rv, reg;

	/*
	 * AICR register reflects the actual input current limit programmed
	 * in the register, either from host or from AICC. After AICC, the
	 * current limit used by regulation may differ from the AICR_HOST
	 * register settings.
	 */
	rv = raw_read16(chgnum, RT9478_REG_AICR, &reg);
	if (!rv)
		*input_current =
			iaicr_reg_to_current(reg >> RT9478_AICR_CURRENT_SHIFT);

	return rv;
}

static int reg_adc_input_current_to_ma(int reg)
{
	/*
	 * LSB => 50mA.
	 */
	return reg * REG_TO_CHARGING_CURRENT_IN(50);
}

static enum ec_error_list rt9478_get_input_current(int chgnum,
						   int *input_current)
{
	int reg, rv;

	rv = rt9478_adc_start(chgnum,
			      RT_FIELD_MASK(RT9478, ADC_OPTION, EN_ADC_IBUS));
	if (rv)
		goto error;

	/* Read ADC value */
	rv = raw_read16(chgnum, RT9478_REG_ADC_CMPIN_IIN, &reg);
	if (rv)
		goto error;

	reg >>= RT9478_ADC_IIN_CMPIN_IBUS_SHIFT;
	*input_current = reg_adc_input_current_to_ma(reg);

error:
	if (rv)
		CPRINTF("Could not read IBUS ADC! Error: %d\n", rv);
	return rv;
}

static enum ec_error_list rt9478_manufacturer_id(int chgnum, int *id)
{
	return raw_read16(chgnum, RT9478_REG_MANUFACTURER_ID, id);
}

static enum ec_error_list rt9478_device_id(int chgnum, int *id)
{
	return raw_read16(chgnum, RT9478_REG_DEVICE_ADDRESS, id);
}

#ifdef CONFIG_USB_PD_VBUS_MEASURE_CHARGER

static int reg_adc_vbus_to_mv(int reg)
{
	/*
	 * LSB => 96mV, no DC offset.
	 */
	return reg * RT9478_ADC_VBUS_STEP_MV;
}

static enum ec_error_list rt9478_get_vbus_voltage(int chgnum, int port __unused,
						  int *voltage)
{
	int reg, rv;

	rv = rt9478_adc_start(chgnum,
			      RT_FIELD_MASK(RT9478, ADC_OPTION, EN_ADC_VBUS));
	if (rv)
		goto error;

	/* Read ADC value */
	rv = raw_read16(chgnum, RT9478_REG_ADC_VBUS_PSYS, &reg);
	if (rv)
		goto error;

	reg >>= RT9478_ADC_VBUS_PSYS_VBUS_SHIFT;
	*voltage = reg_adc_vbus_to_mv(reg);

error:
	if (rv)
		CPRINTF("Could not read VBUS ADC! Error: %d\n", rv);
	return rv;
}
#endif

static enum ec_error_list rt9478_get_option(int chgnum, int *option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_0, option);
}

static enum ec_error_list rt9478_set_option(int chgnum, int option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_0, option);
}

int rt9478_set_min_system_voltage(int chgnum, int mv)
{
	int reg;

	reg = min_system_voltage_to_reg(mv);
	return raw_write16(chgnum, RT9478_REG_MIN_SYSTEM_VOLTAGE, reg);
}

#ifdef CONFIG_CHARGE_RAMP_HW

static void rt9478_chg_ramp_handle(void)
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
			CPRINTF("rt9478: stable ramp current=%d\n", ramp_curr);
	} else {
		CPRINTF("rt9478: AICC stall, ramp current=%d\n", ramp_curr);
	}
	/*
	 * Disable AICC mode. When AICC mode is active the input current limit
	 * is given by the value in register AICR (0x22)
	 */
	charger_set_hw_ramp(0);
}
DECLARE_DEFERRED(rt9478_chg_ramp_handle);

static enum ec_error_list rt9478_set_hw_ramp(int chgnum, int enable)
{
	int option3_reg, option2_reg, rv;

	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_3, &option3_reg);
	if (rv)
		return rv;
	rv = raw_read16(chgnum, RT9478_REG_CHARGE_OPTION_2, &option2_reg);
	if (rv)
		return rv;

	if (enable) {
		/*
		 * AICC mode can only be used when a battery is present.
		 * If there is no battery, or if the battery has not recovered
		 * yet from cutoff, then enabling AICC mode will lead to VSYS
		 * dropping out.
		 */
		if (battery_is_present() != BP_YES ||
		    (battery_get_disconnect_state() !=
		     BATTERY_NOT_DISCONNECTED)) {
			CPRINTF("rt9478: no battery, skip AICC enable\n");
			return EC_ERROR_UNKNOWN;
		}

		/*  Enable AICC algorithm */
		option3_reg = SET_RT_FIELD(RT9478, CHARGE_OPTION_3, EN_AICC, 1,
					   option3_reg);

		/* 0b: Input current limit is set by RT9478_REG_AICR_HOST */
		option2_reg = SET_RT_FIELD(RT9478, CHARGE_OPTION_2, EN_EXTILIM,
					   0, option2_reg);

		/* Charge ramp may take up to 2s to settle down */
		hook_call_deferred(&rt9478_chg_ramp_handle_data, (4 * SECOND));
	} else {
		/*  Disable AICC algorithm */
		option3_reg = SET_RT_FIELD(RT9478, CHARGE_OPTION_3, EN_AICC, 0,
					   option3_reg);

		/*
		 * 1b: Input current limit is set by the lower value of
		 * ILIM_HIZ pin and RT9478_REG_AICR_HOST
		 */
		option2_reg = SET_RT_FIELD(RT9478, CHARGE_OPTION_2, EN_EXTILIM,
					   1, option2_reg);
	}

	rv = raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_2, option2_reg);
	if (rv)
		return rv;
	return raw_write16(chgnum, RT9478_REG_CHARGE_OPTION_3, option3_reg);
}

static int rt9478_ramp_is_stable(int chgnum)
{
	int reg;

	if (raw_read16(chgnum, RT9478_REG_CHARGER_STATUS, &reg))
		return 0;

	return reg & RT_FIELD_MASK(RT9478, CHARGER_STATUS, AICC_DONE);
}

static int rt9478_ramp_get_current_limit(int chgnum)
{
	int reg, rv;

	rv = raw_read16(chgnum, RT9478_REG_AICR, &reg);
	if (rv) {
		CPRINTF("Could not read iaicr current limit! Error: %d\n", rv);
		return 0;
	}

	return iaicr_reg_to_current(reg >> RT9478_AICR_CURRENT_SHIFT);
}
#endif /* CONFIG_CHARGE_RAMP_HW */

#ifdef CONFIG_ZEPHYR
static void init_mutex(void)
{
	k_mutex_init(&rt9478_perf_mode_mutex);
}
DECLARE_HOOK(HOOK_INIT, init_mutex, HOOK_PRIO_FIRST);
#endif

#ifdef CONFIG_CHARGER_RT9478_IDCHG_LIMIT_MA
/* Called on AP S5 -> S3  and S3/S0iX -> S0 transition */
static void rt9478_chipset_startup(void)
{
	rt9478_set_low_power_mode(CHARGER_SOLO, false);
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, rt9478_chipset_startup, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, rt9478_chipset_startup, HOOK_PRIO_DEFAULT);

/* Called on AP S0 -> S0iX/S3 or S3 -> S5 transition */
static void rt9478_chipset_suspend(void)
{
	int reg;
	if (raw_read16(CHARGER_SOLO, RT9478_REG_CHARGE_OPTION_0, &reg))
		return;

	/*
	 * Enable low power mode regardless of current performance mode.
	 * The current state would be restored on the following startup
	 */

	reg = SET_CO0(EN_LPWR, true, reg);
	raw_write16(CHARGER_SOLO, RT9478_REG_CHARGE_OPTION_0, reg);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, rt9478_chipset_suspend, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, rt9478_chipset_suspend, HOOK_PRIO_DEFAULT);
#endif

#ifdef CONFIG_CMD_CHARGER_DUMP
static void console_rt9478_dump_regs(int chgnum)
{
	int i;
	int val;

	/* Dump all readable registers on rt9478. */
	static const uint8_t regs[] = {
		RT9478_REG_CHARGE_OPTION_0,  RT9478_REG_CHARGE_CURRENT,
		RT9478_REG_CHARGE_VOLTAGE,   RT9478_REG_CHARGER_STATUS,
		RT9478_REG_PROCHOT_STATUS,   RT9478_REG_AICR,
		RT9478_REG_ADC_VBUS_PSYS,    RT9478_REG_ADC_IBAT,
		RT9478_REG_ADC_CMPIN_IIN,    RT9478_REG_ADC_VSYS_VBAT,
		RT9478_REG_CHARGE_OPTION_1,  RT9478_REG_CHARGE_OPTION_2,
		RT9478_REG_CHARGE_OPTION_3,  RT9478_REG_PROCHOT_OPTION_0,
		RT9478_REG_PROCHOT_OPTION_1, RT9478_REG_ADC_OPTION,
		RT9478_REG_CHARGE_OPTION_4,  RT9478_REG_VMIN_ACTIVE_PROTECTION,
		RT9478_REG_OTG_VOLTAGE,	     RT9478_REG_OTG_CURRENT,
		RT9478_REG_INPUT_VOLTAGE,    RT9478_REG_MIN_SYSTEM_VOLTAGE,
		RT9478_REG_AICR_HOST,	     RT9478_REG_MANUFACTURER_ID,
		RT9478_REG_DEVICE_ADDRESS,
	};

	for (i = 0; i < ARRAY_SIZE(regs); ++i) {
		if (raw_read16(chgnum, regs[i], &val))
			continue;
		ccprintf("RT9478 REG 0x%02x:  0x%04x\n", regs[i], val);
	}
}
#endif /* CONFIG_CMD_CHARGER_DUMP */

const struct charger_drv rt9478_drv = {
	.init = &rt9478_init,
	.post_init = &rt9478_post_init,
	.get_info = &rt9478_get_info,
	.get_status = &rt9478_get_status,
	.set_mode = &rt9478_set_mode,
	.enable_otg_power = &rt9478_enable_otg_power,
	.set_otg_current_voltage = &rt9478_set_otg_current_voltage,
	.get_current = &rt9478_get_current,
	.set_current = &rt9478_set_current,
	.get_voltage = &rt9478_get_voltage,
	.set_voltage = &rt9478_set_voltage,
	.discharge_on_ac = &rt9478_discharge_on_ac,
#ifdef CONFIG_USB_PD_VBUS_MEASURE_CHARGER
	.get_vbus_voltage = &rt9478_get_vbus_voltage,
#endif
	.set_input_current_limit = &rt9478_set_input_current_limit,
	.get_input_current_limit = &rt9478_get_input_current_limit,
	.get_input_current = &rt9478_get_input_current,
	.manufacturer_id = &rt9478_manufacturer_id,
	.device_id = &rt9478_device_id,
	.get_option = &rt9478_get_option,
	.set_option = &rt9478_set_option,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &rt9478_set_hw_ramp,
	.ramp_is_stable = &rt9478_ramp_is_stable,
	.ramp_get_current_limit = &rt9478_ramp_get_current_limit,
#endif /* CONFIG_CHARGE_RAMP_HW */
#ifdef CONFIG_CMD_CHARGER_DUMP
	.dump_registers = &console_rt9478_dump_regs,
#endif
};
