/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "hooks.h"
#include "host_command.h"
#include "temp_sensor.h"
#include "util.h"

#ifdef CONFIG_ZEPHYR
#include "temp_sensor/temp_sensor.h"
#endif

/* Console output macros */
#define CPUTS(outstr) cputs(CC_DPTF, outstr)
#define CPRINTS(format, args...) cprints(CC_DPTF, format, ##args)

#ifdef CONFIG_DPTF_DEBUG_PRINTS
#define DPRINTS(format, args...) cprints(CC_DPTF, format, ##args)
#else
#define DPRINTS(format, args...)
#endif

/*****************************************************************************/
/* DPTF temperature thresholds */

static struct {
	int temp; /* degrees K, negative for disabled */
	cond_t over; /* watch for crossings */
} dptf_threshold[TEMP_SENSOR_COUNT][DPTF_THRESHOLDS_PER_SENSOR];
_STATIC_ASSERT(TEMP_SENSOR_COUNT > 0,
	       "CONFIG_PLATFORM_EC_DPTF enabled, but no temp sensors");

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
static atomic_t dptf_seen;

int dptf_query_next_sensor_event(void)
{
	int id;

	for (id = 0; id < TEMP_SENSOR_COUNT; id++)
		if ((uint32_t)dptf_seen & BIT(id)) {
			atomic_clear_bits(&dptf_seen, BIT(id));
			return id;
		}

	return -1;
}

/* Return true if any threshold transition occurs. */
static int dptf_check_temp_threshold(int sensor_id, int temp)
{
	int tripped = 0;
	int max, i;

	if (sensor_id >= TEMP_SENSOR_COUNT) {
		CPRINTS("DPTF: Invalid sensor ID");
		return 0;
	}

	for (i = 0; i < DPTF_THRESHOLDS_PER_SENSOR; i++) {
		max = dptf_threshold[sensor_id][i].temp;
		if (max < 0) /* disabled? */
			continue;

		if (temp >= max)
			cond_set_true(&dptf_threshold[sensor_id][i].over);
		else if (temp <= max - DPTF_THRESHOLD_HYSTERESIS)
			cond_set_false(&dptf_threshold[sensor_id][i].over);

		if (cond_went_true(&dptf_threshold[sensor_id][i].over)) {
			DPRINTS("DPTF over threshold [%d][%d]", sensor_id, i);
			atomic_or(&dptf_seen, BIT(sensor_id));
			tripped = 1;
		}
		if (cond_went_false(&dptf_threshold[sensor_id][i].over)) {
			DPRINTS("DPTF under threshold [%d][%d]", sensor_id, i);
			atomic_or(&dptf_seen, BIT(sensor_id));
			tripped = 1;
		}
	}

	return tripped;
}

void dptf_set_temp_threshold(int sensor_id, int temp, int idx, int enable)
{
	DPRINTS("DPTF sensor %d, threshold %d C, index %d, %sabled", sensor_id,
		K_TO_C(temp), idx, enable ? "en" : "dis");

	if ((sensor_id >= TEMP_SENSOR_COUNT) ||
	    (idx >= DPTF_THRESHOLDS_PER_SENSOR)) {
		CPRINTS("DPTF: Invalid sensor ID");
		return;
	}

	if (enable) {
		/* Don't update threshold condition if already enabled */
		if (dptf_threshold[sensor_id][idx].temp == -1)
			cond_init(&dptf_threshold[sensor_id][idx].over, 0);
		dptf_threshold[sensor_id][idx].temp = temp;
		atomic_clear_bits(&dptf_seen, BIT(sensor_id));
	} else {
		dptf_threshold[sensor_id][idx].temp = -1;
	}
}

/*****************************************************************************/
/* EC-specific thermal controls */

test_mockable_static void smi_sensor_failure_warning(void)
{
	CPRINTS("can't read any temp sensors!");
	host_set_single_event(EC_HOST_EVENT_THERMAL);
}

static void thermal_control_dptf(void)
{
	int i, t, rv;
	int dptf_tripped;
	int num_sensors_read;

	dptf_tripped = 0;
	num_sensors_read = 0;

	/* go through all the sensors */
	for (i = 0; i < TEMP_SENSOR_COUNT; ++i) {
		rv = temp_sensor_read(i, &t);
		if (rv != EC_SUCCESS)
			continue;
		else
			num_sensors_read++;
		/* and check the dptf thresholds */
		dptf_tripped |= dptf_check_temp_threshold(i, t);
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
		if (!chipset_in_state(CHIPSET_STATE_HARD_OFF))
			smi_sensor_failure_warning();
	}

	/* Don't forget to signal any DPTF thresholds */
	if (dptf_tripped)
		host_set_single_event(EC_HOST_EVENT_THERMAL_THRESHOLD);
}

/* Wait until after the sensors have been read */
DECLARE_HOOK(HOOK_SECOND, thermal_control_dptf, HOOK_PRIO_TEMP_SENSOR_DONE);

/*****************************************************************************/
/* Console commands */

static int command_dptftemp(int argc, const char **argv)
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

	ccprintf("AP seen mask: 0x%08x\n", (int)dptf_seen);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(dptftemp, command_dptftemp, NULL,
			"Print DPTF thermal parameters (degrees Kelvin)");
