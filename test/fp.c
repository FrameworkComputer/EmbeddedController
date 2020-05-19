/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Explicitly include common.h to populate predefined macros in test_config.h
 * early. e.g. CONFIG_FPU, which is needed in math_util.h
 */
#include "common.h"

#include "mat33.h"
#include "mat44.h"
#include "math_util.h"
#include "test_util.h"
#include "vec3.h"

#if defined(TEST_FP) && !defined(CONFIG_FPU)
#define NORM_TOLERANCE FLOAT_TO_FP(0.01f)
#define NORM_SQUARED_TOLERANCE FLOAT_TO_FP(0.0f)
#define DOT_TOLERANCE FLOAT_TO_FP(0.001f)
#define SCALAR_MUL_TOLERANCE FLOAT_TO_FP(0.005f)
#define EIGENBASIS_TOLERANCE FLOAT_TO_FP(0.03f)
#define LUP_TOLERANCE FLOAT_TO_FP(0.0005f)
#define SOLVE_TOLERANCE FLOAT_TO_FP(0.0005f)
#elif defined(TEST_FLOAT) && defined(CONFIG_FPU)
#define NORM_TOLERANCE FLOAT_TO_FP(0.0f)
#define NORM_SQUARED_TOLERANCE FLOAT_TO_FP(0.0f)
#define DOT_TOLERANCE FLOAT_TO_FP(0.0f)
#define SCALAR_MUL_TOLERANCE FLOAT_TO_FP(0.005f)
#define EIGENBASIS_TOLERANCE FLOAT_TO_FP(0.02f)
#define LUP_TOLERANCE FLOAT_TO_FP(0.0f)
#define SOLVE_TOLERANCE FLOAT_TO_FP(0.0f)
#else
#error "No such test configuration."
#endif

#define IS_FPV3_VECTOR_EQUAL(a, b, diff)                                       \
	(IS_FP_EQUAL((a)[0], (b)[0], (diff)) &&                                \
	 IS_FP_EQUAL((a)[1], (b)[1], (diff)) &&                                \
	 IS_FP_EQUAL((a)[2], (b)[2], (diff)))
#define IS_FP_EQUAL(a, b, diff) ((a) >= ((b)-diff) && (a) <= ((b) + diff))
#define IS_FLOAT_EQUAL(a, b, diff) IS_FP_EQUAL(a, b, diff)

static int test_fpv3_scalar_mul(void)
{
	const int N = 3;
	const float s = 2.0f;
	floatv3_t r = {1.0f, 2.0f, 4.0f};
	/* Golden result g = s * r; */
	const floatv3_t g = {2.0f, 4.0f, 8.0f};
	int i;
	fpv3_t a;

	for (i = 0; i < N; ++i)
		a[i] = FLOAT_TO_FP(r[i]);

	fpv3_scalar_mul(a, FLOAT_TO_FP(s));

	for (i = 0; i < N; ++i)
		TEST_ASSERT(IS_FP_EQUAL(a[i], FLOAT_TO_FP(g[i]), 0));

	return EC_SUCCESS;
}

static int test_fpv3_dot(void)
{
	const int N = 3;
	int i;
	floatv3_t a = {1.8f, 2.12f, 4.12f};
	floatv3_t b = {3.1f, 4.3f, 5.8f};
	/* Golden result g = dot(a, b) */
	float g = 38.592f;
	fpv3_t fpa, fpb;

	for (i = 0; i < N; ++i) {
		fpa[i] = FLOAT_TO_FP(a[i]);
		fpb[i] = FLOAT_TO_FP(b[i]);
	}

	TEST_ASSERT(IS_FP_EQUAL(fpv3_dot(fpa, fpb), FLOAT_TO_FP(g),
				DOT_TOLERANCE));

	return EC_SUCCESS;
}

static int test_fpv3_norm_squared(void)
{
	const int N = 3;
	int i;
	floatv3_t a = {3.0f, 4.0f, 5.0f};
	/* Golden result g = norm_squared(a) */
	float g = 50.0f;
	fpv3_t fpa;

	for (i = 0; i < N; ++i)
		fpa[i] = FLOAT_TO_FP(a[i]);

	TEST_ASSERT(IS_FP_EQUAL(fpv3_norm_squared(fpa), FLOAT_TO_FP(g),
				NORM_SQUARED_TOLERANCE));

	return EC_SUCCESS;
}

