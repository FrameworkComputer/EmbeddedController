/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "hwtimer.h"
#include "online_calibration.h"
#include "common.h"

/** Entry of the temperature cache */
struct temp_cache_entry {
	/** The temperature that's cached (-1 if invalid) */
	int temp;
	/** The timestamp at which the temperature was cached */
	uint32_t timestamp;
};

/** Cache for internal sensor temperatures. */
static struct temp_cache_entry sensor_temp_cache[SENSOR_COUNT];

static int get_temperature(struct motion_sensor_t *sensor, int *temp)
{
	struct temp_cache_entry *entry =
		&sensor_temp_cache[motion_sensors - sensor];
	uint32_t now;

	if (sensor->drv->read_temp == NULL)
		return EC_ERROR_UNIMPLEMENTED;

	now = __hw_clock_source_read();
	if (entry->temp < 0 ||
	    time_until(entry->timestamp, now) > CONFIG_TEMP_CACHE_STALE_THRES) {
		int t;
		int rc = sensor->drv->read_temp(sensor, &t);

		if (rc == EC_SUCCESS) {
			entry->temp = t;
			entry->timestamp = now;
		} else {
			return rc;
		}
	}

	*temp = entry->temp;
	return EC_SUCCESS;
}

void online_calibration_init(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sensor_temp_cache); i++)
		sensor_temp_cache[i].temp = -1;
}

int online_calibration_process_data(
	struct ec_response_motion_sensor_data *data,
	struct motion_sensor_t *sensor,
	uint32_t timestamp)
{
	int rc;
	int temperature;

	rc = get_temperature(sensor, &temperature);
	/*
	 * TODO actual implementation will come in following CLs.
	 * The goal of this change is to establish the interface of
	 * online calibration with the rest of the code base.
	 */
	return rc;
}

