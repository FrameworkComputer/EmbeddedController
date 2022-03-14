/* Copyright 2022 The Chromium OS Authors. All rights reserved.
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
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

/******************************************************************************/

static const char * const adp_id_names[] = {
	"unknown",
	"tiny",
	"tio1",
	"tio2",
};

/* ADP_ID control */
struct adpater_id_params tio1_power[] = {
	{
	.min_voltage = 3300,
	.max_voltage = 3300,
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
	.obp95 = 2820,
	.obp85 = 916,
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
	.min_voltage = 531,
	.max_voltage = 607,
	.charge_voltage = 20000,
	.charge_current = 6000,
	.watt = 120,
	.obp95 = 1990,
	.obp85 = 1780,
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
	.min_voltage = 1062,
	.max_voltage = 1126,
	.charge_voltage = 20000,
	.charge_current = 8500,
	.watt = 170,
	.obp95 = 2820,
	.obp85 = 916,
	},
	{
	.min_voltage = 2816,
	.max_voltage = 3300,
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
	.obp95 = 0x2D3,
	.obp85 = 0x286,
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
	.obp95 = 2820,
	.obp85 = 916,
	},
	{
	.min_voltage = 1749,
	.max_voltage = 1968,
	.charge_voltage = 20000,
	.charge_current = 11500,
	.watt = 230,
	.obp95 = 3810,
	.obp85 = 3410,
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
	CPRINTF("Adapter voltage over then 95%% trigger prochot.");
}

void obp_point_85(void)
{
	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 0);
	/* Enable the voltage high interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);

	/* Release the PROCHOT */
	gpio_set_level(GPIO_EC_PROCHOT_ODL, 1);
	CPRINTF("Adapter voltage less then 85%% release prochot.");
}

struct npcx_adc_thresh_t adc_obp_point_95 = {
	.adc_ch = ADC_PWR_IN_IMON,
	.adc_thresh_cb = obp_point_95,
	.thresh_assert = 3300,	/* Default */
};

struct npcx_adc_thresh_t adc_obp_point_85 = {
	.adc_ch = ADC_PWR_IN_IMON,
	.adc_thresh_cb = obp_point_85,
	.lower_or_higher = 1,
	.thresh_assert = 0,	/* Default */
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
 * Tiny: Twice adapter ADC values are less than 0x3FF.
 * TIO1: Twice adapter ADC values are 0x3FF.
 * TIO2: First adapter ADC value less than 0x3FF.
 *       Second adpater ADC value is 0x3FF.
 */
static void adp_id_deferred(void);
DECLARE_DEFERRED(adp_id_deferred);
void adp_id_deferred(void)
{
	struct charge_port_info pi = { 0 };
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
	} else if (adp_id_value_debounce == 0x3FF && adp_id_value == 0x3FF) {
		adp_finial_adc_value = adp_id_value;
		adp_type = TIO1;
	} else if (adp_id_value_debounce < 0x3FF && adp_id_value == 0x3FF) {
		adp_finial_adc_value = adp_id_value_debounce;
		adp_type = TIO2;
	} else if (adp_id_value_debounce < 0x3FF && adp_id_value < 0x3FF) {
		adp_finial_adc_value = adp_id_value;
		adp_type = TINY;
	} else {
		CPRINTS("ADP_ID mismatch anything!");
		/* Set the default shipping adaptor max ADC value */
		adp_finial_adc_value = 0x69;
		adp_type = TINY;
	}


	switch (adp_type) {
	case TIO1:
		power_type_len = sizeof(tio1_power) /
					sizeof(struct adpater_id_params);
		memcpy(&power_type, &tio1_power, sizeof(tio1_power));
		break;
	case TIO2:
		power_type_len = sizeof(tio2_power) /
					sizeof(struct adpater_id_params);
		memcpy(&power_type, &tio2_power, sizeof(tio2_power));
		break;
	case TINY:
		power_type_len = sizeof(tiny_power) /
					sizeof(struct adpater_id_params);
		memcpy(&power_type, &tiny_power, sizeof(tiny_power));
		break;
	}

	for (i = 0; (i < power_type_len) && adp_type; i++) {
		if (adp_finial_adc_value <= power_type[i].max_voltage) {
			adc_obp_point_95.thresh_assert = power_type[i].obp95;
			adc_obp_point_85.thresh_assert = power_type[i].obp85;
			pi.voltage = power_type[i].charge_voltage;
			pi.current = power_type[i].charge_current;
			set_up_adc_irqs();
			charge_manager_update_charge(CHARGE_SUPPLIER_DEDICATED,
						DEDICATED_CHARGE_PORT, &pi);
			CPRINTS("Power type %s, %dW", adp_id_names[adp_type],
				power_type[i].watt);
			break;
		}
	}
}

static void adp_id_init(void)
{
	/* Delay 220ms to get the first ADP_ID value */
	hook_call_deferred(&adp_id_deferred_data, 220 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, adp_id_init, HOOK_PRIO_DEFAULT);
