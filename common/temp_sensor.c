/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Temperature sensor module for Chrome EC */

#include "i2c.h"
#include "temp_sensor.h"
#include "uart.h"
#include "util.h"
#include "console.h"
#include "board.h"
#include "peci.h"
#include "tmp006.h"
#include "task.h"
#include "chip_temp_sensor.h"

/* Defined in board_temp_sensor.c. Must be in the same order as
 * in enum temp_sensor_id.
 */
extern const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT];

int temp_sensor_read(enum temp_sensor_id id)
{
	const struct temp_sensor_t *sensor;

	if (id < 0 || id >= TEMP_SENSOR_COUNT)
		return -1;
	sensor = temp_sensors + id;
	return sensor->read(sensor->idx);
}

void poll_all_sensors(void)
{
#ifdef CONFIG_TMP006
	tmp006_poll();
#endif
#ifdef CONFIG_PECI
	peci_temp_sensor_poll();
#endif
#ifdef CHIP_lm4
	chip_temp_sensor_poll();
#endif
}

void temp_sensor_task(void)
{
	while (1) {
		poll_all_sensors();
		/* Wait 1s */
		task_wait_msg(1000000);
	}
}

/*****************************************************************************/
/* Console commands */

static int command_temps(int argc, char **argv)
{
	int i;
	int rv = 0;
	int t;

	uart_puts("Reading temperature sensors...\n");

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		uart_printf("  Temp from %s:  ", temp_sensors[i].name);
		t = temp_sensor_read(i);
		if (t < 0) {
			uart_printf("Error.\n\n");
			rv = -1;
		}
		else
			uart_printf("%d K = %d C\n\n", t, t - 273);
	}

	if (rv == -1)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(temps, command_temps);

/*****************************************************************************/
/* Initialization */

int temp_sensor_init(void)
{
	return EC_SUCCESS;
}
