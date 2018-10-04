/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "mat33.h"
#include "math.h"
#include "util.h"

#define K_EPSILON 1E-5f

void mat33_fp_init_zero(mat33_fp_t A)
{
	memset(A, 0, sizeof(mat33_fp_t));
}

void mat33_fp_init_diagonal(mat33_fp_t A, fp_t x)
{
	const size_t N = 3;
	size_t i;

	mat33_fp_init_zero(A);

	for (i = 0; i < N; ++i)
		A[i][i] = x;
}

void mat33_fp_scalar_mul(mat33_fp_t A, fp_t c)
{
	const size_t N = 3;
	size_t i;

	for (i = 0; i < N; ++i) {
		size_t j;
		for (j = 0; j < N; ++j)
			A[i][j] = fp_mul(A[i][j], c);
	}
}

void mat33_fp_swap_rows(mat33_fp_t A, const size_t i, const size_t j)
{
	const size_t N = 3;
	size_t k;

	if (i == j)
		return;

	for (k = 0; k < N; ++k) {
		fp_t tmp = A[i][k];
		A[i][k] = A[j][k];
		A[j][k] = tmp;
	}
}

/*
 * Returns the eigenvalues and corresponding eigenvectors of the _symmetric_
 * matrix.
 * The i-th eigenvalue corresponds to the eigenvector in the i-th _row_ of
 * "eigenvecs".
 */
void mat33_fp_get_eigenbasis(mat33_fp_t S, fpv3_t e_vals,
			     mat33_fp_t e_vecs)
{
	const size_t N = 3;
	sizev3_t ind;
	size_t i, j, k, l, m;

	for (k = 0; k < N; ++k) {
		ind[k] = mat33_fp_maxind(S, k);
		e_vals[k] = S[k][k];
	}

	mat33_fp_init_diagonal(e_vecs, FLOAT_TO_FP(1.0f));

	for (;;) {
		fp_t y, t, s, c, p, sum;

		m = 0;
		for (k = 1; k + 1 < N; ++k)
			if (fp_abs(S[k][ind[k]]) > fp_abs(S[m][ind[m]]))
				m = k;

		k = m;
		l = ind[m];
		p = S[k][l];

		/*
		 * Note: K_EPSILON(1E-5) is too small to fit into 32-bit
		 * fixed-point(with 16 fp bits). The minimum positive value is
		 * 1 which is approximately 1.52E-5, so the
		 * FLOAT_TO_FP(K_EPSILON) becomes zero.
		 */
		if (fp_abs(p) <= FLOAT_TO_FP(K_EPSILON))
			break;

		y = fp_mul(e_vals[l] - e_vals[k], FLOAT_TO_FP(0.5f));

		t = fp_abs(y) + fp_sqrtf(fp_sq(p) + fp_sq(y));
		s = fp_sqrtf(fp_sq(p) + fp_sq(t));
		c = fp_div_dbz(t, s);
		s = fp_div_dbz(p, s);
		t = fp_div_dbz(fp_sq(p), t);

		if (y < FLOAT_TO_FP(0.0f)) {
			s = -s;
			t = -t;
		}

		S[k][l] = FLOAT_TO_FP(0.0f);

		e_vals[k] -= t;
		e_vals[l] += t;

		for (i = 0; i < k; ++i)
			mat33_fp_rotate(S, c, s, i, k, i, l);

		for (i = k + 1; i < l; ++i)
			mat33_fp_rotate(S, c, s, k, i, i, l);

		for (i = l + 1; i < N; ++i)
			mat33_fp_rotate(S, c, s, k, i, l, i);

		for (i = 0; i < N; ++i) {
			fp_t tmp = fp_mul(c, e_vecs[k][i]) -
				   fp_mul(s, e_vecs[l][i]);
			e_vecs[l][i] = fp_mul(s, e_vecs[k][i]) +
				       fp_mul(c, e_vecs[l][i]);
			e_vecs[k][i] = tmp;
		}

		ind[k] = mat33_fp_maxind(S, k);
		ind[l] = mat33_fp_maxind(S, l);

		sum = FLOAT_TO_FP(0.0f);
		for (i = 0; i < N; ++i)
			for (j = i + 1; j < N; ++j)
				sum += fp_abs(S[i][j]);

		/*
		 * Note: K_EPSILON(1E-5) is too small to fit into 32-bit
		 * fixed-point(with 16 fp bits). The minimum positive value is
		 * 1 which is approximately 1.52E-5, so the
		 * FLOAT_TO_FP(K_EPSILON) becomes zero.
		 */
		if (sum <= FLOAT_TO_FP(K_EPSILON))
			break;
	}

	for (k = 0; k < N; ++k) {
		m = k;
		for (l = k + 1; l < N; ++l)
			if (e_vals[l] > e_vals[m])
				m = l;

		if (k != m) {
			fp_t tmp = e_vals[k];
			e_vals[k] = e_vals[m];
			e_vals[m] = tmp;

			mat33_fp_swap_rows(e_vecs, k, m);
		}
	}
}

/* index of largest off-diagonal element in row k */
size_t mat33_fp_maxind(mat33_fp_t A, size_t k)
{
	const size_t N = 3;
	size_t i, m = k + 1;

	for (i = k + 2; i < N; ++i)
		if (fp_abs(A[k][i]) > fp_abs(A[k][m]))
			m = i;

	return m;
}

void mat33_fp_rotate(mat33_fp_t A, fp_t c, fp_t s,
		     size_t k, size_t l, size_t i, size_t j)
{
	fp_t tmp = fp_mul(c, A[k][l]) - fp_mul(s, A[i][j]);
	A[i][j] = fp_mul(s, A[k][l]) + fp_mul(c, A[i][j]);
	A[k][l] = tmp;
}
