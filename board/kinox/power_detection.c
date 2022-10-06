/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "power.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ##args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ##args)

/******************************************************************************/

static const char *const adp_id_names[] = {
	"unknown", "tiny", "tio1", "tio2", "typec",
};

/* ADP_ID control */
struct adpater_id_params tio1_power[] = {
	{
		.min_voltage = 2816,
		.max_voltage = 2816,
		.charge_voltage = 20000,
		.charge_current = 6000,
		.watt = 120,
		.obp95 = 1990,
		.obp85 = 1780,
	},
};

struct adpater_id_params tio2_power[] = {
	{
		.min_voltage = 0,
		.max_voltage = 68,
		.charge_voltage = 20000,
		.charge_current = 8500,
		.watt = 170,
		.obp95 = 2816,
		.obp85 = 2520,
	},
	{
		.min_voltage = 68,
		.max_voltage = 142,
		.charge_voltage = 20000,
		.charge_current = 2250,
		.watt = 45,
		.obp95 = 750,
		.obp85 = 670,
	},
	{
		.min_voltage = 200,
		.max_voltage = 288,
		.charge_voltage = 20000,
		.charge_current = 3250,
		.watt = 65,
		.obp95 = 1080,
		.obp85 = 960,
	},
	{
		.min_voltage = 384,
		.max_voltage = 480,
		.charge_voltage = 20000,
		.charge_current = 7500,
		.watt = 150,
		.obp95 = 2490,
		.obp85 = 2220,
	},
	{
		.min_voltage = 531,
		.max_voltage = 607,
		.charge_voltage = 20000,
		.charge_current = 6000,
		.watt = 120,
		.obp95 = 1990,
		.obp85 = 1780,
	},
	{
		.min_voltage = 1062,
		.max_voltage = 1126,
		.charge_voltage = 20000,
		.charge_current = 8500,
		.watt = 170,
		.obp95 = 2816,
		.obp85 = 2520,
	},
	{
		.min_voltage = 2816,
		.max_voltage = 2816,
		.charge_voltage = 20000,
		.charge_current = 6000,
		.watt = 120,
		.obp95 = 1990,
		.obp85 = 1780,
	},
};

struct adpater_id_params tiny_power[] = {
	{
		.min_voltage = 68,
		.max_voltage = 142,
		.charge_voltage = 20000,
		.charge_current = 2250,
		.watt = 45,
		.obp95 = 750,
		.obp85 = 670,
	},
	{
		.min_voltage = 200,
		.max_voltage = 288,
		.charge_voltage = 20000,
		.charge_current = 3250,
		.watt = 65,
		.obp95 = 1080,
		.obp85 = 960,
	},
	{
		.min_voltage = 384,
		.max_voltage = 480,
		.charge_voltage = 20000,
		.charge_current = 4500,
		.watt = 90,
		.obp95 = 1490,
		.obp85 = 1330,
	},
	{
		.min_voltage = 531,
		.max_voltage = 607,
		.charge_voltage = 20000,
		.charge_current = 6000,
		.watt = 120,
		.obp95 = 1990,
		.obp85 = 1780,
	},
	{
		.min_voltage = 653,
		.max_voltage = 783,
		.charge_voltage = 20000,
		.charge_current = 6750,
		.watt = 135,
		.obp95 = 2240,
		.obp85 = 2000,
	},
	{
		.min_voltage = 851,
		.max_voltage = 997,
		.charge_voltage = 20000,
		.charge_current = 7500,
		.watt = 150,
		.obp95 = 2490,
		.obp85 = 2220,
	},
	{
		.min_voltage = 1063,
		.max_voltage = 1226,
		.charge_voltage = 20000,
		.charge_current = 8500,
		.watt = 170,
		.obp95 = 2816,
		.obp85 = 2520,
	},
	{
		.min_voltage = 1749,
		.max_voltage = 1968,
		.charge_voltage = 20000,
		.charge_current = 11500,
		.watt = 230,
		.obp95 = 2816,
		.obp85 = 2815,
	},
};

