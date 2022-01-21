/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "console.h"
#include "cpu.h"
#include "hooks.h"
#include "task.h"

#define CPRINTF(format, args...) cprintf(CC_SYSTEM, format, ## args)
#define CPUTS(format, args...) cputs(CC_SYSTEM, format, ## args)

/* Floating point unit common code */

/*
 * As defined by Armv7-M Reference Manual B1.5.6 "Exception Entry Behavior",
 * the structure of the saved context on the stack is:
 * r0, r1, r2, r3, r12, lr, pc, psr, ...
 */
#define STACK_IDX_REG_LR 5
#define STACK_IDX_REG_PC 6

static atomic_t fpu_exc_present;
static uint32_t fpu_pc;
static uint32_t fpu_lr;
static uint32_t fpu_fpscr;
static int fpu_task;

static void fpu_warn(void)
{
	if (!IS_ENABLED(CONFIG_FPU_WARNINGS) || !fpu_exc_present)
		return;

	CPRINTF("\n### FPU exception at PC=0x%08x LR=0x%08x ", fpu_pc, fpu_lr);
	if (fpu_task == -1)
		CPUTS("(exc) ###\n");
	else
		CPRINTF("(task %d) ###\n", fpu_task);

	CPRINTF("### FPSCR=0x%08x => ", fpu_fpscr);
	if (fpu_fpscr & FPU_FPSCR_IOC)
		CPUTS("Invalid Operation ");

	if (fpu_fpscr & FPU_FPSCR_DZC)
		CPUTS("Division By Zero ");

	if (fpu_fpscr & FPU_FPSCR_OFC)
		CPUTS("Overflow ");

	if (fpu_fpscr & FPU_FPSCR_UFC)
		CPUTS("Underflow ");

	if (fpu_fpscr & FPU_FPSCR_IXC)
		CPUTS("Inexact ");

	if (fpu_fpscr & FPU_FPSCR_IDC)
		CPUTS("Input Denormal ");

	CPUTS("###\n");

	atomic_clear(&fpu_exc_present);
}

DECLARE_DEFERRED(fpu_warn);

test_mockable
void __keep fpu_irq(uint32_t excep_lr, uint32_t excep_sp)
{
	/*
	 * Get address of exception FPU exception frame. FPCAR register points
	 * to the beginning of allocated FPU exception frame on the stack.
	 */
	uint32_t *fpu_state = (uint32_t *)CPU_FPU_FPCAR;

	if (IS_ENABLED(CONFIG_FPU_WARNINGS)) {
		uint32_t *stack;

		if (!fpu_exc_present) {
			/*
			 * Examine least significant 4 bits from exception LR
			 * to find which stack should be used to find the
			 * exception frame:
			 * - 0xd - CPU was in Thread Mode and PSP was used
			 * - 0x9 - CPU was in Thread Mode and MSP was used
			 * - 0x1 - CPU was in Handler Mode and MSP was used
			 */
			if ((excep_lr & 0xf) == 0xd)
				asm("mrs %0, psp" : "=r"(stack));
			else
				stack = (uint32_t *)excep_sp;

			fpu_pc = stack[STACK_IDX_REG_PC];
			fpu_lr = stack[STACK_IDX_REG_LR];
			fpu_fpscr = fpu_state[FPU_IDX_REG_FPSCR];
			fpu_task = -1;

			if ((excep_lr & 0xf) != 0x1)
				fpu_task = task_get_current();

			atomic_add(&fpu_exc_present, 1);
		}
		hook_call_deferred(&fpu_warn_data, 0);
	}

	/* Clear FPSCR on stack */
	fpu_state[FPU_IDX_REG_FPSCR] &= ~FPU_FPSCR_EXC_FLAGS;
}
