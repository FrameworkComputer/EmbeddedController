/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * x86 task swtching and interrupt handling
 */

#include "config.h"
#include "registers.h"
#include "task_defs.h"

.text

.extern current_task
.extern next_task

.global __task_start
.global __switchto
.global default_int_handler

# Start the task scheduling. Start current_task (hook_task)
# This function is not an ISR but imitates the sequence.
.align 4
.func __task_start
__task_start:
	movl current_task, %eax
	movl (%eax), %esp
	popa
	iret
.endfunc

# Default interrupt handler - to handle exceptions
# and prints error
.align 4
.func default_int_handler
default_int_handler:

	pusha
	add $1, __in_isr

	call unhandled_vector		# Handle system interrupts and
					# unregistered user interrupts

	cmpl $LAPIC_SPURIOUS_INT_VECTOR, %eax
	je 1f				# No EOI for LAPIC_SPURIOUS_INT_VECTOR

	movl %eax, IOAPIC_EOI_REG	# Indicate completion of servicing the
					# interrupt to IOAPIC first
	sub $1, __in_isr
	movl $0x00, LAPIC_EOI_REG	# Indicate completion of servicing the
					# interrupt to LAPIC next
	1:
	popa
	iret

.endfunc

# Switches from one task to another if ready.
# __schedule triggeres software interrupt ISH_TS_VECTOR, which is handled by
# __switchto
.align 4
.func __switchto
__switchto:

	# Save current task
	pusha
	addl $1, __in_isr

	# __schedule() copies 'resched' to %ecx and 'desched' to %edx before
	# triggering ISH_TS_VECTOR
	#
	# Push %ecx and %edx into stack to pass them as function parameters
	# to switch_handler(desched, resched)

	push %ecx
	push %edx
	call switch_handler
	addl $0x8, %esp		# Clean up stack

	test %eax, %eax		# Check if task switch required
	jz 1f

	movl current_task, %eax

#ifdef CONFIG_FPU
	fnsave 20(%eax)		# Save current FPU context at current->fp_ctx
#endif

	# Save SP of current task and switch to new task
	movl %esp, (%eax)
	movl next_task, %eax
	movl %eax, current_task
	movl (%eax), %esp

#ifdef CONFIG_FPU
	frstor 20(%eax)		# Restore next FPU context
#endif

	1:
	subl $1, __in_isr

	# Indicate completion of servicing the interrupt to LAPIC.
	# No IOAPIC EOI needed as this is SW triggered.
	movl $0x00, LAPIC_EOI_REG

	# Restore general purpose registers.
	popa
	iret

.endfunc
