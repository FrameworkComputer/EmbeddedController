/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NEW thermal engine module for Chrome EC. This is a completely different
 * implementation from the original version that shipped on Link.
 */

#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ## args)

/*****************************************************************************/
/* DPTF temperature thresholds */

static struct {
	int temp;			/* degrees K, negative for disabled */
	cond_t over;			/* watch for crossings */
} dptf_threshold[TEMP_SENSOR_COUNT][DPTF_THRESHOLDS_PER_SENSOR];

static void dptf_init(void)
{
	int id, t;

	for (id = 0; id < TEMP_SENSOR_COUNT; id++)
		for (t = 0; t < DPTF_THRESHOLDS_PER_SENSOR; t++) {
			dptf_threshold[id][t].temp = -1;
			cond_init(&dptf_threshold[id][t].over, 0);
		}

}
DECLARE_HOOK(HOOK_INIT, dptf_init, HOOK_PRIO_DEFAULT);

/* Keep track of which triggered sensor thresholds the AP has seen */
static uint32_t dptf_seen;

int dptf_query_next_sensor_event(void)
{
	int id;

	for (id = 0; id < TEMP_SENSOR_COUNT; id++)
		if (dptf_seen & (1 << id)) {	/* atomic? */
			atomic_clear(&dptf_seen, (1 << id));
			return id;
		}

	return -1;
}

/* Return true if any threshold transition occurs. */
static int dpft_check_temp_threshold(int sensor_id, int temp)
{
	int tripped = 0;
	int max, i;

	for (i = 0; i < DPTF_THRESHOLDS_PER_SENSOR; i++) {

		max = dptf_threshold[sensor_id][i].temp;
		if (max < 0)			/* disabled? */
			continue;

		if (temp >= max)
			cond_set_true(&dptf_threshold[sensor_id][i].over);
		else if (temp <= max - DPTF_THRESHOLD_HYSTERESIS)
			cond_set_false(&dptf_threshold[sensor_id][i].over);

		if (cond_went_true(&dptf_threshold[sensor_id][i].over)) {
			CPRINTF("[%T DPTF over threshold [%d][%d]\n",
				sensor_id, i);
			atomic_or(&dptf_seen, (1 << sensor_id));
			tripped = 1;
		}
		if (cond_went_false(&dptf_threshold[sensor_id][i].over)) {
			CPRINTF("[%T DPTF under threshold [%d][%d]\n",
				sensor_id, i);
			atomic_or(&dptf_seen, (1 << sensor_id));
			tripped = 1;
		}
	}

	return tripped;
}

void dptf_set_temp_threshold(int sensor_id, int temp, int idx, int enable)
{
	CPRINTF("[%T DPTF sensor %d, threshold %d C, index %d, %sabled]\n",
		sensor_id, K_TO_C(temp), idx, enable ? "en" : "dis");

	if (enable) {
		/* Don't update threshold condition if already enabled */
		if (dptf_threshold[sensor_id][idx].temp == -1)
			cond_init(&dptf_threshold[sensor_id][idx].over, 0);
		dptf_threshold[sensor_id][idx].temp = temp;
		atomic_clear(&dptf_seen, (1 << sensor_id));
	} else {
		dptf_threshold[sensor_id][idx].temp = -1;
	}
}

/*****************************************************************************/
/* EC-specific thermal controls */

test_mockable_static void smi_sensor_failure_warning(void)
{
	CPRINTF("[%T can't read any temp sensors!]\n");
	host_set_single_event(EC_HOST_EVENT_THERMAL);
}

static int fan_percent(int low, int high, int cur)
{
	if (cur < low)
		return 0;
	if (cur > high)
		return 100;
	return 100 * (cur - low) / (high - low);
}

/* The logic below is hard-coded for only three thresholds: WARN, HIGH, HALT.
 * This is just a sanity check to be sure we catch any changes in thermal.h
 */
BUILD_ASSERT(EC_TEMP_THRESH_COUNT == 3);

/* Keep track of which thresholds have triggered */
static cond_t cond_hot[EC_TEMP_THRESH_COUNT];

