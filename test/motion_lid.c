/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>
#include <stdio.h>

#include "accelgyro.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

extern enum chipset_state_mask sensor_active;

/*
 * Period in us for the motion task period.
 * The task will read the vectors at that interval
 */
#define TEST_LID_EC_RATE (10 * MSEC)

/*
 * Time in ms to wait for the task to read the vectors.
 */
#define TEST_LID_SLEEP_RATE (TEST_LID_EC_RATE / 5)
#define ONE_G_MEASURED (1 << 14)

/*****************************************************************************/
/* Mock functions */
static int accel_init(const struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}

static int accel_read(const struct motion_sensor_t *s, intv3_t v)
{
	rotate(s->xyz, *s->rot_standard_ref, v);
	return EC_SUCCESS;
}

static int accel_set_range(const struct motion_sensor_t *s,
			   const int range,
			   const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_range(const struct motion_sensor_t *s)
{
	return s->default_range;
}

static int accel_get_resolution(const struct motion_sensor_t *s)
{
	return 0;
}

int test_data_rate[2] = { 0 };

static int accel_set_data_rate(const struct motion_sensor_t *s,
			      const int rate,
			      const int rnd)
{
	test_data_rate[s - motion_sensors] = rate;
	return EC_SUCCESS;
}

static int accel_get_data_rate(const struct motion_sensor_t *s)
{
	return test_data_rate[s - motion_sensors];
}

const struct accelgyro_drv test_motion_sense = {
	.init = accel_init,
	.read = accel_read,
	.set_range = accel_set_range,
	.get_range = accel_get_range,
	.get_resolution = accel_get_resolution,
	.set_data_rate = accel_set_data_rate,
	.get_data_rate = accel_get_data_rate,
};

struct motion_sensor_t motion_sensors[] = {
	[BASE] = {
		.name = "base",
		.active_mask = SENSOR_ACTIVE_S0_S3_S5,
		.chip = MOTIONSENSE_CHIP_LSM6DS0,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &test_motion_sense,
		.rot_standard_ref = NULL,
		.default_range = MOTION_SCALING_FACTOR / ONE_G_MEASURED,
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 119000 | ROUND_UP_FLAG,
				.ec_rate = TEST_LID_EC_RATE
			},
			/* Used for double tap */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 119000 | ROUND_UP_FLAG,
				.ec_rate = TEST_LID_EC_RATE * 100,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
			},
		},
	},
	[LID] = {
		.name = "lid",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_KXCJ9,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &test_motion_sense,
		.rot_standard_ref = NULL,
		.default_range = MOTION_SCALING_FACTOR / ONE_G_MEASURED,
		.config = {
			/* AP: by default shutdown all sensors */
			[SENSOR_CONFIG_AP] = {
				.odr = 0,
				.ec_rate = 0,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 119000 | ROUND_UP_FLAG,
				.ec_rate = TEST_LID_EC_RATE,
			},
			/* Used for double tap */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 200000 | ROUND_UP_FLAG,
				.ec_rate = TEST_LID_EC_RATE * 100,
			},
			[SENSOR_CONFIG_EC_S5] = {
				.odr = 0,
				.ec_rate = 0,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/*****************************************************************************/
/* Test utilities */
static void wait_for_valid_sample(void)
{
	uint8_t sample;
	uint8_t *lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);

	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	usleep(TEST_LID_EC_RATE);
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		usleep(TEST_LID_SLEEP_RATE);
}

static int test_lid_angle(void)
{

	struct motion_sensor_t *base = &motion_sensors[
		CONFIG_LID_ANGLE_SENSOR_BASE];
	struct motion_sensor_t *lid = &motion_sensors[
		CONFIG_LID_ANGLE_SENSOR_LID];
	int lid_angle;

	/* We don't have TASK_CHIP so simulate init ourselves */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S5);
	TEST_ASSERT(accel_get_data_rate(lid) == 0);

	/* Go to S0 state */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	hook_notify(HOOK_CHIPSET_RESUME);
	msleep(1000);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S0);
	TEST_ASSERT(accel_get_data_rate(lid) == 119000);

	/*
	 * Set the base accelerometer as if it were sitting flat on a desk
	 * and set the lid to closed.
	 */
	base->xyz[X] = 0;
	base->xyz[Y] = 0;
	base->xyz[Z] = ONE_G_MEASURED;
	lid->xyz[X] = 0;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = -ONE_G_MEASURED;
	gpio_set_level(GPIO_LID_OPEN, 0);
	/* Initial wake up, like init does */
	task_wake(TASK_ID_MOTIONSENSE);

	/* wait for the EC sampling period to expire   */
	msleep(TEST_LID_EC_RATE);
	task_wake(TASK_ID_MOTIONSENSE);

	wait_for_valid_sample();
	lid_angle = motion_lid_get_angle();
	cprints(CC_ACCEL, "LID(%d, %d, %d)/BASE(%d, %d, %d): %d",
			lid->xyz[X], lid->xyz[Y], lid->xyz[Z],
			base->xyz[X], base->xyz[Y], base->xyz[Z],
			lid_angle);
	TEST_ASSERT(lid_angle == 0);

	/* Set lid open to 90 degrees. */
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED;
	lid->xyz[Z] = 0;
	gpio_set_level(GPIO_LID_OPEN, 1);
	msleep(100);
	wait_for_valid_sample();

	TEST_ASSERT(motion_lid_get_angle() == 90);

	/* Set lid open to 225. */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.707106;
	lid->xyz[Z] = ONE_G_MEASURED * 0.707106;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 225);

	/* Set lid open to 350 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 350);

	/*
	 * Set lid open to 10.  Since the lid switch still indicates that it's
	 * open, we should be getting an unreliable reading.
	 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);

	/* Rotate back to 180 and then 10 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = ONE_G_MEASURED;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 180);

	/*
	 * Again, since the lid isn't closed, the angle should be unreliable.
	 * See SMALL_LID_ANGLE_RANGE.
	 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);

	/*
	 * Align base with hinge and make sure it returns unreliable for angle.
	 * In this test it doesn't matter what the lid acceleration vector is.
	 */
	base->xyz[X] = ONE_G_MEASURED;
	base->xyz[Y] = 0;
	base->xyz[Z] = 0;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);

	/*
	 * Use all three axes and set lid to negative base and make sure
	 * angle is 180.
	 */
	base->xyz[X] = 5296;
	base->xyz[Y] = 7856;
	base->xyz[Z] = 13712;
	lid->xyz[X] = 5296;
	lid->xyz[Y] = 7856;
	lid->xyz[Z] = 13712;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 180);

	/*
	 * Close the lid and set the angle to 0.
	 */
	base->xyz[X] = 0;
	base->xyz[Y] = 0;
	base->xyz[Z] = ONE_G_MEASURED;
	lid->xyz[X] = 0;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = -1 * ONE_G_MEASURED;
	gpio_set_level(GPIO_LID_OPEN, 0);
	msleep(100);
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 0);

	/*
	 * Make the angle large, but since the lid is closed, the angle should
	 * be regarded as unreliable.
	 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);

	/*
	 * Open the lid to 350, and then close the lid and set the angle
	 * to 10. The reading of small angle shouldn't be corrected.
	 */
	gpio_set_level(GPIO_LID_OPEN, 1);
	msleep(100);
	gpio_set_level(GPIO_LID_OPEN, 0);
	msleep(100);
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 10);

	return EC_SUCCESS;
}


void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_lid_angle);

	test_print_result();
}
