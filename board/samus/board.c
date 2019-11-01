/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kxcj9.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "driver/als_isl29035.h"
#include "driver/temp_sensor/tmp006.h"
#include "extpower.h"
#include "fan.h"
#include "gesture.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "keyboard_8042.h"
#include "keyboard_8042_sharedlib.h"
#include "lid_switch.h"
#include "lightbar.h"
#include "motion_sense.h"
#include "motion_lid.h"
#include "peci.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "switch.h"
#include "system.h"
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
	host_command_pd_send_status(PD_CHARGE_NO_CHANGE);
}

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_PP1050_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "PGOOD_PP1050"},
	{GPIO_PP1200_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "PGOOD_PP1200"},
	{GPIO_PP1800_PGOOD,  POWER_SIGNAL_ACTIVE_HIGH, "PGOOD_PP1800"},
	{GPIO_VCORE_PGOOD,   POWER_SIGNAL_ACTIVE_HIGH, "PGOOD_VCORE"},
	{GPIO_PCH_SLP_S0_L,  POWER_SIGNAL_ACTIVE_HIGH, "SLP_S0_DEASSERTED"},
	{GPIO_PCH_SLP_S3_L,  POWER_SIGNAL_ACTIVE_HIGH, "SLP_S3_DEASSERTED"},
	{GPIO_PCH_SLP_S5_L,  POWER_SIGNAL_ACTIVE_HIGH, "SLP_S5_DEASSERTED"},
	{GPIO_PCH_SLP_SUS_L, POWER_SIGNAL_ACTIVE_HIGH, "SLP_SUS_DEASSERTED"},
	{GPIO_PCH_SUSWARN_L, POWER_SIGNAL_ACTIVE_HIGH, "SUSWARN_DEASSERTED"},
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
	 * TODO(crosbug.com/p/23827): We don't know what to expect here, but
	 * it's an analog input that's pulled high. We're using it as a battery
	 * presence indicator for now. We'll return just 0 - ADC_READ_MAX for
	 * now.
	 */
	{"BatteryTemp", LM4_ADC_SEQ2, 1, 1, 0,
	 LM4_AIN(10), 0x06 /* IE0 | END0 */, LM4_GPIO_B, BIT(4)},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* PWM channels. Must be in the exactly same order as in enum pwm_channel. */
const struct pwm_t pwm_channels[] = {
	{4, 0},
};
BUILD_ASSERT(ARRAY_SIZE(pwm_channels) == PWM_CH_COUNT);

/* Physical fans. These are logically separate from pwm_channels. */
const struct fan_conf fan_conf_0 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 2,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_conf fan_conf_1 = {
	.flags = FAN_USE_RPM_MODE,
	.ch = 3,	/* Use MFT id to control fan */
	.pgood_gpio = -1,
	.enable_gpio = -1,
};

const struct fan_rpm fan_rpm_0 = {
	.rpm_min = 1000,
	.rpm_start = 1000,
	.rpm_max = 6350,
};

const struct fan_t fans[] = {
	{ .conf = &fan_conf_0, .rpm = &fan_rpm_0, },
	{ .conf = &fan_conf_1, .rpm = &fan_rpm_0, },
};
BUILD_ASSERT(ARRAY_SIZE(fans) == CONFIG_FANS);

