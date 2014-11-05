/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Auron board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "chipset.h"
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

#include "gpio_list.h"

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
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_t fans[] = {
	{.flags = FAN_USE_RPM_MODE,
	 .rpm_min = 1000,
	 .rpm_start = 1000,
	 .rpm_max = 5050,
	 .ch = 2,
	 .pgood_gpio = GPIO_PP5000_PGOOD,
	 .enable_gpio = GPIO_PP5000_FAN_EN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", 0, 100},
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
	{{C_TO_K(95), C_TO_K(97), C_TO_K(99)}, C_TO_K(55), C_TO_K(85)},
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
		0xa4, 0xff, 0xf6, 0x55, 0xfa, 0xca  /* full set */
	},
};

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
