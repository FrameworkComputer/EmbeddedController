/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Software emulation for CTZ instruction
 */

#include "common.h"

/**
 * Count trailing zeros
 *
 * @param x non null integer.
 * @return the number of trailing 0-bits in x,
 * starting at the least significant bit position.
 *
 * Using a de Brujin sequence, as documented here:
 * http://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
 */
int __keep __ctzsi2(int x)
{
	static const uint8_t MulDeBruijnBitPos[32] = {
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};
	return MulDeBruijnBitPos[((uint32_t)((x & -x) * 0x077CB531U)) >> 27];
}
