/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* EC for Samus board configuration */

#include "als.h"
#include "adc.h"
#include "adc_chip.h"
#include "backlight.h"
#include "battery.h"
#include "capsense.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kxcj9.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "driver/als_isl29035.h"
#include "driver/temp_sensor/tmp006.h"
#include "extpower.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "jtag.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "peci.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "timer.h"
#include "thermal.h"
#include "uart.h"
#include "util.h"

static void pd_mcu_interrupt(enum gpio_signal signal)
{
	/* Exchange status with PD MCU. */
	host_command_pd_send_status();
}

#include "gpio_list.h"

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
	/*
	 * EC internal temperature is calculated by
	 * 273 + (295 - 450 * ADC_VALUE / ADC_READ_MAX) / 2
	 * = -225 * ADC_VALUE / ADC_READ_MAX + 420.5
	 */
	{"ECTemp", LM4_ADC_SEQ0, -225, ADC_READ_MAX, 420,
	 LM4_AIN_NONE, 0x0e /* TS0 | IE0 | END0 */, 0, 0},

	/*
	 * The charger current is measured with a 0.005-ohm
	 * resistor. IBAT is 20X the voltage across that resistor when
	 * charging, and either 8X or 16X (default) when discharging, if it's
	 * even enabled (default is not). Nothing looks at this except the
	 * console command, so let's just leave it at unity gain. The ADC
	 * returns 0x000-0xFFF for 0.0-3.3V. You do the math.
	 */
	{"ChargerCurrent", LM4_ADC_SEQ1, 1, 1, 0,
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
	{"batt_chg", 0, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"lightbar", 1, 400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"thermal",  5, 100, GPIO_I2C5_SCL, GPIO_I2C5_SDA},
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
	{{C_TO_K(95), C_TO_K(97), C_TO_K(99)}, C_TO_K(43), C_TO_K(75)},
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

/* Initialize board. */
static void board_init(void)
{
	gpio_enable_interrupt(GPIO_PD_MCU_INT);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_BATTERY_PRESENT_CUSTOM
/**
 * Physical check of battery presence.
 */
enum battery_present battery_is_present(void)
{
	/*
	 * This pin has a pullup, so if it's not completely pegged there's
	 * something attached. Probably a battery.
	 */
	int analog_val = adc_read_channel(ADC_CH_BAT_TEMP);
	return analog_val < (9 * ADC_READ_MAX / 10) ? BP_YES : BP_NO;
}
#endif

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	return charger_discharge_on_ac(enable);
}

/* Base Sensor mutex */
static struct mutex g_base_mutex;

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

/* kxcj9 local/private data */
struct kxcj9_data g_kxcj9_data;

/* Four Motion sensors */
struct motion_sensor_t motion_sensors[] = {

	/*
	 * Note: lsm6ds0: supports accelerometer and gyro sensor
	 * Requriement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	{SENSOR_ACTIVE_S0_S3_S5, "Base", SENSOR_CHIP_LSM6DS0,
		SENSOR_ACCELEROMETER, LOCATION_BASE,
		&lsm6ds0_drv, &g_base_mutex, NULL,
		LSM6DS0_ADDR1, 119000, 2},

	{SENSOR_ACTIVE_S0, "Lid",  SENSOR_CHIP_KXCJ9,
		SENSOR_ACCELEROMETER, LOCATION_LID,
		&kxcj9_drv, &g_lid_mutex, &g_kxcj9_data,
		KXCJ9_ADDR0, 100000, 2},

	{SENSOR_ACTIVE_S0, "Base Gyro", SENSOR_CHIP_LSM6DS0,
		SENSOR_GYRO, LOCATION_BASE,
		&lsm6ds0_drv, &g_base_mutex, NULL,
		LSM6DS0_ADDR1, 119000, 2000},

};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Define the accelerometer orientation matrices. */
const struct accel_orientation acc_orient = {
	/* Lid and base sensor are already aligned. */
	.rot_align = {
		{ 0, -1,  0},
		{ 1,  0,  0},
		{ 0,  0,  1}
	},

	/* Hinge aligns with y axis. */
	.rot_hinge_90 = {
		{ 1,  0,  0},
		{ 0,  1,  0},
		{ 0,  0,  1}
	},
	.rot_hinge_180 = {
		{ 1,  0,  0},
		{ 0,  1,  0},
		{ 0,  0,  1}
	},
	.rot_standard_ref = {
		{-1,  0,  0},
		{ 0, -1,  0},
		{ 0,  0, -1}
	},
	.hinge_axis = {0, 1, 0},
};
