/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Cortex-M0 CPU initialization
 */

#include "config.h"

#ifdef CHIP_FAMILY_STM32F0
/* Allocate space for SRAM vector table at SRAM based address */
.section .bss.vector_table
sram_vtable: .skip (48*4)
#endif

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
	movs r0, #0          @ priv. mode / main stack / no floating point
	msr control, r0
	isb                  @ ensure the write is done

	/* Clear BSS */
	movs r0, #0
	ldr  r1,_bss_start
	ldr  r2,_bss_end
bss_loop:
	str  r0, [r1]
	adds r1, #4
	cmp  r1, r2
	blt bss_loop

#ifdef CHIP_FAMILY_STM32F0
	/*
	 * STM32F0 parts don't have the VTOR register for relocating
	 * the vector table. Instead, we must copy the vector table from
	 * flash into SRAM.
	 */
	ldr r1, =vectors
	ldr r2, =sram_vtable
	movs r0, #0
vtable_loop:
	ldr r3, [r1]
	str r3, [r2]
	adds r1, #4
	adds r2, #4
	adds r0, #1
	cmp r0, #48
	blt vtable_loop

	/* Set SYSCFG_CFGR1 mem_mode to load vector table from SRAM */
	movs r0, #3
	ldr r1, =0x40010000
	str r0, [r1]
#else
	/* Set the vector table on our current code */
	ldr r1, =vectors
	ldr r2, =0xE000ED08   /* VTOR register in SCB*/
	str r1, [r2]
#endif

	/* Copy initialized data to Internal RAM */
	ldr r0,_data_lma_start
	ldr r1,_data_start
	ldr r2,_data_end
data_loop:
	ldr  r3, [r0]
	adds r0, #4
	str  r3, [r1]
	adds r1, #4
	cmp  r1, r2
	blt data_loop

	/*
	 * Set stack pointer.  Already done by Cortex-M hardware, but re-doing
	 * this here allows software to jump directly to the reset vector.
	 */
	ldr r0, =stack_end
	mov sp, r0

	/* Jump to C code */
	bl main

	/* That should not return.  If it does, loop forever. */
fini_loop:
	b fini_loop

/* Default exception handler */
.thumb_func
default_handler:
	ldr r0, =exception_panic
	bx r0

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

