/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek 5A 1-4 cell buck-boost switching battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "builtin/endian.h"
#include "charger.h"
#include "charge_manager.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "rt9490.h"
#include "task.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) \
	cprints(CC_CHARGER, "%s " format, "RT9490", ## args)

/* Charger parameters */
#define CHARGER_NAME    "rt9490"
#define CHARGE_V_MAX    18800
#define CHARGE_V_MIN    3000
#define CHARGE_V_STEP   10
#define CHARGE_I_MAX    5000
#define CHARGE_I_MIN    50
#define CHARGE_I_STEP   10
#define INPUT_I_MAX     3300
#define INPUT_I_MIN     100
#define INPUT_I_STEP    10

/* Charger parameters */
static const struct charger_info rt9490_charger_info = {
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

static const struct rt9490_init_setting default_init_setting = {
	.eoc_current = 200,
	.mivr = 4000,
	.boost_voltage = 5050,
	.boost_current = 1500,
};

static enum ec_error_list rt9490_read8(int chgnum, int reg, int *val)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			chg_chips[chgnum].i2c_addr_flags, reg, val);
}

static enum ec_error_list rt9490_write8(int chgnum, int reg, int val)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			chg_chips[chgnum].i2c_addr_flags, reg, val);
}

static enum ec_error_list rt9490_read16(int chgnum, int reg, uint16_t *val)
{
	int reg_val;

	RETURN_ERROR(i2c_read16(chg_chips[chgnum].i2c_port,
			chg_chips[chgnum].i2c_addr_flags, reg, &reg_val));

	*val = be16toh(reg_val);

	return EC_SUCCESS;
}

static enum ec_error_list rt9490_write16(int chgnum, int reg, uint16_t val)
{
	int reg_val = htobe16(val);

	return i2c_write16(chg_chips[chgnum].i2c_port,
			chg_chips[chgnum].i2c_addr_flags, reg, reg_val);
}

static int rt9490_field_update8(int chgnum, int reg, int mask, int val)
{
	return i2c_field_update8(chg_chips[chgnum].i2c_port,
				 chg_chips[chgnum].i2c_addr_flags,
				 reg, mask, val);
}

static inline int rt9490_update8(int chgnum, int reg, int mask,
				 enum mask_update_action action)
{
	return i2c_update8(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags,
			   reg, mask, action);
}

static inline int rt9490_set_bit(int chgnum, int reg, int mask)
{
	return rt9490_update8(chgnum, reg, mask, MASK_SET);
}

static inline int rt9490_clr_bit(int chgnum, int reg, int mask)
{
	return rt9490_update8(chgnum, reg, mask, MASK_CLR);
}

static inline int rt9490_enable_hz(int chgnum, bool en)
{
	return rt9490_update8(chgnum, RT9490_REG_CHG_CTRL0, RT9490_EN_HZ,
			      en ? MASK_SET : MASK_CLR);
}

static const struct charger_info *rt9490_get_info(int chgnum)
{
	return &rt9490_charger_info;
}

static enum ec_error_list rt9490_get_current(int chgnum, int *current)
{
	uint16_t val = 0;
	const struct charger_info * const info = rt9490_get_info(chgnum);

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_ICHG_CTRL, &val));

	val = (val & RT9490_ICHG_MASK) >> RT9490_ICHG_SHIFT;
	val *= info->current_step;
	*current = CLAMP(val, info->current_min, info->current_max);

	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_current(int chgnum, int current)
{
	uint16_t reg_ichg;
	const struct charger_info *const info = rt9490_get_info(chgnum);

	if (current == 0)
		current = info->current_min;

	if (!IN_RANGE(current, info->current_min, info->current_max + 1))
		return EC_ERROR_PARAM2;
	reg_ichg = current / info->current_step;

	return rt9490_write16(chgnum, RT9490_REG_ICHG_CTRL, reg_ichg);
}

static enum ec_error_list rt9490_get_voltage(int chgnum, int *voltage)
{
	uint16_t val = 0;
	const struct charger_info * const info = rt9490_get_info(chgnum);

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_VCHG_CTRL, &val));

	val = val & RT9490_CV_MASK;
	val *= info->voltage_step;
	*voltage = CLAMP(val, info->voltage_min, info->voltage_max);

	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_voltage(int chgnum, int voltage)
{
	uint16_t reg_cv;
	const struct charger_info *const info = rt9490_get_info(chgnum);

	if (voltage == 0)
		voltage = info->voltage_min;

	if (!IN_RANGE(voltage, info->voltage_min, info->voltage_max + 1))
		return EC_ERROR_PARAM2;
	reg_cv = voltage / info->voltage_step;

	return rt9490_write16(chgnum, RT9490_REG_VCHG_CTRL, reg_cv);
}

