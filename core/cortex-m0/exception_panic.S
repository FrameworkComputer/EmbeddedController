/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "asm_offsets.h"

.syntax unified
.text
.thumb
.cpu cortex-m0

.global exception_panic
.thumb_func
.align 2
exception_panic:
	ldr r3, pstack
	ldr r0, pregs
	mov ip, r3
	mrs r1, psp
	mrs r2, ipsr
	mov r3, sp
	stmia r0!, {r1-r7}
	mov r1, r8
	mov r2, r9
	mov r3, r10
	mov r4, r11
	mov r5, lr
	stmia r0!, {r1-r5}
	mov sp, ip
	bl report_panic

pstack:
	.word ASM_PANIC_STACK_ADDR

.equ PREGS, (ASM_PANIC_DATA_CM_REGS_OFFSET + ASM_PANIC_DATA_PTR)
pregs:
	.word PREGS
