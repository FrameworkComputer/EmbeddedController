/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * __muldi3.S: signed 64 bit multiplication
 */

#define P1H	$r1
#define P1L	$r0
#define P2H	$r3
#define P2L	$r2

	.text
	.align	2
	.globl	__muldi3
	.type	__muldi3, @function
__muldi3:
	mul	$r5, P1H, P2L	! (ah=a>>31)*(bl=b&0xffffffff)
	srli	P1H, P1L, 16	! alh=al>>16
	maddr32	$r5, P1L, P2H	! ah*bl+(bh=b>>31)*(al=a&0xffffffff)
	zeh	P1L, P1L	! all=al&0xffff
	srli	P2H, P2L, 16	! blh=bl>>16
	zeh	P2L, P2L	! bll=bl&0xffff

	mul	$ta, P1L, P2H	! zA=all*blh
	mul	$r4, P1L, P2L	! zl=all*bll
	mul	P2L, P1H, P2L	! zB=alh*bll
	add	P1L, $ta, P2L	! zA+=zB
	slt	$ta, P1L, P2L	! zA<zB
	slli	$ta, $ta, 16	! (zA<zB)<<16
	slli	P2L, P1L, 16	! zA<<16
	maddr32	$ta, P1H, P2H	! zh=alh*blh+((zA<zB)<<16)
	srli	P1H, P1L, 16	! zA>>16
	add	P1H, P1H, $ta	! zh+=(zA>>16)
	add	P1L, $r4, P2L	! zl+=(zA<<16)
	slt	$ta, P1L, $r4	! zl<zA
	add	P1H, P1H, $ta	! zh+=(zl<zA)
	add	P1H, P1H, $r5	! zh+=ah*bl+bh*al
	ret
	.size	__muldi3, .-__muldi3
