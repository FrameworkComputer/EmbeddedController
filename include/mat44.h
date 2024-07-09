/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_MAT_44_H

#define __CROS_EC_MAT_44_H

#include "math_util.h"
#include "util.h"
#include "vec4.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef float mat44_float_t[4][4];
typedef fp_t mat44_fp_t[4][4];
typedef size_t sizev4_t[4];

void mat44_fp_decompose_lup(mat44_fp_t LU, sizev4_t pivot);

void mat44_fp_swap_rows(mat44_fp_t A, const size_t i, const size_t j);

void mat44_fp_solve(mat44_fp_t A, fpv4_t x, const fpv4_t b,
		    const sizev4_t pivot);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_MAT_44_H */
