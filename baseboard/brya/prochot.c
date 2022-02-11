/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charger.h"
#include "console.h"
#include "driver/charger/bq257x0_regs.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define ADT_RATING_W  (PD_MAX_POWER_MW / 1000)
#define PROCHOT_EVENT_200MS_TICK    TASK_EVENT_CUSTOM_BIT(0)

struct batt_para {
	int battery_continuous_discharge_mw;
	int battery_design_mWh;
	int flags;
	int state_of_charge;
};

struct batt_para batt_params;

static int cal_sys_watt(void)
{
	int adapter_voltage_v;
	int IDPM;
	int Vacpacn;
	int V_iadpt;
	int W_adpt;

	Vacpacn = adc_read_channel(ADC_IADPT);

	/* the ratio selectable through IADPT_GAIN bit. */
	V_iadpt = Vacpacn * 1000 / 40;

	IDPM = V_iadpt / CONFIG_CHARGER_BQ25710_SENSE_RESISTOR_AC;

	adapter_voltage_v = charge_manager_get_charger_voltage() / 1000;

	W_adpt = IDPM * adapter_voltage_v * PROCHOT_ADAPTER_WATT_RATIO / 100;

	return W_adpt;
}

static int get_batt_parameter(void)
{
	int battery_voltage;
	int battery_current;
	int battery_design_voltage_mv;
	int battery_design_capacity_mAh;
	int rv = 0;

	batt_params.flags = 0;

	/* read battery voltage */
	if (sb_read(SB_VOLTAGE, &battery_voltage))
		batt_params.flags |= BATT_FLAG_BAD_VOLTAGE;

	/* Battery_current sometimes return a very huge number
	 * and cause prochot keep toggling so add (int16_t) to guard it.
	 */
	if (sb_read(SB_CURRENT, &battery_current))
		batt_params.flags |= BATT_FLAG_BAD_CURRENT;
	else
		battery_current = (int16_t)battery_current;

	/* calculate battery wattage and convert to mW */
	batt_params.battery_continuous_discharge_mw =
		(battery_voltage * battery_current) / 1000;

	rv |= sb_read(SB_DESIGN_VOLTAGE, &battery_design_voltage_mv);
	rv |= sb_read(SB_DESIGN_CAPACITY, &battery_design_capacity_mAh);
	batt_params.battery_design_mWh = (battery_design_voltage_mv *
		battery_design_capacity_mAh) / 1000;

	if (sb_read(SB_RELATIVE_STATE_OF_CHARGE, &batt_params.state_of_charge))
		batt_params.flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	return (batt_params.flags || rv);
}

static int get_chg_watt(void)
{
	int adapter_current_ma;
	int adapter_voltage_mv;
	int adapter_wattage;

	/* Get adapter wattage */
	adapter_current_ma = charge_manager_get_charger_current();
	adapter_voltage_mv = charge_manager_get_charger_voltage();
	adapter_wattage = adapter_current_ma * adapter_voltage_mv / 1000 / 1000;

	return adapter_wattage;
}

static int set_register_charge_option(void)
{
	int reg;
	int rv;

	rv = i2c_read16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
		    BQ25710_REG_CHARGE_OPTION_0, &reg);
	if (rv == EC_SUCCESS) {
		reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, IADP_GAIN, 1, reg);
		/* if AC only, disable IDPM,
		 * because it will cause charger keep asserting PROCHOT
		 */
		if (!battery_hw_present())
			reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_IDPM, 0,
					   reg);
		else
			reg = SET_BQ_FIELD(BQ257X0, CHARGE_OPTION_0, EN_IDPM, 1,
					   reg);
	} else {
		CPRINTS("Failed to read bq25720");
		return rv;
	}

	return i2c_write16(I2C_PORT_CHARGER, BQ25710_SMBUS_ADDR1_FLAGS,
				BQ25710_REG_CHARGE_OPTION_0, reg);
}