static int test_fpv3_norm(void)
{
	const int N = 3;
	floatv3_t a = {3.1f, 4.2f, 5.3f};
	/* Golden result g = norm(a) */
	float g = 7.439085483551025390625f;
	int i;
	fpv3_t fpa;

	for (i = 0; i < N; ++i)
		fpa[i] = FLOAT_TO_FP(a[i]);

	TEST_ASSERT(
		IS_FP_EQUAL(fpv3_norm(fpa), FLOAT_TO_FP(g), NORM_TOLERANCE));

	return EC_SUCCESS;
}

static int test_mat33_fp_init_zero(void)
{
	const int N = 3;
	int i, j;
	mat33_fp_t a;

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j)
			a[i][j] = FLOAT_TO_FP(55.66f);

	mat33_fp_init_zero(a);

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j)
			TEST_ASSERT(a[i][j] == FLOAT_TO_FP(0.0f));

	return EC_SUCCESS;
}

static int test_mat33_fp_init_diagonal(void)
{
	const int N = 3;
	int i, j;
	mat33_fp_t a;
	fp_t v = FLOAT_TO_FP(-3.45f);

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j)
			a[i][j] = FLOAT_TO_FP(55.66f);

	mat33_fp_init_diagonal(a, v);

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j) {
			if (i == j)
				TEST_ASSERT(a[i][j] == v);
			else
				TEST_ASSERT(a[i][j] == FLOAT_TO_FP(0.0f));
		}

	return EC_SUCCESS;
}

static int test_mat33_fp_scalar_mul(void)
{
	const int N = 3;
	float scale = 3.11f;
	mat33_float_t a = {
		{1.0f, 2.0f, 3.0f},
		{1.1f, 2.2f, 3.3f},
		{0.38f, 13.2f, 88.3f}
	};
	/* Golden result g = scalar_mul(a, scale) */
	mat33_float_t g = {{3.11f, 6.22f, 9.33f},
			   {3.421f, 6.842f, 10.263f},
			   {1.18179988861083984375f, 41.051998138427734375f,
			    274.613006591796875f}
	};
	int i, j;
	mat33_fp_t fpa;

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j)
			fpa[i][j] = FLOAT_TO_FP(a[i][j]);

	mat33_fp_scalar_mul(fpa, FLOAT_TO_FP(scale));

	for (i = 0; i < N; ++i)
		for (j = 0; j < N; ++j)
			TEST_ASSERT(IS_FP_EQUAL(fpa[i][j], FLOAT_TO_FP(g[i][j]),
						SCALAR_MUL_TOLERANCE));

	return EC_SUCCESS;
}

static int test_mat33_fp_get_eigenbasis(void)
{
	mat33_fp_t s = {
		{FLOAT_TO_FP(4.0f), FLOAT_TO_FP(2.0f), FLOAT_TO_FP(2.0f)},
		{FLOAT_TO_FP(2.0f), FLOAT_TO_FP(4.0f), FLOAT_TO_FP(2.0f)},
		{FLOAT_TO_FP(2.0f), FLOAT_TO_FP(2.0f), FLOAT_TO_FP(4.0f)}
	};
	fpv3_t e_vals;
	mat33_fp_t e_vecs;
	int i, j;

	/* Golden result from float version. */
	mat33_fp_t gold_vecs = {
		{FLOAT_TO_FP(0.55735206f), FLOAT_TO_FP(0.55735206f),
		 FLOAT_TO_FP(0.55735206f)},
		{FLOAT_TO_FP(0.70710677f), FLOAT_TO_FP(-0.70710677f),
		 FLOAT_TO_FP(0.0f)},
		{FLOAT_TO_FP(-0.40824828f), FLOAT_TO_FP(-0.40824828f),
		 FLOAT_TO_FP(0.81649655f)}
	};
	fpv3_t gold_vals = {FLOAT_TO_FP(8.0f), FLOAT_TO_FP(2.0f),
			    FLOAT_TO_FP(2.0f)};

	mat33_fp_get_eigenbasis(s, e_vals, e_vecs);

	for (i = 0; i < 3; ++i) {
		TEST_ASSERT(IS_FP_EQUAL(gold_vals[i], e_vals[i],
					EIGENBASIS_TOLERANCE));
		for (j = 0; j < 3; ++j) {
			TEST_ASSERT(IS_FP_EQUAL(gold_vecs[i][j], e_vecs[i][j],
						EIGENBASIS_TOLERANCE));
		}
	}

	return EC_SUCCESS;
}

