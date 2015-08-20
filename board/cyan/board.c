/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Cyan board-specific configuration */

#include "charger.h"
#include "charge_state.h"
#include "driver/accel_kxcj9.h"
#include "driver/temp_sensor/tmp432.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "math_util.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "registers.h"
#include "spi.h"
#include "switch.h"
#include "task.h"
#include "temp_sensor.h"
#include "temp_sensor_chip.h"
#include "thermal.h"
#include "uart.h"
#include "util.h"

#define GPIO_KB_INPUT (GPIO_INPUT | GPIO_PULL_UP)
#define GPIO_KB_OUTPUT (GPIO_ODR_HIGH)
#ifdef CONFIG_KEYBOARD_COL2_INVERTED
 #define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_LOW)
#else
 #define GPIO_KB_OUTPUT_COL2 (GPIO_OUT_HIGH)
#endif

#include "gpio_list.h"

/* power signal list.  Must match order of enum power_signal. */
const struct power_signal_info power_signal_list[] = {
	{GPIO_ALL_SYS_PGOOD,     1, "ALL_SYS_PWRGD"},
	{GPIO_RSMRST_L_PGOOD,    1, "RSMRST_N_PWRGD"},
	{GPIO_PCH_SLP_S3_L,      1, "SLP_S3#_DEASSERTED"},
	{GPIO_PCH_SLP_S4_L,      1, "SLP_S4#_DEASSERTED"},
};
BUILD_ASSERT(ARRAY_SIZE(power_signal_list) == POWER_SIGNAL_COUNT);

const struct i2c_port_t i2c_ports[]  = {
	{"batt_chg", MEC1322_I2C0_0, 100, GPIO_I2C0_0_SCL, GPIO_I2C0_0_SDA},
	{"sensors",  MEC1322_I2C1,   100, GPIO_I2C1_SCL,   GPIO_I2C1_SDA  },
	{"soc",      MEC1322_I2C2,   100, GPIO_I2C2_SCL,   GPIO_I2C2_SDA  },
	{"thermal",  MEC1322_I2C3,   100, GPIO_I2C3_SCL,   GPIO_I2C3_SDA  },
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* SPI devices */
const struct spi_device_t spi_devices[] = {
	{ CONFIG_SPI_FLASH_PORT, 0, GPIO_PVT_CS0 },
};
const unsigned int spi_devices_used = ARRAY_SIZE(spi_devices);

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_POWER_BUTTON_L,
};

const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/*
 * Temperature sensors data; must be in same order as enum temp_sensor_id.
 * Sensor index and name must match those present in coreboot:
 *     src/mainboard/google/${board}/acpi/dptf.asl
 */
const struct temp_sensor_t temp_sensors[] = {
	{"TMP432_Internal", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_LOCAL, 4},
	{"TMP432_Sensor_1", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE1, 4},
	{"TMP432_Sensor_2", TEMP_SENSOR_TYPE_BOARD, tmp432_get_val,
		TMP432_IDX_REMOTE2, 4},
	{"Battery", TEMP_SENSOR_TYPE_BATTERY, charge_temp_sensor_get_val, 0, 4},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/* Thermal limits for each temp sensor. All temps are in degrees K. Must be in
 * same order as enum temp_sensor_id. To always ignore any temp, use 0.
 */
struct ec_thermal_config thermal_params[] = {
	{{0, 0, 0}, 0, 0}, /* TMP432_Internal */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_1 */
	{{0, 0, 0}, 0, 0}, /* TMP432_Sensor_2 */
	{{0, 0, 0}, 0, 0}, /* Battery Sensor */
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

/* kxcj9 mutex and local/private data*/
static struct mutex g_kxcj9_mutex[2];
struct kxcj9_data g_kxcj9_data[2];

/* Four Motion sensors */
/* Matrix to rotate accelrator into standard reference frame */
const matrix_3x3_t base_standard_ref = {
	{ 0,  FLOAT_TO_FP(1),  0},
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0,  0,  FLOAT_TO_FP(1)}
};

const matrix_3x3_t lid_standard_ref = {
	{FLOAT_TO_FP(-1),  0,  0},
	{ 0, FLOAT_TO_FP(1),  0},
	{ 0,  0,  FLOAT_TO_FP(-1)}
};

struct motion_sensor_t motion_sensors[] = {
	{.name = "Base",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &kxcj9_drv,
	 .mutex = &g_kxcj9_mutex[0],
	 .drv_data = &g_kxcj9_data[0],
	 .addr = KXCJ9_ADDR1,
	 .rot_standard_ref = &base_standard_ref,
	 .default_config = {
		 .odr = 100000,
		 .range = 2,
		 .ec_rate = SUSPEND_SAMPLING_INTERVAL,
	 }
	},
	{.name = "Lid",
	 .active_mask = SENSOR_ACTIVE_S0_S3,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &kxcj9_drv,
	 .mutex = &g_kxcj9_mutex[1],
	 .drv_data = &g_kxcj9_data[1],
	 .addr = KXCJ9_ADDR0,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_config = {
		 .odr = 100000,
		 .range = 2,
		 .ec_rate = SUSPEND_SAMPLING_INTERVAL,
	 }
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Define the accelerometer orientation matrices. */
const struct accel_orientation acc_orient = {
	/* Hinge aligns with x axis. */
	.rot_hinge_90 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0,  0,  FLOAT_TO_FP(1)},
		{ 0, FLOAT_TO_FP(-1),  0}
	},
	.rot_hinge_180 = {
		{ FLOAT_TO_FP(1),  0,  0},
		{ 0, FLOAT_TO_FP(-1),  0},
		{ 0,  0, FLOAT_TO_FP(-1)}
	},
	.hinge_axis = {1, 0, 0},
};

#ifdef CONFIG_LID_ANGLE_UPDATE
static void track_pad_enable(int enable)
{
	if (enable)
		gpio_set_level(GPIO_TP_INT_DISABLE, 0);
	else
		gpio_set_level(GPIO_TP_INT_DISABLE, 1);
}

void lid_angle_peripheral_enable(int enable)
{
	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
		track_pad_enable(1);
	} else {
		/*
		 * Ensure chipset is off before disabling keyboard. When chipset
		 * is on, EC keeps keyboard enabled and the AP decides when to
		 * ignore keys based on its more accurate lid angle calculation.
		 *
		 * TODO(crosbug.com/p/43695): Remove this check once we have a
		 * host command that can inform EC when we are entering or
		 * exiting tablet mode in S0. Also, add this check back to the
		 * function lid_angle_update in lid_angle.c
		 */
		if (!chipset_in_state(CHIPSET_STATE_ON))
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
		track_pad_enable(0);
	}
}
#endif
