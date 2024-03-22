/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Richtek 5A 1-4 cell buck-boost switching battery charger driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "builtin/assert.h"
#include "builtin/endian.h"
#include "charge_manager.h"
#include "charger.h"
#include "common.h"
#include "config.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "rt9490.h"
#include "task.h"
#include "temp_sensor/temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "usb_charge.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ##args)
#define CPRINTS(format, args...) \
	cprints(CC_CHARGER, "%s " format, "RT9490", ##args)

/* Charger parameters */
#define CHARGER_NAME "rt9490"
#define CHARGE_V_MAX 18800
#define CHARGE_V_MIN 3000
#define CHARGE_V_STEP 10
#define CHARGE_I_MAX 5000

/* b/238980988
 * RT9490 can't measure the 50mA charge current precisely due to insufficient
 * ADC resolution, and faulty leads it into battery supply mode.
 * the final number would be expected between 100mA ~ 200mA.
 * Vendor has done the FT correlation and will revise the datasheet's
 * CHARGE_I_MIN value from 50mA to 150mA as the final solution.
 */
#define CHARGE_I_MIN 150
#define CHARGE_I_STEP 10
#define INPUT_I_MAX 3300
#define INPUT_I_MIN 100
#define INPUT_I_STEP 10

/* Charger parameters */
static const struct charger_info rt9490_charger_info = {
	.name = CHARGER_NAME,
	.voltage_max = CHARGE_V_MAX,
	.voltage_min = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max = CHARGE_I_MAX,
	.current_min = CHARGE_I_MIN,
	.current_step = CHARGE_I_STEP,
	.input_current_max = INPUT_I_MAX,
	.input_current_min = INPUT_I_MIN,
	.input_current_step = INPUT_I_STEP,
};

#ifndef CONFIG_ZEPHYR
const struct rt9490_init_setting rt9490_setting = {
	/* b/230442545#comment28
	 * With EOC-Force-CCM disabled, the real IEOC would be
	 * 30~50mA lower than expected, so move eoc_current one step up
	 */
	.eoc_current = 240,
	.mivr = 4000,
	.boost_voltage = 5050,
	.boost_current = 1500,
};
#endif

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
				chg_chips[chgnum].i2c_addr_flags, reg,
				&reg_val));

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
				 chg_chips[chgnum].i2c_addr_flags, reg, mask,
				 val);
}

static inline int rt9490_update8(int chgnum, int reg, int mask,
				 enum mask_update_action action)
{
	return i2c_update8(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags, reg, mask, action);
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
	const struct charger_info *const info = rt9490_get_info(chgnum);

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

	if (current == 0) {
		current = info->current_min;
		rt9490_clr_bit(chgnum, RT9490_REG_CHG_CTRL0, RT9490_EN_CHG);
	} else
		rt9490_set_bit(chgnum, RT9490_REG_CHG_CTRL0, RT9490_EN_CHG);

	if (!IN_RANGE(current, info->current_min, info->current_max))
		return EC_ERROR_PARAM2;
	reg_ichg = current / info->current_step;

	return rt9490_write16(chgnum, RT9490_REG_ICHG_CTRL, reg_ichg);
}

static enum ec_error_list rt9490_get_voltage(int chgnum, int *voltage)
{
	uint16_t val = 0;
	const struct charger_info *const info = rt9490_get_info(chgnum);

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

	if (!IN_RANGE(voltage, info->voltage_min, info->voltage_max))
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

	if (!IN_RANGE(output_current, RT9490_IOTG_MIN, RT9490_IOTG_MAX))
		return EC_ERROR_PARAM2;
	if (!IN_RANGE(output_voltage, RT9490_VOTG_MIN, RT9490_VOTG_MAX))
		return EC_ERROR_PARAM3;

	reg_cur = (output_current - RT9490_IOTG_MIN) / RT9490_IOTG_STEP + 3;
	reg_vol = (output_voltage - RT9490_VOTG_MIN) / RT9490_VOTG_STEP;
	RETURN_ERROR(rt9490_write8(chgnum, RT9490_REG_IOTG_REGU, reg_cur));

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

int rt9490_enable_adc(int chgnum, bool en)
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
		chgnum, rt9490_setting.boost_current,
		rt9490_setting.boost_voltage));
