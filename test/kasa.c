/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "kasa.h"
#include "test_util.h"
#include "motion_sense.h"
#include <stdio.h>

struct motion_sensor_t motion_sensors[] = {};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static int test_kasa_reset(void)
{
	struct kasa_fit kasa;

	kasa_reset(&kasa);

	TEST_EQ(kasa.nsamples, 0, "%u");
	TEST_NEAR(kasa.acc_x, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_y, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_z, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_w, 0.0f, 0.000001f, "%f");

	TEST_NEAR(kasa.acc_xx, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_xy, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_xz, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_xw, 0.0f, 0.000001f, "%f");

	TEST_NEAR(kasa.acc_yy, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_yz, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_yw, 0.0f, 0.000001f, "%f");

	TEST_NEAR(kasa.acc_zz, 0.0f, 0.000001f, "%f");
	TEST_NEAR(kasa.acc_zw, 0.0f, 0.000001f, "%f");

	return EC_SUCCESS;
}

static int test_kasa_calculate(void)
{
	struct kasa_fit kasa;
	fpv3_t bias;
	float radius;

	kasa_reset(&kasa);
	kasa_accumulate(&kasa, 1.01f, 0.01f, 0.01f);
	kasa_accumulate(&kasa, -0.99f, 0.01f, 0.01f);
	kasa_accumulate(&kasa, 0.01f, 1.01f, 0.01f);
	kasa_accumulate(&kasa, 0.01f, -0.99f, 0.01f);
	kasa_accumulate(&kasa, 0.01f, 0.01f, 1.01f);
	kasa_accumulate(&kasa, 0.01f, 0.01f, -0.99f);
	kasa_compute(&kasa, bias, &radius);

	TEST_NEAR(bias[0], 0.01f, 0.0001f, "%f");
	TEST_NEAR(bias[1], 0.01f, 0.0001f, "%f");
	TEST_NEAR(bias[2], 0.01f, 0.0001f, "%f");
	TEST_NEAR(radius, 1.0f, 0.0001f, "%f");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_kasa_reset);
	RUN_TEST(test_kasa_calculate);

	test_print_result();
}
