/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mat44.h"
#include "math.h"
#include "util.h"

#define K_EPSILON 1E-5f

void mat44_decompose_lup(mat44_t LU, size4_t pivot)
{
	const size_t N = 4;
	size_t i, j, k;

	for (k = 0; k < N; ++k) {
		float max = fabsf(LU[k][k]);
		pivot[k] = k;
		for (j = k + 1; j < N; ++j) {
			if (max < fabsf(LU[j][k])) {
				max = fabsf(LU[j][k]);
				pivot[k] = j;
			}
		}

		if (pivot[k] != k)
			mat44_swap_rows(LU, k, pivot[k]);

		if (fabsf(LU[k][k]) < K_EPSILON)
			continue;

		for (j = k + 1; j < N; ++j)
			LU[k][j] /= LU[k][k];

		for (i = k + 1; i < N; ++i)
			for (j = k + 1; j < N; ++j)
				LU[i][j] -= LU[i][k] * LU[k][j];
	}
}

void mat44_swap_rows(mat44_t A, const size_t i, const size_t j)
{
	const size_t N = 4;
	size_t k;

	if (i == j)
		return;

	for (k = 0; k < N; ++k) {
		float tmp = A[i][k];
		A[i][k] = A[j][k];
		A[j][k] = tmp;
	}
}

void mat44_solve(mat44_t A, vec4_t x, const vec4_t b, const size4_t pivot)
{
	const size_t N = 4;
	vec4_t b_copy;
	size_t i, k;

	memcpy(b_copy, b, sizeof(vec4_t));

	for (k = 0; k < N; ++k) {
		if (pivot[k] != k) {
			float tmp = b_copy[k];
			b_copy[k] = b_copy[pivot[k]];
			b_copy[pivot[k]] = tmp;
		}

		x[k] = b_copy[k];
		for (i = 0; i < k; ++i)
			x[k] -= x[i] * A[k][i];
		x[k] /= A[k][k];
	}

	for (k = N; k-- > 0;)
		for (i = k + 1; i < N; ++i)
			x[k] -= x[i] * A[k][i];
}



