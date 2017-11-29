/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

/*
 * __builtin_ffs:
 * Returns one plus the index of the least significant 1-bit of x,
 * or if x is zero, returns zero.
 */
int __keep __ffssi2(int x)
{
	return 32 - __builtin_clz(x & -x);
}