#ifdef CONFIG_CHARGER_OTG
static enum ec_error_list rt9490_enable_otg_power(int chgnum, int enabled)
{
	return rt9490_update8(chgnum, RT9490_REG_CHG_CTRL3, RT9490_EN_OTG,
			      enabled ? MASK_SET : MASK_CLR);
}

enum ec_error_list rt9490_set_otg_current_voltage(int chgnum,
						  int output_current,
						  int output_voltage)
{
	uint16_t reg_cur, reg_vol;

	if (!IN_RANGE(output_current, RT9490_IOTG_MIN, RT9490_IOTG_MAX + 1))
		return EC_ERROR_PARAM2;
	if (!IN_RANGE(output_voltage, RT9490_VOTG_MIN, RT9490_VOTG_MAX + 1))
		return EC_ERROR_PARAM3;

	reg_cur = (output_current - RT9490_IOTG_MIN) / RT9490_IOTG_STEP;
	reg_vol = (output_voltage - RT9490_VOTG_MIN) / RT9490_VOTG_STEP;
	RETURN_ERROR(rt9490_write16(chgnum, RT9490_REG_IOTG_REGU, reg_cur));

	return rt9490_write16(chgnum, RT9490_REG_VOTG_REGU, reg_vol);
}

static int rt9490_is_sourcing_otg_power(int chgnum, int port)
{
	int val;

	if (rt9490_read8(chgnum, RT9490_REG_CHG_CTRL3, &val))
		return 0;
	return !!(val & RT9490_EN_OTG);
}
#endif

/* Reset all registers' value to default */
static int rt9490_reset_chip(int chgnum)
{
	/* disable hz before reset chip */
	RETURN_ERROR(rt9490_enable_hz(chgnum, false));

	return rt9490_set_bit(chgnum, RT9490_REG_EOC_CTRL, RT9490_RST_ALL_MASK);
}

static inline int rt9490_enable_chgdet_flow(int chgnum, bool en)
{
	return rt9490_update8(chgnum, RT9490_REG_CHG_CTRL2, RT9490_BC12_EN,
			      en ? MASK_SET : MASK_CLR);
}

static inline int rt9490_enable_wdt(int chgnum, bool en)
{
	int val = en ? RT9490_WATCHDOG_40_SEC : RT9490_WATCHDOG_DISABLE;

	return rt9490_field_update8(chgnum, RT9490_REG_CHG_CTRL1,
				    RT9490_WATCHDOG_MASK, val);
}

static inline int rt9490_set_mivr(int chgnum, unsigned int mivr)
{
	uint8_t reg_mivr = mivr / RT9490_MIVR_STEP;

	return rt9490_write8(chgnum, RT9490_REG_MIVR_CTRL, reg_mivr);
}

static inline int rt9490_set_ieoc(int chgnum, unsigned int ieoc)
{
	uint8_t reg_ieoc = ieoc / RT9490_IEOC_STEP;

	return rt9490_field_update8(chgnum, RT9490_REG_EOC_CTRL,
				    RT9490_IEOC_MASK, reg_ieoc);
}

static inline int rt9490_enable_jeita(int chgnum, bool en)
{
	return rt9490_update8(chgnum, RT9490_REG_JEITA_CTRL1, RT9490_JEITA_DIS,
			      en ? MASK_CLR : MASK_SET);
}

static inline int rt9490_enable_adc(int chgnum, bool en)
{
	return rt9490_update8(chgnum, RT9490_REG_ADC_CTRL, RT9490_ADC_EN,
			      en ? MASK_SET : MASK_CLR);
}

static int rt9490_set_iprec(int chgnum, unsigned int iprec)
{
	uint8_t reg_iprec = iprec / RT9490_IPRE_CHG_STEP;

	return rt9490_field_update8(chgnum, RT9490_REG_PRE_CHG,
				    RT9490_IPRE_CHG_MASK,
				    reg_iprec << RT9490_IPREC_SHIFT);
}

