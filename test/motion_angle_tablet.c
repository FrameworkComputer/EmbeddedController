/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code, when in tablet mode.
 */

#include "accelgyro.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_common.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "test_util.h"
#include "util.h"

#include <math.h>
#include <stdio.h>

/*****************************************************************************/
/* Test utilities */

/* convert array value from g to m.s^2. */
int filler(const struct motion_sensor_t *s, const float v)
{
	return FP_TO_INT(
		fp_div(FLOAT_TO_FP(v) * MOTION_SCALING_FACTOR,
		       fp_mul(INT_TO_FP(s->current_range), MOTION_ONE_G)));
}

static int test_lid_angle_less180(void)
{
	int index = 0, lid_angle;
	struct motion_sensor_t *lid =
		&motion_sensors[CONFIG_LID_ANGLE_SENSOR_LID];
	struct motion_sensor_t *base =
		&motion_sensors[CONFIG_LID_ANGLE_SENSOR_BASE];

	/* We don't have TASK_CHIP so simulate init ourselves */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S5);
	TEST_ASSERT(lid->drv->get_data_rate(lid) == 0);

	/* Go to S0 state */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	hook_notify(HOOK_CHIPSET_RESUME);
	crec_msleep(1000);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S0);
	TEST_ASSERT(lid->drv->get_data_rate(lid) == TEST_LID_FREQUENCY);

	/* Open lid, testing close to 180 degree. */
	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(1000);

	cprints(CC_ACCEL, "start loop");
	/* Force clamshell mode, to be sure we go in tablet mode ASAP. */
	tablet_set_mode(0, TABLET_TRIGGER_LID);

	/* Check we stay in tablet mode, even when hinge is vertical. */
	while (index < kAccelerometerVerticalHingeTestDataLength) {
		feed_accel_data(kAccelerometerVerticalHingeTestData, &index,
				filler);
		wait_for_valid_sample();
		lid_angle = motion_lid_get_angle();
		cprints(CC_ACCEL, "%d : LID(%d, %d, %d)/BASE(%d, %d, %d): %d",
			index / TEST_LID_SAMPLE_SIZE, lid->xyz[X], lid->xyz[Y],
			lid->xyz[Z], base->xyz[X], base->xyz[Y], base->xyz[Z],
			lid_angle);
		/* We need few sample to debounce and enter laptop mode. */
		TEST_ASSERT(index < 2 * TEST_LID_SAMPLE_SIZE *
					    (TABLET_MODE_DEBOUNCE_COUNT + 2) ||
			    tablet_get_mode());
	}
	/*
	 * Check we stay in tablet mode, even when hinge is vertical and
	 * shaked.
	 */
	tablet_set_mode(0, TABLET_TRIGGER_LID);
	while (index < kAccelerometerVerticalHingeUnstableTestDataLength) {
		feed_accel_data(kAccelerometerVerticalHingeUnstableTestData,
				&index, filler);
		wait_for_valid_sample();
		lid_angle = motion_lid_get_angle();
		cprints(CC_ACCEL, "%d : LID(%d, %d, %d)/BASE(%d, %d, %d): %d",
			index / TEST_LID_SAMPLE_SIZE, lid->xyz[X], lid->xyz[Y],
			lid->xyz[Z], base->xyz[X], base->xyz[Y], base->xyz[Z],
			lid_angle);
		/* We need few sample to debounce and enter laptop mode. */
		TEST_ASSERT(index < TEST_LID_SAMPLE_SIZE *
					    (TABLET_MODE_DEBOUNCE_COUNT + 2) ||
			    tablet_get_mode());
	}
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_lid_angle_less180);

	test_print_result();
}
