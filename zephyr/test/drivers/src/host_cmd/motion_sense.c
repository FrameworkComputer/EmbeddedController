/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "motion_sense.h"
#include "test_state.h"
#include "utils.h"

/**
 * Get the size needed for a struct ec_response_motion_sense
 */
#define RESPONSE_MOTION_SENSE_BUFFER_SIZE(n)       \
	(sizeof(struct ec_response_motion_sense) + \
	 n * sizeof(struct ec_response_motion_sensor_data))

ZTEST_SUITE(host_cmd_motion_sense, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);

ZTEST_USER(host_cmd_motion_sense, test_dump)
{
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(
		ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *result =
		(struct ec_response_motion_sense *)response_buffer;

	/* Set up the motion sensor data */
	for (int i = 0; i < ALL_MOTION_SENSORS; ++i) {
		motion_sensors[i].xyz[0] = i;
		motion_sensors[i].xyz[1] = i + 1;
		motion_sensors[i].xyz[2] = i + 2;
	}
	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS, result);

	zassert_equal(result->dump.module_flags, MOTIONSENSE_MODULE_FLAG_ACTIVE,
		      NULL);
	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS, NULL);

	/*
	 * Test the values returned in the dump. Normally we shouldn't be doing
	 * tests in a loop, but since the number of sensors (as well as the
	 * order) is adjustable by devicetree, it would be too difficult to hard
	 * code here.
	 */
	for (int i = 0; i < ALL_MOTION_SENSORS; ++i) {
		zassert_equal(result->dump.sensor[i].flags,
			      MOTIONSENSE_SENSOR_FLAG_PRESENT, NULL);
		zassert_equal(result->dump.sensor[i].data[0], i, NULL);
		zassert_equal(result->dump.sensor[i].data[1], i + 1, NULL);
		zassert_equal(result->dump.sensor[i].data[2], i + 2, NULL);
	}
}

ZTEST_USER(host_cmd_motion_sense, test_dump__large_max_sensor_count)
{
	uint8_t response_buffer[RESPONSE_MOTION_SENSE_BUFFER_SIZE(
		ALL_MOTION_SENSORS)];
	struct ec_response_motion_sense *result =
		(struct ec_response_motion_sense *)response_buffer;

	host_cmd_motion_sense_dump(ALL_MOTION_SENSORS + 1, result);

	zassert_equal(result->dump.sensor_count, ALL_MOTION_SENSORS, NULL);
}
