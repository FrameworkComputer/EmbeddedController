/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "mag_cal.h"
#include "mat33.h"
#include "mat44.h"

#include "math.h"
#include "math_util.h"
#include "util.h"

/* Data from sensor is in 16th of uT */
#define MAG_CAL_RAW_UT      16

#define MAX_EIGEN_RATIO     25.0f
#define MAX_EIGEN_MAG       (80.0f * MAG_CAL_RAW_UT)
#define MIN_EIGEN_MAG       (10.0f * MAG_CAL_RAW_UT)

#define MAX_FIT_MAG         MAX_EIGEN_MAG
#define MIN_FIT_MAG         MIN_EIGEN_MAG

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ## args)
#define PRINTF_FLOAT(x)  ((int)((x) * 100.0f))

/*
 * eigen value magnitude and ratio test
 *
 * Using the magnetometer information, caculate the 3 eigen values/vectors
 * for the transformation. Check the eigen values are sane.
 */
static int moc_eigen_test(struct mag_cal_t *moc)
{
	mat33_t S;
	vec3_t eigenvals;
	mat33_t eigenvecs;
	float evmax, evmin, evmag;
	int eigen_pass;

	/* covariance matrix */
	S[0][0] = moc->acc[0][0] - moc->acc[0][3] * moc->acc[0][3];
	S[0][1] = S[1][0] = moc->acc[0][1] - moc->acc[0][3] * moc->acc[1][3];
	S[0][2] = S[2][0] = moc->acc[0][2] - moc->acc[0][3] * moc->acc[2][3];
	S[1][1] = moc->acc[1][1] - moc->acc[1][3] * moc->acc[1][3];
	S[1][2] = S[2][1] = moc->acc[1][2] - moc->acc[1][3] * moc->acc[2][3];
	S[2][2] = moc->acc[2][2] - moc->acc[2][3] * moc->acc[2][3];

	mat33_get_eigenbasis(S, eigenvals, eigenvecs);

	evmax = (eigenvals[X] > eigenvals[Y]) ? eigenvals[X] : eigenvals[Y];
	evmax = (eigenvals[Z] > evmax) ? eigenvals[Z] : evmax;

	evmin = (eigenvals[X] < eigenvals[Y]) ? eigenvals[X] : eigenvals[Y];
	evmin = (eigenvals[Z] < evmin) ? eigenvals[Z] : evmin;

	evmag = sqrtf(eigenvals[X] + eigenvals[Y] + eigenvals[Z]);

	eigen_pass = (evmin * MAX_EIGEN_RATIO > evmax)
		&& (evmag > MIN_EIGEN_MAG)
		&& (evmag < MAX_EIGEN_MAG);

#if 0
	CPRINTF("mag eigenvalues: (%d %d %d), ",
		PRINTF_FLOAT(eigenvals[X]),
		PRINTF_FLOAT(eigenvals[Y]),
		PRINTF_FLOAT(eigenvals[Z]));

	CPRINTF("ratio %d, mag %d: pass %d\r\n",
		PRINTF_FLOAT(evmax / evmin),
		PRINTF_FLOAT(evmag),
		PRINTF_FLOAT(eigen_pass));
#endif

	return eigen_pass;
}

/*
 * Kasa sphere fitting with normal equation
 */
