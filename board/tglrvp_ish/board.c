/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TGL RVP ISH board-specific configuration */

#include "accelgyro_lsm6dsm.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "i2c.h"
#include "motion_sense.h"
#include "power.h"
#include "task.h"

#include "gpio_list.h" /* has to be included last */

/* I2C port map */
const struct i2c_port_t i2c_ports[] = {
	{
		.name = "sensor",
		.port = I2C_PORT_SENSOR,
		.kbps = 1000
	},
};
const unsigned int i2c_ports_used = ARRAY_SIZE(i2c_ports);

/* Sensor config */
static struct mutex g_base_mutex;
/* sensor private data */
static struct lsm6dsm_data lsm6dsm_a_data = LSM6DSM_DATA;

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_LSM6DS3,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_a_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR1_FLAGS,
		.rot_standard_ref = NULL, /* TODO rotate correctly */
		.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

int chipset_in_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ON;
}

int chipset_in_or_transitioning_to_state(int state_mask)
{
	return state_mask & CHIPSET_STATE_ON;
}

void chipset_force_shutdown(enum chipset_shutdown_reason reason)
{
}

int board_idle_task(void *unused)
{
	while (1)
		task_wait_event(-1);
}
