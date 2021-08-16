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
#include "driver/charger/bq25710.h"
#include "hooks.h"
#include "i2c.h"
#include "math_util.h"
#include "task.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define ADT_RATING_W  (PD_MAX_POWER_MW / 1000)
#define BATT_MAX_CONTINUE_DISCHARGE_WATT    66
#define PROCHOT_EVENT_200MS_TICK    TASK_EVENT_CUSTOM_BIT(0)

struct batt_para {
	int battery_continuous_discharge_mw;
	int battery_design_mWh;
	int flags;
	int state_of_charge;
};

static struct batt_para batt_params;

static int cal_sys_watt(void)
{
	int Vacpacn;
	int V_iadpt;
	int IDPM;
	int W_adpt;

	/* Read ADC_IADPT from BQ25720 */
	V_iadpt = adc_read_channel(ADC_IADPT);

	/* Calculate V(ACP-ACN)
	 * We select IADPT_FAIN as 40 for more precise
	 */
	Vacpacn = V_iadpt * 1000 / 40;

	/* Calculate the input current */
	IDPM = Vacpacn / CONFIG_CHARGER_SENSE_RESISTOR_AC;

	/* Current multiplied by 20v to calculate actual adapter wattage */
	W_adpt = IDPM * 20 / 97 * 100;

	return W_adpt;
}

static int get_batt_parameter(void)
{
	int battery_voltage_mv;
	int battery_current_ma;
	int battery_design_voltage_mv;
	int battery_design_capacity_mAh;
	int rv = 0;

	batt_params.flags = 0;

	if (sb_read(SB_VOLTAGE, &battery_voltage_mv))
		batt_params.flags |= BATT_FLAG_BAD_VOLTAGE;

	/* Battery_current sometimes return a very huge number
	 * and cause prochot keep toggling so add (int16_t) to guard it.
	 */
	if (sb_read(SB_CURRENT, &battery_current_ma))
		batt_params.flags |= BATT_FLAG_BAD_CURRENT;
	else
		battery_current_ma = (int16_t)battery_current_ma;

	/* calculate battery wattage and convert to mW */
	batt_params.battery_continuous_discharge_mw =
		(battery_voltage_mv * battery_current_ma) / 1000;

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

	adapter_current_ma = charge_manager_get_charger_current();
	adapter_voltage_mv = charge_manager_get_charger_voltage();
	adapter_wattage = adapter_current_ma * adapter_voltage_mv / 1000 / 1000;

	return adapter_wattage;
}

static void assert_prochot(void)
{
	int adapter_wattage;
	int adpt_mw;
	int reg;
	int total_W;

	/* no AC, don't assert PROCHOT */
	if (!extpower_is_present()) {
		gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		return;
	}

	/* Set 0x12 bit4=1 */
	if (charger_get_option(&reg))
		CPRINTS("Failed to read bq25720");
	else {
		/* only execute if get_option succeeded. */
		reg |= BQ25710_CHARGE_OPTION_0_IADP_GAIN;
		if (charger_set_option(reg))
			return;
	}

	/* Calculate actual system W */
	adpt_mw = cal_sys_watt();

	/* Read battery info
	 * if any flag is set, skip this cycle and hope
	 * the next cycle succeeds
	 */
	if (get_batt_parameter())
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

	if (adapter_wattage < ADT_RATING_W) {
		/* if adapter < 60W */
		/* if no battery or battery < 10% */
		if (!battery_hw_present() ||
			batt_params.state_of_charge <= 10) {
			if (total_W > (adapter_wattage * 105/100))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W < (adapter_wattage * 90/100))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		} else {
			/* AC + battery */
			if (total_W > (adapter_wattage +
				BATT_MAX_CONTINUE_DISCHARGE_WATT))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W < (adapter_wattage +
				(BATT_MAX_CONTINUE_DISCHARGE_WATT *
					90/100)))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		}
	} else {
		/* if adapter = 60W */
		/* if no battery or battery < 10% */
		if (!battery_hw_present() ||
			batt_params.state_of_charge <= 10) {
			if (total_W > (ADT_RATING_W * 105/100))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W <= ADT_RATING_W)
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
		} else {
			/* AC + battery */
			if (total_W > (ADT_RATING_W +
				BATT_MAX_CONTINUE_DISCHARGE_WATT))
				gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
			else if (total_W < (ADT_RATING_W +
				BATT_MAX_CONTINUE_DISCHARGE_WATT) * 95/100)
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