/* I2C ports */
const struct i2c_port_t i2c_ports[] = {
	{"batt_chg", 0, 100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"lightbar", 1, 400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"thermal",  5, 100, GPIO_I2C5_SCL, GPIO_I2C5_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#define TEMP_U40_REG_ADDR_FLAGS		(0x40 | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U41_REG_ADDR_FLAGS		(0x44 | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U42_REG_ADDR_FLAGS		(0x41 | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U43_REG_ADDR_FLAGS		(0x45 | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U115_REG_ADDR_FLAGS	(0x42 | I2C_FLAG_BIG_ENDIAN)
#define TEMP_U116_REG_ADDR_FLAGS	(0x43 | I2C_FLAG_BIG_ENDIAN)

#define TEMP_U40_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					TEMP_U40_REG_ADDR_FLAGS)
#define TEMP_U41_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					TEMP_U41_REG_ADDR_FLAGS)
#define TEMP_U42_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					TEMP_U42_REG_ADDR_FLAGS)
#define TEMP_U43_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					TEMP_U43_REG_ADDR_FLAGS)
#define TEMP_U115_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					 TEMP_U115_REG_ADDR_FLAGS)
#define TEMP_U116_ADDR_FLAGS TMP006_ADDR(I2C_PORT_THERMAL,\
					 TEMP_U116_REG_ADDR_FLAGS)

const struct tmp006_t tmp006_sensors[TMP006_COUNT] = {
	{"Charger", TEMP_U40_ADDR_FLAGS},
	{"CPU", TEMP_U41_ADDR_FLAGS},
	{"Left C", TEMP_U42_ADDR_FLAGS},
	{"Right C", TEMP_U43_ADDR_FLAGS},
	{"Right D", TEMP_U115_ADDR_FLAGS},
	{"Left D", TEMP_U116_ADDR_FLAGS},
};
BUILD_ASSERT(ARRAY_SIZE(tmp006_sensors) == TMP006_COUNT);

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
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_get_battery_temp, 0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* ALS instances. Must be in same order as enum als_id. */
struct als_t als[] = {
	{"ISL", isl29035_init, isl29035_read_lux, 5},
};
BUILD_ASSERT(ARRAY_SIZE(als) == ALS_COUNT);


/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	/* {Twarn, Thigh, Thalt}, fan_off, fan_max */
	{{C_TO_K(95), C_TO_K(101), C_TO_K(104)},
	 {0, 0, 0}, C_TO_K(55), C_TO_K(90)},		/* PECI */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},			/* EC */
	{{0, 0, 0}, {0, 0, 0}, C_TO_K(41), C_TO_K(55)},	/* Charger die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, C_TO_K(35), C_TO_K(49)},	/* CPU die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, 0, 0},			/* Left C die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, 0, 0},			/* Right C die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, 0, 0},			/* Right D die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, C_TO_K(43), C_TO_K(54)},	/* Left D die */
	{{0, 0, 0}, {0, 0, 0}, 0, 0},
	{{0, 0, 0}, {0, 0, 0}, 0, 0},			/* Battery */
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

static int discharging_on_ac;

/**
 * Discharge battery when on AC power for factory test.
 */
int board_discharge_on_ac(int enable)
{
	int rv = charger_discharge_on_ac(enable);

	if (rv == EC_SUCCESS)
		discharging_on_ac = enable;

	return rv;
}

/**
 * Check if we are discharging while connected to AC
 */
int board_is_discharging_on_ac(void)
{
	return discharging_on_ac;
}

/**
 * Reset PD MCU
 */
void board_reset_pd_mcu(void)
{
	gpio_set_level(GPIO_USB_MCU_RST, 1);
	usleep(100);
	gpio_set_level(GPIO_USB_MCU_RST, 0);
}

void sensor_board_proc_double_tap(void)
{
	lightbar_sequence(LIGHTBAR_TAP);
}

const int usb_port_enable[CONFIG_USB_PORT_POWER_SMART_PORT_COUNT] = {
	GPIO_USB1_ENABLE,
	GPIO_USB2_ENABLE,
};

/* Base Sensor mutex */
static struct mutex g_base_mutex;

/* Lid Sensor mutex */
static struct mutex g_lid_mutex;

/* kxcj9 local/private data */
struct kionix_accel_data g_kxcj9_data;

/* lsm6ds0 local sensor data (per-sensor) */
struct lsm6ds0_data g_saved_data[2];

