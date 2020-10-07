/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_DEDEDE_NPCX796FC configuration */

#include "adc_chip.h"
#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "registers.h"
#include "task.h"
#include "timer.h"

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPRINTS(format, args...) cprints(CC_SYSTEM, format, ## args)

void pp3300_a_pgood_high(void)
{
	deprecated_atomic_or(&pp3300_a_pgood, 1);

	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 0);
	/* Enable the voltage low interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);

	/*
	 * Call power_signal_interrupt() with a fake GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

void pp3300_a_pgood_low(void)
{
	deprecated_atomic_clear_bits(&pp3300_a_pgood, 1);

	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 0);
	/* Enable the voltage high interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);

	/*
	 * Call power_signal_interrupt() with a fake GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

const struct npcx_adc_thresh_t adc_pp3300_a_pgood_high = {
	.adc_ch = ADC_VSNS_PP3300_A,
	.adc_thresh_cb = pp3300_a_pgood_high,
	.thresh_assert = 2700,
};

const struct npcx_adc_thresh_t adc_pp3300_a_pgood_low = {
	.adc_ch = ADC_VSNS_PP3300_A,
	.adc_thresh_cb = pp3300_a_pgood_low,
	.lower_or_higher = 1,
	.thresh_assert = 600,
};

static void set_up_adc_irqs(void)
{
	/* Set interrupt thresholds for the ADC. */
	npcx_adc_register_thresh_irq(NPCX_ADC_THRESH1,
				     &adc_pp3300_a_pgood_high);
	npcx_adc_register_thresh_irq(NPCX_ADC_THRESH2, &adc_pp3300_a_pgood_low);
	npcx_set_adc_repetitive(adc_channels[ADC_VSNS_PP3300_A].input_ch, 1);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);
}
DECLARE_HOOK(HOOK_INIT, set_up_adc_irqs, HOOK_PRIO_INIT_ADC+1);

static void disable_adc_irqs_deferred(void)
{
	CPRINTS("%s", __func__);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 0);
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 0);
	npcx_set_adc_repetitive(adc_channels[ADC_VSNS_PP3300_A].input_ch, 0);

	/*
	 * If we're already in G3, PP3300_A is already off.  Since the ADC
	 * interrupts were already disabled, this data is stale.  Therefore,
	 * force the PGOOD value to 0 and have the chipset task re-evaluate.
	 * This should help prevent leakage.
	 */
	if (chipset_in_state(CHIPSET_STATE_HARD_OFF))
		pp3300_a_pgood = 0;
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}
DECLARE_DEFERRED(disable_adc_irqs_deferred);

/*
 * The ADC interrupts are only needed for booting up.  The assumption is that
 * the PP3300_A rail will not go down during runtime.  Therefore, we'll disable
 * the ADC interrupts shortly after booting up and also after shutting down.
 */
static void disable_adc_irqs(void)
{
	int delay = 200 * MSEC;

	/*
	 * The EC stays in S5 for about 10s after shutting before heading down
	 * to G3.  Therefore, we'll postpone disabling the ADC IRQs until after
	 * this occurs.
	 */
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		delay = 15 * SECOND;
	hook_call_deferred(&disable_adc_irqs_deferred_data, delay);
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, disable_adc_irqs, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESUME, disable_adc_irqs, HOOK_PRIO_DEFAULT);

/*
 * We only need the ADC interrupts functional when powering up.  Therefore, only
 * enable them from our wake sources.  These will be the power button, or lid
 * open.  Below is a summary of the ADC interrupt action per power state and
 * wake source.
 *
 * Powering up to S0: ADC interrupts will be disabled after ~200ms.
 * S0ix/S3: No action as ADC interrupts are already disabled if suspending.
 * Powering down to S5/G3: ADC interrupts will be disabled after ~15s.
 * Powering up from S5/G3: ADC interrupts will be enabled.  They will be
 *                         disabled ~200ms after passing thru S3.
 * Power button press: If the system is in S5/G3, ADC interrupts will be
 *                     enabled.
 * Lid open: ADC interrupts will be enabled.
 */
static void enable_adc_irqs(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		CPRINTS("%s", __func__);
		hook_call_deferred(&disable_adc_irqs_deferred_data, -1);
		npcx_set_adc_repetitive(adc_channels[ADC_VSNS_PP3300_A].input_ch,
					1);
		npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);
		npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, enable_adc_irqs, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, enable_adc_irqs, HOOK_PRIO_DEFAULT);

static void enable_adc_irqs_via_lid(void)
{
	if (lid_is_open())
		enable_adc_irqs();
}
DECLARE_HOOK(HOOK_LID_CHANGE, enable_adc_irqs_via_lid, HOOK_PRIO_DEFAULT);

/* I2C Ports */
const struct i2c_port_t i2c_ports[] = {
	{
		"eeprom", I2C_PORT_EEPROM, 1000, GPIO_EC_I2C_EEPROM_SCL,
		GPIO_EC_I2C_EEPROM_SDA
	},

	{
		"battery", I2C_PORT_BATTERY, 100, GPIO_EC_I2C_BATTERY_SCL,
		GPIO_EC_I2C_BATTERY_SDA
	},

	{
		"sensor", I2C_PORT_SENSOR, 400, GPIO_EC_I2C_SENSOR_SCL,
		GPIO_EC_I2C_SENSOR_SDA
	},

	{
		"usbc0", I2C_PORT_USB_C0, 1000, GPIO_EC_I2C_USB_C0_SCL,
		GPIO_EC_I2C_USB_C0_SDA
	},

	{
		"sub_usbc1", I2C_PORT_SUB_USB_C1, 1000,
		GPIO_EC_I2C_SUB_USB_C1_SCL, GPIO_EC_I2C_SUB_USB_C1_SDA
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