static int rt9490_init_setting(int chgnum)
{
	const struct battery_info *batt_info = battery_get_info();

#ifdef CONFIG_CHARGER_OTG
	/*  Disable boost-mode output voltage */
	RETURN_ERROR(rt9490_enable_otg_power(chgnum, 0));
	RETURN_ERROR(rt9490_set_otg_current_voltage(
			chgnum,
			default_init_setting.boost_current,
			default_init_setting.boost_voltage));
#endif
	/* Disable ILIM_HZ pin current limit */
	RETURN_ERROR(rt9490_clr_bit(
			chgnum, RT9490_REG_CHG_CTRL5, RT9490_ILIM_HZ_EN));
	/* Disable BC 1.2 detection by default. It will be enabled on demand */
	RETURN_ERROR(rt9490_enable_chgdet_flow(chgnum, false));
	/* Disable WDT */
	RETURN_ERROR(rt9490_enable_wdt(chgnum, false));
	/* Disable battery thermal protection */
	RETURN_ERROR(rt9490_set_bit(
			chgnum, RT9490_REG_ADD_CTRL0, RT9490_JEITA_COLD_HOT));
	/* Disable AUTO_AICR / AUTO_MIVR */
	RETURN_ERROR(rt9490_clr_bit(
			chgnum,
			RT9490_REG_ADD_CTRL0,
			RT9490_AUTO_AICR | RT9490_AUTO_MIVR));
	/* Disable charge timer */
	RETURN_ERROR(rt9490_clr_bit(
			chgnum,
			RT9490_REG_SAFETY_TMR_CTRL,
			RT9490_EN_TRICHG_TMR |
			RT9490_EN_PRECHG_TMR |
			RT9490_EN_FASTCHG_TMR));
	RETURN_ERROR(rt9490_set_mivr(chgnum, default_init_setting.mivr));
	RETURN_ERROR(rt9490_set_ieoc(chgnum, default_init_setting.eoc_current));
	RETURN_ERROR(rt9490_set_iprec(chgnum, batt_info->precharge_current));
	RETURN_ERROR(rt9490_enable_adc(chgnum, true));
	RETURN_ERROR(rt9490_enable_jeita(chgnum, false));
	RETURN_ERROR(rt9490_field_update8(
			chgnum, RT9490_REG_CHG_CTRL1, RT9490_VAC_OVP_MASK,
			RT9490_VAC_OVP_26V << RT9490_VAC_OVP_SHIFT));


	return EC_SUCCESS;
}

static void rt9490_init(int chgnum)
{
	int ret = rt9490_init_setting(chgnum);

	CPRINTS("init%d %s(%d)", chgnum, ret ? "fail" : "good", ret);
}

static enum ec_error_list rt9490_get_status(int chgnum, int *status)
{
	int val = 0;

	*status = 0;

	RETURN_ERROR(rt9490_read8(chgnum, RT9490_REG_CHG_CTRL0, &val));
	if (!(val & RT9490_EN_CHG))
		*status |= CHARGER_CHARGE_INHIBITED;

	RETURN_ERROR(rt9490_read8(chgnum, RT9490_REG_FAULT_STATUS0, &val));
	if (val & RT9490_VBAT_OVP_STAT)
		*status |= CHARGER_VOLTAGE_OR;

	RETURN_ERROR(rt9490_read8(chgnum, RT9490_REG_CHG_STATUS4, &val));
	if (val & RT9490_JEITA_COLD_MASK) {
		*status |= CHARGER_RES_COLD;
		*status |= CHARGER_RES_UR;
	}
	if (val & RT9490_JEITA_COOL_MASK) {
		*status |= CHARGER_RES_COLD;
	}
	if (val & RT9490_JEITA_WARM_MASK) {
		*status |= CHARGER_RES_HOT;
	}
	if (val & RT9490_JEITA_HOT_MASK) {
		*status |= CHARGER_RES_HOT;
		*status |= CHARGER_RES_OR;
	}
	return EC_SUCCESS;
}

static int rt9490_reset_to_zero(int chgnum)
{
	RETURN_ERROR(rt9490_set_current(chgnum, 0));
	RETURN_ERROR(rt9490_set_voltage(chgnum, 0));
	RETURN_ERROR(rt9490_enable_hz(chgnum, true));

	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_mode(int chgnum, int mode)
{
	if (mode & CHARGE_FLAG_POR_RESET)
		RETURN_ERROR(rt9490_reset_chip(chgnum));
	if (mode & CHARGE_FLAG_RESET_TO_ZERO)
		RETURN_ERROR(rt9490_reset_to_zero(chgnum));
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_get_actual_current(int chgnum, int *current)
{
	uint16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_IBAT_ADC, &reg_val));
	*current = (int)reg_val * 1000;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_get_actual_voltage(int chgnum, int *voltage)
{
	uint16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_VBAT_ADC, &reg_val));
	*voltage = (int)reg_val * 1000;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_discharge_on_ac(int chgnum, int enable)
{
	return rt9490_enable_hz(chgnum, enable);
}

static enum ec_error_list rt9490_get_vbus_voltage(int chgnum, int port,
						  int *voltage)
{
	uint16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_VBUS_ADC, &reg_val));
	*voltage = (int)reg_val * 1000;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_input_current_limit(int chgnum,
							 int input_current)
{
	uint16_t reg_val = input_current / RT9490_AICR_STEP;

	return rt9490_write16(chgnum, RT9490_REG_AICR_CTRL, reg_val);
}