static int moc_fit(struct mag_cal_t *moc, vec3_t bias, float *radius)
{
	size4_t pivot;
	vec4_t out;
	int success = 0;

	/*
	 * To reduce stack size, moc->acc is A,
	 * moc->acc_w is b: we are looking for out, where:
	 *
	 *    A    *   out   =    b
	 * (4 x 4)   (4 x 1)   (4 x 1)
	 */
	/* complete the matrix: */
	moc->acc[1][0] = moc->acc[0][1];
	moc->acc[2][0] = moc->acc[0][2];
	moc->acc[2][1] = moc->acc[1][2];
	moc->acc[3][0] = moc->acc[0][3];
	moc->acc[3][1] = moc->acc[1][3];
	moc->acc[3][2] = moc->acc[2][3];
	moc->acc[3][3] = 1.0f;

	moc->acc_w[X] *= -1;
	moc->acc_w[Y] *= -1;
	moc->acc_w[Z] *= -1;
	moc->acc_w[W] *= -1;

	mat44_decompose_lup(moc->acc, pivot);

	mat44_solve(moc->acc, out, moc->acc_w, pivot);

	/*
	 * spherei is defined by:
	 * (x - xc)^2 + (y - yc)^2 + (z - zc)^2 = r^2
	 *
	 * Where r is:
	 * xc = -out[X] / 2, yc = -out[Y] / 2, zc = -out[Z] / 2
	 * r = sqrt(xc^2 + yc^2 + zc^2 - out[W])
	 */

	memcpy(bias, out, sizeof(vec3_t));
	vec3_scalar_mul(bias, -0.5f);

	*radius = sqrtf(vec3_dot(bias, bias) - out[W]);

#if 0
	CPRINTF("mag cal: bias (%d, %d, %d), R %d uT\n",
		PRINTF_FLOAT(bias[X] / MAG_CAL_RAW_UT),
		PRINTF_FLOAT(bias[Y] / MAG_CAL_RAW_UT),
		PRINTF_FLOAT(bias[Z] / MAG_CAL_RAW_UT),
		PRINTF_FLOAT(*radius / MAG_CAL_RAW_UT));
#endif

	/* TODO (menghsuan): bound on bias as well? */
	if (*radius > MIN_FIT_MAG && *radius < MAX_FIT_MAG)
		success = 1;

	return success;
}

void init_mag_cal(struct mag_cal_t *moc)
{
	memset(moc->acc, 0, sizeof(moc->acc));
	memset(moc->acc_w, 0, sizeof(moc->acc_w));
	moc->nsamples = 0;
}

int mag_cal_update(struct mag_cal_t *moc, const vector_3_t v)
{
	int new_bias = 0;

	/* 1. run accumulators */
	float w = v[X] * v[X] + v[Y] * v[Y] + v[Z] * v[Z];

	moc->acc[0][3] += v[X];
	moc->acc[1][3] += v[Y];
	moc->acc[2][3] += v[Z];
	moc->acc_w[W] += w;

	moc->acc[0][0] += v[X] * v[X];
	moc->acc[0][1] += v[X] * v[Y];
	moc->acc[0][2] += v[X] * v[Z];
	moc->acc_w[X] += v[X] * w;

	moc->acc[1][1] += v[Y] * v[Y];
	moc->acc[1][2] += v[Y] * v[Z];
	moc->acc_w[Y] += v[Y] * w;

	moc->acc[2][2] += v[Z] * v[Z];
	moc->acc_w[Z] += v[Z] * w;

	if (moc->nsamples < MAG_CAL_MAX_SAMPLES)
		moc->nsamples++;

	/* 2. batch has enough samples? */
	if (moc->batch_size > 0 && moc->nsamples >= moc->batch_size) {
		float inv = 1.0f / moc->nsamples;

		moc->acc[0][3] *= inv;
		moc->acc[1][3] *= inv;
		moc->acc[2][3] *= inv;
		moc->acc_w[W] *= inv;

		moc->acc[0][0] *= inv;
		moc->acc[0][1] *= inv;
		moc->acc[0][2] *= inv;
		moc->acc_w[X] *= inv;

		moc->acc[1][1] *= inv;
		moc->acc[1][2] *= inv;
		moc->acc_w[Y] *= inv;

		moc->acc[2][2] *= inv;
		moc->acc_w[Z] *= inv;

		/* 3. eigen test */
		if (moc_eigen_test(moc)) {
			vec3_t bias;
			float radius;

			/* 4. Kasa sphere fitting */
			if (moc_fit(moc, bias, &radius)) {

				moc->bias[X] = bias[X] * -1;
				moc->bias[Y] = bias[Y] * -1;
				moc->bias[Z] = bias[Z] * -1;

				moc->radius = radius;

				new_bias = 1;
			}
		}
		/* 5. reset for next batch */
		init_mag_cal(moc);
	}

	return new_bias;
}

