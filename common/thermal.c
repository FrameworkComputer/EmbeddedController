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
#include "timer.h"
#include "util.h"
#include "x86_power.h"

/* Defined in board_temp_sensor.c. Must be in the same order as
 * in enum temp_sensor_id.
 */
extern const struct temp_sensor_t temp_sensors[TEMP_SENSOR_COUNT];

/* Temperature threshold configuration. Must be in the same order as in
 * enum temp_sensor_type. Threshold values for overheated action first.
 * Followed by fan speed stepping thresholds. */
static struct thermal_config_t thermal_config[TEMP_SENSOR_TYPE_COUNT] = {
	/* TEMP_SENSOR_TYPE_CPU */
	{THERMAL_CONFIG_WARNING_ON_FAIL,
	 {341, 358, 368, 318, 323, 328, 333, 338}},
	/* TEMP_SENSOR_TYPE_BOARD */
	{THERMAL_CONFIG_NO_FLAG, {THERMAL_THRESHOLD_DISABLE_ALL}},
	/* TEMP_SENSOR_TYPE_CASE */
	{THERMAL_CONFIG_NO_FLAG, {333, THERMAL_THRESHOLD_DISABLE, 348,
	 THERMAL_THRESHOLD_DISABLE_ALL}},
};

/* Fan speed settings. */
/* Max RPM is about 11000. Setting each step to be 20% of the max RPM. */
static const int fan_speed[THERMAL_FAN_STEPS + 1] = {0, 2200, 4400, 6600,
						     8800, -1};

/* Number of consecutive overheated events for each temperature sensor. */
static int8_t ot_count[TEMP_SENSOR_COUNT][THRESHOLD_COUNT + THERMAL_FAN_STEPS];

/* Flag that indicate if each threshold is reached.
 * Note that higher threshold reached does not necessarily mean lower thresholds
 * are reached (since we can disable any threshold.) */
static int8_t overheated[THRESHOLD_COUNT + THERMAL_FAN_STEPS];
static int8_t *fan_threshold_reached = overheated + THRESHOLD_COUNT;

static int fan_ctrl_on = 1;


int thermal_set_threshold(enum temp_sensor_type type, int threshold_id, int value)
{
	if (threshold_id < 0 ||
	    threshold_id >= THRESHOLD_COUNT + THERMAL_FAN_STEPS)
		return EC_ERROR_INVAL;
	if (value < 0)
		return EC_ERROR_INVAL;

	thermal_config[type].thresholds[threshold_id] = value;

	return EC_SUCCESS;
}


int thermal_get_threshold(enum temp_sensor_type type, int threshold_id)
{
	if (threshold_id < 0 ||
	    threshold_id >= THRESHOLD_COUNT + THERMAL_FAN_STEPS)
		return EC_ERROR_INVAL;

	return thermal_config[type].thresholds[threshold_id];
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


/* TODO: When we need different overheated action for different boards,
 *       move these action to board-specific file. (e.g. board_thermal.c)
 */
static void overheated_action(void)
{
	if (overheated[THRESHOLD_POWER_DOWN]) {
		x86_power_force_shutdown();
		return;
	}

	if (overheated[THRESHOLD_CPU_DOWN])
		x86_power_cpu_overheated(1);
	else
		x86_power_cpu_overheated(0);

	if (overheated[THRESHOLD_WARNING]) {
		smi_overheated_warning();
		gpio_set_flags(GPIO_CPU_PROCHOTn, GPIO_OUTPUT);
		gpio_set_level(GPIO_CPU_PROCHOTn, 0);
	}
	else
		gpio_set_flags(GPIO_CPU_PROCHOTn, 0);

	if (fan_ctrl_on) {
		int i;
		for (i = THERMAL_FAN_STEPS - 1; i >= 0; --i)
			if (fan_threshold_reached[i])
				break;
		pwm_set_fan_target_rpm(fan_speed[i + 1]);
	}
}


/* Update counter and check if the counter has reached delay limit.
 * Note that we have 10 seconds delay to prevent one error value triggering
 * overheated action. */
static inline void update_and_check_stat(int temp,
					 int sensor_id,
					 int threshold_id)
{
	enum temp_sensor_type type = temp_sensors[sensor_id].type;
	const struct thermal_config_t *config = thermal_config + type;
	const int16_t threshold = config->thresholds[threshold_id];

	if (threshold > 0 && temp >= threshold) {
		++ot_count[sensor_id][threshold_id];
		if (ot_count[sensor_id][threshold_id] >= 10) {
			ot_count[sensor_id][threshold_id] = 10;
			overheated[threshold_id] = 1;
		}
	}
	else if (ot_count[sensor_id][threshold_id] >= 10 &&
		 temp >= threshold - 3) {
		/* Once the threshold is reached, only if the temperature
		 * drops to 3 degrees below threshold do we deassert
		 * overheated signal. This is to prevent temperature
		 * oscillating around the threshold causing threshold
		 * keep being triggered. */
		overheated[threshold_id] = 1;
	} else
		ot_count[sensor_id][threshold_id] = 0;
}


static void thermal_process(void)
{
	int i, j;
	int cur_temp;

	for (i = 0; i < THRESHOLD_COUNT + THERMAL_FAN_STEPS; ++i)
		overheated[i] = 0;

	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		enum temp_sensor_type type = temp_sensors[i].type;
		int flag = thermal_config[type].config_flags;

		if (!temp_sensor_powered(i))
			continue;

		cur_temp = temp_sensor_read(i);

		/* Sensor failure. */
		if (cur_temp == -1) {
			if (flag & THERMAL_CONFIG_WARNING_ON_FAIL)
				smi_sensor_failure_warning();
			continue;
		}
		for (j = 0; j < THRESHOLD_COUNT + THERMAL_FAN_STEPS; ++j)
			update_and_check_stat(cur_temp, i, j);
	}

	overheated_action();
}