static void assert_prochot(void)
{
	int adapter_wattage;
	int adpt_mw;
	int total_W;

	/* Set 0x12 bit4=1 */
	if (set_register_charge_option()) {
		CPRINTS("Failed to set bq25720");
		return;
	}

	/* Calculate actual system W */
	adpt_mw = cal_sys_watt();

	/* If any battery flag is set and no AC, skip this cycle and hope
	 * the next cycle succeeds
	 */
	if (get_batt_parameter() && !extpower_is_present())
		return;

	/* When battery is discharging, the battery current will be negative */
	if (batt_params.battery_continuous_discharge_mw < 0) {
		total_W = adpt_mw +
			ABS(batt_params.battery_continuous_discharge_mw);
	} else {
		/* we won't assert prochot when battery is charging. */
		total_W = adpt_mw;
	}
	total_W /= 1000;

	/* Get adapter wattage */
	adapter_wattage = get_chg_watt();

	/*
	 * no AC, don't assert PROCHOT.
	 * If AC exists, PROCHOT will only be asserted when the battery
	 * is physical present and the battery wattage is over 95% of
	 * the max continue discharge current of battery spec.
	 * When the battery wattage is lower than 85% of the max
	 * continue discharge current of battery spec, PROCHOT will be
	 * deasserted.
	 */
	if (!extpower_is_present()) {
		if (!battery_hw_present()) {
			gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		} else {
			batt_params.battery_continuous_discharge_mw =
			ABS(batt_params.battery_continuous_discharge_mw);
			if ((batt_params.battery_continuous_discharge_mw /
			 1000) > BATT_MAX_CONTINUE_DISCHARGE_WATT *
			 PROCHOT_ASSERTION_BATTERY_RATIO / 100)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if ((batt_params.battery_continuous_discharge_mw
			 / 1000) < BATT_MAX_CONTINUE_DISCHARGE_WATT *
			 PROCHOT_DEASSERTION_BATTERY_RATIO / 100)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		}
		return;
	}

	if (adapter_wattage >= ADT_RATING_W) {
		/* if adapter >= 60W */
		/* if no battery or battery < 10% */
		if (!battery_hw_present() ||
		batt_params.state_of_charge <= 10) {
			if (total_W > ADT_RATING_W *
			PROCHOT_ASSERTION_PD_RATIO / 100)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W <= ADT_RATING_W *
			PROCHOT_DEASSERTION_PD_RATIO / 100)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		} else {
			/* AC + battery */
			if (total_W > (ADT_RATING_W +
			BATT_MAX_CONTINUE_DISCHARGE_WATT))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W < (ADT_RATING_W +
			BATT_MAX_CONTINUE_DISCHARGE_WATT) *
			PROCHOT_DEASSERTION_PD_BATTERY_RATIO / 100)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		}
	} else {
		/* if adapter < 60W */
		/* if no battery or battery < 10% */
		if (!battery_hw_present() ||
		batt_params.state_of_charge <= 10) {
			if (total_W > (adapter_wattage *
			PROCHOT_ASSERTION_ADAPTER_RATIO / 100))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W <= (adapter_wattage *
			PROCHOT_DEASSERTION_ADAPTER_RATIO / 100))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		} else {
			/* AC + battery */
			if (total_W > (adapter_wattage +
				BATT_MAX_CONTINUE_DISCHARGE_WATT))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W < (adapter_wattage +
				(BATT_MAX_CONTINUE_DISCHARGE_WATT *
				PROCHOT_DEASSERTION_ADAPTER_BATT_RATIO / 100)))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		}
	}
}

/* Called by hook task every 200 ms */
static void control_prochot_tick(void)
{
	task_set_event(TASK_ID_PROCHOT, PROCHOT_EVENT_200MS_TICK);
}
DECLARE_HOOK(HOOK_TICK, control_prochot_tick, HOOK_PRIO_DEFAULT);

void prochot_task(void *u)
{
	uint32_t evt;

	while (1) {
		evt = task_wait_event(-1);

		if (evt & PROCHOT_EVENT_200MS_TICK)
			assert_prochot();
	}
}
