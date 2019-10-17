/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "kasa.h"
#include "mat44.h"
#include <string.h>

void kasa_reset(struct kasa_fit *kasa)
{
	memset(kasa, 0, sizeof(struct kasa_fit));
}

void kasa_accumulate(struct kasa_fit *kasa, fp_t x, fp_t y, fp_t z)
{
	fp_t w = fp_sq(x) + fp_sq(y) + fp_sq(z);

	kasa->acc_x += x;
	kasa->acc_y += y;
	kasa->acc_z += z;
	kasa->acc_w += w;

	kasa->acc_xx += fp_sq(x);
	kasa->acc_xy += fp_mul(x, y);
	kasa->acc_xz += fp_mul(x, z);
	kasa->acc_xw += fp_mul(x, w);

	kasa->acc_yy += fp_sq(y);
	kasa->acc_yz += fp_mul(y, z);
	kasa->acc_yw += fp_mul(y, w);

	kasa->acc_zz += fp_sq(z);
	kasa->acc_zw += fp_mul(z, w);

	kasa->nsamples += 1;
}

void kasa_compute(struct kasa_fit *kasa, fpv3_t bias, fp_t *radius)
{
	/*    A    *   out   =    b
	 * (4 x 4)   (4 x 1)   (4 x 1)
	 */
	mat44_fp_t A;
	fpv4_t b, out;
	sizev4_t pivot;

	A[0][0] = kasa->nsamples;
	A[0][1] = A[1][0] = kasa->acc_x;
	A[0][2] = A[2][0] = kasa->acc_y;
	A[0][3] = A[3][0] = kasa->acc_z;
	A[1][1] = kasa->acc_xx;
	A[1][2] = A[2][1] = kasa->acc_xy;
	A[1][3] = A[3][1] = kasa->acc_xz;
	A[2][2] = kasa->acc_yy;
	A[2][3] = A[3][2] = kasa->acc_yz;
	A[3][3] = kasa->acc_zz;

	b[0] = -kasa->acc_w;
	b[1] = -kasa->acc_xw;
	b[2] = -kasa->acc_yw;
	b[3] = -kasa->acc_zw;

	mat44_fp_decompose_lup(A, pivot);
	mat44_fp_solve(A, out, b, pivot);

	bias[0] = fp_mul(out[1], FLOAT_TO_FP(-0.5f));
	bias[1] = fp_mul(out[2], FLOAT_TO_FP(-0.5f));
	bias[2] = fp_mul(out[3], FLOAT_TO_FP(-0.5f));

	*radius = fpv3_dot(bias, bias) - out[0];
	*radius = (*radius > 0) ? fp_sqrtf(*radius) : FLOAT_TO_FP(0.0f);
}
