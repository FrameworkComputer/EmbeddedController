/* Copyright 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M CPU initialization
 */

#include "config.h"

.text
.syntax unified
.code 16

.global reset
.thumb_func
reset:
	/*
	 * Ensure we're in privileged mode with main stack.  Necessary if
	 * we've jumped directly here from another image after task_start().
	 */
#ifdef CONFIG_FPU
	mov r0, #(1 << 2)    @ priv. mode / main stack / floating point on
#else
	mov r0, #0           @ priv. mode / main stack / no floating point
#endif
	msr control, r0
	isb                  @ ensure the write is done

	/* Set the vector table on our current code */
	ldr r1, =vectors
	ldr r2, =0xE000ED08   /* VTABLE register in SCB*/
	str r1, [r2]

	/* Clear BSS */
	mov r0, #0
	ldr r1,_bss_start
	ldr r2,_bss_end
bss_loop:
	cmp r1, r2
	it lt
	strlt r0, [r1], #4
	blt bss_loop

	/* Copy initialized data to Internal RAM */
	ldr r0,_data_lma_start

	/*
	 * When the .data section is linked into the .init_rom section,
	 * _data_lma_start is defined as a flash offset instead of a full
	 * 32-bit address by the linker script. Add the 32-bit flash base
	 * address to get a full 32-bit address.
	 *
	 * Flash locking isn't needed here as no tasks have been started.
	 */
#ifdef CONFIG_CHIP_DATA_IN_INIT_ROM
	ldr r1, =CONFIG_MAPPED_STORAGE_BASE
	add r0, r0, r1
#endif

	ldr r1,_data_start
	ldr r2,_data_end
data_loop:
	ldr r3, [r0], #4
	cmp r1, r2
	it lt
	strlt r3, [r1], #4
	blt data_loop

	/*
	 * Set stack pointer.  Already done by Cortex-M hardware, but re-doing
	 * this here allows software to jump directly to the reset vector.
	 */
	ldr r0, =stack_end
	mov sp, r0

#ifdef CONFIG_FPU
	/* Enable FPU */
	/* CPACR is located at address 0xE000ED88 */
	ldr r0, =0xE000ED88
	/* Read CPACR */
	ldr r1, [r0]
	/* Set bits 20-23 to enable CP10 and CP11 coprocessors */
	orr r1, r1, #(0xF << 20)
	/* Write back the modified value to the CPACR */
	str r1, [r0] /* wait for store to complete */
	dsb
	/* reset pipeline now the FPU is enabled */
	isb
#endif /* CONFIG_FPU */

#ifdef CONFIG_DEBUG_DISABLE_WRITE_BUFFER
	/* Disable write buffer used for default memory map accesses */
	ldr r0, =0xE000E008 /* Load address of ACTLR */
	ldr r1, [r0]        /* Read ACTLR */
	orr r1, r1, #2      /* Set DISDEFWBUF bit */
	str r1, [r0]        /* Write back ACTLR */
	dsb                 /* Wait for store to complete */
	isb                 /* Reset pipeline */
#endif /* CONFIG_DEBUG_DISABLE_WRITE_BUFFER */

	/* Jump to C code */
	bl main

	/* That should not return.  If it does, loop forever. */
fini_loop:
	b fini_loop

.align 2
_bss_start:
.long __bss_start
_bss_end:
.long __bss_end
_data_start:
.long __data_start
_data_end:
.long __data_end
_data_lma_start:
.long __data_lma_start

/* Mock functions to avoid linker complaints */
.global __aeabi_unwind_cpp_pr0
.global __aeabi_unwind_cpp_pr1
.global __aeabi_unwind_cpp_pr2
__aeabi_unwind_cpp_pr0:
__aeabi_unwind_cpp_pr1:
__aeabi_unwind_cpp_pr2:
	bx lr

/* Reserve space for system stack */
.section .bss.system_stack
stack_start:
.space CONFIG_STACK_SIZE, 0
stack_end:
.global stack_end

