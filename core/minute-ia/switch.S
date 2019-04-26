/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * x86 task swtching and interrupt handling
 */

#include "config.h"
#include "registers.h"
#include "task_defs.h"

#include "irq_handler_common.S"

.text

.extern current_task
.extern next_task

.global __task_start
.global __switchto
.global default_int_handler
.global sw_irq_handler

# Start the task scheduling. Start current_task (hook_task)
# This function is not an ISR but imitates the sequence.
.align 4
.func __task_start
__task_start:
	movl 0x4(%esp), %ecx
	movl current_task, %eax
	movl (%eax), %esp
#ifdef CONFIG_FPU
	movl USE_FPU_OFFSET(%eax), %ebx
	test %ebx, %ebx
	jz 1f
	frstor FPU_CTX_OFFSET(%eax)
	1:
#endif
	movl $0x1, (%ecx)       # first task is ready. set start_called = 1
	popa
	iret
.endfunc

# Default interrupt handler - to handle exceptions
# and prints error
.align 4
.func default_int_handler
default_int_handler:

	pusha
	ASM_LOCK_PREFIX addl $1, __in_isr

	movl %esp, %eax
	movl $stack_end, %esp		# use system stack
	push %eax			# push sp of preempted context

	call unhandled_vector		# Handle system interrupts and
					# unregistered user interrupts
	pop %esp			# restore sp of preempted context
	# unhandled_vector call loads eax with vector for comparison
	cmpl $LAPIC_SPURIOUS_INT_VECTOR, %eax
	je 1f				# No EOI for LAPIC_SPURIOUS_INT_VECTOR

	movl %eax, IOAPIC_EOI_REG_ADDR	# Indicate completion of servicing the
					# interrupt to IOAPIC first
	movl $0x00, LAPIC_EOI_REG_ADDR	# Indicate completion of servicing the
					# interrupt to LAPIC next
	1:
	# Ensure we balance the __in_isr counter
	ASM_LOCK_PREFIX subl $1, __in_isr
	popa
	iret
.endfunc

.align 4
.func sw_irq_handler
sw_irq_handler:
	pusha
	ASM_LOCK_PREFIX addl $1, __in_isr

	# Call sw irq handler with irq number(%ecx) from task_trigger_irq.
	# Pass 0 for both desched and resched since we don't need to deschedule
	# our current task (and idle task can always be rescheduled)
	irq_handler_common $0 $0 %ecx

	# Indicate completion of servicing the interrupt to LAPIC.
	# No IOAPIC EOI needed as this is SW triggered.
	movl $0x00, LAPIC_EOI_REG_ADDR

	# Decrement ISR counter and restore general purpose registers.
	ASM_LOCK_PREFIX subl $1, __in_isr
	popa
	iret
.endfunc


# Switches from one task to another if ready.
# __schedule triggers software interrupt ISH_TS_VECTOR, which is handled by
# __switchto
.align 4
.func __switchto
__switchto:
	pusha
	ASM_LOCK_PREFIX addl $1, __in_isr

	# __schedule() copies 'resched' to %ecx and 'desched' to %edx before
	# triggering ISH_TS_VECTOR.
	# Call sw_irq with an invalid IRQ so it will skip calling a routine
	irq_handler_common %edx %ecx $CONFIG_IRQ_COUNT

	# Indicate completion of servicing the interrupt to LAPIC.
	# No IOAPIC EOI needed as this is SW triggered.
	movl $0x00, LAPIC_EOI_REG_ADDR

	# Decrement ISR counter and restore general purpose registers.
	ASM_LOCK_PREFIX subl $1, __in_isr
	popa
	iret
.endfunc
