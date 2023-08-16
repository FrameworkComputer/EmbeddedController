/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* NEW thermal engine module for Chrome EC. This is a completely different
 * implementation from the original version that shipped on Link.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 15

#include "chipset.h"
#include "common.h"
#include "console.h"
#include "fan.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "throttle_ap.h"
#include "timer.h"
#include "util.h"

#ifdef CONFIG_ZEPHYR
#include "temp_sensor/temp_sensor.h"
#endif

/* Console output macros */
#define CPUTS(outstr) cputs(CC_THERMAL, outstr)
#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)

/*****************************************************************************/
/* EC-specific thermal controls */

test_mockable_static void smi_sensor_failure_warning(void)
{
	CPRINTS("can't read any temp sensors!");
	host_set_single_event(EC_HOST_EVENT_THERMAL);
}

int thermal_fan_percent(int low, int high, int cur)
{
	if (cur < low)
		return 0;
	if (cur > high)
		return 100;
	return 100 * (cur - low) / (high - low);
}

/* The logic below is hard-coded for only three thresholds: WARN, HIGH, HALT.
 * This is just a validity check to be sure we catch any changes in thermal.h
 */
BUILD_ASSERT(EC_TEMP_THRESH_COUNT == 3);

/* Keep track of which thresholds have triggered */
static cond_t cond_hot[EC_TEMP_THRESH_COUNT];

/* thermal sensor read delay */
#if defined(CONFIG_TEMP_SENSOR_POWER) && \
	defined(CONFIG_TEMP_SENSOR_FIRST_READ_DELAY_MS)
static int first_read_delay = CONFIG_TEMP_SENSOR_FIRST_READ_DELAY_MS;
#endif

static void thermal_control(void)
{
	int i, j, t, rv;
	int count_over[EC_TEMP_THRESH_COUNT];
	int count_under[EC_TEMP_THRESH_COUNT];
	int num_valid_limits[EC_TEMP_THRESH_COUNT];
	int num_sensors_read;
#ifdef CONFIG_FANS
#ifndef CONFIG_CUSTOM_FAN_CONTROL
	int f = 0;
	int fmax = 0;
	int temp_fan_configured = 0;
#else
	int temp[TEMP_SENSOR_COUNT];
#endif
#endif

	/* add delay to ensure thermal sensor is ready when EC boot */
#if defined(CONFIG_TEMP_SENSOR_POWER) && \
	defined(CONFIG_TEMP_SENSOR_FIRST_READ_DELAY_MS)
	if (first_read_delay != 0) {
		msleep(first_read_delay);
		first_read_delay = 0;
	}
#endif

	/* Get ready to count things */
	memset(count_over, 0, sizeof(count_over));
	memset(count_under, 0, sizeof(count_under));
	memset(num_valid_limits, 0, sizeof(num_valid_limits));
	num_sensors_read = 0;

	/* go through all the sensors */
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		/* read one */
		rv = temp_sensor_read(i, &t);

#if defined(CONFIG_FANS) && defined(CONFIG_CUSTOM_FAN_CONTROL)
		/* Store all sensors value */
		temp[i] = K_TO_C(t);
#endif

		if (rv != EC_SUCCESS)
			continue;
		else
			num_sensors_read++;

		/* check all the limits */
		for (j = 0; j < EC_TEMP_THRESH_COUNT; j++) {
			int limit = thermal_params[i].temp_host[j];
			int release = thermal_params[i].temp_host_release[j];
			if (limit) {
				num_valid_limits[j]++;
				if (t > limit) {
					count_over[j]++;
				} else if (release) {
					if (t < release)
						count_under[j]++;
				} else if (t < limit) {
					count_under[j]++;
				}
			}
		}

#ifdef CONFIG_FANS
#ifndef CONFIG_CUSTOM_FAN_CONTROL
		/* figure out the max fan needed, too */
		if (thermal_params[i].temp_fan_off &&
		    thermal_params[i].temp_fan_max) {
			f = thermal_fan_percent(thermal_params[i].temp_fan_off,
						thermal_params[i].temp_fan_max,
						t);
			if (f > fmax)
				fmax = f;

			temp_fan_configured = 1;
		}
#endif
#endif
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
		 *
		 * If in G3, then there is no need trigger an SMI event since
		 * the AP is off and this can be an expected state if
		 * temperature sensors are powered by a power rail that's only
		 * on if the AP is out of G3. Note this could be 'ANY_OFF' as
		 * well, but that causes the thermal unit test to fail.
		 */
		if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
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
		CPRINTS("thermal SHUTDOWN");

		/* Print temperature sensor values before shutting down AP */
		if (IS_ENABLED(CONFIG_CMD_TEMP_SENSOR)) {
			print_temps();
			cflush();
		}

		chipset_force_shutdown(CHIPSET_SHUTDOWN_THERMAL);
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_HALT])) {
		/* We don't reboot automatically - the user has to push
		 * the power button. It's likely that we can't even
		 * detect this sensor transition until then, but we
		 * do have to check in order to clear the cond_t.
		 */
		CPRINTS("thermal no longer shutdown");
	}

	if (cond_went_true(&cond_hot[EC_TEMP_THRESH_HIGH])) {
		CPRINTS("thermal HIGH");
		throttle_ap(THROTTLE_ON, THROTTLE_HARD, THROTTLE_SRC_THERMAL);
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_HIGH])) {
		CPRINTS("thermal no longer high");
		throttle_ap(THROTTLE_OFF, THROTTLE_HARD, THROTTLE_SRC_THERMAL);
	}

	if (cond_went_true(&cond_hot[EC_TEMP_THRESH_WARN])) {
		CPRINTS("thermal WARN");
		throttle_ap(THROTTLE_ON, THROTTLE_SOFT, THROTTLE_SRC_THERMAL);
	} else if (cond_went_false(&cond_hot[EC_TEMP_THRESH_WARN])) {
		CPRINTS("thermal no longer warn");
		throttle_ap(THROTTLE_OFF, THROTTLE_SOFT, THROTTLE_SRC_THERMAL);
	}