void thermal_task(void)
{
	while (1) {
		thermal_process();
		usleep(1000000);
	}
}

/*****************************************************************************/
/* Console commands */

static void print_thermal_config(enum temp_sensor_type type)
{
	const struct thermal_config_t *config = thermal_config + type;
	ccprintf("Sensor Type %d:\n", type);
	ccprintf("\tWarning: %d K\n",
		 config->thresholds[THRESHOLD_WARNING]);
	ccprintf("\tCPU Down: %d K\n",
		 config->thresholds[THRESHOLD_CPU_DOWN]);
	ccprintf("\tPower Down: %d K\n",
		 config->thresholds[THRESHOLD_POWER_DOWN]);
}


static void print_fan_stepping(enum temp_sensor_type type)
{
	const struct thermal_config_t *config = thermal_config + type;
	int i;

	ccprintf("Sensor Type %d:\n", type);
	ccprintf("\tLowest speed: %d RPM\n", fan_speed[0]);
	for (i = 0; i < THERMAL_FAN_STEPS; ++i)
		ccprintf("\t%3d K:        %d RPM\n",
			 config->thresholds[THRESHOLD_COUNT + i],
			 fan_speed[i+1]);
}


static int command_thermal_config(int argc, char **argv)
{
	char *e;
	int sensor_type, threshold_id, value;

	if (argc != 2 && argc != 4) {
		ccputs("Usage: thermal <sensor_type> "
		       "[<threshold_id> <value>]\n");
		return EC_ERROR_UNKNOWN;
	}

	sensor_type = strtoi(argv[1], &e, 0);
	if ((e && *e) || sensor_type < 0 ||
	    sensor_type >= TEMP_SENSOR_TYPE_COUNT) {
		ccputs("Bad sensor type ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc == 2) {
		print_thermal_config(sensor_type);
		return EC_SUCCESS;
	}

	threshold_id = strtoi(argv[2], &e, 0);
	if ((e && *e) || threshold_id < 0 || threshold_id >= THRESHOLD_COUNT) {
		ccputs("Bad threshold ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	value = strtoi(argv[3], &e, 0);
	if ((e && *e) || value < 0) {
		ccputs("Bad threshold value.\n");
		return EC_ERROR_UNKNOWN;
	}

	thermal_config[sensor_type].thresholds[threshold_id] = value;
	ccprintf("Setting threshold %d of sensor type %d to %d\n",
		 threshold_id, sensor_type, value);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermal, command_thermal_config);


static int command_fan_config(int argc, char **argv)
{
	char *e;
	int sensor_type, stepping_id, value;

	if (argc != 2 && argc != 4) {
		ccputs("Usage: thermalfan <sensor_type> "
		       "[<stepping_id> <value>]\n");
		return EC_ERROR_UNKNOWN;
	}

	sensor_type = strtoi(argv[1], &e, 0);
	if ((e && *e) || sensor_type < 0 ||
	    sensor_type >= TEMP_SENSOR_TYPE_COUNT) {
		ccputs("Bad sensor type ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	if (argc == 2) {
		print_fan_stepping(sensor_type);
		return EC_SUCCESS;
	}

	stepping_id = strtoi(argv[2], &e, 0);
	if ((e && *e) || stepping_id < 0 || stepping_id >= THERMAL_FAN_STEPS) {
		ccputs("Bad stepping ID.\n");
		return EC_ERROR_UNKNOWN;
	}

	value = strtoi(argv[3], &e, 0);
	if ((e && *e) || value < 0) {
		ccputs("Bad threshold value.\n");
		return EC_ERROR_UNKNOWN;
	}

	thermal_config[sensor_type].thresholds[THRESHOLD_COUNT + stepping_id] =
		value;
	ccprintf("Setting fan step %d of sensor type %d to %d K\n",
		 stepping_id, sensor_type, value);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalfan, command_fan_config);


static int command_thermal_auto_fan_ctrl(int argc, char **argv)
{
	return thermal_toggle_auto_fan_ctrl(1);
}
DECLARE_CONSOLE_COMMAND(autofan, command_thermal_auto_fan_ctrl);
