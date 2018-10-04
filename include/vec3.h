/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_VEC_3_H
#define __CROS_EC_VEC_3_H

#include "math_util.h"

typedef float floatv3_t[3];
typedef fp_t fpv3_t[3];

void fpv3_scalar_mul(fpv3_t v, fp_t c);
fp_t fpv3_dot(const fpv3_t v, const fpv3_t w);
fp_t fpv3_norm_squared(const fpv3_t v);
fp_t fpv3_norm(const fpv3_t v);
#endif  /* __CROS_EC_VEC_3_H */