#ifdef CONFIG_FANS
#ifdef CONFIG_CUSTOM_FAN_CONTROL
	for (i = 0; i < fan_get_count(); i++) {
		if (!is_thermal_control_enabled(i))
			continue;

		board_override_fan_control(i, temp);
	}
#else
	if (temp_fan_configured) {
		/* TODO(crosbug.com/p/23797): For now, we just treat all
		 * fans the same. It would be better if we could assign
		 * different thermal profiles to each fan - in case one
		 * fan cools the CPU while another cools the radios or
		 * battery.
		 */
		for (i = 0; i < fan_get_count(); i++)
			fan_set_percent_needed(i, fmax);
	}
#endif
#endif
}

/* Wait until after the sensors have been read */
DECLARE_HOOK(HOOK_SECOND, thermal_control, HOOK_PRIO_TEMP_SENSOR_DONE);

/*****************************************************************************/
/* Console commands */

static int command_thermalget(int argc, const char **argv)
{
	int i;

	ccprintf("sensor  warn  high  halt   fan_off fan_max   name\n");
	for (i = 0; i < TEMP_SENSOR_COUNT; i++) {
		ccprintf(" %2d      %3d   %3d    %3d    %3d     %3d     %s\n",
			 i, thermal_params[i].temp_host[EC_TEMP_THRESH_WARN],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HIGH],
			 thermal_params[i].temp_host[EC_TEMP_THRESH_HALT],
			 thermal_params[i].temp_fan_off,
			 thermal_params[i].temp_fan_max, temp_sensors[i].name);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(thermalget, command_thermalget, NULL,
			"Print thermal parameters (degrees Kelvin)");

static int command_thermalset(int argc, const char **argv)
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
			" Use -1 to skip.");

/*****************************************************************************/
/* Host commands. We'll reuse the host command number, but this is version 1,
 * not version 0. Different structs, different meanings.
 */

static enum ec_status
thermal_command_set_threshold(struct host_cmd_handler_args *args)
{
	const struct ec_params_thermal_set_threshold_v1 *p = args->params;

	if (p->sensor_num >= TEMP_SENSOR_COUNT)
		return EC_RES_INVALID_PARAM;

	thermal_params[p->sensor_num] = p->cfg;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_THERMAL_SET_THRESHOLD,
		     thermal_command_set_threshold, EC_VER_MASK(1));

static enum ec_status
thermal_command_get_threshold(struct host_cmd_handler_args *args)
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
		     thermal_command_get_threshold, EC_VER_MASK(1));
