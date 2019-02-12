/* Runtime ABI for the ARM Cortex-M
 * uldivmod.S: unsigned 64 bit division
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
	.code 16



@ {unsigned long long quotient, unsigned long long remainder}
@ __aeabi_uldivmod(unsigned long long numerator, unsigned long long denominator)
@
@ Divide r1:r0 by r3:r2 and return the quotient in r1:r0 and the remainder
@ in r3:r2 (all unsigned)
@
	.thumb_func
	.section .text.__aeabi_uldivmod
        .global __aeabi_uldivmod
__aeabi_uldivmod:

	cmp	r3, #0
	bne	L_large_denom
	cmp	r2, #0
	beq	L_divison_by_0
	cmp	r1, #0
	beq	L_fallback_32bits



	@ case 1: num >= 2^32 and denom < 2^32
	@ Result might be > 2^32, therefore we first calculate the upper 32
	@ bits of the result. It is done similar to the calculation of the
	@ lower 32 bits, but with a denominator that is shifted by 32.
	@ Hence the lower 32 bits of the denominator are always 0 and the
	@ costly 64 bit shift and sub operations can be replaced by cheap 32
	@ bit operations.

	push	{r4, r5, r6, r7, lr}

	@ shift left the denominator until it is greater than the numerator
	@ denom(r7:r6) = r3:r2 << 32

	@ TODO(crosbug.com/p/36128): Loops like this (which occur in several
	@ places in this file) are inefficent in ARMv6-m.

	movs	r5, #1		@ bitmask
	adds	r7, r2, #0	@ dont shift if denominator would overflow
	bmi	L_upper_result
	cmp	r1, r7
	blo	L_upper_result

L_denom_shift_loop1:
	lsl	r5, #1
	lsls	r7, #1
	bmi	L_upper_result	@ dont shift if overflow
	cmp	r1, r7
	bhs	L_denom_shift_loop1

L_upper_result:
	mov	r3, r1
	mov	r2, r0
	mov	r1, #0		@ upper result = 0
	mov	r6, #0

L_sub_loop1:
	orr	r1, r5		@ result(r7:r6) |= bitmask(r5)
	subs	r2, r6		@ num -= denom
	sbcs	r3, r7
	bhs	L_done_sub1

	eor	r1, r5		@ undo add mask
	adds	r2, r6		@ undo subtract
	adc	r3, r3, r7

L_done_sub1:
	lsrs	r7, #1		@ denom(r7:r6) >>= 1
	rrx	r6, r6
	lsrs	r5, #1		@ bitmask(r5) >>= 1
	bne	L_sub_loop1

	rrx r5, r5
	b	L_lower_result



	@ case 2: division by 0
	@ call __aeabi_ldiv0

L_divison_by_0:
	b	__aeabi_ldiv0



	@ case 3: num < 2^32 and denom < 2^32
	@ fallback to 32 bit division

L_fallback_32bits:
	mov	r1, r0
	udiv	r0, r0, r2	@ r0 = quotient
	mul	r3, r0, r2	@ r3 = quotient * divisor
	sub	r2, r1, r3	@ r2 = remainder
	mov	r1, #0
	mov	r3, #0
	bx	lr



	@ case 4: denom >= 2^32
	@ result is smaller than 2^32

L_large_denom:
	push	{r4, r5, r6, r7, lr}

	mov	r7, r3
	mov	r6, r2
	mov	r3, r1
	mov	r2, r0

	@ Shift left the denominator until it is greater than the numerator

	mov	r1, #0		@ high word of result is 0
	mov	r5, #1		@ bitmask
	adds	r7, #0		@ dont shift if denominator would overflow
	bmi	L_lower_result
	cmp	r3, r7
	blo	L_lower_result

L_denom_shift_loop4:
	lsl	r5, #1
	lsls	r6, #1		@ denom(r7:r6) <<= 1
	adcs	r7, r7
	bmi	L_lower_result	@ dont shift if overflow
	cmp	r3, r7
	bhs	L_denom_shift_loop4



L_lower_result:
	movs	r0, #0

L_sub_loop4:
	orr	r0, r5		@ result(r1:r0) |= bitmask(r5)
	subs	r2, r6		@ numerator -= denom
	sbcs	r3, r7
	bhs	L_done_sub4

	eor	r0, r5		 @ undo add mask
	adds	r2, r6		 @ undo subtract
	adc	r3, r3, r7

L_done_sub4:
	lsrs	r7, #1		@ denom(r7:r6) >>= 1
	rrx	r6, r6
	lsrs	r5, #1		@ bitmask(r5) >>= 1
	bne	L_sub_loop4

	pop	{r4, r5, r6, r7, pc}

__aeabi_ldiv0:
	ldr	SOFTWARE_PANIC_REASON_REG, =PANIC_SW_DIV_ZERO
	bl	exception_panic
