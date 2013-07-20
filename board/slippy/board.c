/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Slippy board configuration */

#include "adc.h"
#include "backlight.h"
#include "chip_temp_sensor.h"
#include "chipset_haswell.h"
#include "chipset_x86_common.h"
#include "common.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "lm4_adc.h"
#include "peci.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "temp_sensor.h"
#include "temp_sensor_g781.h"
#include "timer.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[GPIO_COUNT] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTON_L",       LM4_GPIO_A, (1<<2), GPIO_INT_BOTH,
	 power_button_interrupt},
	{"LID_OPEN",             LM4_GPIO_A, (1<<3), GPIO_INT_BOTH,
	 lid_interrupt},
	{"AC_PRESENT",           LM4_GPIO_H, (1<<3), GPIO_INT_BOTH,
	 extpower_interrupt},
	{"PCH_BKLTEN",           LM4_GPIO_M, (1<<3), GPIO_INT_BOTH,
	 backlight_interrupt},
	{"PCH_SLP_S0_L",         LM4_GPIO_G, (1<<6), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PCH_SLP_S3_L",         LM4_GPIO_G, (1<<7), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PCH_SLP_S5_L",         LM4_GPIO_H, (1<<1), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PCH_SLP_SUS_L",        LM4_GPIO_G, (1<<3), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PP1050_PGOOD",         LM4_GPIO_H, (1<<4), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PP1350_PGOOD",         LM4_GPIO_H, (1<<6), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PP5000_PGOOD",         LM4_GPIO_N, (1<<0), GPIO_INT_BOTH,
	 x86_interrupt},
	{"VCORE_PGOOD",          LM4_GPIO_C, (1<<6), GPIO_INT_BOTH,
	 x86_interrupt},
	{"PCH_EDP_VDD_EN",       LM4_GPIO_J, (1<<1), GPIO_INT_BOTH,
	 haswell_interrupt},
	{"RECOVERY_L",           LM4_GPIO_A, (1<<5), GPIO_PULL_UP|GPIO_INT_BOTH,
	 switch_interrupt},
	{"WP_L",                 LM4_GPIO_A, (1<<4), GPIO_INT_BOTH,
	 switch_interrupt},

	/* Other inputs */
	{"FAN_ALERT_L",          LM4_GPIO_B, (1<<0), GPIO_INPUT, NULL},
	{"PCH_SUSWARN_L",        LM4_GPIO_G, (1<<2), GPIO_INT_BOTH, NULL},
	{"USB1_OC_L",            LM4_GPIO_E, (1<<7), GPIO_INPUT, NULL},
	{"USB2_OC_L",            LM4_GPIO_E, (1<<0), GPIO_INPUT, NULL},
	{"BOARD_VERSION1",       LM4_GPIO_Q, (1<<5), GPIO_INPUT, NULL},
	{"BOARD_VERSION2",       LM4_GPIO_Q, (1<<6), GPIO_INPUT, NULL},
	{"BOARD_VERSION3",       LM4_GPIO_Q, (1<<7), GPIO_INPUT, NULL},
	{"CPU_PGOOD",            LM4_GPIO_C, (1<<4), GPIO_INPUT, NULL},
	{"BAT_DETECT_L",         LM4_GPIO_B, (1<<4), GPIO_INPUT, NULL},

	/* Outputs; all unasserted by default except for reset signals */
	{"CPU_PROCHOT",          LM4_GPIO_B, (1<<1), GPIO_OUT_LOW, NULL},
	{"PP1350_EN",            LM4_GPIO_H, (1<<5), GPIO_OUT_LOW, NULL},
	{"PP3300_DSW_GATED_EN",  LM4_GPIO_J, (1<<3), GPIO_OUT_LOW, NULL},
	{"PP3300_DX_EN",         LM4_GPIO_J, (1<<2), GPIO_OUT_LOW, NULL},
	{"PP3300_LTE_EN",        LM4_GPIO_D, (1<<2), GPIO_OUT_LOW, NULL},
	{"PP3300_WLAN_EN",       LM4_GPIO_J, (1<<0), GPIO_OUT_LOW, NULL},
	{"SUSP_VR_EN",           LM4_GPIO_C, (1<<7), GPIO_OUT_LOW, NULL},
	{"VCORE_EN",             LM4_GPIO_C, (1<<5), GPIO_OUT_LOW, NULL},
	{"PP5000_EN",            LM4_GPIO_H, (1<<7), GPIO_OUT_LOW, NULL},
	{"SYS_PWROK",            LM4_GPIO_H, (1<<2), GPIO_OUT_LOW, NULL},
	{"WLAN_OFF_L",           LM4_GPIO_J, (1<<4), GPIO_OUT_LOW, NULL},
	{"CHARGE_L",             LM4_GPIO_E, (1<<6), GPIO_OUT_LOW, NULL},

	{"ENABLE_BACKLIGHT",     LM4_GPIO_M, (1<<7), GPIO_OUT_LOW, NULL},
	{"ENABLE_TOUCHPAD",      LM4_GPIO_N, (1<<1), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW",          LM4_GPIO_D, (1<<3), GPIO_OUT_LOW, NULL},
	{"PCH_DPWROK",           LM4_GPIO_G, (1<<0), GPIO_OUT_LOW, NULL},
	/*
	 * HDA_SDO is technically an output, but we need to leave it as an
	 * input until we drive it high.  So can't use open-drain (HI_Z).
	 */
	{"PCH_HDA_SDO",          LM4_GPIO_G, (1<<1), GPIO_INPUT, NULL},
	{"PCH_WAKE_L",           LM4_GPIO_F, (1<<0), GPIO_OUT_HIGH, NULL},
	{"PCH_NMI_L",            LM4_GPIO_F, (1<<2), GPIO_OUT_HIGH, NULL},
	{"PCH_PWRBTN_L",         LM4_GPIO_H, (1<<0), GPIO_OUT_HIGH, NULL},
	{"PCH_PWROK",            LM4_GPIO_F, (1<<5), GPIO_OUT_LOW, NULL},
	/* FIXME: Why does PL6 not honor open drain semantics? Setting it to 1
	 * drives the pin low while setting it to 0 drives the pin high. To
	 * work around this PCH_RCIN_L is set to an input. It will only
	 * be set to an output when it needs to be driven to 0. */
	{"PCH_RCIN_L",           LM4_GPIO_L, (1<<6), GPIO_INPUT, NULL},
	{"PCH_RSMRST_L",         LM4_GPIO_F, (1<<1), GPIO_OUT_LOW, NULL},
	{"PCH_SMI_L",            LM4_GPIO_F, (1<<4), GPIO_ODR_HIGH, NULL},
	{"TOUCHSCREEN_RESET_L",  LM4_GPIO_N, (1<<7), GPIO_OUT_LOW, NULL},
	{"EC_EDP_VDD_EN",        LM4_GPIO_J, (1<<5), GPIO_OUT_LOW, NULL},

	{"LPC_CLKRUN_L",         LM4_GPIO_M, (1<<2), GPIO_ODR_HIGH, NULL},
	{"USB1_ENABLE",          LM4_GPIO_E, (1<<4), GPIO_OUT_LOW, NULL},
	{"USB2_ENABLE",          LM4_GPIO_D, (1<<5), GPIO_OUT_LOW, NULL},

	{"PCH_SUSACK_L",         LM4_GPIO_F, (1<<3), GPIO_OUT_HIGH, NULL},
	{"PCH_RTCRST_L",         LM4_GPIO_F, (1<<6), GPIO_ODR_HIGH, NULL},
	{"PCH_SRTCRST_L",        LM4_GPIO_F, (1<<7), GPIO_ODR_HIGH, NULL},

	{"BAT_LED0_L",           LM4_GPIO_N, (1<<6), GPIO_ODR_HIGH, NULL},
	{"BAT_LED1_L",           LM4_GPIO_N, (1<<4), GPIO_ODR_HIGH, NULL},
};