struct adpater_id_params typec_power[] = {
	{
		.charge_voltage = 20000,
		.charge_current = 1500,
		.watt = 30,
		.obp95 = 500,
		.obp85 = 440,
	},
	{
		.charge_voltage = 15000,
		.charge_current = 2000,
		.watt = 30,
		.obp95 = 660,
		.obp85 = 590,
	},
	{
		.charge_voltage = 20000,
		.charge_current = 2250,
		.watt = 45,
		.obp95 = 750,
		.obp85 = 670,
	},
	{
		.charge_voltage = 15000,
		.charge_current = 3000,
		.watt = 45,
		.obp95 = 990,
		.obp85 = 890,
	},
	{
		.charge_voltage = 20000,
		.charge_current = 3250,
		.watt = 65,
		.obp95 = 1080,
		.obp85 = 960,
	},
	{
		.charge_voltage = 20000,
		.charge_current = 5000,
		.watt = 100,
		.obp95 = 1660,
		.obp85 = 1480,
	},
};

struct adpater_id_params power_type[8];
static int adp_id_value_debounce;

void obp_point_95(void)
{
	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 0);
	/* Enable the voltage low interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);

	/* Trigger the PROCHOT */
	gpio_set_level(GPIO_EC_PROCHOT_ODL, 0);
	CPRINTS("Adapter voltage over 95%% trigger prochot.");
}

void obp_point_85(void)
{
	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 0);
	/* Enable the voltage high interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);

	/* Release the PROCHOT */
	gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
	CPRINTS("Adapter voltage lower than 85%% release prochot.");
}

struct npcx_adc_thresh_t adc_obp_point_95 = {
	.adc_ch = ADC_PWR_IN_IMON,
	.adc_thresh_cb = obp_point_95,
	.thresh_assert = 3300, /* Default */
};

struct npcx_adc_thresh_t adc_obp_point_85 = {
	.adc_ch = ADC_PWR_IN_IMON,
	.adc_thresh_cb = obp_point_85,
	.lower_or_higher = 1,
	.thresh_assert = 0, /* Default */
};

static void set_up_adc_irqs(void)
{
	/* Set interrupt thresholds for the ADC. */
	CPRINTS("%s", __func__);
	npcx_adc_register_thresh_irq(NPCX_ADC_THRESH1, &adc_obp_point_95);
	npcx_adc_register_thresh_irq(NPCX_ADC_THRESH2, &adc_obp_point_85);
	npcx_set_adc_repetitive(adc_channels[ADC_PWR_IN_IMON].input_ch, 1);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);
}

void set_the_obp(int power_type_index, int adp_type)
{
	struct charge_port_info pi = { 0 };

	adc_obp_point_95.thresh_assert = power_type[power_type_index].obp95;
	adc_obp_point_85.thresh_assert = power_type[power_type_index].obp85;
	set_up_adc_irqs();
	if (adp_type != TYPEC) {
		/* Only the TIO and Tiny need to update */
		pi.voltage = power_type[power_type_index].charge_voltage;
		pi.current = power_type[power_type_index].charge_current;

		switch (adp_type) {
		case TIO1:
		case TIO2:
			gpio_set_level(GPIO_SIO_LEGO_EN_L, 0);
			charge_manager_update_charge(
				CHARGE_SUPPLIER_PROPRIETARY,
				DEDICATED_CHARGE_PORT, &pi);
			break;
		case TINY:
			gpio_set_level(GPIO_SIO_LEGO_EN_L, 1);
			charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
						     DEDICATED_CHARGE_PORT,
						     &pi);
			break;
		}
	}

	CPRINTS("Power type %s, %dW", adp_id_names[adp_type],
		power_type[power_type_index].watt);
}

/*
 *       Scalar change to   Scalar change to
 *      downgrade voltage    3.3V voltage
 *               |                |
 *               |   SIO collect  |   SIO collect
 *               |   1st adapter  |   2nd adapter
 *               |   information  |   information
 *               |   |  |  |  |   |   |  |  |  |
 * -------------------------------------------------------
 *  |            |                |
 *  |---220 ms---|-----400 ms-----|
 *
 * Tiny: Twice adapter ADC values are less than 2.816v.
 * TIO1: Twice adapter ADC values are 2.816v.
 * TIO2: First adapter ADC value less than 2.816v.
 *       Second adpater ADC value is 2.816v.
 */
