/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Header file for common math functions. */
#ifndef __CROS_EC_VEC_3_H
#define __CROS_EC_VEC_3_H

typedef float vec3_t[3];

void vec3_scalar_mul(vec3_t v, float c);
float vec3_dot(const vec3_t v, const vec3_t w);
float vec3_norm_squared(const vec3_t v);
float vec3_norm(const vec3_t v);

#endif  /* __CROS_EC_VEC_3_H */