#endif
	/* Disable ILIM_HZ pin current limit */
	RETURN_ERROR(rt9490_clr_bit(chgnum, RT9490_REG_CHG_CTRL5,
				    RT9490_ILIM_HZ_EN));
	/* Disable BC 1.2 detection by default. It will be enabled on demand */
	RETURN_ERROR(rt9490_enable_chgdet_flow(chgnum, false));
	/* Disable WDT */
	RETURN_ERROR(rt9490_enable_wdt(chgnum, false));
	/* Disable battery thermal protection */
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_ADD_CTRL0,
				    RT9490_JEITA_COLD_HOT));
	/* Disable AUTO_AICR / AUTO_MIVR */
	RETURN_ERROR(rt9490_clr_bit(chgnum, RT9490_REG_ADD_CTRL0,
				    RT9490_AUTO_AICR | RT9490_AUTO_MIVR));
	RETURN_ERROR(rt9490_set_mivr(chgnum, rt9490_setting.mivr));
	RETURN_ERROR(rt9490_set_ieoc(chgnum, rt9490_setting.eoc_current));
	RETURN_ERROR(rt9490_set_iprec(chgnum, batt_info->precharge_current));
	RETURN_ERROR(rt9490_enable_adc(chgnum, true));
	RETURN_ERROR(rt9490_enable_jeita(chgnum, false));
	RETURN_ERROR(rt9490_field_update8(
		chgnum, RT9490_REG_CHG_CTRL1, RT9490_VAC_OVP_MASK,
		RT9490_VAC_OVP_26V << RT9490_VAC_OVP_SHIFT));

	/* Mask all interrupts except BC12 done */
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK0,
				    RT9490_CHG_IRQ_MASK0_ALL));
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK1,
				    RT9490_CHG_IRQ_MASK1_ALL &
					    ~RT9490_BC12_DONE_MASK));
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK2,
				    RT9490_CHG_IRQ_MASK2_ALL));
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK3,
				    RT9490_CHG_IRQ_MASK3_ALL));
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK4,
				    RT9490_CHG_IRQ_MASK4_ALL));
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_IRQ_MASK5,
				    RT9490_CHG_IRQ_MASK5_ALL));
	/* Reduce SW freq from 1.5MHz to 1MHz
	 * for 10% higher current rating b/215294785
	 */
	RETURN_ERROR(rt9490_enable_pwm_1mhz(CHARGER_SOLO, true));

	/* b/230442545#comment28
	 * Disable EOC-Force-CCM which would potentially
	 * cause Vsys drop problem for all silicon version(ES1~ES4)
	 */
	RETURN_ERROR(rt9490_set_bit(chgnum, RT9490_REG_CHG_CTRL2,
				    RT9490_DIS_EOC_FCCM));

	/* b/253568743#comment14 vsys workaround */
	RETURN_ERROR(rt9490_enable_hidden_mode(chgnum, true));
	rt9490_clr_bit(chgnum, RT9490_REG_HD_ADD_CTRL2,
		       RT9490_EN_FON_PP_BAT_TRACK);
	RETURN_ERROR(rt9490_enable_hidden_mode(chgnum, false));

	/* Disable non-standard TA detection */
	RETURN_ERROR(rt9490_clr_bit(chgnum, RT9490_REG_ADD_CTRL2,
				    RT9490_SPEC_TA_EN));

	return EC_SUCCESS;
}

int rt9490_enable_hidden_mode(int chgnum, bool en)
{
	if (en) {
		RETURN_ERROR(
			rt9490_write8(chgnum, RT9490_REG_TM_PAS_CODE1, 0x69));
		RETURN_ERROR(
			rt9490_write8(chgnum, RT9490_REG_TM_PAS_CODE2, 0x96));
	} else {
		RETURN_ERROR(rt9490_write8(chgnum, RT9490_REG_TM_PAS_CODE1, 0));
		RETURN_ERROR(rt9490_write8(chgnum, RT9490_REG_TM_PAS_CODE2, 0));
	}

	return EC_SUCCESS;
}

