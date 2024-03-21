/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Used to implement POPCOUNT(), adapted from:
 * src/third_party/llvm-project/compiler-rt/lib/builtins/popcountsi2.c
 */
int __popcountsi2(int x)
{
	x = x - ((x >> 1) & 0x55555555);
	x = ((x >> 2) & 0x33333333) + (x & 0x33333333);
	x = (x + (x >> 4)) & 0x0F0F0F0F;
	x = (x + (x >> 16));
	return (x + (x >> 8)) & 0x0000003F;
}