/* Four Motion sensors */
/* Matrix to rotate accelrator into standard reference frame */
const mat33_fp_t base_standard_ref = {
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0, FLOAT_TO_FP(-1),  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

const mat33_fp_t lid_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0, FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: lsm6ds0: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	{.name = "Base",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_LSM6DS0,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6ds0_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_saved_data[0],
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = LSM6DS0_ADDR1_FLAGS,
	 .rot_standard_ref = &base_standard_ref,
	 .default_range = 2,  /* g, enough for laptop. */
	 .min_frequency = LSM6DS0_ACCEL_MIN_FREQ,
	 .max_frequency = LSM6DS0_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 119000 | ROUND_UP_FLAG,
			 .ec_rate = 100 * MSEC,
		 },
		 /* Used for double tap */
		 [SENSOR_CONFIG_EC_S3] = {
			 .odr = TAP_ODR_LSM6DS0 | ROUND_UP_FLAG,
			 .ec_rate = CONFIG_GESTURE_SAMPLING_INTERVAL_MS * MSEC,
		 },
		 [SENSOR_CONFIG_EC_S5] = {
			 .odr = TAP_ODR_LSM6DS0 | ROUND_UP_FLAG,
			 .ec_rate = CONFIG_GESTURE_SAMPLING_INTERVAL_MS * MSEC,
		 },
	 },
	},

	{.name = "Lid",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kionix_accel_drv,
	 .mutex = &g_lid_mutex,
	 .drv_data = &g_kxcj9_data,
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = KXCJ9_ADDR0_FLAGS,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_range = 2,  /* g, enough for laptop. */
	 .min_frequency = KXCJ9_ACCEL_MIN_FREQ,
	 .max_frequency = KXCJ9_ACCEL_MAX_FREQ,
	 .config = {
		 /* EC use accel for angle detection */
		 [SENSOR_CONFIG_EC_S0] = {
			 .odr = 100000 | ROUND_UP_FLAG,
			 .ec_rate = 100 * MSEC,
		 },
	 },
	},

	{.name = "Base Gyro",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_LSM6DS0,
	 .type = MOTIONSENSE_TYPE_GYRO,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &lsm6ds0_drv,
	 .mutex = &g_base_mutex,
	 .drv_data = &g_saved_data[1],
	 .port = I2C_PORT_ACCEL,
	 .i2c_spi_addr_flags = LSM6DS0_ADDR1_FLAGS,
	 .rot_standard_ref = NULL,
	 .default_range = 2000,  /* g, enough for laptop. */
	 .min_frequency = LSM6DS0_GYRO_MIN_FREQ,
	 .max_frequency = LSM6DS0_GYRO_MAX_FREQ,
	},

};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#ifdef CONFIG_LOW_POWER_IDLE
void jtag_interrupt(enum gpio_signal signal)
{
	/*
	 * This interrupt is the first sign someone is trying to use
	 * the JTAG. Disable slow speed sleep so that the JTAG action
	 * can take place.
	 */
	disable_sleep(SLEEP_MASK_JTAG);

	/*
	 * Once we get this interrupt, disable it from occurring again
	 * to avoid repeated interrupts when debugging via JTAG.
	 */
	gpio_disable_interrupt(GPIO_JTAG_TCK);
}
#endif /* CONFIG_LOW_POWER_IDLE */


enum ec_error_list keyboard_scancode_callback(uint16_t *make_code,
					      int8_t pressed)
{
	const uint16_t k = *make_code;
	static uint8_t s;
	static const uint16_t a[] = {
		SCANCODE_UP, SCANCODE_UP, SCANCODE_DOWN, SCANCODE_DOWN,
		SCANCODE_LEFT, SCANCODE_RIGHT, SCANCODE_LEFT, SCANCODE_RIGHT,
		SCANCODE_B, SCANCODE_A};

	if (!pressed)
		return EC_SUCCESS;

	/* Lightbar demo mode: keyboard can fake the battery state */
	switch (k) {
	case SCANCODE_UP:
		demo_battery_level(1);
		break;
	case SCANCODE_DOWN:
		demo_battery_level(-1);
		break;
	case SCANCODE_LEFT:
		demo_is_charging(0);
		break;
	case SCANCODE_RIGHT:
		demo_is_charging(1);
		break;
	case SCANCODE_F6:  /* dim */
		demo_brightness(-1);
		break;
	case SCANCODE_F7:  /* bright */
		demo_brightness(1);
		break;
	case SCANCODE_T:
		demo_tap();
		break;
	}

	if (k == a[s])
		s++;
	else if (k != a[0])
		s = 0;
	else if (s != 2)
		s = 1;

	if (s == ARRAY_SIZE(a)) {
		s = 0;
		lightbar_sequence(LIGHTBAR_KONAMI);
	}
	return EC_SUCCESS;
}

/*
 * Use to define going in to hibernate early if low on battery.
 * HIBERNATE_BATT_PCT specifies the low battery threshold
 * for going into hibernate early, and HIBERNATE_BATT_SEC defines
 * the minimum amount of time to stay in G3 before checking for low
 * battery hibernate.
 */
#define HIBERNATE_BATT_PCT 10
#define HIBERNATE_BATT_SEC (3600 * 24)

__override enum critical_shutdown board_system_is_idle(
		uint64_t last_shutdown_time, uint64_t *target, uint64_t now)
{
	if (charge_get_percent() <= HIBERNATE_BATT_PCT) {
		uint64_t t = last_shutdown_time + HIBERNATE_BATT_SEC * SEC_UL;
		*target = MIN(*target, t);
	}
	return now > *target ?
			CRITICAL_SHUTDOWN_HIBERNATE : CRITICAL_SHUTDOWN_IGNORE;
}
