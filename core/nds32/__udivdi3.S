/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * __udivdi3.S: unsigned 64 bit division
 */

	.text
	.align	2
	.globl	__udivdi3
	.type	__udivdi3, @function
__udivdi3:
	movi	$r4, 0			! ignore remainder
	b	__udivmoddi4
	.size	__udivdi3, .-__udivdi3
