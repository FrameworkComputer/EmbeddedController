/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_VEC_3_H
#define __CROS_EC_VEC_3_H

typedef float floatv3_t[3];

void floatv3_scalar_mul(floatv3_t v, float c);
float floatv3_dot(const floatv3_t v, const floatv3_t w);
float floatv3_norm_squared(const floatv3_t v);
float floatv3_norm(const floatv3_t v);

#endif  /* __CROS_EC_VEC_3_H */
