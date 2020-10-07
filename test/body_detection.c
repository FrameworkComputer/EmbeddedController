/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test body_detection algorithm
 */

#include "accelgyro.h"
#include "body_detection.h"
#include "body_detection_test_data.h"
#include "common.h"
#include "motion_common.h"
#include "motion_sense.h"
#include "test_util.h"
#include "util.h"

static struct motion_sensor_t *sensor = &motion_sensors[BASE];
static const int window_size = 50; /* sensor data rate (Hz) */

static int filler(const struct motion_sensor_t *s, const float v)
{
	int resolution = s->drv->get_resolution(s);
	int range = s->drv->get_range(s);
	int data_1g = BIT(resolution - 1) / range;

	return (int)(v * data_1g / 9.8);
}

static void feed_body_detect_data(const struct body_detect_test_data *array,
				  const int idx)
{
	sensor->xyz[X] = filler(sensor, array[idx].x);
	sensor->xyz[Y] = filler(sensor, array[idx].y);
	sensor->xyz[Z] = filler(sensor, array[idx].z);
}

static int get_trigger_time(const struct body_detect_test_data *data,
			    const size_t size,
			    const enum body_detect_states target_state)
{
	int i, action_index = -1, target_index = -1;

	body_detect_reset();
	/*
	 * Clear on-body state when the window is initialized, so
	 * that we do not need to wait for 15 second if the testcase
	 * is in off-body initially.
	 */
	body_detect_change_state(BODY_DETECTION_OFF_BODY);
	for (i = 0; i < size; ++i) {
		enum body_detect_states motion_state;

		if (data[i].action == 1 && action_index == -1) {
			cprints(CC_ACCEL, "action start");
			action_index = i;
		}
		feed_body_detect_data(data, i);
		/* run the body detect */
		body_detect();
		/* skip if action not start yet */
		if (action_index == -1)
			continue;

		motion_state = body_detect_get_state();
		if (target_index == -1 && motion_state == target_state)
			target_index = i;
	}
	if (target_index == -1)
		return -1;
	return target_index - action_index;
}

static int test_body_detect(void)
{
	int ret, trigger_time;

	ret = sensor->drv->set_data_rate(sensor, window_size * 1000, 0);
	TEST_ASSERT(ret == EC_SUCCESS);

	body_detect_set_enable(true);
	/* Onbody test */
	cprints(CC_ACCEL, "start OnBody test");
	trigger_time = get_trigger_time(kBodyDetectOnBodyTestData,
					kBodyDetectOnBodyTestDataLength,
					BODY_DETECTION_OFF_BODY);
	/* It should not enter off-body state ever */
	TEST_ASSERT(trigger_time == -1);

	/* OffOn test */
	cprints(CC_ACCEL, "start Off to On test");
	trigger_time = get_trigger_time(kBodyDetectOffOnTestData,
					kBodyDetectOffOnTestDataLength,
					BODY_DETECTION_ON_BODY);
	/* It should enter on-body state in 3 seconds */
	TEST_ASSERT(trigger_time >= 0 && trigger_time < 3 * window_size);

	/* OnOff test */
	cprints(CC_ACCEL, "start On to Off test");
	trigger_time = get_trigger_time(kBodyDetectOnOffTestData,
					kBodyDetectOnOffTestDataLength,
					BODY_DETECTION_OFF_BODY);
	/* It should enter off-body state between 15 to 20 seconds */
	TEST_ASSERT(15 * window_size <= trigger_time &&
		    trigger_time < 20 * window_size);

	return EC_SUCCESS;
}


void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_body_detect);

	test_print_result();
}
