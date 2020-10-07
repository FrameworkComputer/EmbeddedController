/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Context swtching
 */

#include "config.h"

.text

.syntax unified
.code 16

/**
 * Task context switching
 *
 * Change the task scheduled after returning from the exception.
 *
 * Save the registers of the current task below the exception context on
 * its task, then restore the live registers of the next task and set the
 * process stack pointer to the new stack.
 *
 * r0: pointer to the task to switch from
 * r1: pointer to the task to switch to
 *
 * must be called from interrupt context
 *
 * the structure of the saved context on the stack is :
 *     r0, r1, r2, r3, r12, lr, pc, psr, r4, r5, r6, r7, r8, r9, r10, r11
 *       exception frame              <|> additional registers
 *
 * if using the FPU, then to store FPU context, add FP regs to the stack. in
 * this case the exception frame by default contains:
 *     r0, r1, r2, r3, r12, lr, pc, psr,
 *     s0 - s15, FPSCR, +1 word for 64-bit alignment
 * then in addition we store the following registers:
 *     r4, r5, r6, r7, r8, r9, r10, r11
 *     s16 - s31 (stored iff FP was used by the task (see EXC_RETURN[4]))
 * note that for the context switch to know if the next task has the extra FP
 * regs on the stack or not, we make use of the least significant bit of the
 * stack pointer. lsb of stack pointer is 1 if task has FP regs on stack, and
 * 0 otherwise.
 *
 */
.global __switchto
.thumb_func
__switchto:
  mrs r3, psp            @ get the task stack where the context has been saved
  ldr r2, [r1]           @ get the new scheduled task stack pointer
  stmdb r3!, {r4-r11}    @ save additional r4-r11 in the task stack

#ifdef CONFIG_FPU
  tst lr, #(1<<4)        @ test EXC_RETURN[4] for old task
  itt eq                 @ if EXC_RETURN[4] is zero, add FP regs to stack
  vstmdbeq r3!, {s16-s31}@ save additional FP s16-s31 in the task stack.
                         @ if using lazy stacking, this will trigger saving
                         @ s0-s15 in the reserved stack space.
  orreq r3, #1           @ set lsb of old stack pointer high to represent this
                         @ task uses FPU. note stack pointer should be 64-bit
                         @ aligned, so using this bit should be safe.

  tst r2, #1             @ test lsb of next stack pointer
  ittte ne               @ if lsb is 1, then next task has FP regs on stack
  bicne r2, #1           @ clear lsb of new stack pointer
  bicne lr, #(1<<4)      @ clear EXC_RETURN[4] for next task
  vldmiane r2!, {s16-s31}@ restore s16-s31 for the next task context
  orreq lr, #(1<<4)      @ else if new stack doesn't use FP, set EXC_RETURN[4]
#endif

  ldmia r2!, {r4-r11}    @ restore r4-r11 for the next task context
  str r3, [r0]           @ save the task stack pointer in its context
  msr psp, r2            @ set the process stack pointer to exception context
  bx lr                  @ return from exception

/**
 * Start the task scheduling.  r0 is a pointer to task_stack_ready, which is
 * set to 1 after the task stack is set up.
 */
.global __task_start
.thumb_func
__task_start:
  ldr r2,=scratchpad     @ area used as thread stack for the first switch
  mov r3, #2             @ use : priv. mode / thread stack / no floating point
                         @ setting FP to unused here means first context switch
                         @ will not store FP regs
  add r2, #17*4          @ put the pointer at the top of the stack
  mov r1, #0             @ __Schedule parameter : re-schedule nothing
  msr psp, r2            @ setup a thread stack up to the first context switch
  mov r2, #1
  isb                    @ ensure the write is done
  msr control, r3
  mov r3, r0
  mov r0, #0             @ __Schedule parameter : de-schedule nothing
  isb                    @ ensure the write is done
  str r2, [r3]           @ Task scheduling is now active
  bl __schedule          @ execute the task with the highest priority
  /* we should never return here */
  mov r0, #1             @ set to EC_ERROR_UNKNOWN
  bx lr

