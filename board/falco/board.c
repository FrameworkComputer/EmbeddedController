/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Falco board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "board.h"
#include "charger.h"
#include "common.h"
#include "driver/temp_sensor/g781.h"
#include "extpower.h"
#include "fan.h"
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"
#include "jtag.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "peci.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "switch.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTON_L",       LM4_GPIO_A, (1<<2), GPIO_INT_BOTH_DSLEEP,
	 power_button_interrupt},
	{"LID_OPEN",             LM4_GPIO_A, (1<<3), GPIO_INT_BOTH_DSLEEP,
	 lid_interrupt},
	{"AC_PRESENT",           LM4_GPIO_H, (1<<3), GPIO_INT_BOTH_DSLEEP,
	 extpower_interrupt},
	{"PCH_BKLTEN",           LM4_GPIO_M, (1<<3), GPIO_INT_BOTH,
	 backlight_interrupt},
	{"PCH_SLP_S0_L",         LM4_GPIO_G, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_SLP_S3_L",         LM4_GPIO_G, (1<<7), GPIO_INT_BOTH_DSLEEP,
	 power_signal_interrupt},
	{"PCH_SLP_S5_L",         LM4_GPIO_H, (1<<1), GPIO_INT_BOTH_DSLEEP,
	 power_signal_interrupt},
	{"PCH_SLP_SUS_L",        LM4_GPIO_G, (1<<3), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP1050_PGOOD",         LM4_GPIO_H, (1<<4), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP1350_PGOOD",         LM4_GPIO_H, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP5000_PGOOD",         LM4_GPIO_N, (1<<0), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"VCORE_PGOOD",          LM4_GPIO_C, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_EDP_VDD_EN",       LM4_GPIO_J, (1<<1), GPIO_INT_BOTH,
	 lcdvcc_interrupt},
	{"RECOVERY_L",           LM4_GPIO_A, (1<<5), GPIO_PULL_UP|GPIO_INT_BOTH,
	 switch_interrupt},
	{"WP_L",                 LM4_GPIO_A, (1<<4), GPIO_INT_BOTH,
	 switch_interrupt},
	{"JTAG_TCK",             LM4_GPIO_C, (1<<0), GPIO_DEFAULT,
	 jtag_interrupt},
	{"UART0_RX",             LM4_GPIO_A, (1<<0), GPIO_PULL_UP|
							GPIO_INT_BOTH_DSLEEP,
	 uart_deepsleep_interrupt},

	/* Other inputs */
	{"FAN_ALERT_L",          LM4_GPIO_B, (1<<0), GPIO_INPUT, NULL},
	{"PCH_SUSWARN_L",        LM4_GPIO_G, (1<<2), GPIO_INT_BOTH, NULL},
	{"USB1_OC_L",            LM4_GPIO_E, (1<<7), GPIO_INPUT, NULL},
	{"USB2_OC_L",            LM4_GPIO_E, (1<<0), GPIO_INPUT, NULL},
	{"BOARD_VERSION1",       LM4_GPIO_Q, (1<<5), GPIO_INPUT, NULL},
	{"BOARD_VERSION2",       LM4_GPIO_Q, (1<<6), GPIO_INPUT, NULL},
	{"BOARD_VERSION3",       LM4_GPIO_Q, (1<<7), GPIO_INPUT, NULL},
	{"CPU_PGOOD",            LM4_GPIO_C, (1<<4), GPIO_INPUT, NULL},

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
	/*
	 * PL6 is one of 4 pins on the EC which can't be used in open-drain
	 * mode.  To work around this PCH_RCIN_L is set to an input. It will
	 * only be set to an output when it needs to be driven to 0.
	 */
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

	{"PWR_LED_L",            LM4_GPIO_N, (1<<6), GPIO_OUT_HIGH, NULL},
	{"KB_LED_EN",            LM4_GPIO_D, (1<<4), GPIO_OUT_LOW, NULL},
	{"BAT_LED0",             LM4_GPIO_D, (1<<0), GPIO_OUT_LOW, NULL},
	{"BAT_LED1",             LM4_GPIO_D, (1<<1), GPIO_OUT_LOW, NULL},
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0x03, 1, MODULE_UART, GPIO_PULL_UP},	/* UART0 */
	{GPIO_A, 0x40, 3, MODULE_I2C},			/* I2C1 SCL */
	{GPIO_A, 0x80, 3, MODULE_I2C, GPIO_OPEN_DRAIN},	/* I2C1 SDA */
	{GPIO_B, 0x04, 3, MODULE_I2C},			/* I2C0 SCL */
	{GPIO_B, 0x08, 3, MODULE_I2C, GPIO_OPEN_DRAIN},	/* I2C0 SDA */
	{GPIO_B, 0x40, 3, MODULE_I2C},			/* I2C5 SCL */
	{GPIO_B, 0x80, 3, MODULE_I2C, GPIO_OPEN_DRAIN},	/* I2C5 SDA */
	{GPIO_G, 0x30, 1, MODULE_UART},			/* UART2 */
	{GPIO_J, 0x40, 1, MODULE_PECI},			/* PECI Tx */
	{GPIO_J, 0x80, 0, MODULE_PECI, GPIO_ANALOG},	/* PECI Rx */
	{GPIO_L, 0x3f, 15, MODULE_LPC},			/* LPC */
	{GPIO_M, 0x33, 15, MODULE_LPC},			/* LPC */
	{GPIO_N, 0x0c, 1, MODULE_PWM_FAN},		/* FAN0PWM2 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP5000_PGOOD,  1, "PGOOD_PP5000"},
	{GPIO_PP1350_PGOOD,  1, "PGOOD_PP1350"},
	{GPIO_PP1050_PGOOD,  1, "PGOOD_PP1050"},
	{GPIO_VCORE_PGOOD,   1, "PGOOD_VCORE"},
	{GPIO_PCH_SLP_S0_L,  1, "SLP_S0#_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,  1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,  1, "SLP_S5#_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L, 1, "SLP_SUS#_DEASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

/* ADC channels. Must be in the exactly same order as in enum adc_channel. */
const struct adc_t adc_channels[] = {
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

	/* AC Adapter ID voltage (mv) */
	{"AdapterIDVoltage", LM4_ADC_SEQ2, 3300, ADC_READ_MAX, 0,
	 LM4_AIN(11), 0x06 /* IE0 | END0 */, LM4_GPIO_B, (1<<5)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1000,
	 .rpm_max = 5050,
	 .ch = 2,
	 .pgood_gpio = GPIO_PP5000_PGOOD,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", 0, 100},
	{"lvds",     1, 100},
	{"thermal",  5, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[] = {
	{"PECI", TEMP_SENSOR_TYPE_CPU, peci_temp_sensor_get_val, 0, 2},
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
	{"G781Internal", TEMP_SENSOR_TYPE_BOARD, g781_get_val,
		G781_IDX_INTERNAL, 4},
	{"G781External", TEMP_SENSOR_TYPE_BOARD, g781_get_val,
		G781_IDX_EXTERNAL, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* Only the AP affects the thermal limits and fan speed. */
	{ {C_TO_K(95), C_TO_K(97), C_TO_K(99)}, C_TO_K(55), C_TO_K(85)},
	{ {0, 0, 0}, 0, 0},
	{ {0, 0, 0}, 0, 0},
	{ {0, 0, 0}, 0, 0},
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

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
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

/*
 * Take a nice smooth ramp and make it all chunky.
 * And never turn it off. Bah. That'll do wonders for battery life.
 */
#ifdef CONFIG_FAN_RPM_CUSTOM
int fan_percent_to_rpm(int fan, int pct)
{
	const int FAN_MAX = fans[fan].rpm_max;
	const int FAN_MIN = fans[fan].rpm_min;
	const int NUM_STEPS = 7;
	const int m = 100 * 100 / NUM_STEPS;
	const int m0 = m / 200;

	int chunky = 100 * (pct + m0) / m;
	return FAN_MIN + (FAN_MAX - FAN_MIN) * m * chunky / 10000;
}
#endif	/* CONFIG_FAN_RPM_CUSTOM */