/* x86 signal list.  Must match order of enum x86_signal. */
const struct x86_signal_info x86_signal_list[X86_SIGNAL_COUNT] = {
	{GPIO_PP5000_PGOOD,  1, "PGOOD_PP5000"},
	{GPIO_PP1350_PGOOD,  1, "PGOOD_PP1350"},
	{GPIO_PP1050_PGOOD,  1, "PGOOD_PP1050"},
	{GPIO_VCORE_PGOOD,   1, "PGOOD_VCORE"},
	{GPIO_PCH_SLP_S0_L,  1, "SLP_S0#_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,  1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,  1, "SLP_S5#_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L, 1, "SLP_SUS#_DEASSERTED"},
};

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[ADC_CH_COUNT] = {
	/* EC internal temperature is calculated by
	 * 273 + (295 - 450 * ADC_VALUE / ADC_READ_MAX) / 2
	 * = -225 * ADC_VALUE / ADC_READ_MAX + 420.5
	 */
	{"ECTemp", LM4_ADC_SEQ0, -225, ADC_READ_MAX, 420,
	 LM4_AIN_NONE, 0x0e /* TS0 | IE0 | END0 */, 0, 0},

	/* IOUT == ICMNT is on PE3/AIN0 */
	/* We have 0.01-ohm resistors, and IOUT is 20X the differential
	 * voltage, so 1000mA ==> 200mV.
	 * ADC returns 0x000-0xFFF, which maps to 0.0-3.3V (as configured).
	 * mA = 1000 * ADC_VALUE / ADC_READ_MAX * 3300 / 200
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 33000, ADC_READ_MAX * 2, 0,
	 LM4_AIN(0), 0x06 /* IE0 | END0 */, LM4_GPIO_E, (1<<3)},
};

