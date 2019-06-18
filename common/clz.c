/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Software emulation for CLZ instruction
 */

#include "common.h"

/**
 * Count leading zeros
 *
 * @param x non null integer.
 * @return the number of leading 0-bits in x,
 * starting at the most significant bit position.
 */
int __keep __clzsi2(int x)
{
	int r = 0;

	if (!x)
		return 32;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r += 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r += 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r += 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r += 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r += 1;
	}
	return r;
}
