/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include "accelgyro.h"
#include "common.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <math.h>
#include <stdio.h>

extern enum chipset_state_mask sensor_active;
extern int wait_us;

/*
 * Period in us for the motion task period.
 * The task will read the vectors at that interval
 */
#define TEST_LID_EC_RATE (10 * MSEC)

/*
 * Time in us to wait for the task to read the vectors.
 */
#define TEST_LID_SLEEP_RATE (TEST_LID_EC_RATE / 5)
#define ONE_G_MEASURED (1 << 14)

/*****************************************************************************/
/* Mock functions */
static int accel_init(struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}

static int accel_read(const struct motion_sensor_t *s, intv3_t v)
{
	rotate(s->xyz, *s->rot_standard_ref, v);
	return EC_SUCCESS;
}

static int accel_set_range(struct motion_sensor_t *s, const int range,
			   const int rnd)
{
	s->current_range = range;
	return EC_SUCCESS;
}

static int accel_get_resolution(const struct motion_sensor_t *s)
{
	return 0;
}

int test_data_rate[2] = { 0 };

static int accel_set_data_rate(const struct motion_sensor_t *s, const int rate,
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
		},
	},
	[LID] = {
		.name = "lid",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KXCJ9,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &test_motion_sense,
		.rot_standard_ref = NULL,
		.default_range = MOTION_SCALING_FACTOR / ONE_G_MEASURED,
		.config = {
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
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/*****************************************************************************/
/* Test utilities */
static void wait_for_valid_sample(void)
{
	uint8_t *lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	uint8_t sample;
	int i;

	for (i = 0; i < 2 * TABLET_MODE_DEBOUNCE_COUNT; i++) {
		sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
		crec_usleep(TEST_LID_EC_RATE);
		while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) ==
		       sample)
			crec_usleep(TEST_LID_SLEEP_RATE);
	}
}

static int tablet_hook_count;

static void tablet_mode_change_hook(void)
{
	tablet_hook_count++;
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, tablet_mode_change_hook,
	     HOOK_PRIO_DEFAULT);

void before_test(void)
{
	/* Make sure the device lid is in a consistent state (close). */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	tablet_hook_count = 1;
}

/*
 * The device lid is closed from before_test,
 * Initialize the EC, set the sensors to match the lid angle (0 degree)
 * and go through several lid angles.
 * When lid angle are close to 0 or 360, activate the GMRs GPIO or not
 * and observe their on lid angle data quality and the tablet mode state.
 */
static int test_lid_angle(void)
{
	struct motion_sensor_t *base =
		&motion_sensors[CONFIG_LID_ANGLE_SENSOR_BASE];
	struct motion_sensor_t *lid =
		&motion_sensors[CONFIG_LID_ANGLE_SENSOR_LID];
	int lid_angle;

	/* We don't have TASK_CHIP so simulate init ourselves */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	/* Wait for the sensor task to start */
	crec_msleep(50);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S5);
	TEST_ASSERT(accel_get_data_rate(lid) == 0);
	TEST_ASSERT(base->collection_rate == 0);
	TEST_ASSERT(lid->collection_rate == 0);
	TEST_ASSERT(wait_us == -1);

	/* Go to S0 state */
	hook_notify(HOOK_CHIPSET_SUSPEND);
	hook_notify(HOOK_CHIPSET_RESUME);
	crec_msleep(50);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S0);
	TEST_ASSERT(accel_get_data_rate(lid) == 119000);
	TEST_ASSERT(base->collection_rate != 0);
	TEST_ASSERT(lid->collection_rate != 0);
	TEST_ASSERT(wait_us > 0);

	/* Check we are in clamshell mode initially. */
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());

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

	/* Check we are still in clamshell mode, no event */
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());

	wait_for_valid_sample();
	lid_angle = motion_lid_get_angle();
	cprints(CC_ACCEL, "LID(%d, %d, %d)/BASE(%d, %d, %d): %d", lid->xyz[X],
		lid->xyz[Y], lid->xyz[Z], base->xyz[X], base->xyz[Y],
		base->xyz[Z], lid_angle);
	TEST_ASSERT(lid_angle == 0);

	/* Set lid open to 90 degrees. */
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED;
	lid->xyz[Z] = 0;
	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(100);
	wait_for_valid_sample();

	TEST_ASSERT(motion_lid_get_angle() == 90);
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());

	/* Set lid open to 225. */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.707106;
	lid->xyz[Z] = ONE_G_MEASURED * 0.707106;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 225);

	/* We are now in tablet mode */
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Set lid open to 350 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 350);

	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Assert tablet GMT sensor, no change. */
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/*
	 * Set lid open to 10.  Since the lid switch still indicates that it's
	 * open, we should be getting an unreliable reading.
	 * We are still in tablet mode.
	 */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Rotate back to 180 and then 10 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = ONE_G_MEASURED;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 180);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/*
	 * Again, since the lid isn't closed, the angle should be unreliable.
	 * See SMALL_LID_ANGLE_RANGE.
	 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/*
	 * Align base with hinge and make sure it returns unreliable for angle.
	 * In this test it doesn't matter what the lid acceleration vector is.
	 */
	base->xyz[X] = ONE_G_MEASURED;
	base->xyz[Y] = 0;
	base->xyz[Z] = 0;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

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
	/* Still in tablet mode */
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

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
	crec_msleep(100);
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 0);
	TEST_ASSERT(tablet_hook_count == 3);
	TEST_ASSERT(!tablet_get_mode());

	/*
	 * Make the angle large, but since the lid is closed, the angle should
	 * be regarded as unreliable.
	 */
	lid->xyz[X] = 0;
	lid->xyz[Y] = -1 * ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);
	TEST_ASSERT(tablet_hook_count == 3);
	TEST_ASSERT(!tablet_get_mode());

	/* Open the lid, the large angle is now valid */
	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(100);
	TEST_ASSERT(motion_lid_get_angle() == 350);
	TEST_ASSERT(tablet_hook_count == 4);
	TEST_ASSERT(tablet_get_mode());

	/*
	 * Close the lid and set the angle
	 * to 10. The reading of small angle shouldn't be corrected.
	 */
	crec_msleep(100);
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(100);
	lid->xyz[X] = 0;
	lid->xyz[Y] = ONE_G_MEASURED * 0.1736;
	lid->xyz[Z] = -1 * ONE_G_MEASURED * 0.9848;
	wait_for_valid_sample();
	TEST_ASSERT(motion_lid_get_angle() == 10);
	TEST_ASSERT(tablet_hook_count == 5);
	TEST_ASSERT(!tablet_get_mode());

	/* Shutdown in place, same mode. */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);
	crec_msleep(1000);
	TEST_ASSERT(sensor_active == SENSOR_ACTIVE_S5);
	/* Base ODR is 0, collection rate is 0. */
	TEST_ASSERT(base->collection_rate == 0);
	/* Lid is powered off, collection rate is 0. */
	TEST_ASSERT(lid->collection_rate == 0);
	TEST_ASSERT(wait_us == -1);
	TEST_ASSERT(tablet_hook_count == 5);
	TEST_ASSERT(!tablet_get_mode());

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_lid_angle);

	test_print_result();
}
