/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Context switching
 */

#include "config.h"
#include "cpu.h"

.text

/**
 * Task context switching
 *
 * Change the task scheduled after returning from an interruption.
 *
 * This function must be called in interrupt context.
 *
 * Save the registers of the current task below the interrupt context on
 * its task, then restore the live registers of the next task and set the
 * process stack pointer to the new stack.
 *
 * $r0: pointer to the task to switch from
 * $r1: pointer to the task to switch to
 * $r2: pointer to the stack where the interrupt entry context is saved
 *
 * the structure of the saved context on the stack is :
 * (top to bottom)
 *  sp, lp, fp, r15, r5, r4, r3, r2, r1, r0, r10, r9, r8, r7, r6, ipc, ipsw
 *       interrupt entry frame            <|>
 */
.global __switch_task
__switch_task:
	/* get the (new) highest priority task pointer in r0 */
	jal next_sched_task
	movi55 $r3, 0
	/* pointer to the current task (which are switching from) */
	lwi.gp  $r1, [ + current_task]
	/* reset the re-scheduling request */
	swi.gp  $r3, [ + need_resched]
	/* Nothing to do: let's return to keep the same task scheduled */
	beq $r1, $r0, 1f
	/* save our new scheduled task  */
	swi.gp  $r0, [ + current_task]
	/* get the program status word saved at exception entry */
	mfsr $r4, $IPSW /* to save SP_ADJ bit */
	/* get the task program counter saved at exception entry */
	mfsr $r5, $IPC
	/* get the new scheduled task stack pointer */
	lw   $r3, [$r0]
	/* save ipsw, ipc, r6, r7, r8, r9, r10 on the current process stack */
	smw.adm $r4, [$fp], $r10, 0
	/* restore ipsw, ipc, r6, r7, r8, r9, r10 from the next stack context */
	lmw.bim $r4, [$r3], $r10, 0
	/* set the program status word to restore SP_ADJ bit */
	mtsr $r4, $IPSW
	/* set the task program counter to restore at exception exit */
	mtsr $r5, $IPC
	/* save the task stack pointer in its context */
	sw   $fp, [$r1]
	/* barrier: ensure IPC is taken into account before IRET */
	dsb
	/* exception frame pointer for the new task */
	mov55 $fp, $r3
1:      /* un-pile the interruption entry context */
	/* isr exit */
	jal end_irq_handler
	/* restore r0-r5 */
	lmw.bim $r0, [$fp], $r5, 0
	/* restore r15, fp, lp and sp */
	lmw.bi $r15, [$fp], $r15, 0xb
	/* restore PC and PSW */
	iret

/**
 * Start the task scheduling.
 *
 * $r0 is a pointer to task_stack_ready, which is set to 1 after
 * the task stack is set up.
 */
.global __task_start
__task_start:
	/* area used as thread stack for the first switch */
	la $r3, scratchpad

	movi55 $r4, 1
	movi55 $r2, 0    /* syscall 3rd parameter : not an IRQ emulation */
	movi55 $r1, 0    /* syscall 2nd parameter : re-schedule nothing */
	movi55 $r0, 0    /* syscall 1st parameter : de-schedule nothing */

	/* put the stack pointer at the top of the stack in scratchpad */
	addi $sp, $r3, 4 * TASK_SCRATCHPAD_SIZE
	/* we are ready to re-schedule */
	swi.gp  $r4, [ + need_resched]
	swi.gp  $r4, [ + start_called]

	/* trigger scheduling to execute the task with the highest priority */
	syscall 0
	/* we should never return here: set code to EC_ERROR_UNKNOWN */
	movi55 $r0, 0x1
	ret5   $lp

