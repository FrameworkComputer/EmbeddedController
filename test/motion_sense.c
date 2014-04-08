/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test motion sense code.
 */

#include <math.h>

#include "common.h"
#include "motion_sense.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"

/* Mock acceleration values for motion sense task to read in. */
int mock_x_acc[ACCEL_COUNT], mock_y_acc[ACCEL_COUNT], mock_z_acc[ACCEL_COUNT];

/*****************************************************************************/
/* Mock functions */

int accel_init(enum accel_id id)
{
	return EC_SUCCESS;
}

int accel_read(enum accel_id id, int *x_acc, int *y_acc, int *z_acc)
{
	/* Return the mock values. */
	*x_acc = mock_x_acc[id];
	*y_acc = mock_y_acc[id];
	*z_acc = mock_z_acc[id];

	return EC_SUCCESS;
}

int accel_set_range(const enum accel_id id, const int range, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_range(const enum accel_id id, int * const range)
{
	return EC_SUCCESS;
}
int accel_set_resolution(const enum accel_id id, const int res, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_resolution(const enum accel_id id, int * const res)
{
	return EC_SUCCESS;
}
int accel_set_datarate(const enum accel_id id, const int rate, const int rnd)
{
	return EC_SUCCESS;
}
int accel_get_datarate(const enum accel_id id, int * const rate)
{
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Test utilities */
static int test_lid_angle(void)
{
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
	task_wake(TASK_ID_MOTIONSENSE);
	msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 0);

	/* Set lid open to 90 degrees. */
	mock_x_acc[ACCEL_LID] = -1000;
	mock_y_acc[ACCEL_LID] = 0;
	mock_z_acc[ACCEL_LID] = 0;
	task_wake(TASK_ID_MOTIONSENSE);
	msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 90);

	/* Set lid open to 225. */
	mock_x_acc[ACCEL_LID] = 500;
	mock_y_acc[ACCEL_LID] = 0;
	mock_z_acc[ACCEL_LID] = -500;
	task_wake(TASK_ID_MOTIONSENSE);
	msleep(5);
	TEST_ASSERT(motion_get_lid_angle() == 225);

	/*
	 * Align base with hinge and make sure it returns unreliable for angle.
	 * In this test it doesn't matter what the lid acceleration vector is.
	 */
	mock_x_acc[ACCEL_BASE] = 0;
	mock_y_acc[ACCEL_BASE] = 1000;
	mock_z_acc[ACCEL_BASE] = 0;
	task_wake(TASK_ID_MOTIONSENSE);
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
	task_wake(TASK_ID_MOTIONSENSE);
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
