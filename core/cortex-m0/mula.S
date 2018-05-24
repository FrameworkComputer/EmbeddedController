/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M0 multiply-accumulate[-accumulate] functions
 */

	.syntax unified
	.text
	.thumb

@ uint64_t mula32(uint32_t a, uint32_t b, uint32_t c)
@
@ Multiply a (r0) and b (r1), add c (r2) and return the product in r1:r0
@
	.thumb_func
	.section .text.mula32
	.global mula32
mula32:

	push	{r4, r5}
	uxth	r4, r0		/* r4 = a.lo16 */
	uxth	r5, r1		/* r5 = b.lo16 */
	uxth	r3, r2		/* r3 = c.lo16 */
	muls	r4, r5		/* r4 = a.lo16 * b.lo16 */
	adds	r4, r3		/* r4 = a.lo16 * b.lo16 + c.lo16 == r.lo32 */
	lsrs	r3, r0, 16	/* r3 = a.hi16 */
	lsrs	r2, r2, 16	/* r2 = c.hi16 */
	muls	r5, r3		/* r5 = a.hi16 * b.lo16 */
	adds	r5, r2		/* r5 = a.hi16 * b.lo16 + c.hi16 == r.mid32.1 */
	uxth	r2, r0		/* r2 = a.lo16 */
	lsrs	r1, r1, 16	/* r1 = b.hi16 */
	muls	r2, r1		/* r2 = a.lo16 * b.hi16 == r.mid32.2 */
	muls	r1, r3		/* r1 = b.hi16 * a.hi16 == r.hi32 */
	movs	r3, 0		/* r3 = 0 */
	adds	r5, r2		/* r5 = (r.mid32.1 + r.mid32.2).lo32 == r.mid.lo32 */
	adcs	r3, r3		/* r3 = (r.mid32.1 + r.mid32.2).hi32 == r.mid.hi32 */
	lsls	r0, r5, 16	/* r0 = r.mid.lo32.lo16 << 16 == r.mid.inpos.lo32 */
	lsrs	r5, r5, 16	/* r5 = r.mid.lo32.hi16 >> 16 */
	lsls	r3, r3, 16	/* r3 = r.mid.hi.lo16 << 16 */
	adds	r3, r5		/* r3 = r5 + r3 = r.mid.inpos.hi32 */
	adds	r0, r4		/* r0 = r.lo32 */
	adcs	r1, r3		/* r1 = r.hi32 */
	pop	{r4, r5}
	bx	lr

@ uint64_t mulaa32(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
@
@ Multiply a (r0) and b (r1), add c (r2), add d (r3) and return the product in
@ r1:r0
	.thumb_func
	.section .text.mulaa32
	.global mulaa32
mulaa32:

	push	{r4, r5, r6}
	uxth	r5, r0		/* r5 = a.lo16 */
	uxth	r6, r1		/* r6 = b.lo16 */
	uxth	r4, r2		/* r4 = c.lo16 */
	muls	r5, r6		/* r5 = a.lo16 * b.lo16 */
	adds	r5, r4		/* r5 = a.lo16 * b.lo16 + c.lo16 == r.lo32 */
	lsrs	r4, r0, 16	/* r4 = a.hi16 */
	lsrs	r2, r2, 16	/* r2 = c.hi16 */
	muls	r6, r4		/* r6 = a.hi16 * b.lo16 */
	adds	r6, r2		/* r6 = a.hi16 * b.lo16 + c.hi16 == r.mid32.1 */
	uxth	r2, r0		/* r2 = a.lo16 */
	lsrs	r1, r1, 16	/* r1 = b.hi16 */
	muls	r2, r1		/* r2 = a.lo16 * b.hi16 == r.mid32.2 */
	muls	r1, r4		/* r1 = b.hi16 * a.hi16 == r.hi32 */
	movs	r4, 0		/* r4 = 0 */
	adds	r6, r2		/* r6 = (r.mid32.1 + r.mid32.2).lo32 == r.mid.lo32 */
	adcs	r4, r4		/* r4 = (r.mid32.1 + r.mid32.2).hi32 == r.mid.hi32 */
	lsls	r0, r6, 16	/* r0 = r.mid.lo32.lo16 << 16 == r.mid.inpos.lo32 */
	lsrs	r6, r6, 16	/* r6 = r.mid.lo32.hi16 >> 16 */
	lsls	r4, r4, 16	/* r4 = r.mid.hi.lo16 << 16 */
	adds	r0, r3		/* r0 = r.mid.inposition.lo32 + d */
	adcs	r4, r6		/* r4 = r6 + r4 + carry = r.mid.inpos.hi32 */
	adds	r0, r5		/* r0 = r.lo32 */
	adcs	r1, r4		/* r1 = r.hi32 */
	pop	{r4, r5, r6}
	bx	lr
