/* Copyright 2015 The ChromiumOS Authors
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

/* Data from sensor is in 16th of uT, 0.0625 uT/LSB */
#define MAG_CAL_RAW_UT 16

#define MAX_EIGEN_RATIO FLOAT_TO_FP(25.0f)
#define MAX_EIGEN_MAG FLOAT_TO_FP(80.0f * MAG_CAL_RAW_UT)
#define MIN_EIGEN_MAG FLOAT_TO_FP(10.0f * MAG_CAL_RAW_UT)

#define MAX_FIT_MAG MAX_EIGEN_MAG
#define MIN_FIT_MAG MIN_EIGEN_MAG

#define CPRINTF(format, args...) cprintf(CC_ACCEL, format, ##args)
#define PRINTF_FLOAT(x) ((int)((x) * 100.0f))

/**
 * Compute the covariance element: (avg(ab) - avg(a)*avg(b))
 *
 * @param sq The accumulated sum of a*b
 * @param a The accumulated sum of a
 * @param b The accumulated sum of b
 * @return (sq - ((a * b) * inv)) * inv
 */
static inline fp_t covariance_element(fp_t sq, fp_t a, fp_t b, fp_t inv)
{
	return fp_mul(sq - fp_mul(fp_mul(a, b), inv), inv);
}
/*
 * eigen value magnitude and ratio test
 *
 * Using the magnetometer information, calculate the 3 eigen values/vectors
 * for the transformation. Check the eigen values are reasonable.
 */
static int moc_eigen_test(struct mag_cal_t *moc)
{
	mat33_fp_t S;
	fpv3_t eigenvals;
	mat33_fp_t eigenvecs;
	fp_t evmax, evmin, evmag;
	fp_t inv = fp_div_dbz(FLOAT_TO_FP(1.0f),
			      INT_TO_FP((int)moc->kasa_fit.nsamples));
	int eigen_pass;

	/* covariance matrix */
	S[0][0] = covariance_element(moc->kasa_fit.acc_xx, moc->kasa_fit.acc_x,
				     moc->kasa_fit.acc_x, inv);
	S[0][1] = S[1][0] = covariance_element(moc->kasa_fit.acc_xy,
					       moc->kasa_fit.acc_x,
					       moc->kasa_fit.acc_y, inv);
	S[0][2] = S[2][0] = covariance_element(moc->kasa_fit.acc_xz,
					       moc->kasa_fit.acc_x,
					       moc->kasa_fit.acc_z, inv);
	S[1][1] = covariance_element(moc->kasa_fit.acc_yy, moc->kasa_fit.acc_y,
				     moc->kasa_fit.acc_y, inv);
	S[1][2] = S[2][1] = covariance_element(moc->kasa_fit.acc_yz,
					       moc->kasa_fit.acc_y,
					       moc->kasa_fit.acc_z, inv);
	S[2][2] = covariance_element(moc->kasa_fit.acc_zz, moc->kasa_fit.acc_z,
				     moc->kasa_fit.acc_z, inv);

	mat33_fp_get_eigenbasis(S, eigenvals, eigenvecs);

	evmax = (eigenvals[X] > eigenvals[Y]) ? eigenvals[X] : eigenvals[Y];
	evmax = (eigenvals[Z] > evmax) ? eigenvals[Z] : evmax;

	evmin = (eigenvals[X] < eigenvals[Y]) ? eigenvals[X] : eigenvals[Y];
	evmin = (eigenvals[Z] < evmin) ? eigenvals[Z] : evmin;

	evmag = fp_sqrtf(eigenvals[X] + eigenvals[Y] + eigenvals[Z]);

	eigen_pass = (fp_mul(evmin, MAX_EIGEN_RATIO) > evmax) &&
		     (evmag > MIN_EIGEN_MAG) && (evmag < MAX_EIGEN_MAG);

#if 0
	CPRINTF("mag eigenvalues: (%.02d %.02d %.02d), ",
		PRINTF_FLOAT(eigenvals[X]),
		PRINTF_FLOAT(eigenvals[Y]),
		PRINTF_FLOAT(eigenvals[Z]));

	CPRINTF("ratio %.02d, mag %.02d: pass %d\r\n",
		PRINTF_FLOAT(evmax / evmin),
		PRINTF_FLOAT(evmag),
		eigen_pass);
#endif

	return eigen_pass;
}

void init_mag_cal(struct mag_cal_t *moc)
{
	kasa_reset(&moc->kasa_fit);
}

int mag_cal_update(struct mag_cal_t *moc, const intv3_t v)
{
	int new_bias = 0;

	/* 1. run accumulators */
	kasa_accumulate(&moc->kasa_fit, INT_TO_FP(v[X]), INT_TO_FP(v[Y]),
			INT_TO_FP(v[Z]));

	/* 2. batch has enough samples? */
	if (moc->batch_size > 0 && moc->kasa_fit.nsamples >= moc->batch_size) {
		/* 3. eigen test */
		if (moc_eigen_test(moc)) {
			fpv3_t bias;
			fp_t radius;

			/* 4. Kasa sphere fitting */
			kasa_compute(&moc->kasa_fit, bias, &radius);
			if (radius > MIN_FIT_MAG && radius < MAX_FIT_MAG) {
				moc->bias[X] = -FP_TO_INT(bias[X]);
				moc->bias[Y] = -FP_TO_INT(bias[Y]);
				moc->bias[Z] = -FP_TO_INT(bias[Z]);

				moc->radius = radius;

				new_bias = 1;
			}
		}
		/* 5. reset for next batch */
		init_mag_cal(moc);
	}

	return new_bias;
}