/* I2C ports */
const struct i2c_port_t i2c_ports[I2C_PORTS_USED] = {
	/* Note: battery and charger share a port.  Only include it once in
	 * this list so we don't double-initialize it. */
	{"batt_chg", I2C_PORT_BATTERY,  100},
	{"thermal",  I2C_PORT_THERMAL,  100},
};


/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT] = {
#ifdef CONFIG_PECI
	{"PECI", TEMP_SENSOR_TYPE_CPU, peci_temp_sensor_get_val, 0, 2},
#endif
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
	{"G781Internal", TEMP_SENSOR_TYPE_BOARD, g781_get_val, 0, 4},
	{"G781External", TEMP_SENSOR_TYPE_BOARD, g781_get_val, 1, 4},
};

struct keyboard_scan_config keyscan_config = {
	.output_settle_us = 40,
	.debounce_down_us = 6 * MSEC,
	.debounce_up_us = 30 * MSEC,
	.scan_period_us = 1500,
	.min_post_scan_delay_us = 1000,
	.poll_timeout_us = SECOND,
	.actual_key_mask = {
		0x14, 0xff, 0xff, 0xff, 0xff, 0xf5, 0xff,
		0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xc8  /* full set */
	},
};

/**
 * Configure the GPIOs for the pwm module.
 */
void configure_fan_gpios(void)
{
	/* PN2:3 alternate function 1 = channel 0 PWM/tach */
	gpio_set_alternate_function(LM4_GPIO_N, 0x0c, 1);
}

/**
 * Perform necessary actions on host wake events.
 */
void board_process_wake_events(uint32_t active_wake_events)
{
	uint32_t power_button_mask;

	power_button_mask = EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON);

	/* If there are other events aside from the power button press drive
	 * the wake pin. Otherwise ensure it is high. */
	if (active_wake_events & ~power_button_mask)
		gpio_set_level(GPIO_PCH_WAKE_L, 0);
	else
		gpio_set_level(GPIO_PCH_WAKE_L, 1);
}

/**
 * Board-specific g781 power state.
 */
int board_g781_has_power(void)
{
	return gpio_get_level(GPIO_PP3300_DX_EN);
}

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	if (enable)
		gpio_set_level(GPIO_CHARGE_L, 1);
	else
		gpio_set_level(GPIO_CHARGE_L, 0);
	return EC_SUCCESS;
}
