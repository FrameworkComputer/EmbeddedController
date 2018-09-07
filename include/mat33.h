/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MAT_33_H

#define __CROS_EC_MAT_33_H

#include "vec3.h"
#include "util.h"

typedef float mat33_float_t[3][3];
typedef size_t sizev3_t[3];

void init_zero_matrix(mat33_float_t A);
void init_diagonal_matrix(mat33_float_t A, float x);

void mat33_float_scalar_mul(mat33_float_t A, float c);

void mat33_float_swap_rows(mat33_float_t A, const size_t i, const size_t j);

void mat33_float_get_eigenbasis(mat33_float_t S, floatv3_t eigenvals,
				mat33_float_t eigenvecs);

size_t mat33_float_maxind(mat33_float_t A, size_t k);

void mat33_float_rotate(mat33_float_t A, float c, float s,
		  size_t k, size_t l, size_t i, size_t j);

#endif  /* __CROS_EC_MAT_33_H */
