/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_MATH_H__
#define __CROS_EC_MATH_H__

#include <stdbool.h>
#include "fpu.h"

static inline bool isnan(float a)
{
	return __builtin_isnan(a);
}

static inline bool isinf(float a)
{
	return __builtin_isinf(a);
}

#endif /* __CROS_EC_MATH_H__ */