int rt9490_enable_pwm_1mhz(int chgnum, bool en)
{
	return rt9490_update8(chgnum, RT9490_REG_ADD_CTRL1, RT9490_PWM_1MHZ_EN,
			      en ? MASK_SET : MASK_CLR);
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
	*current = (int)reg_val;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_get_actual_voltage(int chgnum, int *voltage)
{
	uint16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_VBAT_ADC, &reg_val));
	*voltage = (int)reg_val;
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
	*voltage = (int)reg_val;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_input_current_limit(int chgnum,
							 int input_current)
{
	uint16_t reg_val;

	input_current = CLAMP(input_current, RT9490_AICR_MIN, RT9490_AICR_MAX);
	reg_val = input_current / RT9490_AICR_STEP;
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
	int16_t reg_val;

	RETURN_ERROR(rt9490_read16(chgnum, RT9490_REG_IBUS_ADC,
				   (uint16_t *)&reg_val));
	*input_current = reg_val;
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

static enum ec_error_list rt9490_get_option(int chgnum, int *option)
{
	/* Ignored: does not exist */
	*option = 0;
	return EC_SUCCESS;
}

static enum ec_error_list rt9490_set_option(int chgnum, int option)
{
	/* Ignored: does not exist */
	return EC_SUCCESS;
}

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
	.get_option = &rt9490_get_option,
	.set_option = &rt9490_set_option,
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

#ifdef CONFIG_USB_CHARGER
/* BC1.2 */
static int rt9490_get_bc12_ilim(enum charge_supplier supplier)
{
	switch (supplier) {
	case CHARGE_SUPPLIER_BC12_DCP:
	case CHARGE_SUPPLIER_BC12_CDP:
		return USB_CHARGER_MAX_CURR_MA;
	case CHARGE_SUPPLIER_BC12_SDP:
	default:
		return USB_CHARGER_MIN_CURR_MA;
	}
}

static enum charge_supplier rt9490_get_bc12_device_type(int chgnum)
{
	int reg, vbus_stat;

	if (rt9490_read8(chgnum, RT9490_REG_CHG_STATUS1, &reg))
		return CHARGE_SUPPLIER_NONE;

	vbus_stat = (reg & RT9490_VBUS_STAT_MASK) >> RT9490_VBUS_STAT_SHIFT;

	switch (vbus_stat) {
	case RT9490_SDP:
		CPRINTS("BC12 SDP");
		return CHARGE_SUPPLIER_BC12_SDP;
	case RT9490_CDP:
		CPRINTS("BC12 CDP");
		return CHARGE_SUPPLIER_BC12_CDP;
	case RT9490_DCP:
		CPRINTS("BC12 DCP");
		return CHARGE_SUPPLIER_BC12_DCP;
	default:
		CPRINTS("BC12 UNKNOWN 0x%02X", vbus_stat);
		return CHARGE_SUPPLIER_NONE;
	}
}

static void rt9490_update_charge_manager(int port,
					 enum charge_supplier new_bc12_type)
{
	static enum charge_supplier current_bc12_type = CHARGE_SUPPLIER_NONE;

	if (new_bc12_type != current_bc12_type) {
		if (current_bc12_type >= 0)
			charge_manager_update_charge(current_bc12_type, port,
						     NULL);

		if (new_bc12_type != CHARGE_SUPPLIER_NONE) {
			struct charge_port_info chg = {
				.current = rt9490_get_bc12_ilim(new_bc12_type),
				.voltage = USB_CHARGER_VOLTAGE_MV,
			};

			charge_manager_update_charge(new_bc12_type, port, &chg);
		}

		current_bc12_type = new_bc12_type;
	}
}

/* TODO: chgnum is not passed into the task, assuming only one charger */
#ifndef CONFIG_CHARGER_SINGLE_CHIP
#error rt9490 bc1.2 driver only works in single charger mode.
#endif

static void rt9490_usb_charger_task_init(const int port)
{
	rt9490_enable_chgdet_flow(CHARGER_SOLO, false);
}

static void rt9490_usb_charger_task_event(const int port, uint32_t evt)
{
	/*
	 * b/193753475#comment33: don't trigger bc1.2 detection after
	 * PRSwap/FRSwap.
	 *
	 * Note that the only scenario we want to catch is power role swap. For
	 * other cases, `is_non_pd_sink` may have false positive (e.g.
	 * pd_capable() is false during initial PD negotiation). But it's okay
	 * to always trigger bc1.2 detection for other cases.
	 */
	bool is_non_pd_sink = !pd_capable(port) &&
			      !usb_charger_port_is_sourcing_vbus(port) &&
			      pd_check_vbus_level(port, VBUS_PRESENT);

	/* vbus change, start bc12 detection */
	if (evt & USB_CHG_EVENT_VBUS) {
		if (is_non_pd_sink)
			rt9490_enable_chgdet_flow(CHARGER_SOLO, true);
		else
			rt9490_update_charge_manager(port,
						     CHARGE_SUPPLIER_NONE);
	}

	/* detection done, update charge_manager and stop detection */
	if (evt & USB_CHG_EVENT_BC12) {
		enum charge_supplier supplier;

		if (is_non_pd_sink)
			supplier = rt9490_get_bc12_device_type(CHARGER_SOLO);
		else
			supplier = CHARGE_SUPPLIER_NONE;

		rt9490_update_charge_manager(port, supplier);
		rt9490_enable_chgdet_flow(CHARGER_SOLO, false);
	}
}

static atomic_t pending_events;

void rt9490_deferred_interrupt(void)
{
	atomic_t current = atomic_clear(&pending_events);

	for (int port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port) {
		int ret, irq_flag;

		if (!(current & BIT(port)))
			continue;

		if (bc12_ports[port].drv != &rt9490_bc12_drv)
			continue;

		/* IRQ flag is read clear, no need to write back */
		ret = rt9490_read8(CHARGER_SOLO, RT9490_REG_CHG_IRQ_FLAG1,
				   &irq_flag);
		if (ret)
			return;

		if (irq_flag & RT9490_BC12_DONE_FLAG)
			usb_charger_task_set_event(port, USB_CHG_EVENT_BC12);
	}
}
DECLARE_DEFERRED(rt9490_deferred_interrupt);

void rt9490_interrupt(int port)
{
	atomic_or(&pending_events, BIT(port));
	hook_call_deferred(&rt9490_deferred_interrupt_data, 0);
}

const struct bc12_drv rt9490_bc12_drv = {
	.usb_charger_task_init = rt9490_usb_charger_task_init,
	.usb_charger_task_event = rt9490_usb_charger_task_event,
};

#ifdef CONFIG_BC12_SINGLE_DRIVER
/* provide a default bc12_ports[] for backward compatibility */
struct bc12_config bc12_ports[CHARGE_PORT_COUNT] = {
	[0 ... (CHARGE_PORT_COUNT - 1)] = {
		.drv = &rt9490_bc12_drv,
	},
};
#endif /* CONFIG_BC12_SINGLE_DRIVER */
#endif /* CONFIG_USB_CHARGER */

int rt9490_get_thermistor_val(const struct temp_sensor_t *sensor, int *temp_ptr)
{
	uint16_t mv;
	int idx = sensor->idx;
#if IS_ENABLED(CONFIG_ZEPHYR) && IS_ENABLED(CONFIG_TEMP_SENSOR)
	const struct thermistor_info *info = sensor->zephyr_info->thermistor;
#else
	const struct thermistor_info *info = &rt9490_thermistor_info;
#endif

	if (idx != 0)
		return EC_ERROR_PARAM1;
	RETURN_ERROR(rt9490_read16(idx, RT9490_REG_TS_ADC, &mv));
	*temp_ptr = thermistor_linear_interpolate(mv, info);
	*temp_ptr = C_TO_K(*temp_ptr);
	return EC_SUCCESS;
}
