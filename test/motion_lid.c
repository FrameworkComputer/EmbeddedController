/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>
#include <stdio.h>

#include "accelgyro.h"
#include "common.h"
#include "hooks.h"
#include "host_command.h"
#include "motion_lid.h"
#include "motion_sense.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/*****************************************************************************/
/* Mock functions */
static int accel_init(const struct motion_sensor_t *s)
{
	return EC_SUCCESS;
}

static int accel_read(const struct motion_sensor_t *s, vector_3_t v)
{
	v[X] = s->xyz[X];
	v[Y] = s->xyz[Y];
	v[Z] = s->xyz[Z];
	return EC_SUCCESS;
}

static int accel_set_range(const struct motion_sensor_t *s,
			   const int range,
			   const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_range(const struct motion_sensor_t *s,
			   int * const range)
{
	return EC_SUCCESS;
}

static int accel_set_resolution(const struct motion_sensor_t *s,
				const int res,
				const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_resolution(const struct motion_sensor_t *s,
				int * const res)
{
	return EC_SUCCESS;
}

static int accel_set_data_rate(const struct motion_sensor_t *s,
			      const int rate,
			      const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_data_rate(const struct motion_sensor_t *s,
			      int * const rate)
{
	return EC_SUCCESS;
}

const struct accelgyro_drv test_motion_sense = {
	.init = accel_init,
	.read = accel_read,
	.set_range = accel_set_range,
	.get_range = accel_get_range,
	.set_resolution = accel_set_resolution,
	.get_resolution = accel_get_resolution,
	.set_data_rate = accel_set_data_rate,
	.get_data_rate = accel_get_data_rate,
};

const matrix_3x3_t base_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

const matrix_3x3_t lid_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	{.name = "base",
	 .active_mask = SENSOR_ACTIVE_S0_S3_S5,
	 .chip = MOTIONSENSE_CHIP_LSM6DS0,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_BASE,
	 .drv = &test_motion_sense,
	 .mutex = NULL,
	 .drv_data = NULL,
	 .i2c_addr = 0,
	 .rot_standard_ref = &base_standard_ref,
	 .default_config = {
		 .odr = 119000,
		 .range = 2
	 }
	},
	{.name = "base",
	 .active_mask = SENSOR_ACTIVE_S0,
	 .chip = MOTIONSENSE_CHIP_KXCJ9,
	 .type = MOTIONSENSE_TYPE_ACCEL,
	 .location = MOTIONSENSE_LOC_LID,
	 .drv = &test_motion_sense,
	 .mutex = NULL,
	 .drv_data = NULL,
	 .i2c_addr = 0,
	 .rot_standard_ref = &lid_standard_ref,
	 .default_config = {
		 .odr = 119000,
		 .range = 2
	 }
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/*****************************************************************************/
/* Test utilities */
static int test_lid_angle(void)
{
	uint8_t *lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	uint8_t sample;

	struct motion_sensor_t *base = &motion_sensors[0];
	struct motion_sensor_t *lid = &motion_sensors[1];

	/* Go to S3 state */
	hook_notify(HOOK_CHIPSET_STARTUP);

	/* Go to S0 state */
	hook_notify(HOOK_CHIPSET_RESUME);

	/*
	 * Set the base accelerometer as if it were sitting flat on a desk
	 * and set the lid to closed.
	 */
	base->xyz[X] = 0;
	base->xyz[Y] = 0;
	base->xyz[Z] = 1000;
	lid->xyz[X] = 0;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = 1000;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_lid_get_angle() == 0);

	/* Set lid open to 90 degrees. */
	lid->xyz[X] = -1000;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = 0;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_lid_get_angle() == 90);

	/* Set lid open to 225. */
	lid->xyz[X] = 500;
	lid->xyz[Y] = 0;
	lid->xyz[Z] = -500;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_lid_get_angle() == 225);

	/*
	 * Align base with hinge and make sure it returns unreliable for angle.
	 * In this test it doesn't matter what the lid acceleration vector is.
	 */
	base->xyz[X] = 0;
	base->xyz[Y] = 1000;
	base->xyz[Z] = 0;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_lid_get_angle() == LID_ANGLE_UNRELIABLE);

	/*
	 * Use all three axes and set lid to negative base and make sure
	 * angle is 180.
	 */
	base->xyz[X] = 500;
	base->xyz[Y] = 400;
	base->xyz[Z] = 300;
	lid->xyz[X] = -500;
	lid->xyz[Y] = -400;
	lid->xyz[Z] = -300;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_lid_get_angle() == 180);

	return EC_SUCCESS;
}


void run_test(void)
{
	test_reset();

	RUN_TEST(test_lid_angle);

	test_print_result();
}
