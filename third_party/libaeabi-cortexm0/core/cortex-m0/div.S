/* Runtime ABI for the ARM Cortex-M0
 * idiv.S: signed 32 bit division (only quotient)
 * idivmod.S: signed 32 bit division (quotient and remainder)
 * uidivmod.S: unsigned 32 bit division
 *
 * Copyright (c) 2012 Jörg Mische <bobbl@gmx.de>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "software_panic.h"


	.syntax unified
	.text
	.thumb
	.cpu cortex-m0

@ int __aeabi_idiv(int num:r0, int denom:r1)
@
@ Divide r0 by r1 and return quotient in r0 (all signed).
@ Use __aeabi_uidivmod() but check signs before and change signs afterwards.
@
	.thumb_func
	.section .text.__aeabi_idiv
	.global __aeabi_idiv
__aeabi_idiv:

	cmp	r0, #0
	bge	L_num_pos

	rsbs	r0, r0, #0		@ num = -num

	cmp	r1, #0
	bge	L_neg_result

	rsbs	r1, r1, #0		@ den = -den
	b	__aeabi_uidivmod

L_num_pos:
	cmp	r1, #0
	bge	__aeabi_uidivmod

	rsbs	r1, r1, #0		@ den = -den

L_neg_result:
	push	{lr}
	bl	__aeabi_uidivmod
	rsbs	r0, r0, #0		@ quot = -quot
	pop	{pc}



@ {int quotient:r0, int remainder:r1}
@ __aeabi_idivmod(int numerator:r0, int denominator:r1)
@
@ Divide r0 by r1 and return the quotient in r0 and the remainder in r1
@
	.thumb_func
	.section .text.__aeabi_idivmod
	.global __aeabi_idivmod
__aeabi_idivmod:

	cmp	r0, #0
	bge	L_num_pos_bis

	rsbs	r0, r0, #0		@ num = -num

	cmp	r1, #0
	bge	L_neg_both

	rsbs	r1, r1, #0		@ den = -den
	push	{lr}
	bl	__aeabi_uidivmod
	rsbs	r1, r1, #0		@ rem = -rem
	pop	{pc}

L_neg_both:
	push	{lr}
	bl	__aeabi_uidivmod
	rsbs	r0, r0, #0		@ quot = -quot
	rsbs	r1, r1, #0		@ rem = -rem
	pop	{pc}

L_num_pos_bis:
	cmp	r1, #0
	bge	__aeabi_uidivmod

	rsbs	r1, r1, #0		@ den = -den
	push	{lr}
	bl	__aeabi_uidivmod
	rsbs	r0, r0, #0		@ quot = -quot
	pop	{pc}



@ unsigned __aeabi_uidiv(unsigned num, unsigned denom)
@
@ Just an alias for __aeabi_uidivmod(), the remainder is ignored
@
	.thumb_func
	.section .text.__aeabi_uidivmod
	.global __aeabi_uidiv
__aeabi_uidiv:



@ {unsigned quotient:r0, unsigned remainder:r1}
@  __aeabi_uidivmod(unsigned numerator:r0, unsigned denominator:r1)
@
@ Divide r0 by r1 and return the quotient in r0 and the remainder in r1
@
	.thumb_func
	.global __aeabi_uidivmod
__aeabi_uidivmod:


	cmp	r1, #0
	bne	L_no_div0
	b	__aeabi_idiv0
L_no_div0:

	@ Shift left the denominator until it is greater than the numerator
	movs	r2, #1		@ counter
	movs	r3, #0		@ result
	cmp	r0, r1
	bls	L_sub_loop0
	adds	r1, #0		@ dont shift if denominator would overflow
	bmi	L_sub_loop0

L_denom_shift_loop:
	lsls	r2, #1
	lsls	r1, #1
	bmi	L_sub_loop0
	cmp	r0, r1
	bhi	L_denom_shift_loop

L_sub_loop0:
	cmp	r0, r1
	bcc	L_dont_sub0	@ if (num>denom)

	subs	r0, r1		@ numerator -= denom
	orrs	r3, r2		@ result(r3) |= bitmask(r2)
L_dont_sub0:

	lsrs	r1, #1		@ denom(r1) >>= 1
	lsrs	r2, #1		@ bitmask(r2) >>= 1
	bne	L_sub_loop0

	mov	r1, r0		@ remainder(r1) = numerator(r0)
	mov	r0, r3		@ quotient(r0) = result(r3)
	bx	lr

__aeabi_idiv0:
	ldr	SOFTWARE_PANIC_REASON_REG, =PANIC_SW_DIV_ZERO
	bl	exception_panic
