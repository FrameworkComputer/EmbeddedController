/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
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
 */
.global __switchto
.thumb_func
__switchto:
  mrs r3, psp            @ get the task stack where the context has been saved
  ldr r2, [r1]           @ get the new scheduled task stack pointer
  stmdb r3!, {r4-r11}    @ save additional r4-r11 in the task stack
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
  ldr r2,=scratchpad     @ area used as dummy thread stack for the first switch
#ifdef CONFIG_FPU
  mov r3, #6             @ use : priv. mode / thread stack / floating point on
#else
  mov r3, #2             @ use : priv. mode / thread stack / no floating point
#endif
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

