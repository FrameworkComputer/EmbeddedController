/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MAT_33_H

#define __CROS_EC_MAT_33_H

#include "math_util.h"
#include "util.h"
#include "vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef float mat33_float_t[3][3];
typedef size_t sizev3_t[3];

void mat33_fp_init_zero(mat33_fp_t A);
void mat33_fp_init_diagonal(mat33_fp_t A, fp_t x);

void mat33_fp_scalar_mul(mat33_fp_t A, fp_t c);

void mat33_fp_swap_rows(mat33_fp_t A, const size_t i, const size_t j);

void mat33_fp_get_eigenbasis(mat33_fp_t S, fpv3_t eigenvals,
			     mat33_fp_t eigenvecs);

size_t mat33_fp_maxind(mat33_fp_t A, size_t k);

void mat33_fp_rotate(mat33_fp_t A, fp_t c, fp_t s, size_t k, size_t l, size_t i,
		     size_t j);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MAT_33_H */
