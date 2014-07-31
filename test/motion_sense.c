/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>

#include "accelgyro.h"
#include "common.h"
#include "host_command.h"
#include "motion_sense.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

/* Mock acceleration values for motion sense task to read in. */
int mock_x_acc[ACCEL_COUNT], mock_y_acc[ACCEL_COUNT], mock_z_acc[ACCEL_COUNT];

/*****************************************************************************/
/* Mock functions */

static int accel_init(void *drv_data, int i2c_addr)
{
	return EC_SUCCESS;
}

static int accel_read_base(void *drv_data, int *x_acc, int *y_acc, int *z_acc)
{
	/* Return the mock values. */
	*x_acc = mock_x_acc[ACCEL_BASE];
	*y_acc = mock_y_acc[ACCEL_BASE];
	*z_acc = mock_z_acc[ACCEL_BASE];

	return EC_SUCCESS;
}

static int accel_read_lid(void *drv_data, int *x_acc, int *y_acc, int *z_acc)
{
	/* Return the mock values. */
	*x_acc = mock_x_acc[ACCEL_LID];
	*y_acc = mock_y_acc[ACCEL_LID];
	*z_acc = mock_z_acc[ACCEL_LID];

	return EC_SUCCESS;
}

static int accel_set_range(void *drv_data,
			   const int range,
			   const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_range(void *drv_data,
			   int * const range)
{
	return EC_SUCCESS;
}

static int accel_set_resolution(void *drv_data,
				const int res,
				const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_resolution(void *drv_data,
				int * const res)
{
	return EC_SUCCESS;
}

static int accel_set_datarate(void *drv_data,
			      const int rate,
			      const int rnd)
{
	return EC_SUCCESS;
}

static int accel_get_datarate(void *drv_data,
			      int * const rate)
{
	return EC_SUCCESS;
}

const struct accelgyro_info test_motion_sense_base = {
	.chip_type = CHIP_TEST,
	.sensor_type = SENSOR_ACCELEROMETER,
	.init = accel_init,
	.read = accel_read_base,
	.set_range = accel_set_range,
	.get_range = accel_get_range,
	.set_resolution = accel_set_resolution,
	.get_resolution = accel_get_resolution,
	.set_datarate = accel_set_datarate,
	.get_datarate = accel_get_datarate,
};

const struct accelgyro_info test_motion_sense_lid = {
	.chip_type = CHIP_TEST,
	.sensor_type = SENSOR_ACCELEROMETER,
	.init = accel_init,
	.read = accel_read_lid,
	.set_range = accel_set_range,
	.get_range = accel_get_range,
	.set_resolution = accel_set_resolution,
	.get_resolution = accel_get_resolution,
	.set_datarate = accel_set_datarate,
	.get_datarate = accel_get_datarate,
};

const struct motion_sensor_t motion_sensors[] = {
	{"test base sensor", LOCATION_BASE, &test_motion_sense_base, NULL, 0},
	{"test lid sensor", LOCATION_LID, &test_motion_sense_lid, NULL, 0},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/*****************************************************************************/
/* Test utilities */
static int test_lid_angle(void)
{
	uint8_t *lpc_status = host_get_memmap(EC_MEMMAP_ACC_STATUS);
	uint8_t sample;

	/*
	 * Set the base accelerometer as if it were sitting flat on a desk
	 * and set the lid to closed.
	 */
	mock_x_acc[ACCEL_BASE] = 0;
	mock_y_acc[ACCEL_BASE] = 0;
	mock_z_acc[ACCEL_BASE] = 1000;
	mock_x_acc[ACCEL_LID] = 0;
	mock_y_acc[ACCEL_LID] = 0;
	mock_z_acc[ACCEL_LID] = 1000;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 0);

	/* Set lid open to 90 degrees. */
	mock_x_acc[ACCEL_LID] = -1000;
	mock_y_acc[ACCEL_LID] = 0;
	mock_z_acc[ACCEL_LID] = 0;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 90);

	/* Set lid open to 225. */
	mock_x_acc[ACCEL_LID] = 500;
	mock_y_acc[ACCEL_LID] = 0;
	mock_z_acc[ACCEL_LID] = -500;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 225);

	/*
	 * Align base with hinge and make sure it returns unreliable for angle.
	 * In this test it doesn't matter what the lid acceleration vector is.
	 */
	mock_x_acc[ACCEL_BASE] = 0;
	mock_y_acc[ACCEL_BASE] = 1000;
	mock_z_acc[ACCEL_BASE] = 0;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == LID_ANGLE_UNRELIABLE);

	/*
	 * Use all three axes and set lid to negative base and make sure
	 * angle is 180.
	 */
	mock_x_acc[ACCEL_BASE] = 500;
	mock_y_acc[ACCEL_BASE] = 400;
	mock_z_acc[ACCEL_BASE] = 300;
	mock_x_acc[ACCEL_LID] = -500;
	mock_y_acc[ACCEL_LID] = -400;
	mock_z_acc[ACCEL_LID] = -300;
	sample = *lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK;
	task_wake(TASK_ID_MOTIONSENSE);
	while ((*lpc_status & EC_MEMMAP_ACC_STATUS_SAMPLE_ID_MASK) == sample)
		msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 180);

	return EC_SUCCESS;
}


void run_test(void)
{
	test_reset();

	RUN_TEST(test_lid_angle);

	test_print_result();
}
