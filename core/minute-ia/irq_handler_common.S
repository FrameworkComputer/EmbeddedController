/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * x86 task swtching and interrupt handling
 */

#include "config.h"
#include "registers.h"
#include "task_defs.h"

# desched resched irq are all inputs and should be set to registers or immediate
# values
.macro irq_handler_common desched resched irq
	# __schedule() copies 'resched' to %ecx and 'desched' to %edx before
	movl %esp, %eax
	movl $stack_end, %esp		# use system stack
	push %eax			# push sp of preempted context

	# Push resched and desched on stack to pass them as function parameters
	# to switch_handler(desched, resched). After call, we clean up stack
	# pointer. Note, we do this now before call_irq_service_routine has a
	# chance to clobber these caller-saved registers.
	push \resched
	push \desched

	push \irq
#ifdef CONFIG_TASK_PROFILING
	call task_start_irq_handler
#endif
	# Leave IRQ on stack for handler
	call call_irq_service_routine
	addl $0x04, %esp

	# Call switch_handler(desched, resched).
	call switch_handler		# switch task if needed
	addl $0x08, %esp

	pop %esp			# restore sp of preempted context

	test %eax, %eax			# Check if task switch required
	jz 1f

	movl current_task, %eax

#ifdef CONFIG_FPU
	movl USE_FPU_OFFSET(%eax), %ecx
	test %ecx, %ecx
	jz 2f
	fnsave FPU_CTX_OFFSET(%eax) # Save current FPU context(current->fp_ctx)
	2:
#endif

	# Save SP of current task and switch to new task
	movl %esp, (%eax)
	movl next_task, %eax
	movl %eax, current_task
	movl (%eax), %esp

#ifdef CONFIG_FPU
	movl USE_FPU_OFFSET(%eax), %ecx
	test %ecx, %ecx
	jz 1f
	frstor FPU_CTX_OFFSET(%eax)		# Restore next FPU context
#endif

	1:
.endm
