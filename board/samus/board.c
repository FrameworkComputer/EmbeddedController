/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Samus board configuration */

#include "als.h"
#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "common.h"
#include "driver/temp_sensor/tmp006.h"
#include "driver/als_isl29035.h"
#include "extpower.h"
#include "fan.h"
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "peci.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "timer.h"
#include "thermal.h"
#include "util.h"

/* GPIO signal list.  Must match order from enum gpio_signal. */
const struct gpio_info gpio_list[] = {
	/* Inputs with interrupt handlers are first for efficiency */
	{"POWER_BUTTON_L",       LM4_GPIO_A, (1<<2), GPIO_INT_BOTH,
	 power_button_interrupt},
	{"LID_OPEN",             LM4_GPIO_A, (1<<3), GPIO_INT_BOTH,
	 lid_interrupt},
	{"AC_PRESENT",           LM4_GPIO_H, (1<<3), GPIO_INT_BOTH,
	 extpower_interrupt},
	{"PCH_SLP_S0_L",         LM4_GPIO_G, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_SLP_S3_L",         LM4_GPIO_G, (1<<7), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_SLP_S5_L",         LM4_GPIO_H, (1<<1), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_SLP_SUS_L",        LM4_GPIO_G, (1<<3), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PCH_SUSWARN_L",        LM4_GPIO_G, (1<<2), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP1050_PGOOD",         LM4_GPIO_H, (1<<4), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP1200_PGOOD",         LM4_GPIO_H, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"PP1800_PGOOD",         LM4_GPIO_L, (1<<7), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"VCORE_PGOOD",          LM4_GPIO_C, (1<<6), GPIO_INT_BOTH,
	 power_signal_interrupt},
	{"RECOVERY_L",           LM4_GPIO_A, (1<<5), GPIO_PULL_UP|GPIO_INT_BOTH,
	 switch_interrupt},
	{"WP_L",                 LM4_GPIO_A, (1<<4), GPIO_INT_BOTH,
	 switch_interrupt},
	{"PCH_BL_EN",            LM4_GPIO_M, (1<<3), GPIO_INT_RISING,
	 backlight_interrupt},

	/* Other inputs */
	{"BOARD_VERSION1",       LM4_GPIO_Q, (1<<5), GPIO_INPUT, NULL},
	{"BOARD_VERSION2",       LM4_GPIO_Q, (1<<6), GPIO_INPUT, NULL},
	{"BOARD_VERSION3",       LM4_GPIO_Q, (1<<7), GPIO_INPUT, NULL},
	{"CPU_PGOOD",            LM4_GPIO_C, (1<<4), GPIO_INPUT, NULL},
	{"ONEWIRE",              LM4_GPIO_F, (1<<7), GPIO_INPUT, NULL},
	{"THERMAL_DATA_READY_L", LM4_GPIO_B, (1<<0), GPIO_INPUT, NULL},
	{"USB1_OC_L",            LM4_GPIO_E, (1<<7), GPIO_INPUT, NULL},
	{"USB1_STATUS_L",        LM4_GPIO_E, (1<<6), GPIO_INPUT, NULL},
	{"USB2_OC_L",            LM4_GPIO_E, (1<<0), GPIO_INPUT, NULL},
	{"USB2_STATUS_L",        LM4_GPIO_D, (1<<7), GPIO_INPUT, NULL},
	/* Not yet sure if this will need to be handled as an interrupt */
	{"CAPSENSE_INT_L",       LM4_GPIO_N, (1<<0), GPIO_INPUT, NULL},

	/* Outputs; all unasserted by default except for reset signals */
	{"CPU_PROCHOT",          LM4_GPIO_B, (1<<1), GPIO_OUT_LOW, NULL},
	{"PP1200_EN",            LM4_GPIO_H, (1<<5), GPIO_OUT_LOW, NULL},
	{"PP3300_DSW_EN",        LM4_GPIO_F, (1<<6), GPIO_OUT_LOW, NULL},
	{"PP3300_DSW_GATED_EN",  LM4_GPIO_J, (1<<3), GPIO_OUT_LOW, NULL},
	{"PP3300_LTE_EN",        LM4_GPIO_D, (1<<2), GPIO_OUT_LOW, NULL},
	{"PP3300_WLAN_EN",       LM4_GPIO_J, (1<<0), GPIO_OUT_LOW, NULL},
	{"PP1050_EN",            LM4_GPIO_C, (1<<7), GPIO_OUT_LOW, NULL},
	{"PP5000_USB_EN",        LM4_GPIO_C, (1<<5), GPIO_OUT_LOW, NULL},
	{"PP5000_EN",            LM4_GPIO_H, (1<<7), GPIO_OUT_LOW, NULL},
	{"PP1800_EN",            LM4_GPIO_L, (1<<6), GPIO_OUT_LOW, NULL},
	{"SYS_PWROK",            LM4_GPIO_H, (1<<2), GPIO_OUT_LOW, NULL},
	{"WLAN_OFF_L",           LM4_GPIO_J, (1<<4), GPIO_OUT_LOW, NULL},

	{"ENABLE_BACKLIGHT",     LM4_GPIO_M, (1<<7), GPIO_OUT_LOW, NULL},
	{"ENABLE_TOUCHPAD",      LM4_GPIO_N, (1<<1), GPIO_OUT_LOW, NULL},
	{"ENTERING_RW",          LM4_GPIO_D, (1<<3), GPIO_OUT_LOW, NULL},
	{"LIGHTBAR_RESET_L",     LM4_GPIO_J, (1<<2), GPIO_OUT_LOW, NULL},
	{"PCH_DPWROK",           LM4_GPIO_G, (1<<0), GPIO_OUT_LOW, NULL},
	/*
	 * HDA_SDO is technically an output, but we need to leave it as an
	 * input until we drive it high.  So can't use open-drain (HI_Z).
	 */
	{"PCH_HDA_SDO",          LM4_GPIO_G, (1<<1), GPIO_INPUT, NULL},
	{"PCH_WAKE_L",           LM4_GPIO_F, (1<<0), GPIO_ODR_HIGH, NULL},
	{"PCH_NMI_L",            LM4_GPIO_F, (1<<2), GPIO_ODR_HIGH, NULL},
	{"PCH_PWRBTN_L",         LM4_GPIO_H, (1<<0), GPIO_ODR_HIGH, NULL},
	{"PCH_PWROK",            LM4_GPIO_F, (1<<5), GPIO_OUT_LOW, NULL},
	{"PCH_RCIN_L",           LM4_GPIO_F, (1<<3), GPIO_ODR_HIGH, NULL},
	{"PCH_SYS_RST_L",        LM4_GPIO_F, (1<<1), GPIO_ODR_HIGH, NULL},
	{"PCH_SMI_L",            LM4_GPIO_F, (1<<4), GPIO_ODR_HIGH, NULL},
	{"TOUCHSCREEN_RESET_L",  LM4_GPIO_N, (1<<7), GPIO_OUT_LOW, NULL},
	{"PCH_ACOK",             LM4_GPIO_M, (1<<6), GPIO_OUT_LOW, NULL},
#ifndef HEY_USE_BUILTIN_CLKRUN
	{"LPC_CLKRUN_L",         LM4_GPIO_M, (1<<2), GPIO_ODR_HIGH, NULL},
#endif
	{"USB1_CTL1",            LM4_GPIO_E, (1<<1), GPIO_OUT_LOW, NULL},
	{"USB1_CTL2",            LM4_GPIO_E, (1<<2), GPIO_OUT_HIGH, NULL},
	{"USB1_CTL3",            LM4_GPIO_E, (1<<3), GPIO_OUT_LOW, NULL},
	{"USB1_ENABLE",          LM4_GPIO_E, (1<<4), GPIO_OUT_HIGH, NULL},
	{"USB1_ILIM_SEL",        LM4_GPIO_E, (1<<5), GPIO_OUT_LOW, NULL},
	{"USB2_CTL1",            LM4_GPIO_D, (1<<0), GPIO_OUT_LOW, NULL},
	{"USB2_CTL2",            LM4_GPIO_D, (1<<1), GPIO_OUT_HIGH, NULL},
	{"USB2_CTL3",            LM4_GPIO_D, (1<<4), GPIO_OUT_LOW, NULL},
	{"USB2_ENABLE",          LM4_GPIO_D, (1<<5), GPIO_OUT_HIGH, NULL},
	{"USB2_ILIM_SEL",        LM4_GPIO_D, (1<<6), GPIO_OUT_LOW, NULL},
};
BUILD_ASSERT(ARRAY_SIZE(gpio_list) == GPIO_COUNT);

