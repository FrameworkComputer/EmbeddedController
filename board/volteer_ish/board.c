/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer ISH board-specific configuration */

#include "console.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/accel_bma2x2.h"
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

/* Drivers */
struct motion_sensor_t motion_sensors[] = {
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