static int test_mat44_fp_decompose_lup(void)
{
	int i, j;
	sizev4_t pivot;
	mat44_fp_t fpa = {
		{FLOAT_TO_FP(11.0f), FLOAT_TO_FP(9.0f),
		 FLOAT_TO_FP(24.0f), FLOAT_TO_FP(2.0f)},
		{FLOAT_TO_FP(1.0f), FLOAT_TO_FP(5.0f),
		 FLOAT_TO_FP(2.0f), FLOAT_TO_FP(6.0f)},
		{FLOAT_TO_FP(3.0f), FLOAT_TO_FP(17.0f),
		 FLOAT_TO_FP(18.0f), FLOAT_TO_FP(1.0f)},
		{FLOAT_TO_FP(2.0f), FLOAT_TO_FP(5.0f),
		 FLOAT_TO_FP(7.0f), FLOAT_TO_FP(1.0f)}
	};
	/* Golden result from float version. */
	mat44_fp_t gold_lu = {
		{FLOAT_TO_FP(11.0f), FLOAT_TO_FP(0.8181818f),
		 FLOAT_TO_FP(2.1818182f), FLOAT_TO_FP(0.18181819f)},
		{FLOAT_TO_FP(3.0f), FLOAT_TO_FP(14.545454),
		 FLOAT_TO_FP(0.7875f), FLOAT_TO_FP(0.03125f)},
		{FLOAT_TO_FP(1.0f), FLOAT_TO_FP(4.181818f),
		 FLOAT_TO_FP(-3.4750001f), FLOAT_TO_FP(-1.6366906f)},
		{FLOAT_TO_FP(2.0f), FLOAT_TO_FP(3.3636365f),
		 FLOAT_TO_FP(-0.012500286f), FLOAT_TO_FP(0.5107909f)}
	};
	sizev4_t gold_pivot = {0, 2, 2, 3};

	mat44_fp_decompose_lup(fpa, pivot);

	for (i = 0; i < 4; ++i) {
		TEST_ASSERT(gold_pivot[i] == pivot[i]);
		for (j = 0; j < 4; ++j)
			TEST_ASSERT(IS_FP_EQUAL(gold_lu[i][j], fpa[i][j],
						LUP_TOLERANCE));
	}

	return EC_SUCCESS;
}

static int test_mat44_fp_solve(void)
{
	int i;
	fpv4_t x;
	mat44_fp_t A = {
		{FLOAT_TO_FP(11.0f), FLOAT_TO_FP(0.8181818f),
		 FLOAT_TO_FP(2.1818182f), FLOAT_TO_FP(0.18181819f)},
		{FLOAT_TO_FP(3.0f), FLOAT_TO_FP(14.545454),
		 FLOAT_TO_FP(0.7875f), FLOAT_TO_FP(0.03125f)},
		{FLOAT_TO_FP(1.0f), FLOAT_TO_FP(4.181818f),
		 FLOAT_TO_FP(-3.4750001f), FLOAT_TO_FP(-1.6366906f)},
		{FLOAT_TO_FP(2.0f), FLOAT_TO_FP(3.3636365f),
		 FLOAT_TO_FP(-0.012500286f), FLOAT_TO_FP(0.5107909f)}
	};
	sizev4_t pivot = {0, 2, 2, 3};
	fpv4_t b = {FLOAT_TO_FP(1.0f), FLOAT_TO_FP(3.3f), FLOAT_TO_FP(0.8f),
		    FLOAT_TO_FP(8.9f)};
	/* Golden result from float version. */
	fpv4_t gold_x = {FLOAT_TO_FP(-43.50743f), FLOAT_TO_FP(-21.459526f),
			 FLOAT_TO_FP(26.629248f), FLOAT_TO_FP(16.80776f)};

	mat44_fp_solve(A, x, b, pivot);

	for (i = 0; i < 4; ++i)
		TEST_ASSERT(IS_FP_EQUAL(gold_x[i], x[i], SOLVE_TOLERANCE));

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_fpv3_scalar_mul);
	RUN_TEST(test_fpv3_dot);
	RUN_TEST(test_fpv3_norm_squared);
	RUN_TEST(test_fpv3_norm);
	RUN_TEST(test_mat33_fp_init_zero);
	RUN_TEST(test_mat33_fp_init_diagonal);
	RUN_TEST(test_mat33_fp_scalar_mul);
	RUN_TEST(test_mat33_fp_get_eigenbasis);
	RUN_TEST(test_mat44_fp_decompose_lup);
	RUN_TEST(test_mat44_fp_solve);

	test_print_result();
}
