/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Common code for VARIANT_DEDEDE_NPCX796FC configuration */

#include "adc_chip.h"
#include "atomic.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "power.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "task.h"

/* ADC channels */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1] = {
		"TEMP_SENSOR1", NPCX_ADC_CH0, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},

	[ADC_TEMP_SENSOR_2] = {
		"TEMP_SENSOR2", NPCX_ADC_CH1, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},

	[ADC_SUB_ANALOG] = {
		"SUB_ANALOG", NPCX_ADC_CH2, ADC_MAX_VOLT, ADC_READ_MAX+1, 0},

	[ADC_VSNS_PP3300_A] = {
		"PP3300_A_PGOOD", NPCX_ADC_CH9, ADC_MAX_VOLT, ADC_READ_MAX+1,
		0},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

void pp3300_a_pgood_high(void)
{
	atomic_or(&pp3300_a_pgood, 1);

	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 0);
	/* Enable the voltage low interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 1);

	/*
	 * Call power_signal_interrupt() with a dummy GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

void pp3300_a_pgood_low(void)
{
	atomic_clear(&pp3300_a_pgood, 1);

	/* Disable this interrupt while it's asserted. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH2, 0);
	/* Enable the voltage high interrupt. */
	npcx_adc_thresh_int_enable(NPCX_ADC_THRESH1, 1);

	/*
	 * Call power_signal_interrupt() with a dummy GPIO in order for the
	 * chipset task to pick up the change in power sequencing signals.
	 */
	power_signal_interrupt(GPIO_PG_EC_DSW_PWROK);
}

const struct npcx_adc_thresh_t adc_pp3300_a_pgood_high = {
	.adc_ch = ADC_VSNS_PP3300_A,
	.adc_thresh_cb = pp3300_a_pgood_high,
	.thresh_assert = 2700,
	.thresh_deassert = -1,
};

const struct npcx_adc_thresh_t adc_pp3300_a_pgood_low = {
	.adc_ch = ADC_VSNS_PP3300_A,
	.adc_thresh_cb = pp3300_a_pgood_low,
	.lower_or_higher = 1,
	.thresh_assert = 600,
	.thresh_deassert = -1,
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

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	[PWM_CH_KBLIGHT] = {
		.channel = 3,
		.flags = PWM_CONFIG_DSLEEP,
		.freq = 10000,
	},

	[PWM_CH_LED1_AMBER] = {
		.channel = 2,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq = 2400,
	},

	[PWM_CH_LED2_WHITE] = {
		.channel = 0,
		.flags = PWM_CONFIG_DSLEEP | PWM_CONFIG_ACTIVE_LOW,
		.freq = 2400,
	}
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);
