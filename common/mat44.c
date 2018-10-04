/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mat44.h"
#include "math.h"
#include "util.h"

#define K_EPSILON 1E-5f

void mat44_fp_decompose_lup(mat44_fp_t LU, sizev4_t pivot)
{
	const size_t N = 4;
	size_t i, j, k;

	for (k = 0; k < N; ++k) {
		fp_t max = fp_abs(LU[k][k]);
		pivot[k] = k;
		for (j = k + 1; j < N; ++j) {
			const fp_t lu_jk = fp_abs(LU[j][k]);
			if (max < lu_jk) {
				max = lu_jk;
				pivot[k] = j;
			}
		}

		if (pivot[k] != k)
			mat44_fp_swap_rows(LU, k, pivot[k]);

		if (fp_abs(LU[k][k]) < FLOAT_TO_FP(K_EPSILON))
			continue;

		for (j = k + 1; j < N; ++j)
			LU[k][j] = fp_div_dbz(LU[k][j], LU[k][k]);

		for (i = k + 1; i < N; ++i)
			for (j = k + 1; j < N; ++j)
				LU[i][j] -= fp_mul(LU[i][k], LU[k][j]);
	}
}

void mat44_fp_swap_rows(mat44_fp_t A, const size_t i, const size_t j)
{
	const size_t N = 4;
	size_t k;

	if (i == j)
		return;

	for (k = 0; k < N; ++k) {
		fp_t tmp = A[i][k];
		A[i][k] = A[j][k];
		A[j][k] = tmp;
	}
}

void mat44_fp_solve(mat44_fp_t A, fpv4_t x, const fpv4_t b,
		    const sizev4_t pivot)
{
	const size_t N = 4;
	fpv4_t b_copy;
	size_t i, k;

	memcpy(b_copy, b, sizeof(fpv4_t));

	for (k = 0; k < N; ++k) {
		if (pivot[k] != k) {
			fp_t tmp = b_copy[k];
			b_copy[k] = b_copy[pivot[k]];
			b_copy[pivot[k]] = tmp;
		}

		x[k] = b_copy[k];
		for (i = 0; i < k; ++i)
			x[k] -= fp_mul(x[i], A[k][i]);
		x[k] = fp_div_dbz(x[k], A[k][k]);
	}

	for (k = N; k-- > 0;)
		for (i = k + 1; i < N; ++i)
			x[k] -= fp_mul(x[i], A[k][i]);
}
