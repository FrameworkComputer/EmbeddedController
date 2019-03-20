/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * ISH minute-IA CPU initialization
 */

#include "config.h"
#include "interrupts.h"
#include "ia_structs.h"

	.equ	CR0_NW,		(1 << 29)
	.equ	CR0_CD,		(1 << 30)


.global __idt
.global __gdt
.global __gdt_ptr

# GDT is loaded by ISH ROM. The FW code retains the same GDT
# and hence the same segment selector
.set code_seg, 0x8

.section .data.vecttable

# Macro that defines an interrupt descriptor
.macro interrupt_descriptor
	.word def_irq_low	# low 16 bits of default_int_handler
	.word code_seg
	.byte 0
	.byte IDT_DESC_FLAGS
	.word def_irq_high	# high 16 bits of default_int_handler
.endm

.align 32
# Static Interrupt descriptor table (vectors 0-255), all vectors initialized
# to default_int_handler. DECLARE_IRQ() remaps to the appropriate handler.
__idt:
interrupt_descriptor		#  0 - Divide error
interrupt_descriptor		#  1 - Debug
interrupt_descriptor		#  2 - NMI interrupt
interrupt_descriptor		#  3 - Breakpoint
interrupt_descriptor		#  4 - Overflow
interrupt_descriptor		#  5 - Bound range exceeded
interrupt_descriptor		#  6 - Invalid opcode
interrupt_descriptor		#  7 - Device not available
interrupt_descriptor		#  8 - Double fault
interrupt_descriptor		#  9 - Coprocessor overrun
interrupt_descriptor		# 10 - Invalid TSS
interrupt_descriptor		# 11 - Segment not present
interrupt_descriptor		# 12 - Stack segment fault
interrupt_descriptor		# 13 - General protection
interrupt_descriptor		# 14 - Page fault
interrupt_descriptor		# 15 - Reserved
interrupt_descriptor		# 16 - Floating point error
interrupt_descriptor		# 17 - Alignment check
interrupt_descriptor		# 18 - Machine check
interrupt_descriptor		# 19 - SIMD floating-point exception
interrupt_descriptor		# 20 - Virtualization exception
interrupt_descriptor		# 21..31 - Reserved
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 32..255 - User-defined, Maskable Interrupts
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 64
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 96
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 128
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 160
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 192
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 224
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor
interrupt_descriptor		# 255

__gdt:
	# Entry 0: Null descriptor
	.long 0x0
	.long 0x0
	# Entry 1: Code descriptor
	# Base: 0x0
	# Limit: 0xffffffff
	# Flags: 0x9b (Code E/R, Present, DPL0, Acessed=1)
	.long GEN_GDT_DESC_LO(0x0, 0xffffffff, GDT_DESC_CODE_FLAGS)
	.long GEN_GDT_DESC_UP(0x0, 0xffffffff, GDT_DESC_CODE_FLAGS)
	# Entry 2: Data descriptor
	# Base: 0x0
	# Limit: 0xffffffff
	# Flags: 0x93 (Data R/W, Present, DPL0, Acessed=1)
	.long GEN_GDT_DESC_LO(0x0, 0xffffffff, GDT_DESC_DATA_FLAGS)
	.long GEN_GDT_DESC_UP(0x0, 0xffffffff, GDT_DESC_DATA_FLAGS)
#if defined(CONFIG_ISH_PM_AONTASK)
	# placeholder entries, will be updated by init_aon_task()
	# in power_mgt.c
	# Entry 3: placeholder TSS descriptor for main FW
	.long 0x0
	.long 0x0
	# Entry 4: placeholder TSS descriptor for aontask
	.long 0x0
	.long 0x0
	# Entry 5: placeholder LDT descriptor for aontask
	.long 0x0
	.long 0x0
#endif

#.section .data
__idt_ptr:
	.word 2047	# Table size in bytes, count from 0
			# (8N - 1). N = 256 - the number of vectors
	.long __idt	# Base address of IDT

__gdt_ptr:
	.word 24
	.long __gdt

.section .init
.code32

# The .init section is mapped to linear address 0xFF000000, the SRAM start.
# ISH BUP (bring-up) downloads ISH FW to ISH SRAM and jumps to start address.
.global reset

# Entry point - core is already set to 32-bit linear mode by ISH ROM
reset:

	# Disabling interrupts initially. It will be enabled when tasks start
	cli

	# Clear .bss section.
	xorl %eax, %eax
	cld
	movl $__bss_start, %edi
	movl $__bss_size_words, %ecx
	rep stosl

	# System stack is within .bss, already cleared
	movl $stack_end, %esp

	# Load GDT
	lgdt __gdt_ptr

	# Load IDT
	lidt __idt_ptr

	# Enable cache (CR0)
	movl	%cr0, %eax
	andl	$~(CR0_CD | CR0_NW), %eax
	movl	%eax, %cr0

	# Reset Task-Switched flag to prevent an ESC instruction such
	# as fninit, from triggering a Device-not-available fault
	clts

#ifdef CONFIG_FPU
	fninit
#endif

	# Jump to C code
	jmp main

# Reserve space for system stack
.section .bss.system_stack
stack_start:
.space CONFIG_STACK_SIZE, 0
stack_end:
.global stack_end
