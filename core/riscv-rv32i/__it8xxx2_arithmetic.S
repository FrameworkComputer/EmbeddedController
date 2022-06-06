/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * When the 'M' extension is disabled, compiler can not recognize div/mul
 * instructions. So mul/div instructions in the below integer arithmetic
 * routines are hard coded by opcodes.
 *
 * IMPORTANT:
 * The workaround requires the nop instruction, please don't optimize it.
 */

.macro __int_arithmetic func opcode
.section .ram_code_ilm0
.align 2
.globl \func
.type \func, @function
\func:
.word \opcode
nop
ret
.size \func, .-\func
.endm

/* signed 32 bit multiplication. opcode of mul a0,a0,a1 is 0x02b50533 */
__int_arithmetic __mulsi3 0x02b50533

/* signed 32 bit division. opcode of div a0,a0,a1 is 0x02b54533 */
__int_arithmetic __divsi3 0x02b54533

/* unsigned 32 bit division. opcode of divu a0,a0,a1 is 0x02b55533 */
__int_arithmetic __udivsi3 0x02b55533

/*
 * This function return the remainder of the signed division.
 * opcode of rem a0,a0,a1 is 0x02b56533
 */
__int_arithmetic __modsi3 0x02b56533

/*
 * This function return the remainder of the unsigned division.
 * opcode of remu a0,a0,a1 is 0x02b57533
 */
__int_arithmetic __umodsi3 0x02b57533