static void thermal_control(void)
{
	int i, j, t, rv, f;
	int count_over[EC_TEMP_THRESH_COUNT];
	int count_under[EC_TEMP_THRESH_COUNT];
	int num_valid_limits[EC_TEMP_THRESH_COUNT];
	int num_sensors_read;
	int fmax;
	int dptf_tripped;

	/* Get ready to count things */
	memset(count_over, 0, sizeof(count_over));
	memset(count_under, 0, sizeof(count_under));
	memset(num_valid_limits, 0, sizeof(num_valid_limits));
	num_sensors_read = 0;
	fmax = 0;
	dptf_tripped = 0;

	/* go through all the sensors */
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {

		/* read one */
		rv = temp_sensor_read(i, &t);
		if (rv != EC_SUCCESS)
			continue;
		else
			num_sensors_read++;

		/* check all the limits */
		for (j = 0; j < EC_TEMP_THRESH_COUNT; j++) {
			int limit = thermal_params[i].temp_host[j];
			if (limit) {
				num_valid_limits[j]++;
				if (t > limit)
					count_over[j]++;
				else if (t < limit)
					count_under[j]++;
			}
		}

		/* figure out the max fan needed, too */
		if (thermal_params[i].temp_fan_off &&
		    thermal_params[i].temp_fan_max) {
			f = fan_percent(thermal_params[i].temp_fan_off,
					thermal_params[i].temp_fan_max,
					t);
			if (f > fmax)
				fmax = f;
		}

		/* and check the dptf thresholds */
		dptf_tripped |= dpft_check_temp_threshold(i, t);
	}

	if (!num_sensors_read) {
		/*
		 * Trigger a SMI event if we can't read any sensors.
		 *
		 * In theory we could do something more elaborate like forcing
		 * the system to shut down if no sensors are available after
		 * several retries.  This is a very unlikely scenario -
		 * particularly on LM4-based boards, since the LM4 has its own
		 * internal temp sensor.  It's most likely to occur during
		 * bringup of a new board, where we haven't debugged the I2C
		 * bus to the sensors; forcing a shutdown in that case would
		 * merely hamper board bringup.
		 */
		smi_sensor_failure_warning();
		return;
	}

	/* See what the aggregated limits are. Any temp over the limit
	 * means it's hot, but all temps have to be under the limit to
	 * be cool again.
	 */
	for (j = 0; j < EC_TEMP_THRESH_COUNT; j++) {
		if (count_over[j])
			cond_set_true(&cond_hot[j]);
		else if (count_under[j] == num_valid_limits[j])
			cond_set_false(&cond_hot[j]);
	}


	/* What do we do about it? (note hard-coded logic). */

	if (cond_went_true(&cond_hot[EC_TEMP_THRESH_HALT])) {
		CPRINTF("[%T thermal SHUTDOWN]\n");
		chipset_force_shutdown();
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_HALT])) {
		/* We don't reboot automatically - the user has to push
		 * the power button. It's likely that we can't even
		 * detect this sensor transition until then, but we
		 * do have to check in order to clear the cond_t.
		 */
		CPRINTF("[%T thermal no longer shutdown]\n");
	}

	if (cond_went_true(&cond_hot[EC_TEMP_THRESH_HIGH])) {
		CPRINTF("[%T thermal HIGH]\n");
		throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_THERMAL);
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_HIGH])) {
		CPRINTF("[%T thermal no longer high]\n");
		throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_THERMAL);
	}

	if (cond_went_true(&cond_hot[EC_TEMP_THRESH_WARN])) {
		CPRINTF("[%T thermal WARN]\n");
		throttle_ap(THROTTLE_ON, THROTTLE_SOFT, THROTTLE_SRC_THERMAL);
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_WARN])) {
		CPRINTF("[%T thermal no longer warn]\n");
		throttle_ap(THROTTLE_OFF, THROTTLE_SOFT, THROTTLE_SRC_THERMAL);
	}

#ifdef CONFIG_FANS
	/* TODO(crosbug.com/p/23797): For now, we just treat all fans the
	 * same. It would be better if we could assign different thermal
	 * profiles to each fan - in case one fan cools the CPU while another
	 * cools the radios or battery.
	 */
	for (i = 0; i < CONFIG_FANS; i++)
		fan_set_percent_needed(i, fmax);
