/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Link board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "chipset.h"
#include "common.h"
#include "driver/temp_sensor/tmp006.h"
#include "extpower.h"
#include "fan.h"
#include "gpio.h"
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

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PGOOD_5VALW,		1, "PGOOD_5VALW"},
	{GPIO_PGOOD_1_5V_DDR,		1, "PGOOD_1_5V_DDR"},
	{GPIO_PGOOD_1_5V_PCH,		1, "PGOOD_1_5V_PCH"},
	{GPIO_PGOOD_1_8VS,		1, "PGOOD_1_8VS"},
	{GPIO_PGOOD_VCCP,		1, "PGOOD_VCCP"},
	{GPIO_PGOOD_VCCSA,		1, "PGOOD_VCCSA"},
	{GPIO_PGOOD_CPU_CORE,		1, "PGOOD_CPU_CORE"},
	{GPIO_PGOOD_VGFX_CORE,		1, "PGOOD_VGFX_CORE"},
	{GPIO_PCH_SLP_S3_L,		1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,		1, "SLP_S4#_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,		1, "SLP_S5#_DEASSERTED"},
	{GPIO_PCH_SLP_A_L,		1, "SLP_A#_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L,		1, "SLP_SUS#_DEASSERTED"},
	{GPIO_PCH_SLP_ME_CSW_DEV_L,	1, "SLP_ME#_DEASSERTED"},
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

	/* Charger current is mapped from 0~4000mA to 0~1.6V.
	 * And ADC maps 0~3.3V to ADC_READ_MAX.
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 33 * 4000, ADC_READ_MAX * 16, 0,
	 LM4_AIN(11), 0x06 /* IE0 | END0 */, LM4_GPIO_B, (1<<5)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{1, 0},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1500,
	 .rpm_start = 1500,
	 .rpm_max = 9300,
	 .ch = 0,
	 .pgood_gpio = GPIO_PGOOD_5VALW,
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

#define TEMP_PCH_REG_ADDR	((0x41 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_CHARGER_REG_ADDR	((0x43 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_USB_REG_ADDR	((0x46 << 1) | I2C_FLAG_BIG_ENDIAN)
#define TEMP_HINGE_REG_ADDR	((0x44 << 1) | I2C_FLAG_BIG_ENDIAN)

#define TEMP_PCH_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_PCH_REG_ADDR)
#define TEMP_CHARGER_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_CHARGER_REG_ADDR)
#define TEMP_USB_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_USB_REG_ADDR)
#define TEMP_HINGE_ADDR TMP006_ADDR(I2C_PORT_THERMAL, TEMP_HINGE_REG_ADDR)

/* Temperature sensors data; must be in same order as enum temp_sensor_id. */
const struct temp_sensor_t temp_sensors[] = {
	{"I2C-USB C-Die", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 0, 7},
	{"I2C-USB C-Object", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 1, 7},
	{"I2C-PCH D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 2, 7},
	{"I2C-PCH D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 3, 7},
	{"I2C-Hinge C-Die", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 4, 7},
	{"I2C-Hinge C-Object", TEMP_SENSOR_TYPE_IGNORED, tmp006_get_val, 5, 7},
	{"I2C-Charger D-Die", TEMP_SENSOR_TYPE_BOARD, tmp006_get_val, 6, 7},
	{"I2C-Charger D-Object", TEMP_SENSOR_TYPE_CASE, tmp006_get_val, 7, 7},
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
	{"PECI", TEMP_SENSOR_TYPE_CPU, peci_temp_sensor_get_val, 0, 2},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	{{0, 0, 0}, 0, 0},
	/* Only the AP affects the thermal limits and fan speed. */
	{{C_TO_K(100), C_TO_K(102), C_TO_K(104)}, C_TO_K(60), C_TO_K(90)},
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	{"USB C", TEMP_USB_ADDR},
	{"PCH D", TEMP_PCH_ADDR},
	{"Hinge C", TEMP_HINGE_ADDR},
	{"Charger D", TEMP_CHARGER_ADDR},
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
