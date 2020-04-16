/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Treeya board-specific configuration */

#include "button.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "extpower.h"
#include "i2c.h"
#include "lid_switch.h"
#include "power.h"
#include "power_button.h"
#include "pwm.h"
#include "system.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"

#include "gpio_list.h"

const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_AC_PRESENT,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used =  ARRAY_SIZE(hibernate_wake_pins);

/* I2C port map. */
const struct i2c_port_t i2c_ports[] = {
	{"power",   I2C_PORT_POWER,   100, GPIO_I2C0_SCL, GPIO_I2C0_SDA},
	{"tcpc0",   I2C_PORT_TCPC0,   400, GPIO_I2C1_SCL, GPIO_I2C1_SDA},
	{"tcpc1",   I2C_PORT_TCPC1,   400, GPIO_I2C2_SCL, GPIO_I2C2_SDA},
	{"thermal", I2C_PORT_THERMAL_AP, 400, GPIO_I2C3_SCL, GPIO_I2C3_SDA},
	{"sensor",  I2C_PORT_SENSOR,  400, GPIO_I2C7_SCL, GPIO_I2C7_SDA},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

#ifdef HAS_TASK_MOTIONSENSE

/* Motion sensors */
static struct mutex g_lid_mutex_1;
static struct mutex g_base_mutex_1;

/* Lid accel private data */
static struct stprivate_data g_lis2dwl_data;
/* Base accel private data */
static struct lsm6dsm_data g_lsm6dsm_data = LSM6DSM_DATA;


/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t lsm6dsm_base_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

static const mat33_fp_t treeya_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t lid_accel_1 = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LIS2DWL,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &lis2dw12_drv,
	.mutex = &g_lid_mutex_1,
	.drv_data = &g_lis2dwl_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LIS2DWL_ADDR1_FLAGS,
	.rot_standard_ref = NULL,
	.default_range = 2, /* g */
	.min_frequency = LIS2DW12_ODR_MIN_VAL,
	.max_frequency = LIS2DW12_ODR_MAX_VAL,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 12500 | ROUND_UP_FLAG,
		},
		/* Sensor on for lid angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
		},
	},
};

struct motion_sensor_t base_accel_1 = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex_1,
	.drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_ACCEL),
	.int_signal = GPIO_6AXIS_INT_L,
	.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.rot_standard_ref = &lsm6dsm_base_standard_ref,
	.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on for angle detection */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 10000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
	},
};

struct motion_sensor_t base_gyro_1 = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_LSM6DSM,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &lsm6dsm_drv,
	.mutex = &g_base_mutex_1,
	.drv_data = LSM6DSM_ST_DATA(g_lsm6dsm_data,
			MOTIONSENSE_TYPE_GYRO),
	.int_signal = GPIO_6AXIS_INT_L,
	.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.default_range = 1000 | ROUND_UP_FLAG, /* dps */
	.rot_standard_ref = &lsm6dsm_base_standard_ref,
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
};

static int board_use_st_sensor(void)
{
	/* sku_id 0xa8-0xa9 use ST sensors */
	uint32_t sku_id = system_get_sku_id();

	return sku_id == 0xa8 || sku_id == 0xa9;
}

/* treeya board will use two sets of lid/base sensor, we need update
 * sensors info according to sku id.
 */
void board_update_sensor_config_from_sku(void)
{
	if (board_is_convertible()) {
		/* sku_id a8-a9 use ST sensors */
		if (board_use_st_sensor()) {
			motion_sensors[LID_ACCEL] = lid_accel_1;
			motion_sensors[BASE_ACCEL] = base_accel_1;
			motion_sensors[BASE_GYRO] = base_gyro_1;
		} else{
			/*Need to change matrix for treeya*/
			motion_sensors[BASE_ACCEL].rot_standard_ref = &treeya_standard_ref;
			motion_sensors[BASE_GYRO].rot_standard_ref = &treeya_standard_ref;
		}

		/* Enable Gyro interrupts */
		gpio_enable_interrupt(GPIO_6AXIS_INT_L);
	} else {
		motion_sensor_count = 0;
		/* Device is clamshell only */
		tablet_set_mode(0);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_6AXIS_INT_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}

/* bmi160 or lsm6dsm need differenct interrupt function */
void board_bmi160_lsm6dsm_interrupt(enum gpio_signal signal)
{
	if (board_use_st_sensor())
		lsm6dsm_interrupt(signal);
	else
		bmi160_interrupt(signal);
}

#endif
