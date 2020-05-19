/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "newton_fit.h"
#include "motion_sense.h"
#include "test_util.h"
#include <stdio.h>

/*
 * Need to define motion sensor globals just to compile.
 * We include motion task to force the inclusion of math_util.c
 */
struct motion_sensor_t motion_sensors[] = {};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

#define ACC(FIT, X, Y, Z, EXPECTED) \
	TEST_EQ(newton_fit_accumulate(FIT, X, Y, Z), EXPECTED, "%d")

static int test_newton_fit_reset(void)
{
	struct newton_fit fit = NEWTON_FIT(4, 15, 0.01f, 0.25f, 1.0e-8f, 100);

	newton_fit_reset(&fit);
	newton_fit_accumulate(&fit, 1.0f, 0.0f, 0.0f);
	TEST_EQ(queue_count(fit.orientations), (size_t)1, "%zu");
	newton_fit_reset(&fit);

	TEST_EQ(queue_count(fit.orientations), (size_t)0, "%zu");

	return EC_SUCCESS;
}

static int test_newton_fit_accumulate(void)
{
	struct newton_fit fit = NEWTON_FIT(4, 15, 0.01f, 0.25f, 1.0e-8f, 100);
	struct queue_iterator it;

	newton_fit_reset(&fit);
	newton_fit_accumulate(&fit, 1.0f, 0.0f, 0.0f);

	TEST_EQ(queue_count(fit.orientations), (size_t)1, "%zu");
	queue_begin(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 1, "%u");

	return EC_SUCCESS;
}

static int test_newton_fit_accumulate_merge(void)
{
	struct newton_fit fit = NEWTON_FIT(4, 15, 0.01f, 0.25f, 1.0e-8f, 100);
	struct queue_iterator it;

	newton_fit_reset(&fit);
	newton_fit_accumulate(&fit, 1.0f, 0.0f, 0.0f);
	newton_fit_accumulate(&fit, 1.05f, 0.0f, 0.0f);

	TEST_EQ(queue_count(fit.orientations), (size_t)1, "%zu");
	queue_begin(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 2, "%u");

	return EC_SUCCESS;
}

static int test_newton_fit_accumulate_prune(void)
{
	struct newton_fit fit = NEWTON_FIT(4, 15, 0.01f, 0.25f, 1.0e-8f, 100);
	struct queue_iterator it;

	newton_fit_reset(&fit);
	newton_fit_accumulate(&fit, 1.0f, 0.0f, 0.0f);
	newton_fit_accumulate(&fit, -1.0f, 0.0f, 0.0f);
	newton_fit_accumulate(&fit, 0.0f, 1.0f, 0.0f);
	newton_fit_accumulate(&fit, 0.0f, -1.0f, 0.0f);

	TEST_EQ(queue_is_full(fit.orientations), 1, "%d");
	queue_begin(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 1, "%u");
	queue_next(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 1, "%u");
	queue_next(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 1, "%u");
	queue_next(fit.orientations, &it);
	TEST_EQ(((struct newton_fit_orientation *)it.ptr)->nsamples, 1, "%u");

	newton_fit_accumulate(&fit, 0.0f, 0.0f, 1.0f);
	TEST_EQ(queue_is_full(fit.orientations), 0, "%d");

	return EC_SUCCESS;
}

static int test_newton_fit_calculate(void)
{
	struct newton_fit fit = NEWTON_FIT(4, 3, 0.01f, 0.25f, 1.0e-8f, 100);
	floatv3_t bias;
	float radius;

	newton_fit_reset(&fit);

	ACC(&fit, 1.01f, 0.01f, 0.01f, false);
	ACC(&fit, 1.01f, 0.01f, 0.01f, false);
	ACC(&fit, 1.01f, 0.01f, 0.01f, false);

	ACC(&fit, -0.99f, 0.01f, 0.01f, false);
	ACC(&fit, -0.99f, 0.01f, 0.01f, false);
	ACC(&fit, -0.99f, 0.01f, 0.01f, false);

	ACC(&fit, 0.01f, 1.01f, 0.01f, false);
	ACC(&fit, 0.01f, 1.01f, 0.01f, false);
	ACC(&fit, 0.01f, 1.01f, 0.01f, false);

	ACC(&fit, 0.01f, 0.01f, 1.01f, false);
	ACC(&fit, 0.01f, 0.01f, 1.01f, false);
	ACC(&fit, 0.01f, 0.01f, 1.01f, true);

	fpv3_init(bias, 0.0f, 0.0f, 0.0f);
	newton_fit_compute(&fit, bias, &radius);

	TEST_NEAR(bias[0], 0.01f, 0.0001f, "%f");
	TEST_NEAR(bias[1], 0.01f, 0.0001f, "%f");
	TEST_NEAR(bias[2], 0.01f, 0.0001f, "%f");
	TEST_NEAR(radius, 1.0f, 0.0001f, "%f");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_newton_fit_reset);
	RUN_TEST(test_newton_fit_accumulate);
	RUN_TEST(test_newton_fit_accumulate_merge);
	RUN_TEST(test_newton_fit_accumulate_prune);
	RUN_TEST(test_newton_fit_calculate);

	test_print_result();
}
