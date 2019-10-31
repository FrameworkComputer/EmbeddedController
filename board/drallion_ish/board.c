/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Drallion ISH board-specific configuration */

#include "console.h"
#include "driver/accel_lis2dh.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/mag_lis2mdl.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "tablet_mode.h"
#include "task.h"

#include "gpio_list.h" /* has to be included last */

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "sensor",
		/* SDA and SCL gpio must be set correctly in coreboot gpio */
		.port = I2C_PORT_SENSOR,
		.kbps = 400,
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Sensor config */
static struct mutex g_lid_mutex;
static struct mutex g_lid_mag_mutex;
static struct mutex g_base_mutex;

/* sensor private data */
static struct lsm6dsm_data lsm6dsm_a_data;
static struct stprivate_data g_lis2dh_data;
static struct lis2mdl_private_data lis2mdl_a_data;

/* Matrix to rotate lid sensor into standard reference frame */
const mat33_fp_t lid_rot_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_LSM6DS3,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lsm6dsm_drv,
		.mutex = &g_lid_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_a_data,
				MOTIONSENSE_TYPE_ACCEL),
		.int_signal = GPIO_ACCEL_GYRO_INT_L,
		.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = &lid_rot_ref,
		.default_range = 4,  /* g */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 13000 | ROUND_UP_FLAG,
			},
		},
	},

	[LID_GYRO] = {
		.name = "Lid Gyro",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_LSM6DS3,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lsm6dsm_drv,
		.mutex = &g_lid_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_a_data,
				MOTIONSENSE_TYPE_GYRO),
		.int_signal = GPIO_ACCEL_GYRO_INT_L,
		.flags = MOTIONSENSE_FLAG_INT_SIGNAL,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &lid_rot_ref,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},

	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_LNG2DM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lis2dh_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_lis2dh_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LNG2DM_ADDR0_FLAGS,
		.rot_standard_ref = NULL, /* Identity matrix */
		/* We only use 2g because its resolution is only 8-bits */
		.default_range = 2, /* g */
		.min_frequency = LIS2DH_ODR_MIN_VAL,
		.max_frequency = LIS2DH_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},

	[LID_MAG] = {
		.name = "Lid Mag",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_LIS2MDL,
		.type = MOTIONSENSE_TYPE_MAG,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2mdl_drv,
		.mutex = &g_lid_mag_mutex,
		.drv_data = LIS2MDL_ST_DATA(lis2mdl_a_data),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2MDL_ADDR_FLAGS,
		.default_range = 1 << 11,	/* 16LSB / uT, fixed  */
		.rot_standard_ref = &lid_rot_ref,
		.min_frequency = LIS2MDL_ODR_MIN_VAL,
		.max_frequency = LIS2MDL_ODR_MAX_VAL,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* Initialize board. */
static void board_init(void)
{
	/* Enable interrupt for LSM6DS3 sensor */
	gpio_enable_interrupt(GPIO_ACCEL_GYRO_INT_L);
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

/*
 * The only use for chipset state is sensors, so we hard code the AP state to on
 * and make the sensor on in S0. The sensors are always on when the ISH is
 * powered.
 */
int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ON;
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	return chipset_in_state(state_mask);
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
	/* Required, but nothing to do */
}

/* Needed for empty chipset task */
int board_idle_task(void *unused)
{
	while (1)
		task_wait_event(-1);
}

static void board_tablet_mode_change(void)
{
	/* Update GPIO to EC letting it know that we entered tablet mode */
	gpio_set_level(GPIO_NB_MODE_L, tablet_get_mode());
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, board_tablet_mode_change,
	     HOOK_PRIO_DEFAULT);
