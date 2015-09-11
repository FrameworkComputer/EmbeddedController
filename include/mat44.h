/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_MAT_44_H

#define __CROS_EC_MAT_44_H

#include "vec4.h"
#include "util.h"

typedef float mat44_t[4][4];
typedef size_t size4_t[4];

void mat44_decompose_lup(mat44_t LU, size4_t pivot);

void mat44_swap_rows(mat44_t A, const size_t i, const size_t j);

void mat44_solve(mat44_t A, vec4_t x, const vec4_t b, const size4_t pivot);

#endif  /* __CROS_EC_MAT_44_H */
