/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ARMv7-M architectural caches maintenance operations.
 */

.syntax unified
.text
.thumb

/* System Control Block: cache registers */
#define SCB_CCSIDR 0xe000ed80
#define SCB_CCSELR 0xe000ed84
#define SCB_DCISW  0xe000ef60
#define SCB_DCCISW 0xe000ef74

.macro dcache_set_way_op name register
@
@ Perform an operation on all D-cache sets/ways.
@
@ Note: implemented in assembly to guarantee that we are not touching the
@ D-cache in the middle of the loop.
@
.thumb_func
.section .text.\name
.global \name
\name:
	/* Select Level-1 Data cache (for operations on CCSIDR). */
	ldr  r1, =SCB_CCSELR
	movs r0, #0
	ldr  r2, =SCB_CCSIDR
	str  r0, [r1]  /* set CCSELR = 0 */

	/* Ensure the CCSELR write is effective before reading CCSIDR. */
	dsb
	/* CCSIDR contains the cache geometry. */
	ldr  r3, [r2]  /* [27:13] Number of sets -1  [12:3] Number of ways -1 */

	/* register used to do the set/way cache operation. */
	ldr  r0, =\register
	/* r2 is the number of cache 'sets' - 1 */
	ubfx r2, r3, #13, #15
	/* r12 is the number of cache 'ways' - 1 */
	ubfx r12, r3, #3, #10

1:
	mov  r1, r12    /* reset way index */
2:
	/*
	 * Build address Set/Way operation e.g DC(C)ISW
	 * [31:30] way index [13:5] set index
	 */
	lsls r3, r2, #5 /* set index */
	/* TODO(crbug.com/848704) remove cache geometry assumptions */
	orr  r3, r3, r1, lsl #30 /* way index */
	/* Perform operation (e.g invalidate) on a D-cache line */
	str  r3, [r0]
	/* go to previous way */
	subs r1, #1
	bcs  2b
	/* go to previous set */
	subs r2, #1
	bcs  1b

	/* Ensure everything has propagated and return. */
	dsb
	isb
	bx  lr
.endm

/* D-cache Invalidate by set-way */
dcache_set_way_op cpu_invalidate_dcache SCB_DCISW

/* D-cache Clean and Invalidate by set-way, to Point of Coherency */
dcache_set_way_op cpu_clean_invalidate_dcache SCB_DCCISW
