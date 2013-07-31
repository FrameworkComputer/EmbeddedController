/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock temperature sensor module for Chrome EC */

#include "common.h"
#include "console.h"
#include "temp_sensor.h"
#include "timer.h"
#include "util.h"

static int temp_val[TEMP_SENSOR_TYPE_COUNT];

int temp_sensor_powered(enum temp_sensor_id id)
{
	/* Always powered */
	return 1;
}


int temp_sensor_read(enum temp_sensor_id id)
{
	return temp_val[temp_sensors[id].type];
}


void temp_sensor_task(void)
{
	/* Do nothing */
	while (1)
		sleep(5);
}


static int command_set_temp(int argc, char **argv, int type)
{
	char *e;
	int t;

	ASSERT(argc == 2);
	t = strtoi(argv[1], &e, 0);
	temp_val[type] = t;
	return EC_SUCCESS;
}


static int command_set_cpu_temp(int argc, char **argv)
{
	return command_set_temp(argc, argv, TEMP_SENSOR_TYPE_CPU);
}
DECLARE_CONSOLE_COMMAND(setcputemp, command_set_cpu_temp,
			"value",
			"Set mock CPU temperature value",
			NULL);


static int command_set_board_temp(int argc, char **argv)
{
	return command_set_temp(argc, argv, TEMP_SENSOR_TYPE_BOARD);
}
DECLARE_CONSOLE_COMMAND(setboardtemp, command_set_board_temp,
			"value",
			"Set mock board temperature value",
			NULL);


static int command_set_case_temp(int argc, char **argv)
{
	return command_set_temp(argc, argv, TEMP_SENSOR_TYPE_CASE);
}
DECLARE_CONSOLE_COMMAND(setcasetemp, command_set_case_temp,
			"value",
			"Set mock case temperature value",
			NULL);
