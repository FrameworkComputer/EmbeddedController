/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Rambi board configuration */

#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "driver/temp_sensor/tmp432.h"
#include "extpower.h"
#include "gpio.h"
#include "host_command.h"
#include "i2c.h"
#include "jtag.h"
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
#include "thermal.h"
#include "timer.h"
#include "uart.h"
#include "util.h"

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP1050_PGOOD,      1, "PGOOD_PP1050"},
	{GPIO_PP3300_PCH_PGOOD,  1, "PGOOD_PP3300_PCH"},
	{GPIO_PP5000_PGOOD,      1, "PGOOD_PP5000"},
	{GPIO_S5_PGOOD,          1, "PGOOD_S5"},
	{GPIO_VCORE_PGOOD,       1, "PGOOD_VCORE"},
	{GPIO_PP1000_S0IX_PGOOD, 1, "PGOOD_PP1000_S0IX"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4#_DEASSERTED"},
#ifdef CONFIG_CHIPSET_DEBUG
	{GPIO_PCH_SLP_SX_L,      1, "SLP_SX#_DEASSERTED"},
	{GPIO_PCH_SUS_STAT_L,    0, "SUS_STAT#_ASSERTED"},
	{GPIO_PCH_SUSPWRDNACK,   1, "SUSPWRDNACK_ASSERTED"},
#endif
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
	/* We have 0.01-ohm resistors, and IOUT is 40X the differential
	 * voltage, so 1000mA ==> 400mV.
	 * ADC returns 0x000-0xFFF, which maps to 0.0-3.3V (as configured).
	 * mA = 1000 * ADC_VALUE / ADC_READ_MAX * 3300 / 400
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 33000, ADC_READ_MAX * 4, 0,
	 LM4_AIN(0), 0x06 /* IE0 | END0 */, LM4_GPIO_E, (1<<3)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{4, PWM_CONFIG_ACTIVE_LOW},
	{3, PWM_CONFIG_ACTIVE_LOW},
};

BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", 0, 100},
	{"thermal",  5, 100},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
	{"ECInternal", TEMP_SENSOR_TYPE_BOARD, chip_temp_sensor_get_val, 0, 4},
	{"TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_LOCAL, 4},
	{"TMP432_Power_top", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE1, 4},
	{"TMP432_CPU_bottom", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE2, 4},
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

int board_allow_i2c_passthru(int port)
{
	return 0;
}