#endif

	/* Don't forget to signal any DPTF thresholds */
	if (dptf_tripped)
		host_set_single_event(EC_HOST_EVENT_THERMAL_THRESHOLD);
}

/* Wait until after the sensors have been read */
DECLARE_HOOK(HOOK_SECOND, thermal_control, HOOK_PRIO_TEMP_SENSOR_DONE);

/*****************************************************************************/
/* Console commands */

static int command_thermalget(int argc, char **argv)
{
	int i;

	ccprintf("sensor  warn  high  halt   fan_off fan_max   name\n");
	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		ccprintf(" %2d      %3d   %3d    %3d    %3d     %3d     %s\n",
			 i,
			 thermal_params[i].temp_host[EC_TEMP_THRESH_WARN],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HIGH],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HALT],
			 thermal_params[i].temp_fan_off,
			 thermal_params[i].temp_fan_max,
			 temp_sensors[i].name);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalget, command_thermalget,
			NULL,
			"Print thermal parameters (degrees Kelvin)",
			NULL);


static int command_thermalset(int argc, char **argv)
{
	unsigned int n;
	int i, val;
	char *e;

	if (argc < 3 || argc > 7)
		return EC_ERROR_PARAM_COUNT;

	n = (unsigned int)strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	for (i = 2; i < argc; i++) {
		val = strtoi(argv[i], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1 + i - 1;
		if (val < 0)
			continue;
		switch (i) {
		case 2:
			thermal_params[n].temp_host[EC_TEMP_THRESH_WARN] = val;
			break;
		case 3:
			thermal_params[n].temp_host[EC_TEMP_THRESH_HIGH] = val;
			break;
		case 4:
			thermal_params[n].temp_host[EC_TEMP_THRESH_HALT] = val;
			break;
		case 5:
			thermal_params[n].temp_fan_off = val;
			break;
		case 6:
			thermal_params[n].temp_fan_max = val;
			break;
		}
	}

	command_thermalget(0, 0);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalset, command_thermalset,
			"sensor warn [high [shutdown [fan_off [fan_max]]]]",
			"Set thermal parameters (degrees Kelvin)."
			" Use -1 to skip.",
			NULL);


static int command_dptftemp(int argc, char **argv)
{
	int id, t;
	int temp, trig;

	ccprintf("sensor   thresh0   thresh1\n");
	for (id = 0; id < TEMP_SENSOR_COUNT; id++) {
		ccprintf(" %2d", id);
		for (t = 0; t < DPTF_THRESHOLDS_PER_SENSOR; t++) {
			temp = dptf_threshold[id][t].temp;
			trig = cond_is_true(&dptf_threshold[id][t].over);
			if (temp < 0)
				ccprintf("       --- ");
			else
				ccprintf("       %3d%c", temp,
					 trig ? '*' : ' ');
		}
		ccprintf("    %s\n", temp_sensors[id].name);
	}

	ccprintf("AP seen mask: 0x%08x\n", dptf_seen);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dptftemp, command_dptftemp,
			NULL,
			"Print DPTF thermal parameters (degrees Kelvin)",
			NULL);


/*****************************************************************************/
/* Host commands. We'll reuse the host command number, but this is version 1,
 * not version 0. Different structs, different meanings.
 */

static int thermal_command_set_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_set_threshold_v1 *p = args->params;

	if (p->sensor_num >= TEMP_SENSOR_COUNT)
		return EC_RES_INVALID_PARAM;

	thermal_params[p->sensor_num] = p->cfg;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_SET_THRESHOLD,
		     thermal_command_set_threshold,
		     EC_VER_MASK(1));

static int thermal_command_get_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_get_threshold_v1 *p = args->params;
	struct ec_thermal_config *r = args->response;

	if (p->sensor_num >= TEMP_SENSOR_COUNT)
		return EC_RES_INVALID_PARAM;

	*r = thermal_params[p->sensor_num];
	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_GET_THRESHOLD,
		     thermal_command_get_threshold,
		     EC_VER_MASK(1));