static void adp_id_deferred(void);
DECLARE_DEFERRED(adp_id_deferred);
void adp_id_deferred(void)
{
	int i = 0;
	int adp_type = 0;
	int adp_id_value;
	int adp_finial_adc_value;
	int power_type_len = 0;

	adp_id_value = adc_read_channel(ADC_ADP_ID);

	if (!adp_id_value_debounce) {
		adp_id_value_debounce = adp_id_value;
		/* for delay the 400ms to get the next APD_ID value */
		hook_call_deferred(&adp_id_deferred_data, 400 * MSEC);
	} else if (adp_id_value_debounce == ADC_MAX_VOLT &&
		   adp_id_value == ADC_MAX_VOLT) {
		adp_finial_adc_value = adp_id_value;
		adp_type = TIO1;
	} else if (adp_id_value_debounce < ADC_MAX_VOLT &&
		   adp_id_value == ADC_MAX_VOLT) {
		adp_finial_adc_value = adp_id_value_debounce;
		adp_type = TIO2;
	} else if (adp_id_value_debounce < ADC_MAX_VOLT &&
		   adp_id_value < ADC_MAX_VOLT) {
		adp_finial_adc_value = adp_id_value;
		adp_type = TINY;
	} else {
		CPRINTS("ADP_ID mismatch anything!");
		/* Set the default TINY 45w adapter */
		adp_finial_adc_value = 142;
		adp_type = TINY;
	}

	switch (adp_type) {
	case TIO1:
		power_type_len =
			sizeof(tio1_power) / sizeof(struct adpater_id_params);
		memcpy(&power_type, &tio1_power, sizeof(tio1_power));
		break;
	case TIO2:
		power_type_len =
			sizeof(tio2_power) / sizeof(struct adpater_id_params);
		memcpy(&power_type, &tio2_power, sizeof(tio2_power));
		break;
	case TINY:
		power_type_len =
			sizeof(tiny_power) / sizeof(struct adpater_id_params);
		memcpy(&power_type, &tiny_power, sizeof(tiny_power));
		break;
	}

	for (i = 0; (i < power_type_len) && adp_type; i++) {
		if (adp_finial_adc_value <= power_type[i].max_voltage) {
			set_the_obp(i, adp_type);
			break;
		}
	}
}

static void barrel_jack_setting(void)
{
	struct charge_port_info pi = { 0 };
	/* Check ADP_ID when barrel jack is present */
	if (!gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL)) {
		/* Set the default TINY 45w adapter */
		pi.voltage = 20000;
		pi.current = 2250;

		charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
					     DEDICATED_CHARGE_PORT, &pi);

		/* Delay 220ms to get the first ADP_ID value */
		hook_call_deferred(&adp_id_deferred_data, 220 * MSEC);
	}
}
DECLARE_HOOK(HOOK_INIT, barrel_jack_setting, HOOK_PRIO_DEFAULT);

static void typec_adapter_setting(void)
{
	int i = 0;
	int adp_type = TYPEC;
	int adapter_current_ma;
	int power_type_len;

	/* Check the barrel jack is not present */
	if (gpio_get_level(GPIO_BJ_ADP_PRESENT_ODL)) {
		adapter_current_ma = charge_manager_get_charger_current();
		power_type_len =
			sizeof(typec_power) / sizeof(struct adpater_id_params);

		memcpy(&power_type, &typec_power, sizeof(typec_power));
		for (i = (power_type_len - 1); i >= 0; i--) {
			if (adapter_current_ma >=
			    power_type[i].charge_current) {
				set_the_obp(i, adp_type);
				break;
			}
		}
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, typec_adapter_setting, HOOK_PRIO_DEFAULT);

/* IRQ for BJ plug/unplug. It shouldn't be called if BJ is the power source. */
void adp_connect_interrupt(enum gpio_signal signal)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		hook_call_deferred(&adp_id_deferred_data, 0);
}