/* Pins with alternate functions */
const struct gpio_alt_func gpio_alt_funcs[] = {
	{GPIO_A, 0x03, 1, MODULE_UART},			/* UART0 */
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
#ifdef HEY_USE_BUILTIN_CLKRUN
	{GPIO_M, 0x04, 15, MODULE_LPC, GPIO_OPEN_DRAIN},/* LPC */
#endif
	{GPIO_N, 0x3c, 1, MODULE_PWM_FAN},		/* FAN0PWM 2&3 */
	{GPIO_N, 0x40, 1, MODULE_PWM_KBLIGHT},		/* FAN0PWM4 */
};
const int gpio_alt_funcs_count = ARRAY_SIZE(gpio_alt_funcs);

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP1050_PGOOD,  1, "PGOOD_PP1050"},
	{GPIO_PP1200_PGOOD,  1, "PGOOD_PP1200"},
	{GPIO_PP1800_PGOOD,  1, "PGOOD_PP1800"},
	{GPIO_VCORE_PGOOD,   1, "PGOOD_VCORE"},
	{GPIO_PCH_SLP_S0_L,  1, "SLP_S0_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,  1, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,  1, "SLP_S5_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L, 1, "SLP_SUS_DEASSERTED"},
	{GPIO_PCH_SUSWARN_L, 1, "SUSWARN_DEASSERTED"},
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

	/* We're measuring the adapter input current through a 0.01-ohm
	 * resistor (ACP/ACN). IOUT is 40X the differential voltage, so
	 * 1000mA => 400mV. ADC returns 0x000-0xFFF over 0.0-3.3V.
	 * mA = 1000 * ADC_VALUE / ADC_READ_MAX * 3.3(V) * 100(R) / 40(gain)
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 33000, ADC_READ_MAX * 4, 0,
	 LM4_AIN(11), 0x06 /* IE0 | END0 */, LM4_GPIO_B, (1<<5)},

	/*
	 * TODO(crosbug.com/p/23827): We don't know what to expect here, but
	 * it's an analog input that's pulled high. We're using it as a battery
	 * presence indicator for now. We'll return just 0 - ADC_READ_MAX for
	 * now.
	 */
	{"BatteryTemp", LM4_ADC_SEQ2, 1, 1, 0,
	 LM4_AIN(10), 0x06 /* IE0 | END0 */, LM4_GPIO_B, (1<<4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{4, 0},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1000,
	 .rpm_max = 6500,
	 .ch = 2,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1000,
	 .rpm_max = 6500,
	 .ch = 3,
	 .pgood_gpio = -1,
	 .enable_gpio = -1,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", 0, 100},
	{"lightbar", 1, 400},
	{"thermal",  5, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define TEMP_U40_REG_ADDR	((0x40 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U41_REG_ADDR	((0x44 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U42_REG_ADDR	((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U43_REG_ADDR	((0x45 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U115_REG_ADDR	((0x42 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U116_REG_ADDR	((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_U40_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U40_REG_ADDR)
#define TEMP_U41_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U41_REG_ADDR)
#define TEMP_U42_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U42_REG_ADDR)
#define TEMP_U43_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U43_REG_ADDR)
#define TEMP_U115_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U115_REG_ADDR)
#define TEMP_U116_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_U116_REG_ADDR)

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	{"Charger", TEMP_U40_ADDR},
	{"CPU", TEMP_U41_ADDR},
	{"Left C", TEMP_U42_ADDR},
	{"Right C", TEMP_U43_ADDR},
	{"Right D", TEMP_U115_ADDR},
	{"Left D", TEMP_U116_ADDR},
};

/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[] = {
	{"PECI", TEMP_SENSOR_TYPE_CPU, peci_temp_sensor_get_val, 0, 2},
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
	{"I2C-Charger-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 0, 7},
	{"I2C-Charger-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 1, 7},
	{"I2C-CPU-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 2, 7},
	{"I2C-CPU-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 3, 7},
	{"I2C-Left C-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 4, 7},
	{"I2C-Left C-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 5, 7},
	{"I2C-Right C-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 6, 7},
	{"I2C-Right C-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 7, 7},
	{"I2C-Right D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 8, 7},
	{"I2C-Right D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 9, 7},
	{"I2C-Left D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 10, 7},
	{"I2C-Left D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 11, 7},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{"ISL", isl29035_read_lux},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);


/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* Only the AP affects the thermal limits and fan speed. */
	{{C_TO_K(95), C_TO_K(97), C_TO_K(99)}, C_TO_K(55), C_TO_K(85)},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
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

#ifdef CONFIG_BATTERY_PRESENT_CUSTOM
/**
 * Physical check of battery presence.
 */
int battery_is_present(void)
{
	return adc_read_channel(ADC_CH_BAT_TEMP) < (9 * ADC_READ_MAX / 10);
}
#endif