static enum ec_error_list rt9490_get_input_current_limit(int chgnum,
							 int *input_current)
{
	uint16_t val = 0;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_AICR_CTRL, &val));
	val = (val & RT9490_AICR_MASK) >> RT9490_AICR_SHIFT;
	val *= RT9490_AICR_STEP;
	*input_current = CLAMP(val, RT9490_AICR_MIN, RT9490_AICR_MAX);
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_get_input_current(int chgnum,
						   int *input_current)
{
	uint16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_IBUS_ADC, &reg_val));
	*input_current = (int)reg_val * 1000;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_device_id(int chgnum, int *id)
{
	RETURN_ERROR(rt9490_read8(chgnum, RT9490_REG_DEVICE_INFO, id));
	*id &= RT9490_DEVICE_INFO_MASK;
	return EC_SUCCESS;
}

#ifdef CONFIG_CHARGE_RAMP_HW
static enum ec_error_list rt9490_set_hw_ramp(int chgnum, int enable)
{
	if (enable) {
		RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_CTRL0,
					    RT9490_EN_AICC));
		RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_CTRL0,
					    RT9490_FORCE_AICC));
	} else {
		RETURN_ERROR(rt9490_clr_bit(chgnum, RT9490_REG_CHG_CTRL0,
					    RT9490_EN_AICC));
	}
	return EC_SUCCESS;
}

static int rt9490_ramp_is_stable(int chgnum)
{
	int rv;
	int val = 0;

	rv = rt9490_read8(chgnum, RT9490_REG_CHG_CTRL0, &val);
	return !rv && !(val & RT9490_FORCE_AICC);
}

static int rt9490_ramp_is_detected(int chgnum)
{
	return true;
}

static int rt9490_ramp_get_current_limit(int chgnum)
{
	int rv;
	int input_current = 0;

	rv = rt9490_get_input_current_limit(chgnum, &input_current);
	return rv ? -1 : input_current;
}
#endif

#ifdef CONFIG_CMD_CHARGER_DUMP
static void dump_range(int chgnum, int from, int to)
{
	for (int reg = from; reg <= to; ++reg) {
		int val = 0;

		if (!rt9490_read8(chgnum, reg, &val))
			CPRINTS("    0x%02x: 0x%02x", reg, val);
		else
			CPRINTS("    0x%02x: (error)", reg);
	}
}

static void rt9490_dump_registers(int chgnum)
{
	uint16_t ts, tdie;

	CPRINTS("CHG_STATUS:");
	dump_range(chgnum, RT9490_REG_CHG_STATUS0, RT9490_REG_CHG_STATUS4);
	CPRINTS("FAULT_STATUS:");
	dump_range(chgnum, RT9490_REG_FAULT_STATUS0, RT9490_REG_FAULT_STATUS1);
	CPRINTS("IRQ_FLAG:");
	dump_range(chgnum, RT9490_REG_CHG_IRQ_FLAG0, RT9490_REG_CHG_IRQ_FLAG5);

	rt9490_read16(chgnum, RT9490_REG_TS_ADC, &ts);
	CPRINTS("TS_ADC: %d.%d%%", ts / 10, ts % 10);
	rt9490_read16(chgnum, RT9490_REG_TDIE_ADC, &tdie);
	CPRINTS("TDIE_ADC: %d deg C", tdie);
}
#endif

const struct charger_drv rt9490_drv = {
	.init = &rt9490_init,
	.get_info = &rt9490_get_info,
	.get_status = &rt9490_get_status,
	.set_mode = &rt9490_set_mode,
#ifdef CONFIG_CHARGER_OTG
	.enable_otg_power = &rt9490_enable_otg_power,
	.set_otg_current_voltage = &rt9490_set_otg_current_voltage,
	.is_sourcing_otg_power = &rt9490_is_sourcing_otg_power,
#endif
	.get_current = &rt9490_get_current,
	.set_current = &rt9490_set_current,
	.get_voltage = &rt9490_get_voltage,
	.set_voltage = &rt9490_set_voltage,
	.get_actual_current = &rt9490_get_actual_current,
	.get_actual_voltage = &rt9490_get_actual_voltage,
	.discharge_on_ac = &rt9490_discharge_on_ac,
	.get_vbus_voltage = &rt9490_get_vbus_voltage,
	.set_input_current_limit = &rt9490_set_input_current_limit,
	.get_input_current_limit = &rt9490_get_input_current_limit,
	.get_input_current = &rt9490_get_input_current,
	.device_id = &rt9490_device_id,
#ifdef CONFIG_CHARGE_RAMP_HW
	.set_hw_ramp = &rt9490_set_hw_ramp,
	.ramp_is_stable = &rt9490_ramp_is_stable,
	.ramp_is_detected = &rt9490_ramp_is_detected,
	.ramp_get_current_limit = &rt9490_ramp_get_current_limit,
#endif
#ifdef CONFIG_CMD_CHARGER_DUMP
	.dump_registers = &rt9490_dump_registers,
#endif
};
