/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Thermal engine module for Chrome EC */

#include "board.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "lpc_commands.h"
#include "pwm.h"
#include "task.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "uart.h"
#include "util.h"
#include "x86_power.h"

/* Defined in board_thermal.c. Must be in the same order
 * as in enum temp_sensor_id. */
extern struct thermal_config_t thermal_config[TEMP_SENSOR_COUNT];

/* Number of consecutive overheated events for each temperature sensor. */
static int8_t ot_count[TEMP_SENSOR_COUNT][THRESHOLD_COUNT];

/* Flag that indicate if each threshold is reached.
 * Note that higher threshold reached does not necessarily mean lower thresholds
 * are reached (since we can disable any threshold.) */
static int8_t overheated[THRESHOLD_COUNT];

static int fan_ctrl_on = 1;


int thermal_set_threshold(int sensor_id, int threshold_id, int value)
{
	if (sensor_id < 0 || sensor_id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	if (threshold_id < 0 || threshold_id >= THRESHOLD_COUNT)
		return EC_ERROR_INVAL;
	if (value < 0)
		return EC_ERROR_INVAL;

	thermal_config[sensor_id].thresholds[threshold_id] = value;

	return EC_SUCCESS;
}


int thermal_get_threshold(int sensor_id, int threshold_id)
{
	if (sensor_id < 0 || sensor_id >= TEMP_SENSOR_COUNT)
		return EC_ERROR_INVAL;
	if (threshold_id < 0 || threshold_id >= THRESHOLD_COUNT)
		return EC_ERROR_INVAL;

	return thermal_config[sensor_id].thresholds[threshold_id];
}


int thermal_toggle_auto_fan_ctrl(int auto_fan_on)
{
	fan_ctrl_on = auto_fan_on;
	return EC_SUCCESS;
}


static void smi_overheated_warning(void)
{
	lpc_set_host_events(
		EC_LPC_HOST_EVENT_MASK(EC_LPC_HOST_EVENT_THERMAL_OVERLOAD));
}


static void smi_sensor_failure_warning(void)
{
	lpc_set_host_events(
		EC_LPC_HOST_EVENT_MASK(EC_LPC_HOST_EVENT_THERMAL));
}


static void overheated_action(void)
{
	if (overheated[THRESHOLD_POWER_DOWN]) {
		x86_power_force_shutdown();
		return;
	}

	if (overheated[THRESHOLD_CPU_DOWN])
		x86_power_cpu_overheated(1);
	else {
		x86_power_cpu_overheated(0);
		if (overheated[THRESHOLD_WARNING])
			smi_overheated_warning();
	}

	if (fan_ctrl_on) {
		if (overheated[THRESHOLD_FAN_HI])
			pwm_set_fan_target_rpm(-1); /* Max RPM. */
		else if (overheated[THRESHOLD_FAN_LO])
			pwm_set_fan_target_rpm(6000);
		else
			pwm_set_fan_target_rpm(0);
	}
}


/* Update counter and check if the counter has reached delay limit.
 * Note that we have 10 seconds delay to prevent one error value triggering
 * overheated action. */
static inline void update_and_check_stat(int temp,
					 int sensor_id,
					 int threshold_id)
{
	const struct thermal_config_t *config = thermal_config + sensor_id;
	const int16_t threshold = config->thresholds[threshold_id];

	if (threshold > 0 && temp >= threshold) {
		++ot_count[sensor_id][threshold_id];
		if (ot_count[sensor_id][threshold_id] >= 10) {
			ot_count[sensor_id][threshold_id] = 10;
			overheated[threshold_id] = 1;
		}
	}
	else
		ot_count[sensor_id][threshold_id] = 0;
}


static void thermal_process(void)
{
	int i, j;
	int cur_temp;

	for (i = 0; i < THRESHOLD_COUNT; ++i)
		overheated[i] = 0;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		int flag = thermal_config[i].config_flags;

		if (!temp_sensor_powered(i))
			continue;

		cur_temp = temp_sensor_read(i);

		/* Sensor failure. */
		if (cur_temp == -1) {
			if (flag & THERMAL_CONFIG_WARNING_ON_FAIL)
				smi_sensor_failure_warning();
			continue;
		}
		for (j = 0; j < THRESHOLD_COUNT; ++j)
			update_and_check_stat(cur_temp, i, j);
	}

	overheated_action();
}


void thermal_task(void)
{
	while (1) {
		thermal_process();
		/* Wait 1s */
		task_wait_msg(1000000);
	}
}

/*****************************************************************************/
/* Console commands */

static void print_thermal_config(int sensor_id)
{
	const struct thermal_config_t *config = thermal_config + sensor_id;
	uart_printf("Sensor %d:\n", sensor_id);
	uart_printf("\tFan Low: %d K \n",
			config->thresholds[THRESHOLD_FAN_LO]);
	uart_printf("\tFan High: %d K \n",
			config->thresholds[THRESHOLD_FAN_HI]);
	uart_printf("\tWarning: %d K \n",
			config->thresholds[THRESHOLD_WARNING]);
	uart_printf("\tCPU Down: %d K \n",
			config->thresholds[THRESHOLD_CPU_DOWN]);
	uart_printf("\tPower Down: %d K \n",
			config->thresholds[THRESHOLD_POWER_DOWN]);
}


static int command_thermal_config(int argc, char **argv)
{
	char *e;
	int sensor_id, threshold_id, value;

	if (argc != 2 && argc != 4) {
		uart_puts("Usage: thermal <sensor> [<threshold_id> <value>]\n");
		return EC_ERROR_UNKNOWN;
	}

	sensor_id = strtoi(argv[1], &e, 0);
	if ((e && *e) || sensor_id < 0 || sensor_id >= TEMP_SENSOR_COUNT) {
		uart_puts("Bad sensor ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc == 2) {
		print_thermal_config(sensor_id);
		return EC_SUCCESS;
	}

	threshold_id = strtoi(argv[2], &e, 0);
	if ((e && *e) || threshold_id < 0 || threshold_id >= THRESHOLD_COUNT) {
		uart_puts("Bad threshold ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	value = strtoi(argv[3], &e, 0);
	if ((e && *e) || value < 0) {
		uart_puts("Bad threshold value.\n");
		return EC_ERROR_UNKNOWN;
	}

	thermal_config[sensor_id].thresholds[threshold_id] = value;
	uart_printf("Setting threshold %d of sensor %d to %d\n",
			threshold_id, sensor_id, value);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermal, command_thermal_config);


static int command_thermal_auto_fan_ctrl(int argc, char **argv)
{
	return thermal_toggle_auto_fan_ctrl(1);
}
DECLARE_CONSOLE_COMMAND(autofan, command_thermal_auto_fan_ctrl);
